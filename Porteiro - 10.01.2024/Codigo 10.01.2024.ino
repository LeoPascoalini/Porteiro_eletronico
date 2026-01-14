#include <Keypad.h>
#include <EEPROM.h>

//------------------ Variáveis ------------------
String gabarito = "123456";
String gabaritoADM = "246810";
String senha = "";
char caractere;
int comparaGabarito;
int comparaGabaritoADM;
int buzz = 13;
int dig = 0;
const unsigned long timeoutDuration = 10000;
unsigned long lastKeyPressTime = 0;
//----------------------------------------------

//------------------ Teclado --------------------
const byte ROWS = 4;
const byte COLS = 3;

char keys[ROWS][COLS] =
{
  {'1','2','3'},
  {'4','5','6'},
  {'7','8','9'},
  {'*','0','-'}
};

byte rowPins[ROWS] = {7, 8, 9, 10}; // Linhas do teclado
byte colPins[COLS] = {4, 5, 6};     // Colunas do teclado

Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);
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
  delay(1000);
}

void save_gabarito()
{
  for (int i = 0; i < gabarito.length(); i++)
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
}

void setup()
{
  pinMode(buzz, OUTPUT);
  pinMode(2, OUTPUT);
  digitalWrite(2, LOW); // trava o sistema

  Serial.begin(9600);
  load_gabarito();
  Serial.print("Senha carregada da EEPROM: ");
  Serial.println(gabarito);
}

void loop()
{
  char key = keypad.getKey();

  if (key != NO_KEY)
  {
    lastKeyPressTime = millis();

    if (key == '*') // Cancelar
    {
      Serial.println("Senha cancelada.");
      buzzer();
      resetar();
      return;
    }

    if (key == '-') // Enviar
    {
      if (senha.length() == 0)
      {
        Serial.println("Nenhuma senha digitada. Reiniciando.");
        resetar();
        return;
      }

      comparaGabarito = senha.compareTo(gabarito);
      comparaGabaritoADM = senha.compareTo(gabaritoADM);

      if (comparaGabarito == 0)
      {
        Serial.println("Senha correta! Abrindo...");
        abrir();
        zelda();
      }
      else if (comparaGabaritoADM == 0)
      {
        Serial.println("Modo administrador: digite nova senha de 6 dígitos.");
        gabarito = "";
        while (gabarito.length() < 6)
        {
          char newKey = keypad.getKey();
          if (newKey != NO_KEY && isDigit(newKey))
          {
            gabarito += newKey;
            Serial.print("Nova senha: ");
            Serial.println(gabarito);
            buzzer();
          }
        }
        save_gabarito();
        Serial.print("Nova senha salva na EEPROM: ");
        Serial.println(gabarito);
      }
      else
      {
        Serial.println("Senha incorreta.");
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
      Serial.print("Senha atual: ");
      Serial.println(senha);
    }

    // Se a senha for muito longa, reseta
    if (senha.length() > 10)
    {
      Serial.println("Senha muito longa! Reiniciando.");
      resetar();
      buzzer();
    }
  }
}
