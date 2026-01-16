#include <EEPROM.h>

// =======================================================

#define SERIAL_PRINT 1 // 1 para ativar prints no Serial Monitor; 0 para desativar

const byte colPins[] = {7, 6, 5};    // C1, C2, C3
const byte rowPins[] = {10, 9, 8};   // 3 fios físicos (ordem top->bottom das 3 linhas NÃO-BASE)

// Layout visual (o que deve retornar)
const char keypadLayout[4][3] = {
  {'1','2','3'},
  {'4','5','6'},
  {'7','8','9'},
  {'*','0','#'}
};

// base_jumped_to = {baseRow, jumpedRow}
const byte base_jumped_to[2] = {0, 1};   // exemplo: base é a linha 0; jumpeada com linha 1

// return_def = {returnRow, baseCol}
const byte return1_def[2] = {2, 0};      // retorno1 = linha 2 do layout, ligado à tecla base(col 0)
const byte return2_def[2] = {3, 1};      // retorno2 = linha 3 do layout, ligado à tecla base(col 1)

// Timing debounce
const unsigned int settleUs   = 150;
const unsigned int debounceMs = 30;
const unsigned int releaseMs  = 20;

char kp_lastCandidate = 0;
unsigned long kp_candidateSince = 0;
bool kp_pressed = false;
unsigned long kp_releasedSince = 0;

// normalRows[] = as 3 linhas do layout em ordem (top->bottom), pulando a baseRow
byte normalRows[3] = {0, 1, 2};

// Teclas inferidas para o caso 2
char case2_key_base_ret1 = 0;
char case2_key_base_ret2 = 0;
char case2_key_ret1_ret2 = 0;

// Índices físicos (dentro de rowPins[]) calculados automaticamente
int8_t baseLineIdx = -1;
int8_t ret1LineIdx = -1;
int8_t ret2LineIdx = -1;

// =======================================================
// IO helpers
// =======================================================
static inline void hiZ(byte p) { pinMode(p, INPUT); }
static inline void pullup(byte p) { pinMode(p, INPUT_PULLUP); }
static inline void driveLow(byte p) { digitalWrite(p, LOW); pinMode(p, OUTPUT); }
static inline bool isLow(byte p) { return digitalRead(p) == LOW; }

static inline void idleAllPins() {
  for (byte c : colPins) hiZ(c);
  for (byte r : rowPins) hiZ(r);
}

// Ativa um sender (LOW) e prepara os receivers (rowPins em INPUT_PULLUP)
static inline void activateSender(byte senderPin) {
  idleAllPins();
  for (byte r : rowPins) pullup(r);
  driveLow(senderPin);
  delayMicroseconds(settleUs);
}

// =======================================================
// UTIL: construir normalRows (top->bottom pulando baseRow)
// =======================================================
static void buildNormalRows(byte baseRow) {
  byte idx = 0;
  for (byte r = 0; r < 4; r++) {
    if (r == baseRow) continue;
    if (idx < 3) normalRows[idx++] = r;
  }
}

// =======================================================
// UTIL: inferir as 3 teclas do caso 2 pela geometria
// =======================================================
static byte remainingCol(byte c1, byte c2) {
  for (byte c = 0; c < 3; c++) {
    if (c != c1 && c != c2) return c;
  }
  return 0;
}

static void computeCase2KeysFromGeometry() {
  const byte baseRow = base_jumped_to[0];
  const byte c1 = return1_def[1];
  const byte c2 = return2_def[1];

  case2_key_base_ret1 = 0;
  case2_key_base_ret2 = 0;
  case2_key_ret1_ret2 = 0;

  if (baseRow > 3) return;
  if (c1 > 2 || c2 > 2 || c1 == c2) return;

  const byte c3 = remainingCol(c1, c2);

  case2_key_base_ret1 = keypadLayout[baseRow][c1];
  case2_key_base_ret2 = keypadLayout[baseRow][c2];
  case2_key_ret1_ret2 = keypadLayout[baseRow][c3];
}

// =======================================================
// UTIL: mapear "linha do layout" -> índice físico em rowPins[]
// (mesma premissa da fase 1)
// =======================================================
static int8_t findPhysIndexForLayoutRow(byte layoutRow) {
  for (byte phys = 0; phys < 3; phys++) {
    if (normalRows[phys] == layoutRow) return (int8_t)phys;
  }
  return -1;
}

static void buildCase2PhysicalMap() {
  const byte ret1LayoutRow = return1_def[0];
  const byte ret2LayoutRow = return2_def[0];

  ret1LineIdx = findPhysIndexForLayoutRow(ret1LayoutRow);
  ret2LineIdx = findPhysIndexForLayoutRow(ret2LayoutRow);

  baseLineIdx = -1;
  for (byte phys = 0; phys < 3; phys++) {
    if ((int8_t)phys != ret1LineIdx && (int8_t)phys != ret2LineIdx) {
      baseLineIdx = (int8_t)phys;
      break;
    }
  }
}

// =======================================================
// KEYPAD: scan cru (sem debounce)
// =======================================================
char scanRawKeypad6w() {

  // Fase 1: colunas -> linhas (layout-driven, pulando a base)
  for (byte col = 0; col < 3; col++) {
    activateSender(colPins[col]);

    for (byte phys = 0; phys < 3; phys++) {
      if (!isLow(rowPins[phys])) continue;

      const byte layoutRow = normalRows[phys];
      idleAllPins();
      return keypadLayout[layoutRow][col];
    }
  }

  // Fase 2: jumper (base->ret1, base->ret2, ret1->ret2)
  if (!case2_key_base_ret1 || !case2_key_base_ret2 || !case2_key_ret1_ret2) {
    idleAllPins();
    return 0;
  }
  if (baseLineIdx < 0 || ret1LineIdx < 0 || ret2LineIdx < 0) {
    idleAllPins();
    return 0;
  }

  activateSender(rowPins[baseLineIdx]);
  if (isLow(rowPins[ret1LineIdx])) { idleAllPins(); return case2_key_base_ret1; }
  if (isLow(rowPins[ret2LineIdx])) { idleAllPins(); return case2_key_base_ret2; }

  activateSender(rowPins[ret1LineIdx]);
  if (isLow(rowPins[ret2LineIdx])) { idleAllPins(); return case2_key_ret1_ret2; }

  idleAllPins();
  return 0;
}

// =======================================================
// KEYPAD: debounce (1 evento por clique)
// =======================================================
char getKey() {
  unsigned long now = millis();
  char k = scanRawKeypad6w();

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
// PORTEIRO ELETRÔNICO
// =======================================================

//------------------ Variáveis ------------------
String gabarito = "123456";
String gabaritoADM = "246810";
String senha = "";
char caractere;
int comparaGabarito;
int comparaGabaritoADM;

const int BUZZER_PIN = 11; // <<<<<< agora padronizado
int dig = 0;

const unsigned long timeoutDuration = 10000;
unsigned long lastKeyPressTime = 0;
//----------------------------------------------

//------------------ Sons (novos) ----------------
void somTecla() {
  tone(BUZZER_PIN, 1200); delay(40); noTone(BUZZER_PIN);
}

void somErro() {
  tone(BUZZER_PIN, 400); delay(200);
  tone(BUZZER_PIN, 250); delay(300);
  noTone(BUZZER_PIN);
}

void somReajuste() {
  tone(BUZZER_PIN, 600); delay(120);
  tone(BUZZER_PIN, 900); delay(120);
  tone(BUZZER_PIN, 1200); delay(180);
  noTone(BUZZER_PIN);
}

void somNovaConfiguracao() {
  tone(BUZZER_PIN, 800); delay(100);
  tone(BUZZER_PIN, 1200); delay(100);
  tone(BUZZER_PIN, 1600); delay(120);
  tone(BUZZER_PIN, 1200); delay(150);
  noTone(BUZZER_PIN);
}

// Atualizado conforme pedido
void zelda() {
  const int d = 125;   // duração base
  const int gap = 12;  // pausa curtinha

  tone(BUZZER_PIN, 1568); delay(d); noTone(BUZZER_PIN); delay(gap);  // F5
  tone(BUZZER_PIN, 1481); delay(d); noTone(BUZZER_PIN); delay(gap);  // E5
  tone(BUZZER_PIN, 1246); delay(d); noTone(BUZZER_PIN); delay(gap);  // C4
  tone(BUZZER_PIN, 880); delay(d); noTone(BUZZER_PIN); delay(gap);  // G4
  tone(BUZZER_PIN, 826); delay(d); noTone(BUZZER_PIN); delay(gap);  // F4
  tone(BUZZER_PIN, 1318); delay(d); noTone(BUZZER_PIN); delay(gap);  // E5
  tone(BUZZER_PIN, 1664); delay(d); noTone(BUZZER_PIN); delay(gap);  // G5
  tone(BUZZER_PIN, 2093); delay(d); noTone(BUZZER_PIN); delay(gap);  // B5

  noTone(BUZZER_PIN);
  delay(500);
}
//----------------------------------------------

//------------------ Funções --------------------
void abrir() {
  digitalWrite(2, HIGH);
  delay(500);
  digitalWrite(2, LOW);
}

void resetar() {
  senha = "";
  dig = 0;
  delay(150);
}

void save_gabarito() {
  for (int i = 0; i < (int)gabarito.length(); i++) EEPROM.write(i, gabarito[i]);
  EEPROM.write(gabarito.length(), '\0');
}

void load_gabarito() {
  gabarito = "";
  for (int i = 0; i < 50; i++) {
    caractere = EEPROM.read(i);
    if (caractere == '\0') break;
    gabarito += caractere;
  }
  if (gabarito.length() == 0) gabarito = "123456";
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

void setup() {
  pinMode(BUZZER_PIN, OUTPUT);

  pinMode(2, OUTPUT);
  digitalWrite(2, LOW);

  idleAllPins();

#if SERIAL_PRINT
  Serial.begin(9600);
#endif

  buildNormalRows(base_jumped_to[0]);
  computeCase2KeysFromGeometry();
  buildCase2PhysicalMap();

#if SERIAL_PRINT
  Serial.print("baseRow(layout)="); Serial.println(base_jumped_to[0]);
  Serial.print("normalRows: ");
  Serial.print(normalRows[0]); Serial.print(", ");
  Serial.print(normalRows[1]); Serial.print(", ");
  Serial.println(normalRows[2]);

  Serial.print("Case2 keys: base->ret1="); Serial.print(case2_key_base_ret1);
  Serial.print(" base->ret2="); Serial.print(case2_key_base_ret2);
  Serial.print(" ret1->ret2="); Serial.println(case2_key_ret1_ret2);

  Serial.print("Case2 phys idx: baseLineIdx="); Serial.print(baseLineIdx);
  Serial.print(" ret1LineIdx="); Serial.print(ret1LineIdx);
  Serial.print(" ret2LineIdx="); Serial.println(ret2LineIdx);
#endif

  load_gabarito();

#if SERIAL_PRINT
  Serial.print("Senha carregada da EEPROM: ");
  Serial.println(gabarito);
#endif

  somReajuste();
}

void loop() {
  checkTimeout();

  char key = getKey();
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
      gabarito = "";
      somNovaConfiguracao();

      while (gabarito.length() < 6) {
        char newKey = getKey();
        if (newKey && isDigit(newKey)) {
          gabarito += newKey;
#if SERIAL_PRINT
          Serial.print("Nova senha: ");
          Serial.println(gabarito);
#endif
          somTecla();
        }
      }

      save_gabarito();

#if SERIAL_PRINT
      Serial.print("Nova senha salva na EEPROM: ");
      Serial.println(gabarito);
#endif
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
