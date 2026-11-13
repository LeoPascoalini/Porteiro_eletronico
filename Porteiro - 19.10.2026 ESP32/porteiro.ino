#include <Wire.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Update.h>
#include <ArduinoOTA.h>
#include <time.h>
#include "esp_system.h"

// =======================================================
// PORTEIRO ELETRÔNICO - ESP32 + PCF8574 (I2C via RJ45)
// =======================================================

#define SERIAL_PRINT 1 // 1 para ativar prints no Serial Monitor; 0 para desativar

// ------------------ Wi-Fi / Site local ------------------
const char* WIFI_SSID  = "NOME_DA_REDE";
const char* WIFI_SENHA = "SENHA_DA_REDE";
const char* HOSTNAME   = "porteiro";        // acesso por http://porteiro.local
const char* FUSO       = "<-03>3";          // horário de Brasília (para o registro de eventos)

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
const unsigned int debounceMs = 60;   // tecla precisa ficar estável esse tempo para valer
const unsigned int releaseMs  = 50;   // e solta esse tempo antes da próxima

// Ruído elétrico (ex.: motor do portão de carro): depois de uma leitura suspeita,
// o teclado fica ignorado por esse tempo e a digitação em andamento é descartada
const unsigned long QUARENTENA_MS = 1500;

// Tecla "apertada" por mais que isso é registrada como presa (umidade/defeito)
const unsigned long TECLA_PRESA_MS = 10000;

char kp_lastCandidate = 0;
unsigned long kp_candidateSince = 0;
bool kp_pressed = false;
unsigned long kp_releasedSince = 0;

// =======================================================
// REGISTRO DE EVENTOS (Serial + página de administrador do site)
// -------------------------------------------------------
// Guarda os últimos eventos na memória (perde ao reiniciar).
// Nunca registra senhas.
// =======================================================
const uint8_t LOG_TAM = 60;
String logMsg[LOG_TAM];
uint8_t logProx = 0, logQtd = 0;

// Data/hora pela internet quando disponível; senão, tempo desde que ligou
String agoraTexto() {
  char buf[24];
  time_t t = time(nullptr);
  if (t > 1700000000) {
    struct tm tm;
    localtime_r(&t, &tm);
    strftime(buf, sizeof(buf), "%d/%m %H:%M:%S", &tm);
  } else {
    unsigned long s = millis() / 1000;
    snprintf(buf, sizeof(buf), "+%luh%02lum%02lus", s / 3600, (s / 60) % 60, s % 60);
  }
  return buf;
}

void registrar(const String& msg) {
  String linha = agoraTexto() + "  " + msg;
#if SERIAL_PRINT
  Serial.println(linha);
#endif
  logMsg[logProx] = linha;
  logProx = (logProx + 1) % LOG_TAM;
  if (logQtd < LOG_TAM) logQtd++;
}

const char* motivoReset() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON:  return "energia ligada";
    case ESP_RST_SW:       return "reinício pelo software/site";
    case ESP_RST_PANIC:    return "erro no programa";
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT:      return "watchdog";
    case ESP_RST_BROWNOUT: return "queda de tensão (brownout)";
    case ESP_RST_EXT:      return "botão de reset";
    default:               return "desconhecido";
  }
}

// =======================================================
// IO helpers (PCF8574)
// -------------------------------------------------------
// O PCF8574 é quase-bidirecional:
//   bit = 1 -> pull-up fraco (funciona como INPUT_PULLUP / linha)
//   bit = 0 -> dreno em LOW  (funciona como OUTPUT LOW / coluna ativa)
// =======================================================
bool pcfOk = false;
bool i2cErro = false;   // marcado quando alguma transação I2C falha (ruído no cabo)

static bool pcfWrite(uint8_t value) {
  Wire.beginTransmission(PCF_ADDR);
  Wire.write(value);
  bool ok = (Wire.endTransmission() == 0);
  if (!ok) i2cErro = true;
  return ok;
}

// Em caso de falha retorna 0xFF (= nenhuma tecla) e marca i2cErro
static uint8_t pcfRead() {
  if (Wire.requestFrom(PCF_ADDR, (uint8_t)1) != 1) { i2cErro = true; return 0xFF; }
  return Wire.read();
}

static inline void idleAllPins() { pcfWrite(0xFF); }

// Procura o PCF8574 (primeiro no endereço já conhecido); retorna true se achar
static bool pcfDetect() {
  if (pcfWrite(0xFF)) return true;
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

static inline bool isLow(uint8_t state, byte bit) { return !(state & (1 << bit)); }

// =======================================================
// I2C: recuperação automática do barramento
// -------------------------------------------------------
// Um pico elétrico pode deixar o PCF8574 travado no meio de uma
// transmissão, segurando o SDA em LOW: o teclado para de responder.
// Depois de várias falhas seguidas, o ESP32 gera pulsos no SCL para
// destravar, reinicia o I2C e procura o PCF8574 de novo.
// =======================================================
const uint8_t       I2C_FALHAS_LIMITE = 10;    // varreduras seguidas com erro
const unsigned long I2C_RECUPERA_MS   = 3000;  // intervalo mínimo entre tentativas

uint8_t i2cFalhasSeguidas = 0;
unsigned long ultimaRecuperacao = 0;
uint16_t totalRecuperacoes = 0;
bool tecladoComunica = true;

void iniciarI2C() {
  Wire.begin(I2C_SDA, I2C_SCL, I2C_CLOCK);
  Wire.setTimeOut(10);   // não deixa o loop travar se o barramento estiver preso
}

void destravarI2C() {
  Wire.end();
  pinMode(I2C_SDA, INPUT_PULLUP);
  pinMode(I2C_SCL, OUTPUT_OPEN_DRAIN);
  digitalWrite(I2C_SCL, HIGH);
  delayMicroseconds(10);

  // Até 9 pulsos de clock para o PCF8574 terminar o byte e soltar o SDA
  for (uint8_t i = 0; i < 9 && digitalRead(I2C_SDA) == LOW; i++) {
    digitalWrite(I2C_SCL, LOW);  delayMicroseconds(10);
    digitalWrite(I2C_SCL, HIGH); delayMicroseconds(10);
  }

  // START + STOP para zerar o estado do barramento
  pinMode(I2C_SDA, OUTPUT_OPEN_DRAIN);
  digitalWrite(I2C_SDA, LOW);  delayMicroseconds(10);
  digitalWrite(I2C_SDA, HIGH); delayMicroseconds(10);

  iniciarI2C();
}

void verificarI2C(bool falhou) {
  if (!falhou) {
    if (!tecladoComunica) {
      tecladoComunica = true;
      registrar("Teclado: comunicação I2C restabelecida");
    }
    i2cFalhasSeguidas = 0;
    return;
  }

  if (i2cFalhasSeguidas < 255) i2cFalhasSeguidas++;
  if (i2cFalhasSeguidas < I2C_FALHAS_LIMITE) return;

  if (tecladoComunica) {
    tecladoComunica = false;
    registrar("Teclado: sem comunicação I2C, tentando destravar");
  }

  if (millis() - ultimaRecuperacao < I2C_RECUPERA_MS) return;
  ultimaRecuperacao = millis();
  totalRecuperacoes++;
  destravarI2C();
  pcfOk = pcfDetect();
}

// =======================================================
// KEYPAD: proteção contra ruído elétrico e umidade
// -------------------------------------------------------
// A leitura é marcada como RUÍDO (ativa a quarentena) quando acontece algo
// que um dedo apertando tecla nunca causa:
//  - alguma transação I2C falha
//  - em repouso (nenhuma coluna ativa), algum pino aparece em LOW
//  - a coluna ativa não aparece em LOW, ou o pino livre (P0) aparece em LOW
//  - duas leituras seguidas diferem em um pino que não é linha
// Tecla trepidando ou várias teclas juntas só descartam a leitura.
// =======================================================
uint8_t rowMask = 0, colMask = 0, livreMask = 0;   // bits do PCF8574 (calculados no setup)

const char* ruidoMotivo = "";
uint8_t ruidoEstado = 0xFF;

static void marcarRuido(bool& ruido, const char* motivo, uint8_t estado) {
  ruido = true;
  ruidoMotivo = i2cErro ? "erro I2C" : motivo;
  ruidoEstado = estado;
}

// Escreve o estado dos pinos e lê de volta duas vezes
static uint8_t aplicarELer(uint8_t value, bool& ruido, bool& instavel) {
  pcfWrite(value);
  delayMicroseconds(settleUs);
  uint8_t a = pcfRead();
  uint8_t b = pcfRead();
  uint8_t diff = a ^ b;
  if (diff & ~rowMask) ruido = true;   // pino que não é linha mudou sozinho
  else if (diff) instavel = true;      // linha mudou: tecla trepidando
  return a;
}

// =======================================================
// KEYPAD: scan cru (sem debounce)
// Retorna a tecla (ou 0). Se a leitura for suspeita, marca 'ruido'.
// =======================================================
char scanRawKeypad(bool& ruido) {
  ruido = false;
  i2cErro = false;
  bool instavel = false;

  // Nenhuma coluna ativa: nenhuma tecla consegue puxar nada para LOW,
  // então todos os pinos têm que estar em HIGH
  uint8_t idle = aplicarELer(0xFF, ruido, instavel);
  if (ruido || instavel || i2cErro || idle != 0xFF) {
    marcarRuido(ruido, "pino em LOW em repouso", idle);
    return 0;
  }

  char found = 0;
  bool varias = false;

  for (byte col = 0; col < 3; col++) {
    uint8_t ativa = 1 << colPins[col];
    uint8_t state = aplicarELer(0xFF & ~ativa, ruido, instavel);

    // Coluna ativa tem que estar em LOW e o pino livre em HIGH; senão
    // o PCF8574 não recebeu ou não devolveu o valor certo
    if (ruido || i2cErro || (state & ativa) || (~state & livreMask)) {
      idleAllPins();
      marcarRuido(ruido, "leitura inconsistente", state);
      return 0;
    }

    // Outra coluna em LOW só acontece com duas teclas apertadas juntas
    if (~state & colMask & ~ativa) varias = true;

    for (byte row = 0; row < 4; row++) {
      if (!isLow(state, rowPins[row])) continue;
      if (found) varias = true;
      found = keypadLayout[row][col];
    }
  }

  idleAllPins();
  if (instavel || varias) return 0;   // tecla trepidando ou várias juntas: só ignora
  return found;
}

// =======================================================
// KEYPAD: debounce (1 evento por clique) + quarentena de ruído
// =======================================================
unsigned long ultimoRuido = 0;
bool houveRuido = false;
bool novoEpisodioRuido = false;   // avisa o loop para descartar a digitação em andamento
uint16_t episodiosRuido = 0;

unsigned long kp_pressedSince = 0;
bool teclaPresaAvisada = false;

bool tecladoEmQuarentena() {
  return houveRuido && (millis() - ultimoRuido < QUARENTENA_MS);
}

char getKey() {
  unsigned long now = millis();
  bool ruido;
  char k = scanRawKeypad(ruido);

  verificarI2C(i2cErro);

  if (ruido) {
    if (!houveRuido || now - ultimoRuido >= QUARENTENA_MS) {
      novoEpisodioRuido = true;
      episodiosRuido++;
      registrar(String("Teclado: ruído detectado (") + ruidoMotivo +
                ", estado=0x" + String(ruidoEstado, HEX) + ")");
    }
    houveRuido = true;
    ultimoRuido = now;
    // Exige que o teclado fique "solto" antes de aceitar uma nova tecla
    kp_pressed = true;
    kp_pressedSince = now;
    kp_releasedSince = 0;
    kp_lastCandidate = 0;
    return 0;
  }

  bool emQuarentena = tecladoEmQuarentena();

  if (kp_pressed) {
    if (k == 0) {
      if (kp_releasedSince == 0) kp_releasedSince = now;
      if (now - kp_releasedSince >= releaseMs) {
        kp_pressed = false;
        kp_lastCandidate = 0;
        kp_candidateSince = 0;
        kp_releasedSince = 0;
        teclaPresaAvisada = false;
      }
    } else {
      kp_releasedSince = 0;
      if (!teclaPresaAvisada && now - kp_pressedSince >= TECLA_PRESA_MS) {
        teclaPresaAvisada = true;
        registrar(String("Teclado: tecla '") + k + "' parece presa (umidade ou defeito)");
      }
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
    kp_pressedSince = now;
    kp_releasedSince = 0;
    return emQuarentena ? 0 : k;   // durante a quarentena a tecla é ignorada
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
    registrar("Teclado: tempo esgotado (" + String(senha.length()) + " dígito(s) digitado(s))");
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

// Reinício pedido pelo site (espera a resposta chegar ao celular antes de reiniciar)
bool reiniciarPedido = false;
unsigned long reiniciarInicio = 0;

const char PAGINA[] PROGMEM = R"rawliteral(<!doctype html>
<html lang="pt-br">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,user-scalable=no">
<title>Porteiro</title>
<style>
  body{margin:0;min-height:100vh;display:flex;align-items:center;justify-content:center;
       background:#111;color:#eee;font-family:system-ui,sans-serif}
  .box{width:min(320px,92vw);text-align:center;padding:16px 0}
  h1{font-size:22px;font-weight:600;margin:0 0 16px}
  #visor{height:56px;font-size:32px;letter-spacing:10px;background:#222;border-radius:12px;
         display:flex;align-items:center;justify-content:center}
  #msg{min-height:24px;margin:10px 0 14px}
  .teclado{display:grid;grid-template-columns:repeat(3,1fr);gap:10px}
  button{font-size:28px;padding:18px 0;border:0;border-radius:14px;background:#2c2c2c;color:#eee;
         -webkit-tap-highlight-color:transparent}
  button:active{background:#454545}
  .limpar{background:#5a2222}.enviar{background:#1f4d2b}
  .ok{color:#5c5}.erro{color:#e66}
  .dica{color:#888;font-size:13px;margin-top:14px}
  #menu{display:none;flex-direction:column;gap:10px}
  #menu button,#registro button{font-size:18px;padding:16px 0}
  .sair{background:#333;color:#aaa}
  #registro{display:none;text-align:left}
  #status{background:#1b1b1b;border-radius:10px;padding:10px 12px;font-size:14px;
          line-height:1.5;white-space:pre-line;margin-bottom:10px}
  #lista{background:#1b1b1b;border-radius:10px;padding:10px 12px;font:12px/1.5 ui-monospace,monospace;
         white-space:pre-wrap;max-height:50vh;overflow-y:auto;margin:0 0 10px}
  .botoes{display:grid;grid-template-columns:1fr 1fr;gap:10px}
  .box{padding-bottom:72px}   /* espaço para o botão de reiniciar não cobrir o teclado */
  #btnReiniciar{position:fixed;left:16px;bottom:max(16px,env(safe-area-inset-bottom));
                width:48px;height:48px;padding:0;border-radius:50%;background:#2a2016;color:#d9a66a;
                display:flex;align-items:center;justify-content:center}
  #atualizar{display:none;text-align:left}
  #atualizar button{font-size:18px;padding:16px 0}
  .texto{font-size:14px;color:#bbb;line-height:1.5;margin:0 0 12px}
  #arquivo{width:100%;margin:0 0 12px;color:#ccc}
  #progresso{width:100%;height:14px;margin:0 0 12px}
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
    <button onclick="verRegistro()">Ver registro</button>
    <button onclick="telaAtualizar()">Atualizar programa</button>
    <button class="sair" onclick="sair()">Sair</button>
  </div>
  <div id="atualizar">
    <p class="texto">Na Arduino IDE, use <b>Sketch → Exportar binário compilado</b> e envie o arquivo
      <b>porteiro.ino.bin</b> da pasta <b>build</b>. O porteiro reinicia sozinho no final.</p>
    <input type="file" id="arquivo" accept=".bin">
    <progress id="progresso" max="100" value="0"></progress>
    <div class="botoes">
      <button id="btnEnviar" onclick="enviarPrograma()">Enviar</button>
      <button class="sair" onclick="telaMenu()">Voltar</button>
    </div>
  </div>
  <div id="registro">
    <div id="status"></div>
    <pre id="lista"></pre>
    <div class="botoes">
      <button onclick="verRegistro()">Atualizar</button>
      <button class="sair" onclick="telaMenu()">Voltar</button>
    </div>
  </div>
  <div class="dica" id="dica">* apaga &nbsp;·&nbsp; # abre</div>
  <button id="btnReiniciar" onclick="reiniciar()" title="Reiniciar porteiro" aria-label="Reiniciar porteiro">
    <svg viewBox="0 0 24 24" width="22" height="22" fill="none" stroke="currentColor"
         stroke-width="2.2" stroke-linecap="round"><path d="M12 3v9"/><path d="M6.3 6.3a8 8 0 1 0 11.4 0"/></svg>
  </button>
</div>
<script>
  // modo: 'senha' (abrir portão) | 'menu' (administrador) | 'nova' (senha nova)
  //       'log' (registro) | 'atualizar' (enviar programa novo)
  let senha = '', modo = 'senha', token = '', enviando = false;
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
    $('visor').style.display = (m === 'log' || m === 'atualizar') ? 'none' : 'flex';
    $('teclado').style.display = (m === 'senha' || m === 'nova') ? 'grid' : 'none';
    $('menu').style.display = m === 'menu' ? 'flex' : 'none';
    $('registro').style.display = m === 'log' ? 'block' : 'none';
    $('atualizar').style.display = m === 'atualizar' ? 'block' : 'none';
    $('btnReiniciar').style.display = m === 'senha' ? 'flex' : 'none';
  }
  function telaSenha() { token = ''; tela('senha', 'Porteiro', '* apaga &nbsp;·&nbsp; # abre'); }
  function telaMenu()  { tela('menu', 'Administrador', ''); aviso(''); }

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

  // Sessão de administrador expirada: volta para a tela inicial
  function expirou(status, j) {
    if (status !== 403) return false;
    telaSenha(); aviso(j.msg, 'erro');
    return true;
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
      if (expirou(status, j)) return;
      if (j.ok) telaMenu();
      aviso(j.msg, j.ok ? 'ok' : 'erro');
    } catch (e) {
      aviso('Sem conexão com o porteiro', 'erro');
    }
  }

  async function verRegistro() {
    aviso('Carregando...');
    try {
      const { status, j } = await post('/registro', { token });
      if (expirou(status, j)) return;
      tela('log', 'Registro', '');
      $('status').textContent = j.status;
      $('lista').textContent = j.log.length ? j.log.join('\n') : 'Nenhum evento ainda.';
      aviso('');
    } catch (e) {
      aviso('Sem conexão com o porteiro', 'erro');
    }
  }

  function telaAtualizar() {
    tela('atualizar', 'Atualizar programa', '');
    $('arquivo').value = ''; $('progresso').value = 0; aviso('');
  }

  // Usa XMLHttpRequest (e não fetch) para mostrar o progresso do envio
  function enviarPrograma() {
    if (enviando) return;
    const f = $('arquivo').files[0];
    if (!f) { aviso('Escolha o arquivo porteiro.ino.bin', 'erro'); return; }
    if (!f.name.endsWith('.bin')) { aviso('O arquivo precisa terminar em .bin', 'erro'); return; }

    const dados = new FormData();
    dados.append('arquivo', f);
    const x = new XMLHttpRequest();
    x.open('POST', '/atualizar?token=' + encodeURIComponent(token));
    x.upload.onprogress = e => {
      if (!e.lengthComputable) return;
      const p = Math.round(e.loaded * 100 / e.total);
      $('progresso').value = p;
      aviso(p < 100 ? 'Enviando... ' + p + '%' : 'Gravando...');
    };
    x.onload = () => {
      enviando = false;
      let j;
      try { j = JSON.parse(x.responseText); } catch (e) { j = { ok: false, msg: 'Resposta inválida do porteiro' }; }
      if (x.status === 403) { telaSenha(); aviso(j.msg, 'erro'); return; }
      if (j.ok) telaSenha();
      aviso(j.msg, j.ok ? 'ok' : 'erro');
    };
    x.onerror = () => { enviando = false; aviso('Falha no envio. Verifique a conexão.', 'erro'); };
    enviando = true;
    aviso('Enviando...');
    x.send(dados);
  }

  async function reiniciar() {
    if (!confirm('Reiniciar o porteiro agora? O registro de eventos será apagado.')) return;
    try {
      const { j } = await post('/reiniciar', {});
      aviso(j.msg, j.ok ? 'ok' : 'erro');
    } catch (e) {
      aviso('Sem conexão com o porteiro', 'erro');
    }
  }
</script>
</body>
</html>
)rawliteral";

String jsonTexto(const String& s) {
  String r = "\"";
  for (char c : s) {
    if (c == '"' || c == '\\') { r += '\\'; r += c; }
    else if (c == '\n') r += "\\n";
    else r += c;
  }
  return r + "\"";
}

void responder(int codigo, bool ok, const char* msg) {
  String json = String("{\"ok\":") + (ok ? "true" : "false") + ",\"msg\":\"" + msg + "\"}";
  server.send(codigo, "application/json; charset=utf-8", json);
}

String ipCliente() {
  return " (" + server.client().remoteIP().toString() + ")";
}

// Confere o token de administrador; renova a sessão se estiver válido
bool tokenAdminOk(const String& token) {
  if (adminToken.length() == 0 || millis() - adminTokenInicio >= ADMIN_SESSAO_MS) {
    adminToken = "";
    return false;
  }
  if (token != adminToken) return false;
  adminTokenInicio = millis();
  return true;
}

bool sessaoAdminValida() {
  if (tokenAdminOk(server.arg("token"))) return true;
  responder(403, false, "Sessão expirada. Digite a senha de administrador de novo.");
  return false;
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
    registrar("Site: senha correta, portão aberto" + ipCliente());
    webErros = 0;
    abrir();
    zelda();
    responder(200, true, "Portão aberto!");
    return;
  }

  if (s.length() > 0 && s == gabaritoADM) {
    registrar("Site: modo administrador" + ipCliente());
    webErros = 0;
    adminToken = String(esp_random(), HEX) + String(esp_random(), HEX);
    adminTokenInicio = agora;
    server.send(200, "application/json; charset=utf-8",
                "{\"ok\":true,\"admin\":true,\"token\":\"" + adminToken + "\",\"msg\":\"Modo administrador\"}");
    return;
  }

  registrar("Site: senha incorreta" + ipCliente());
  webErros++;
  if (webErros >= WEB_MAX_ERROS) {
    webBloqueioInicio = agora;
    registrar("Site: bloqueado por 1 minuto (muitas tentativas)");
  }
  somErro();
  responder(401, false, "Senha incorreta");
}

void handleTrocar() {
  if (!sessaoAdminValida()) return;

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
  registrar("Site: senha do portão alterada" + ipCliente());
#if SERIAL_PRINT
  Serial.print("Nova senha do portao: "); Serial.println(gabarito);
#endif

  somNovaConfiguracao();
  responder(200, true, "Senha alterada!");
}

void handleRegistro() {
  if (!sessaoAdminValida()) return;

  unsigned long s = millis() / 1000;
  String status;
  status += String("Teclado: ") +
            (!tecladoComunica ? "SEM COMUNICAÇÃO (I2C)" :
             tecladoEmQuarentena() ? "ignorando por ruído agora" : "funcionando") + "\n";
  status += "Ligado há " + String(s / 86400) + "d " + String((s / 3600) % 24) + "h " +
            String((s / 60) % 60) + "min\n";
  status += String("Último reinício: ") + motivoReset() + "\n";
  status += "Episódios de ruído: " + String(episodiosRuido) +
            "  ·  Destravamentos do I2C: " + String(totalRecuperacoes) + "\n";
  status += "Wi-Fi: " + String(WiFi.RSSI()) + " dBm";

  String json = "{\"ok\":true,\"status\":" + jsonTexto(status) + ",\"log\":[";
  for (uint8_t i = 0; i < logQtd; i++) {   // mais recente primeiro
    uint8_t idx = (logProx + LOG_TAM - 1 - i) % LOG_TAM;
    if (i) json += ",";
    json += jsonTexto(logMsg[idx]);
  }
  json += "]}";

  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json; charset=utf-8", json);
}

void handleReiniciar() {
  registrar("Site: reinício solicitado" + ipCliente());
  responder(200, true, "Reiniciando... aguarde uns 15 segundos e recarregue a página.");
  reiniciarPedido = true;
  reiniciarInicio = millis();
}

// =======================================================
// ATUALIZAÇÃO DO PROGRAMA
// -------------------------------------------------------
// Pelo site (modo administrador): upload do porteiro.ino.bin.
// Pela Arduino IDE: em Ferramentas → Porta aparece "porteiro at <IP>";
// a IDE pede a senha, que é a mesma senha de administrador.
// O programa novo é gravado numa área separada da memória e só passa a
// valer quando chega inteiro; se falhar no meio, o antigo continua.
// =======================================================
bool atualizacaoAutorizada = false;
bool atualizacaoOk = false;
String atualizacaoErro = "";
bool otaIniciado = false;

// Desliga relé e buzzer antes de gravar (o loop fica parado durante a gravação)
void pararSaidas() {
  digitalWrite(RELE_PIN, RELE_INATIVO);
  releLigado = false;
  noTone(BUZZER_PIN);
  tocando = false;
  filaIni = filaFim;
}

void falhaAtualizacao(const String& msg) {
  if (atualizacaoErro.length() == 0) atualizacaoErro = msg;
  if (Update.isRunning()) Update.abort();
}

void handleAtualizarUpload() {
  HTTPUpload& up = server.upload();

  if (up.status == UPLOAD_FILE_START) {
    atualizacaoOk = false;
    atualizacaoErro = "";
    atualizacaoAutorizada = tokenAdminOk(server.arg("token"));
    if (!atualizacaoAutorizada) return;

    String nome = up.filename;
    nome.toLowerCase();
    if (!nome.endsWith(".bin") || nome.indexOf("bootloader") >= 0 ||
        nome.indexOf("partitions") >= 0 || nome.indexOf("merged") >= 0) {
      falhaAtualizacao("Arquivo errado: envie o porteiro.ino.bin");
      return;
    }

    registrar("Site: atualização do programa iniciada (" + up.filename + ")" + ipCliente());
    pararSaidas();
    if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH))
      falhaAtualizacao(String("Erro ao iniciar: ") + Update.errorString());
  }
  else if (up.status == UPLOAD_FILE_WRITE) {
    if (!atualizacaoAutorizada || atualizacaoErro.length()) return;
    if (Update.write(up.buf, up.currentSize) != up.currentSize)
      falhaAtualizacao(String("Erro ao gravar: ") + Update.errorString());
  }
  else if (up.status == UPLOAD_FILE_END) {
    if (!atualizacaoAutorizada || atualizacaoErro.length()) return;
    if (up.totalSize < 100000) {
      falhaAtualizacao("Arquivo pequeno demais: envie o porteiro.ino.bin");
      return;
    }
    if (Update.end(true)) atualizacaoOk = true;
    else falhaAtualizacao(String("Erro ao finalizar: ") + Update.errorString());
  }
  else if (up.status == UPLOAD_FILE_ABORTED) {
    falhaAtualizacao("Envio interrompido");
  }
}

void handleAtualizarFim() {
  if (!atualizacaoAutorizada) {
    responder(403, false, "Sessão expirada. Digite a senha de administrador de novo.");
  }
  else if (atualizacaoOk) {
    registrar("Site: programa atualizado, reiniciando");
    responder(200, true, "Programa atualizado! Reiniciando... recarregue a página em uns 15 segundos.");
    reiniciarPedido = true;
    reiniciarInicio = millis();
  }
  else {
    if (atualizacaoErro.length() == 0) atualizacaoErro = "Nenhum arquivo recebido";
    registrar("Site: falha na atualização (" + atualizacaoErro + ")");
    responder(400, false, atualizacaoErro.c_str());
  }

  // Cada envio é autorizado de novo
  atualizacaoAutorizada = false;
  atualizacaoOk = false;
}

void configurarOTA() {
  ArduinoOTA.setHostname(HOSTNAME);
  ArduinoOTA.setPassword(gabaritoADM.c_str());
  ArduinoOTA.setMdnsEnabled(false);   // o mDNS é iniciado em atualizarWifi()
  ArduinoOTA.onStart([]() {
    pararSaidas();
    registrar("IDE: atualização do programa iniciada");
  });
  ArduinoOTA.onEnd([]() { registrar("IDE: programa atualizado, reiniciando"); });
  ArduinoOTA.onError([](ota_error_t erro) {
    registrar("IDE: falha na atualização (erro " + String((int)erro) + ")");
  });
}

// Mostra o IP e (re)inicia o mDNS sempre que o Wi-Fi conecta
void atualizarWifi() {
  bool conectado = (WiFi.status() == WL_CONNECTED);

  if (conectado && !wifiConectado) {
    MDNS.end();
    if (MDNS.begin(HOSTNAME)) {
      MDNS.addService("http", "tcp", 80);
      MDNS.enableArduino(3232, true);   // aparece como porta de rede na Arduino IDE
    }
    if (!otaIniciado) { ArduinoOTA.begin(); otaIniciado = true; }
    registrar("Wi-Fi conectado. Site: http://" + WiFi.localIP().toString() +
              "  ou  http://" + HOSTNAME + ".local");
  }
  if (!conectado && wifiConectado) registrar("Wi-Fi desconectado, tentando reconectar...");

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

  registrar(String("Porteiro ligado (motivo: ") + motivoReset() + ")");

  for (byte c : colPins) colMask |= (1 << c);
  for (byte r : rowPins) rowMask |= (1 << r);
  livreMask = ~(colMask | rowMask);

  // Destrava o barramento antes de começar, caso o PCF8574 tenha ficado preso
  destravarI2C();
  pcfOk = pcfDetect(); // acha o endereço e já deixa os pinos em idle

  if (pcfOk) {
    registrar("PCF8574 encontrado no endereço 0x" + String(PCF_ADDR, HEX));
  } else {
    registrar("PCF8574 NÃO ENCONTRADO - verifique cabo SDA/SCL, 3V3 e GND");
  }

  prefs.begin("porteiro", false);
  load_gabarito();

#if SERIAL_PRINT
  Serial.print("Senha carregada da memoria: ");
  Serial.println(gabarito);
#endif

  configurarOTA();

  // Wi-Fi: conecta em segundo plano; o teclado funciona mesmo sem rede
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(HOSTNAME);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_SENHA);

  // Data/hora pela internet, só para o registro de eventos
  configTzTime(FUSO, "pool.ntp.org", "a.st1.ntp.br");

  server.on("/", HTTP_GET, handlePagina);
  server.on("/abrir", HTTP_POST, handleAbrir);
  server.on("/trocar", HTTP_POST, handleTrocar);
  server.on("/registro", HTTP_POST, handleRegistro);
  server.on("/reiniciar", HTTP_POST, handleReiniciar);
  server.on("/atualizar", HTTP_POST, handleAtualizarFim, handleAtualizarUpload);
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
    registrar("Teclado: nova senha igual à de administrador, digite outra");
    somErro();
    novoGabarito = "";
    return;
  }

  gabarito = novoGabarito;
  save_gabarito();
  modoAdmin = false;

  registrar("Teclado: senha do portão alterada");
  somNovaConfiguracao();
  resetar();
}

void loop() {
  server.handleClient();
  if (otaIniciado) ArduinoOTA.handle();
  atualizarSom();
  atualizarRele();
  atualizarWifi();

  if (reiniciarPedido && millis() - reiniciarInicio > 800) ESP.restart();

  char key = getKey();

  // Ruído elétrico detectado: os dígitos recentes podem ser falsos, descarta sem apitar
  if (novoEpisodioRuido) {
    novoEpisodioRuido = false;
    if (senha.length() > 0 || novoGabarito.length() > 0) {
      registrar("Teclado: digitação descartada por ruído (" +
                String(senha.length() + novoGabarito.length()) + " dígito(s))");
      resetar();
      novoGabarito = "";
    }
  }

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
    registrar("Teclado: senha cancelada (*)");
    somErro();
    resetar();
    return;
  }

  if (key == '#') {  // Enviar
    if (senha.length() == 0) {
      registrar("Teclado: # sem senha digitada");
      somErro();
      resetar();
      return;
    }

    comparaGabarito = senha.compareTo(gabarito);
    comparaGabaritoADM = senha.compareTo(gabaritoADM);

    if (comparaGabarito == 0) {
      registrar("Teclado: senha correta, portão aberto");
      abrir();
      zelda();
    }
    else if (comparaGabaritoADM == 0) {
      registrar("Teclado: modo administrador");
      modoAdmin = true;
      novoGabarito = "";
      somNovaConfiguracao();
    }
    else {
      registrar("Teclado: senha incorreta (" + String(senha.length()) + " dígito(s))");
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
    registrar("Teclado: senha muito longa, reiniciando digitação");
    somErro();
    resetar();
  }
}
