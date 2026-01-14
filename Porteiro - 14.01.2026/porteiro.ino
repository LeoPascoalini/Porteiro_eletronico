/*
  PORTEIRO ELETRÔNICO + Keypad 6 fios (scan híbrido)

  Keypad (6 fios):
    colPins: C1=8, C2=9, C3=10
    rowPins: L1=7, L2=6, L3=5

  Teclas:
    - Dígitos: '0'..'9'
    - Cancelar: '*'
    - Enviar:   '#'   (no seu esquema novo, "#" é gerado na fase extra)

  Observação:
    - Removido uso da biblioteca <Keypad.h>
    - Agora o código usa getKey() abaixo (mesmo estilo do Keypad)
*/

#include <EEPROM.h>

// =======================================================
// CONFIG: SERIAL
// =======================================================
#define SERIAL_PRINT 1

// =======================================================
// KEYPAD 6 FIOS (listas)
// =======================================================
const byte colPins[] = {8, 9, 10}; // C1, C2, C3
const byte rowPins[] = {7, 6, 5};  // L1, L2, L3

// Layout visual padrão
const char keypadLayout[4][3] = {
  {'1','2','3'},
  {'4','5','6'},
  {'7','8','9'},
  {'*','0','#'}
};

// Timing/debounce do keypad
const unsigned int settleUs   = 150;
const unsigned int debounceMs = 30;
const unsigned int releaseMs  = 20;

char kp_lastCandidate = 0;
unsigned long kp_candidateSince = 0;
bool kp_pressed = false;
unsigned long kp_releasedSince = 0;

// ---------- IO helpers ----------
static inline void hiZ(byte p) { pinMode(p, INPUT); }
static inline void pullup(byte p) { pinMode(p, INPUT_PULLUP); }
static inline void driveLow(byte p) { digitalWrite(p, LOW); pinMode(p, OUTPUT); }
static inline bool isLow(byte p) { return digitalRead(p) == LOW; }

static inline void idleAllPins() {
  for (byte c : colPins) hiZ(c);
  for (byte r : rowPins) hiZ(r);
}

// Ativa um sender (LOW) e prepara os receivers (linhas em INPUT_PULLUP)
static inline void activateSender(byte senderPin) {
  idleAllPins();
  for (byte r : rowPins) pullup(r);
  driveLow(senderPin);
  delayMicroseconds(settleUs);
}

// Scan cru (sem debounce)
char scanRawKeypad6w() {

  // Fase 1: colunas -> linhas (1..9)
  for (byte col = 0; col < 3; col++) {
    activateSender(colPins[col]);

    for (byte row = 0; row < 3; row++) {
      if (isLow(rowPins[row])) {
        idleAllPins();
        return keypadLayout[row][col];
      }
    }
  }

  // Fase 2: linhas auxiliares (* 0 #)
  // L1 envia: L2 recebe = '#', L3 recebe = '*'
  activateSender(rowPins[0]); // L1
  if (isLow(rowPins[1])) { idleAllPins(); return keypadLayout[3][2]; } // '#'
  if (isLow(rowPins[2])) { idleAllPins(); return keypadLayout[3][0]; } // '*'

  // L2 envia: L3 recebe = '0'
  activateSender(rowPins[1]); // L2
  if (isLow(rowPins[2])) { idleAllPins(); return keypadLayout[3][1]; } // '0'

  idleAllPins();
  return 0;
}

// Retorna 1 tecla por pressão (com debounce e sem repetir segurando)
char getKey() {
  unsigned long now = millis();
  char k = scanRawKeypad6w();

  // Já pressionado: só libera quando soltar estável
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

  // Debounce do candidato
  if (k != kp_lastCandidate) {
    kp_lastCandidate = k;
    kp_candidateSince = now;
    return 0;
  }

  // Confirma após estabilidade
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
const int buzz = 11;
int dig = 0;

const unsigned long timeoutDuration = 10000; // 10s
unsigned long lastKeyPressTime = 0;
//----------------------------------------------

//------------------ Funções --------------------
void buzzer()
{
  tone(buzz, 1568);
  delay(150);
  noTone(buzz);
}

void zelda()
{
  tone(buzz, 1568); delay(200);
  tone(buzz, 1481); delay(200);
  tone(buzz, 1246); delay(200);
  tone(buzz, 880);  delay(200);
  tone(buzz, 826);  delay(200);
  tone(buzz, 1318); delay(200);
  tone(buzz, 1664); delay(150);
  tone(buzz, 2093); delay(250);
  noTone(buzz);
}

void abrir()
{
  digitalWrite(2, HIGH);
  delay(500);
  digitalWrite(2, LOW);
}

void resetar()
{
  senha = "";
  dig = 0;
  delay(150);
}

void save_gabarito()
{
  for (int i = 0; i < (int)gabarito.length(); i++)
    EEPROM.write(i, gabarito[i]);
  EEPROM.write(gabarito.length(), '\0');
}

void load_gabarito()
{
  gabarito = "";
  for (int i = 0; i < 50; i++)
  {
    caractere = EEPROM.read(i);
    if (caractere == '\0') break;
    gabarito += caractere;
  }

  // Se EEPROM vazia / lixo, mantém padrão
  if (gabarito.length() == 0) gabarito = "123456";
}

// Timeout: se passou tempo demais sem tecla, cancela a senha
void checkTimeout()
{
  if (senha.length() > 0 && (millis() - lastKeyPressTime) > timeoutDuration)
  {
#if SERIAL_PRINT
    Serial.println("Timeout: senha cancelada.");
#endif
    resetar();
  }
}

void setup()
{
  pinMode(buzz, OUTPUT);

  // Saída que aciona fechadura
  pinMode(2, OUTPUT);
  digitalWrite(2, LOW); // trava o sistema

  // Deixa pinos do keypad em estado seguro
  idleAllPins();

#if SERIAL_PRINT
  Serial.begin(9600);
#endif

  load_gabarito();

#if SERIAL_PRINT
  Serial.print("Senha carregada da EEPROM: ");
  Serial.println(gabarito);
#endif
}

void loop()
{
  checkTimeout();

  char key = getKey();   // <<<<<< aqui está a troca principal (sem Keypad.h)

  if (key)
  {
    lastKeyPressTime = millis();

    if (key == '*') // Cancelar
    {
#if SERIAL_PRINT
      Serial.println("Senha cancelada.");
#endif
      buzzer();
      resetar();
      return;
    }

    if (key == '#') // Enviar
    {
      if (senha.length() == 0)
      {
#if SERIAL_PRINT
        Serial.println("Nenhuma senha digitada. Reiniciando.");
#endif
        resetar();
        return;
      }

      comparaGabarito = senha.compareTo(gabarito);
      comparaGabaritoADM = senha.compareTo(gabaritoADM);

      if (comparaGabarito == 0)
      {
#if SERIAL_PRINT
        Serial.println("Senha correta! Abrindo...");
#endif
        abrir();
        zelda();
      }
      else if (comparaGabaritoADM == 0)
      {
#if SERIAL_PRINT
        Serial.println("Modo administrador: digite nova senha de 6 dígitos.");
#endif
        gabarito = "";

        // Captura nova senha (somente dígitos)
        while (gabarito.length() < 6)
        {
          char newKey = getKey();
          if (newKey && isDigit(newKey))
          {
            gabarito += newKey;
#if SERIAL_PRINT
            Serial.print("Nova senha: ");
            Serial.println(gabarito);
#endif
            buzzer();
          }
        }

        save_gabarito();

#if SERIAL_PRINT
        Serial.print("Nova senha salva na EEPROM: ");
        Serial.println(gabarito);
#endif
      }
      else
      {
#if SERIAL_PRINT
        Serial.println("Senha incorreta.");
#endif
        buzzer();
      }

      resetar(); // Sempre resetar após enviar
      return;
    }

    // Se for dígito, adiciona à senha
    if (isDigit(key))
    {
      senha += key;
      dig++;
      buzzer();

#if SERIAL_PRINT
      Serial.print("Senha atual: ");
      Serial.println(senha);
#endif
    }

    // Se a senha for muito longa, reseta
    if (senha.length() > 10)
    {
#if SERIAL_PRINT
      Serial.println("Senha muito longa! Reiniciando.");
#endif
      resetar();
      buzzer();
    }
  }
}
