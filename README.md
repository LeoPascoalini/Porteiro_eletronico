# 📟 Porteiro Eletrônico

Porteiro eletrônico com teclado matricial e buzzer no portão, ligados à placa por um único cabo RJ45. Digitando a senha certa, o relé abre o portão e toca uma música.

## 📂 Versões

Cada pasta é uma montagem de hardware diferente, com o próprio código e README.

| Pasta | Placa | Teclado | Situação |
| ----- | ----- | ------- | -------- |
| [`esp32_i2c`](esp32_i2c/) | ESP32 | 3×4 via PCF8574 (I2C), + site local pelo Wi-Fi | ✅ **Atual** (19.10.2026) |
| [`arduino_teclado_6fios`](arduino_teclado_6fios/) | Arduino | 4×3 com 6 fios (método próprio de varredura) | Anterior (14.01.2026) |
| [`arduino_teclado_7fios`](arduino_teclado_7fios/) | Arduino | 3×4 com 7 fios (biblioteca `Keypad`) | Primeira versão (10.01.2024) |

## 🔑 Senhas

As senhas ficam no `segredos.h` de cada pasta, que **não vai para o git**:

1. Copie o `segredos.exemplo.h` para `segredos.h`, na mesma pasta.
2. Preencha os valores (senha de administrador e, no ESP32, o Wi-Fi).

O `segredos.h` é compilado junto com o `.ino` e aparece como uma aba na Arduino IDE.

## 🧾 Autoria

**Leonardo Pascoalini de Souza**
