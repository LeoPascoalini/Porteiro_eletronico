#include <Wire.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>

// =======================================================
// PORTEIRO ELETRÔNICO - ESP32 + PCF8574 (I2C via RJ45)
// =======================================================

#define SERIAL_PRINT 1 // 1 para ativar prints no Serial Monitor; 0 para desativar

// ------------------ Wi-Fi / Site local ------------------
const char* WIFI_SSID  = "NOME_DA_REDE";
const char* WIFI_SENHA = "SENHA_DA_REDE";
const char* HOSTNAME   = "porteiro";        // acesso por http://porteiro.local

// Proteção contra tentativas pelo site
const uint8_t       WEB_MAX_ERROS   = 5;      // erros seguidos até bloquear
const unsigned long WEB_BLOQUEIO_MS = 60000;  // tempo de bloqueio
const unsigned long ADMIN_SESSAO_MS = 180000; // sessão de administrador no site expira após 3 min sem uso

// ------------------ Pinos do ESP32 ------------------
const int I2C_SDA    = 21;
const int I2C_SCL    = 22;
const int BUZZER_PIN = 25;
const int RELE_PIN   = 26;

// Nível que aciona o relé (muitos módulos de relé são ativos em LOW)
const int RELE_ATIVO   = HIGH;
const int RELE_INATIVO = (RELE_ATIVO == HIGH) ? LOW : HIGH;
const unsigned long RELE_PULSO_MS = 500;

// ------------------ PCF8574 ------------------
// Endereço detectado automaticamente no setup (PCF8574 = 0x20..0x27 | PCF8574A = 0x38..0x3F),
// então a posição dos switches A0/A1/A2 não importa
uint8_t PCF_ADDR = 0x20;
const uint32_t I2C_CLOCK = 50000;  // clock reduzido por causa do cabo longo

// Bits do PCF8574 (P0..P7) usados pelo teclado 3x4 (7 conexões)
const byte colPins[] = {3, 7, 5};      // C1=P3, C2=P7, C3=P5
const byte rowPins[] = {4, 2, 1, 6};   // L1=P4, L2=P2, L3=P1, L4=P6  (P0 livre)

// Layout visual (o que deve retornar)
const char keypadLayout[4][3] = {
  {'1','2','3'},
  {'4','5','6'},
  {'7','8','9'},
  {'*','0','#'}
};

// Timing debounce
const unsigned int settleUs   = 150;
const unsigned int debounceMs = 30;
const unsigned int releaseMs  = 20;

char kp_lastCandidate = 0;
unsigned long kp_candidateSince = 0;
bool kp_pressed = false;
unsigned long kp_releasedSince = 0;

// =======================================================
// IO helpers (PCF8574)
// -------------------------------------------------------
// O PCF8574 é quase-bidirecional:
//   bit = 1 -> pull-up fraco (funciona como INPUT_PULLUP / linha)
//   bit = 0 -> dreno em LOW  (funciona como OUTPUT LOW / coluna ativa)
// =======================================================
bool pcfOk = false;

static bool pcfWrite(uint8_t value) {
  Wire.beginTransmission(PCF_ADDR);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

// Em caso de falha retorna 0xFF (= nenhuma tecla)
static uint8_t pcfRead() {
  if (Wire.requestFrom(PCF_ADDR, (uint8_t)1) != 1) return 0xFF;
  return Wire.read();
}

static inline void idleAllPins() { pcfWrite(0xFF); }

// Procura o PCF8574 em todos os endereços possíveis; retorna true se achar
static bool pcfDetect() {
  const uint8_t bases[] = {0x20, 0x38};
  for (uint8_t base : bases) {
    for (uint8_t i = 0; i < 8; i++) {
      PCF_ADDR = base + i;
      if (pcfWrite(0xFF)) return true;
    }
  }
  PCF_ADDR = 0x20;
  return false;
}

// Lê duas vezes; se as leituras não baterem (ruído no cabo), considera "nada apertado"
static uint8_t pcfReadEstavel() {
  uint8_t a = pcfRead();
  uint8_t b = pcfRead();
  return (a == b) ? a : 0xFF;
}

// Escreve o estado dos pinos e lê de volta, de forma estável
static inline uint8_t aplicarELer(uint8_t value) {
  pcfWrite(value);
  delayMicroseconds(settleUs);
  return pcfReadEstavel();
}

static inline bool isLow(uint8_t state, byte bit) { return !(state & (1 << bit)); }

// =======================================================
// KEYPAD: proteção contra fuga (umidade) e ruído
// -------------------------------------------------------
// Uma leitura é descartada quando:
//  - sem nenhuma coluna ativa, já existe linha/coluna em LOW (fuga para o GND)
//  - uma coluna que não está ativa aparece em LOW
//  - mais de uma tecla aparece ao mesmo tempo
// =======================================================
uint8_t keypadMask = 0;            // bits do PCF8574 usados pelo teclado
unsigned long ultimoAvisoFuga = 0;

static void avisarFuga(const char* motivo, uint8_t estado) {
#if SERIAL_PRINT
  if (millis() - ultimoAvisoFuga < 5000) return;   // no máximo 1 aviso a cada 5 s
  ultimoAvisoFuga = millis();
  Serial.print("Teclado: leitura descartada ("); Serial.print(motivo);
  Serial.print(") estado=0x"); Serial.print(estado, HEX);
  Serial.println(" - possivel umidade/fuga no teclado ou na placa");
#endif
}

// =======================================================
// KEYPAD: scan cru (sem debounce)
// =======================================================
char scanRawKeypad() {
  // Nenhuma coluna ativa: tudo que o teclado usa deveria estar em HIGH
  uint8_t idle = aplicarELer(0xFF);
  if (~idle & keypadMask) { avisarFuga("fuga em repouso", idle); return 0; }

  char found = 0;

  for (byte col = 0; col < 3; col++) {
    uint8_t state = aplicarELer(0xFF & ~(1 << colPins[col]));

    // Outra coluna em LOW sem estar ativa: leitura inválida
    for (byte c = 0; c < 3; c++) {
      if (c != col && isLow(state, colPins[c])) {
        idleAllPins(); avisarFuga("coluna inativa em LOW", state); return 0;
      }
    }

    for (byte row = 0; row < 4; row++) {
      if (!isLow(state, rowPins[row])) continue;
      if (found) {   // mais de uma tecla ao mesmo tempo
        idleAllPins(); avisarFuga("varias teclas", state); return 0;
      }
      found = keypadLayout[row][col];
    }
  }

  idleAllPins();
  return found;
}

// =======================================================
// KEYPAD: debounce (1 evento por clique)
// =======================================================
char getKey() {
  unsigned long now = millis();
  char k = scanRawKeypad();

  if (kp_pressed) {
    if (k == 0) {
      if (kp_releasedSince == 0) kp_releasedSince = now;
      if (now - kp_releasedSince >= releaseMs) {
        kp_pressed = false;
        kp_lastCandidate = 0;
        kp_candidateSince = 0;
        kp_releasedSince = 0;
      }
    } else {
      kp_releasedSince = 0;
    }
    return 0;
  }

  if (k != kp_lastCandidate) {
    kp_lastCandidate = k;
    kp_candidateSince = now;
    return 0;
  }

  if (k && (now - kp_candidateSince) >= debounceMs) {
    kp_pressed = true;
    kp_releasedSince = 0;
    return k;
  }

  return 0;
}

// =======================================================
// SOM: fila de notas tocada sem travar o loop
// -------------------------------------------------------
// As funções de som só colocam notas na fila; atualizarSom()
// (chamada no loop) toca cada uma no tempo certo.
// =======================================================
struct Nota { uint16_t freq; uint16_t ms; };   // freq = 0 -> silêncio

const uint8_t FILA_TAM = 48;
Nota fila[FILA_TAM];
uint8_t filaIni = 0, filaFim = 0;
bool tocando = false;
unsigned long notaInicio = 0;
uint16_t notaDuracao = 0;

void tocar(uint16_t freq, uint16_t ms) {
  uint8_t prox = (filaFim + 1) % FILA_TAM;
  if (prox == filaIni) return;   // fila cheia: descarta
  fila[filaFim] = {freq, ms};
  filaFim = prox;
}

void pausa(uint16_t ms) { tocar(0, ms); }

void atualizarSom() {
  if (tocando && millis() - notaInicio < notaDuracao) return;

  if (filaIni == filaFim) {
    if (tocando) { noTone(BUZZER_PIN); tocando = false; }
    return;
  }

  Nota n = fila[filaIni];
  filaIni = (filaIni + 1) % FILA_TAM;

  if (n.freq) tone(BUZZER_PIN, n.freq);
  else        noTone(BUZZER_PIN);

  tocando = true;
  notaInicio = millis();
  notaDuracao = n.ms;
}

// =======================================================
// RELÉ: pulso sem travar o loop
// =======================================================
bool releLigado = false;
unsigned long releInicio = 0;

void atualizarRele() {
  if (releLigado && millis() - releInicio >= RELE_PULSO_MS) {
    digitalWrite(RELE_PIN, RELE_INATIVO);
    releLigado = false;
  }
}

// =======================================================
// PORTEIRO ELETRÔNICO
// =======================================================

//------------------ Variáveis ------------------
String gabarito = "123456";
String gabaritoADM = "246810";
String senha = "";
int comparaGabarito;
int comparaGabaritoADM;

int dig = 0;

const unsigned long timeoutDuration = 10000;
unsigned long lastKeyPressTime = 0;

// Modo administrador (cadastro de nova senha pelo teclado físico)
bool modoAdmin = false;
String novoGabarito = "";

Preferences prefs; // memória não volátil do ESP32 (substitui a EEPROM)
//----------------------------------------------

//------------------ Sons (novos) ----------------
void somTecla() {
  tocar(1200, 40);
}

void somErro() {
  tocar(400, 200);
  tocar(250, 300);
}

void somReajuste() {
  tocar(600, 120);
  tocar(900, 120);
  tocar(1200, 180);
}

void somNovaConfiguracao() {
  tocar(800, 100);
  tocar(1200, 100);
  tocar(1600, 120);
  tocar(1200, 150);
}

// Atualizado conforme pedido
void zelda() {
  const int d = 125;   // duração base
  const int gap = 12;  // pausa curtinha

  tocar(1568, d); pausa(gap);  // F5
  tocar(1481, d); pausa(gap);  // E5
  tocar(1246, d); pausa(gap);  // C4
  tocar(880, d);  pausa(gap);  // G4
  tocar(826, d);  pausa(gap);  // F4
  tocar(1318, d); pausa(gap);  // E5
  tocar(1664, d); pausa(gap);  // G5
  tocar(2093, d); pausa(gap);  // B5

  pausa(500);
}
//----------------------------------------------

//------------------ Funções --------------------
void abrir() {
  digitalWrite(RELE_PIN, RELE_ATIVO);
  releLigado = true;
  releInicio = millis();
}

void resetar() {
  senha = "";
  dig = 0;
}

void save_gabarito() {
  prefs.putString("gabarito", gabarito);
}

void load_gabarito() {
  gabarito = prefs.getString("gabarito", "");
  if (gabarito.length() == 0) gabarito = "123456";
}

// Senhas cadastradas precisam ter exatamente 6 dígitos
bool senhaValida(const String& s) {
  if (s.length() != 6) return false;
  for (char c : s) if (!isDigit(c)) return false;
  return true;
}

void checkTimeout() {
  if (senha.length() > 0 && (millis() - lastKeyPressTime) > timeoutDuration) {
#if SERIAL_PRINT
    Serial.println("Timeout: senha cancelada.");
#endif
    somErro();
    resetar();
  }
}

// =======================================================
// SITE LOCAL
// =======================================================
WebServer server(80);
bool wifiConectado = false;

uint8_t webErros = 0;
unsigned long webBloqueioInicio = 0;

// Sessão de administrador no site (token entregue ao digitar a senha de administrador)
String adminToken = "";
unsigned long adminTokenInicio = 0;

const char PAGINA[] PROGMEM = R"rawliteral(<!doctype html>
<html lang="pt-br">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,user-scalable=no">
<title>Porteiro</title>
<style>
  body{margin:0;min-height:100vh;display:flex;align-items:center;justify-content:center;
       background:#111;color:#eee;font-family:system-ui,sans-serif}
  .box{width:min(320px,90vw);text-align:center}
  h1{font-size:22px;font-weight:600;margin:0 0 16px}
  #visor{height:56px;font-size:32px;letter-spacing:10px;background:#222;border-radius:12px;
         display:flex;align-items:center;justify-content:center}
  #msg{height:24px;margin:10px 0 14px}
  .teclado{display:grid;grid-template-columns:repeat(3,1fr);gap:10px}
  button{font-size:28px;padding:18px 0;border:0;border-radius:14px;background:#2c2c2c;color:#eee;
         -webkit-tap-highlight-color:transparent}
  button:active{background:#454545}
  .limpar{background:#5a2222}.enviar{background:#1f4d2b}
  .ok{color:#5c5}.erro{color:#e66}
  .dica{color:#888;font-size:13px;margin-top:14px}
  #menu{display:none;flex-direction:column;gap:10px}
  #menu button{font-size:18px;padding:16px 0}
  .sair{background:#333;color:#aaa}
</style>
</head>
<body>
<div class="box">
  <h1 id="titulo">Porteiro</h1>
  <div id="visor"></div>
  <div id="msg"></div>
  <div class="teclado" id="teclado"></div>
  <div id="menu">
    <button onclick="escolher()">Trocar senha do portão</button>
    <button class="sair" onclick="sair()">Sair</button>
  </div>
  <div class="dica" id="dica">* apaga &nbsp;·&nbsp; # abre</div>
</div>
<script>
  // modo: 'senha' (abrir portão) | 'menu' (administrador) | 'nova' (digitando senha nova)
  let senha = '', modo = 'senha', token = '';
  const $ = id => document.getElementById(id);

  '123456789*0#'.split('').forEach(k => {
    const b = document.createElement('button');
    b.textContent = k;
    if (k === '*') b.className = 'limpar';
    if (k === '#') b.className = 'enviar';
    b.onclick = () => tecla(k);
    $('teclado').appendChild(b);
  });

  // Na senha nova os dígitos aparecem, para conferir antes de salvar
  function mostrar() { $('visor').textContent = modo === 'nova' ? senha : '•'.repeat(senha.length); }
  function aviso(t, c) { $('msg').textContent = t; $('msg').className = c || ''; }

  function tela(m, titulo, dica) {
    modo = m; senha = ''; mostrar();
    $('titulo').textContent = titulo;
    $('dica').innerHTML = dica;
    $('teclado').style.display = m === 'menu' ? 'none' : 'grid';
    $('menu').style.display = m === 'menu' ? 'flex' : 'none';
  }
  function telaSenha() { token = ''; tela('senha', 'Porteiro', '* apaga &nbsp;·&nbsp; # abre'); }
  function telaMenu()  { tela('menu', 'Administrador', ''); }

  function escolher() {
    tela('nova', 'Nova senha do portão', '* apaga &nbsp;·&nbsp; # salva');
    aviso('Digite 6 dígitos e aperte #');
  }
  function sair() { telaSenha(); aviso(''); }

  function tecla(k) {
    if (k === '*') { senha = ''; mostrar(); aviso(''); return; }
    if (k === '#') {
      if (modo === 'senha' && senha) enviar();
      if (modo === 'nova') {
        if (senha.length === 6) trocar(); else aviso('A senha deve ter 6 dígitos', 'erro');
      }
      return;
    }
    if (senha.length < (modo === 'nova' ? 6 : 10)) { senha += k; mostrar(); aviso(''); }
  }

  async function post(url, dados) {
    const r = await fetch(url, {
      method: 'POST',
      headers: {'Content-Type': 'application/x-www-form-urlencoded'},
      body: new URLSearchParams(dados).toString()
    });
    return { status: r.status, j: await r.json() };
  }

  async function enviar() {
    const s = senha;
    senha = ''; mostrar(); aviso('Enviando...');
    try {
      const { j } = await post('/abrir', { senha: s });
      if (j.admin) { token = j.token; telaMenu(); aviso(j.msg, 'ok'); return; }
      aviso(j.msg, j.ok ? 'ok' : 'erro');
    } catch (e) {
      aviso('Sem conexão com o porteiro', 'erro');
    }
  }

  async function trocar() {
    const nova = senha;
    senha = ''; mostrar(); aviso('Salvando...');
    try {
      const { status, j } = await post('/trocar', { token, nova });
      if (status === 403) { telaSenha(); aviso(j.msg, 'erro'); return; }
      if (j.ok) telaMenu();
      aviso(j.msg, j.ok ? 'ok' : 'erro');
    } catch (e) {
      aviso('Sem conexão com o porteiro', 'erro');
    }
  }
</script>
</body>
</html>
)rawliteral";

void responder(int codigo, bool ok, const char* msg) {
  String json = String("{\"ok\":") + (ok ? "true" : "false") + ",\"msg\":\"" + msg + "\"}";
  server.send(codigo, "application/json; charset=utf-8", json);
}

void handlePagina() {
  // Sem cache: depois de gravar um código novo, o celular sempre pega a página atualizada
  server.sendHeader("Cache-Control", "no-store");
  server.send_P(200, "text/html; charset=utf-8", PAGINA);
}

void handleAbrir() {
  unsigned long agora = millis();

  if (webErros >= WEB_MAX_ERROS) {
    if (agora - webBloqueioInicio < WEB_BLOQUEIO_MS) {
      responder(429, false, "Muitas tentativas. Aguarde 1 minuto.");
      return;
    }
    webErros = 0;
  }

  String s = server.arg("senha");

  if (s.length() > 0 && s == gabarito) {
#if SERIAL_PRINT
    Serial.println("[SITE] Senha correta! Abrindo...");
#endif
    webErros = 0;
    abrir();
    zelda();
    responder(200, true, "Portão aberto!");
    return;
  }

  if (s.length() > 0 && s == gabaritoADM) {
#if SERIAL_PRINT
    Serial.println("[SITE] Modo administrador.");
#endif
    webErros = 0;
    adminToken = String(esp_random(), HEX) + String(esp_random(), HEX);
    adminTokenInicio = agora;
    server.send(200, "application/json; charset=utf-8",
                "{\"ok\":true,\"admin\":true,\"token\":\"" + adminToken + "\",\"msg\":\"Modo administrador\"}");
    return;
  }

#if SERIAL_PRINT
  Serial.println("[SITE] Senha incorreta.");
#endif
  webErros++;
  if (webErros >= WEB_MAX_ERROS) webBloqueioInicio = agora;
  somErro();
  responder(401, false, "Senha incorreta");
}

void handleTrocar() {
  String token = server.arg("token");
  if (adminToken.length() == 0 || token != adminToken ||
      millis() - adminTokenInicio >= ADMIN_SESSAO_MS) {
    adminToken = "";
    responder(403, false, "Sessão expirada. Digite a senha de administrador de novo.");
    return;
  }
  adminTokenInicio = millis(); // renova a sessão a cada uso

  String nova = server.arg("nova");

  if (!senhaValida(nova)) {
    responder(400, false, "A senha deve ter 6 dígitos");
    return;
  }
  if (nova == gabaritoADM) {
    responder(400, false, "Não pode ser igual à senha de administrador");
    return;
  }

  gabarito = nova;
  save_gabarito();
#if SERIAL_PRINT
  Serial.print("[SITE] Nova senha do portao salva: "); Serial.println(gabarito);
#endif

  somNovaConfiguracao();
  responder(200, true, "Senha alterada!");
}

// Mostra o IP e (re)inicia o mDNS sempre que o Wi-Fi conecta
void atualizarWifi() {
  bool conectado = (WiFi.status() == WL_CONNECTED);

  if (conectado && !wifiConectado) {
    MDNS.end();
    if (MDNS.begin(HOSTNAME)) MDNS.addService("http", "tcp", 80);
#if SERIAL_PRINT
    Serial.print("Wi-Fi conectado. Site: http://");
    Serial.print(WiFi.localIP());
    Serial.print("  ou  http://"); Serial.print(HOSTNAME); Serial.println(".local");
#endif
  }
#if SERIAL_PRINT
  if (!conectado && wifiConectado) Serial.println("Wi-Fi desconectado, tentando reconectar...");
#endif

  wifiConectado = conectado;
}

// =======================================================
// SETUP / LOOP
// =======================================================
void setup() {
  pinMode(BUZZER_PIN, OUTPUT);

  pinMode(RELE_PIN, OUTPUT);
  digitalWrite(RELE_PIN, RELE_INATIVO);

#if SERIAL_PRINT
  Serial.begin(115200);
#endif

  Wire.begin(I2C_SDA, I2C_SCL, I2C_CLOCK);
  pcfOk = pcfDetect(); // acha o endereço e já deixa os pinos em idle

  for (byte c : colPins) keypadMask |= (1 << c);
  for (byte r : rowPins) keypadMask |= (1 << r);

#if SERIAL_PRINT
  if (pcfOk) {
    Serial.print("PCF8574 encontrado no endereco 0x"); Serial.println(PCF_ADDR, HEX);
  } else {
    Serial.println("PCF8574 NAO ENCONTRADO - verifique cabo SDA/SCL, 3V3 e GND");
  }
#endif

  prefs.begin("porteiro", false);
  load_gabarito();

#if SERIAL_PRINT
  Serial.print("Senha carregada da memoria: ");
  Serial.println(gabarito);
#endif

  // Wi-Fi: conecta em segundo plano; o teclado funciona mesmo sem rede
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(HOSTNAME);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_SENHA);

  server.on("/", HTTP_GET, handlePagina);
  server.on("/abrir", HTTP_POST, handleAbrir);
  server.on("/trocar", HTTP_POST, handleTrocar);
  server.begin();

  somReajuste();
}

// Teclado físico no modo administrador: aceita só dígitos até completar 6
void processarAdmin(char key) {
  if (!isDigit(key)) return;

  novoGabarito += key;
  somTecla();
#if SERIAL_PRINT
  Serial.print("Nova senha: ");
  Serial.println(novoGabarito);
#endif

  if (novoGabarito.length() < 6) return;

  // Igual à senha de administrador deixaria o modo admin inacessível: recomeça
  if (novoGabarito == gabaritoADM) {
#if SERIAL_PRINT
    Serial.println("Nova senha igual a de administrador. Digite outra.");
#endif
    somErro();
    novoGabarito = "";
    return;
  }

  gabarito = novoGabarito;
  save_gabarito();
  modoAdmin = false;

#if SERIAL_PRINT
  Serial.print("Nova senha salva na memoria: ");
  Serial.println(gabarito);
#endif
  somNovaConfiguracao();
  resetar();
}

void loop() {
  server.handleClient();
  atualizarSom();
  atualizarRele();
  atualizarWifi();

  char key = getKey();

  if (modoAdmin) {
    if (key) processarAdmin(key);
    return;
  }

  checkTimeout();

  if (!key) return;

  lastKeyPressTime = millis();

  // Som de tecla ao detectar qualquer tecla válida
  somTecla();

  if (key == '*') {  // Cancelar
#if SERIAL_PRINT
    Serial.println("Senha cancelada.");
#endif
    somErro();
    resetar();
    return;
  }

  if (key == '#') {  // Enviar
    if (senha.length() == 0) {
#if SERIAL_PRINT
      Serial.println("Nenhuma senha digitada. Reiniciando.");
#endif
      somErro();
      resetar();
      return;
    }

    comparaGabarito = senha.compareTo(gabarito);
    comparaGabaritoADM = senha.compareTo(gabaritoADM);

    if (comparaGabarito == 0) {
#if SERIAL_PRINT
      Serial.println("Senha correta! Abrindo...");
#endif
      abrir();
      zelda();
    }
    else if (comparaGabaritoADM == 0) {
#if SERIAL_PRINT
      Serial.println("Modo administrador: digite nova senha de 6 dígitos.");
#endif
      modoAdmin = true;
      novoGabarito = "";
      somNovaConfiguracao();
    }
    else {
#if SERIAL_PRINT
      Serial.println("Senha incorreta.");
#endif
      somErro();
    }

    resetar();
    return;
  }

  // Dígitos
  if (isDigit(key)) {
    senha += key;
    dig++;

#if SERIAL_PRINT
    Serial.print("Senha atual: ");
    Serial.println(senha);
#endif
  }

  if (senha.length() > 10) {
#if SERIAL_PRINT
    Serial.println("Senha muito longa! Reiniciando.");
#endif
    somErro();
    resetar();
  }
}
