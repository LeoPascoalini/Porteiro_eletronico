# 📟 Porteiro Eletrônico – ESP32 + I2C (PCF8574) via RJ45

## 📌 Visão geral

Nova versão do porteiro eletrônico. O funcionamento é **o mesmo da versão 14.01.2026**: senha, modo administrador, timeout, memória da senha e músicas. A mudança está na conexão:

* 🧠 **ESP32** no lugar do Arduino
* 🔌 **Placa I2C (PCF8574)** junto do teclado, ligada ao ESP32 por **um cabo RJ45**
* ⌨️ **Teclado matricial 3×4 padrão** (3 colunas + 4 linhas = 7 conexões) ligado ao PCF8574
* 🔔 **Buzzer** em 2 fios do mesmo RJ45
* 🔓 **Relé** do portão ligado direto no ESP32
* 📱 **Site local** com teclado virtual para abrir o portão pelo celular

---

## 🔌 Cabo RJ45 (padrão T568B)

Cada sinal I2C vai trançado com um GND, o que reduz interferência em cabo longo.

| Par     | Cor              | Função          | Lado ESP32 | Lado teclado     |
| ------- | ---------------- | --------------- | ---------- | ---------------- |
| Laranja | Laranja + Branco | 3V3             | 3V3        | VCC do PCF8574   |
| Laranja | Laranja          | GND             | GND        | GND do PCF8574   |
| Verde   | Verde + Branco   | SDA             | GPIO 21    | SDA do PCF8574   |
| Verde   | Verde            | GND             | GND        | GND              |
| Azul    | Azul + Branco    | SCL             | GPIO 22    | SCL do PCF8574   |
| Azul    | Azul             | GND             | GND        | GND              |
| Marrom  | Marrom + Branco  | Buzzer (sinal)  | GPIO 25    | Buzzer +         |
| Marrom  | Marrom           | Buzzer (GND)    | GND        | Buzzer −         |

> ⚠️ Alimente o PCF8574 com **3V3** (não 5V): assim os níveis do I2C ficam compatíveis com o ESP32.
> Para cabos longos, coloque **pull-ups de 4,7 kΩ** em SDA e SCL para 3V3, a menos que o módulo PCF8574 já tenha (o módulo vermelho PCF8574T já vem com 3,3 kΩ; confira medindo SDA–VCC e SCL–VCC com tudo desligado). O clock está reduzido para 50 kHz (`I2C_CLOCK`); se der erro de leitura, baixe para 10–20 kHz.

---

## 🔓 Relé

| ESP32   | Relé |
| ------- | ---- |
| GPIO 26 | IN   |
| GND     | GND  |

O relé fica acionado por 500 ms ao abrir (`RELE_PULSO_MS`). Se o seu módulo for **ativo em LOW**, mude `RELE_ATIVO` para `LOW` no código.

---

## ⌨️ PCF8574 → Teclado

Teclado matricial 3×4 padrão, com 7 conexões. A varredura coloca uma coluna por vez em LOW e lê as 4 linhas, que ficam em pull-up (o PCF8574 é quase-bidirecional: bit em `1` = pull-up, bit em `0` = LOW).

| PCF8574 | Teclado  | Teclas       |
| ------- | -------- | ------------ |
| P0      | –        | livre        |
| P1      | Linha 3  | 7, 8, 9      |
| P2      | Linha 2  | 4, 5, 6      |
| P3      | Coluna 1 | 1, 4, 7, *   |
| P4      | Linha 1  | 1, 2, 3      |
| P5      | Coluna 3 | 3, 6, 9, #   |
| P6      | Linha 4  | *, 0, #      |
| P7      | Coluna 2 | 2, 5, 8, 0   |

> 💡 A ordem dos pinos no conector do teclado muda de fabricante para fabricante. Identifique as colunas e linhas com um multímetro (modo continuidade, apertando as teclas). Se alguma tecla sair trocada, basta ajustar `colPins[]` / `rowPins[]` no código.
>
> ⚠️ **Coloque um resistor de 10 kΩ de cada linha (P1, P2, P4 e P6) para o VCC.** O pull-up interno do PCF8574 é muito fraco (~100 µA), e a umidade do lado de fora basta para puxar a linha para LOW, gerando teclas fantasmas (aconteceu na instalação, de madrugada). Os resistores ficam em paralelo com a ligação existente: uma perna no pino da linha e a outra no VCC, sem cortar nada. Nas colunas não precisa.

---

## 📱 Site local

O ESP32 se conecta ao Wi-Fi da casa e serve uma página com um **teclado 3×4 virtual**. Ela funciona igual ao teclado físico: digite a senha, `*` apaga e `#` abre.

1. Preencha `WIFI_SSID` e `WIFI_SENHA` no `segredos.h` (veja [Senhas](#-senhas-segredosh)) e grave no ESP32.
2. Abra o Serial Monitor (115200). Quando conectar, ele mostra o endereço do site, por exemplo `http://192.168.0.50`.
3. No celular, conectado ao mesmo Wi-Fi, abra `http://porteiro.local` ou o IP mostrado. Dá para adicionar à tela inicial como atalho.

* A senha é a mesma do teclado físico.
* **Modo administrador pelo site:** digite a senha de administrador e aperte `#`. Aparece a opção **Trocar senha do portão**; digite os 6 dígitos novos (eles aparecem na tela para conferir) e aperte `#` para salvar. O portão toca o som de nova configuração. A sessão expira após 3 minutos sem uso.
* A senha de administrador é fixa no `segredos.h` (`SENHA_ADM`) e não pode ser trocada pelo site nem pelo teclado. A senha do portão não pode ser igual a ela.
* Senha certa pelo site: o relé aciona e a música toca no portão. Senha errada: toca o som de erro.
* Depois de **5 erros seguidos**, o site fica bloqueado por **1 minuto**.
* O teclado físico funciona normalmente mesmo sem Wi-Fi. Se a rede cair, o ESP32 reconecta sozinho.
* O site funciona só dentro da rede da casa. Não abra porta no roteador para ele: a página usa HTTP sem criptografia.

> 💡 Digite o endereço com **`http://`** (sem o "s"). Se o navegador trocar sozinho para `https://`, a página não abre. Depois de abrir, salve como atalho na tela inicial.
>
> 💡 Alguns celulares Android não reconhecem endereços `.local`. Nesse caso, use o IP. Para o IP não mudar, reserve um IP fixo para o ESP32 no roteador (DHCP reservado).

---

## ⚙️ Configurações no código

| Constante      | Padrão  | Descrição                                          |
| -------------- | ------- | -------------------------------------------------- |
| `PCF_ADDR`     | auto    | Detectado ao ligar (0x20–0x27 ou 0x38–0x3F); a posição dos switches A0/A1/A2 não importa |
| `I2C_CLOCK`    | `50000` | Clock do I2C em Hz                                 |
| `RELE_ATIVO`   | `HIGH`  | Nível que aciona o relé                            |
| `SERIAL_PRINT` | `1`     | Mensagens no Serial Monitor (115200 baud)          |
| `HOSTNAME`     | `porteiro` | Nome do site: `http://porteiro.local`          |
| `WEB_MAX_ERROS` / `WEB_BLOQUEIO_MS` | `5` / `60000` | Erros seguidos pelo site até bloquear, e por quanto tempo |

Na inicialização, o Serial Monitor mostra em qual endereço o PCF8574 foi encontrado.

---

## 🔑 Senhas (`segredos.h`)

O Wi-Fi e a senha de administrador ficam no arquivo `segredos.h`, fora do código principal. Ele está no `.gitignore` e não vai para o GitHub.

1. Na primeira vez, copie o `segredos.exemplo.h` para `segredos.h`, na mesma pasta.
2. Preencha `WIFI_SSID`, `WIFI_SENHA` e `SENHA_ADM`.

A Arduino IDE mostra o `segredos.h` como uma aba ao lado do `.ino`. Ele é compilado junto e as senhas vão normalmente para o ESP32.

---

## 💾 Memória

A senha é salva na memória não volátil do ESP32 (`Preferences`), no lugar da EEPROM do Arduino. Ela continua salva depois de desligar. Na primeira gravação deste código no ESP32, a senha volta para o padrão `123456`.

---

## 🛠️ Melhorias pendentes

### Ocorrência de 29.10.2026

Logo de manhã, o teclado físico parou de responder: nenhuma tecla dava bip e o portão não abria. Pelo site funcionava normalmente. Desconectar e reconectar o RJ45 resolveu.

**Causa provável:** o barramento I2C travou. Um ruído no cabo longo pode pegar o PCF8574 no meio de uma leitura, e ele fica segurando o SDA em LOW. A partir daí toda leitura falha, e `pcfRead()` devolve `0xFF` ("nenhuma tecla"), sem som e sem aviso. Reconectar o cabo desliga e religa o PCF8574 (o 3V3 vem pelo RJ45), e isso destrava o barramento. O buzzer não passa pelo I2C, por isso continuou funcionando.

Descartadas:
* Tecla fantasma por umidade: os pull-ups de 10 kΩ nas linhas já estão instalados e o problema não voltou desde então.
* Mau contato no conector: não dá para descartar de vez. Se voltar a acontecer, vale olhar se há oxidação.

### 1. Recuperação automática do I2C (prioridade)

Hoje o código só procura o PCF8574 no `setup()` e nunca percebe quando ele para de responder.

* Contar as falhas seguidas de `pcfWrite()` / `pcfRead()`.
* A partir de umas 20 falhas seguidas, destravar o barramento:
  1. `Wire.end()`
  2. SCL como saída e 9 pulsos manuais, para o PCF8574 soltar o SDA
  3. Gerar uma condição de STOP (SDA de LOW para HIGH com o SCL em HIGH)
  4. `Wire.begin(I2C_SDA, I2C_SCL, I2C_CLOCK)` e `pcfDetect()`
* Zerar o contador quando uma leitura der certo, e mostrar a recuperação no Serial.

São poucas linhas de código, sem impacto no tamanho do programa.

### 2. Tecla presa (opcional, prioridade baixa)

Se a mesma tecla aparecer pressionada por mais de ~5 s, tratar como presa e liberar o debounce (`kp_pressed`). Com os pull-ups instalados, isso é só uma proteção extra.

### 3. Página de diagnóstico no site (opcional)

Uma rota `/status` em texto simples, sem HTML: se o PCF8574 responde, o endereço dele, o total de erros de I2C, quantas recuperações foram feitas e há quanto tempo o ESP32 está ligado. Serve para conferir pelo celular, antes de mexer no cabo, se o problema é no I2C.

> 💡 Sobre o espaço: a maior parte do programa já é Wi-Fi + WebServer, e isso já está incluído. Uma rota de texto puro acrescenta poucas centenas de bytes. Antes e depois da mudança, compare a linha "O sketch usa X bytes (Y%)" na compilação da Arduino IDE.

---

## 🧾 Autoria

**Leonardo Pascoalini de Souza**

O método de teclado 4×3 com 6 fios continua disponível na pasta [`arduino_teclado_6fios`](../arduino_teclado_6fios/).
