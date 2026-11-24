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
* **Modo administrador pelo site:** digite a senha de administrador e aperte `#`. A sessão expira após 3 minutos sem uso. Opções:
  * **Trocar senha do portão:** digite os 6 dígitos novos (eles aparecem na tela para conferir) e aperte `#` para salvar. O portão toca o som de nova configuração.
  * **Atualizar programa:** envia um programa novo pelo site (veja *Atualizar o programa sem USB*).
  * **Ver registro:** situação do teclado e os últimos 60 eventos com data e hora (aberturas, senhas erradas, ruído, falhas de comunicação, motivo do último reinício). Senhas nunca aparecem no registro. O registro é apagado quando o ESP32 reinicia.
* **Reiniciar porteiro:** botão redondo com o símbolo de ligar/desligar, no canto inferior esquerdo da tela inicial. Pede confirmação e reinicia o ESP32 em cerca de 15 segundos.
* A senha de administrador é fixa no código (`gabaritoADM`) e não pode ser trocada pelo site nem pelo teclado. A senha do portão não pode ser igual a ela.
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

## 🔄 Atualizar o programa sem USB

A primeira gravação com esta versão precisa ser pelo USB. Depois disso, dá para atualizar pelo Wi-Fi de dois jeitos:

**Pela Arduino IDE**
1. Em **Ferramentas → Porta**, escolha `porteiro at 192.168.x.x` (porta de rede).
2. Grave normalmente. A IDE pede uma senha: é a **senha de administrador**.

**Pelo site (dá até pelo celular)**
1. Na Arduino IDE: **Sketch → Exportar binário compilado**. A IDE cria a pasta `build` dentro da pasta do projeto.
2. No site, entre no modo administrador → **Atualizar programa** → escolha o arquivo **`esp32_i2c.ino.bin`** → **Enviar**.
3. O porteiro grava, reinicia sozinho e volta em uns 15 segundos.

Os outros arquivos da pasta `build` (`.bootloader.bin`, `.partitions.bin`, `.merged.bin`) são recusados. A pasta `build` está no `.gitignore`.

Cuidados:

* **Sempre grave com o nome e a senha do Wi-Fi preenchidos.** Se o programa enviado não conectar no Wi-Fi, a gravação sem fio deixa de funcionar e é preciso usar o USB de novo. O Wi-Fi fica no `segredos.h` (veja [Senhas](#-senhas-segredosh)).
* Em **Ferramentas → Partition Scheme**, use **Default 4MB with spiffs**. Esquemas "No OTA" desativam a gravação sem fio.
* Se a energia cair no meio da gravação, o programa antigo continua funcionando: o novo só passa a valer quando chega inteiro.
* Durante a gravação (uns 20 a 40 s), o teclado e o site param de responder e o relé fica desligado.
* Quem souber a senha de administrador e estiver no seu Wi-Fi consegue gravar um programa novo no porteiro.

---

## ⚡ Ruído elétrico (motor do portão de carro)

O motor do portão basculante gera picos elétricos que podem chegar ao teclado pelo cabo. O código se protege assim:

* **Leituras impossíveis são tratadas como ruído:** falha na comunicação I2C, pino em LOW sem nenhuma coluna ativa, coluna ativa que não aparece em LOW ou pino livre (P0) em LOW.
* **Quarentena:** ao detectar ruído, o teclado fica 1,5 s ignorando tudo (`QUARENTENA_MS`) e a digitação em andamento é descartada sem apitar.
* **Destravamento automático do I2C:** um pico pode deixar o PCF8574 travado segurando o SDA, e o teclado para de responder. Depois de 10 falhas seguidas, o ESP32 gera pulsos no SCL para destravar, reinicia o I2C e procura o PCF8574 de novo (a cada 3 s, enquanto não voltar).
* **Tecla presa:** se uma tecla ficar "apertada" por mais de 10 s, isso aparece no registro (sinal de umidade ou defeito).

Hardware recomendado:

* **Pull-up de 10 kΩ** de cada linha (P1, P2, P4, P6) para o 3V3 na placa do PCF8574. O pull-up interno do PCF8574 é muito fraco e deixa o teclado sensível a umidade.
* **Capacitor de 100 nF** (cerâmico, "104") entre VCC e GND na placa do PCF8574.
* Cabo do porteiro separado dos fios do motor, ferrites no cabo RJ45 e motor aterrado.

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

## 🛠️ Ocorrências

### Ocorrência de 29.10.2026

Logo de manhã, o teclado físico parou de responder: nenhuma tecla dava bip e o portão não abria. Pelo site funcionava normalmente. Desconectar e reconectar o RJ45 resolveu.

**Causa provável:** o barramento I2C travou. Um ruído no cabo longo pode pegar o PCF8574 no meio de uma leitura, e ele fica segurando o SDA em LOW. A partir daí toda leitura falha, e `pcfRead()` devolve `0xFF` ("nenhuma tecla"), sem som e sem aviso. Reconectar o cabo desliga e religa o PCF8574 (o 3V3 vem pelo RJ45), e isso destrava o barramento. O buzzer não passa pelo I2C, por isso continuou funcionando.

Descartadas:
* Tecla fantasma por umidade: os pull-ups de 10 kΩ nas linhas já estão instalados e o problema não voltou desde então.
* Mau contato no conector: não dá para descartar de vez. Se voltar a acontecer, vale olhar se há oxidação.

Resolvido com a versão de 13.11.2026: destravamento automático do I2C, aviso de tecla presa e situação do teclado na tela de registro do site.

### Novembro de 2026: teclado volta a parar depois de horas ou dias

Com o destravamento automático, o teclado ainda para de vez em quando. O registro mostra:

* Rajadas de erro de I2C de 10–20 s, várias vezes por dia, que o destravamento resolve em poucos segundos.
* Muito mais destravamentos do que episódios de ruído (23540 contra 279 em 5 dias). Como o código tenta a cada 3 s, isso indica períodos longos (horas) sem comunicação, em que os pulsos no SCL e o reinício do `Wire` não trazem o PCF8574 de volta.
* Ruído logo depois de o próprio portão abrir (o relé aciona a fechadura elétrica).
* Às vezes o teclado volta depois de insistir nas teclas. Ponto cego do código: se as falhas forem intermitentes (nunca 10 seguidas), não há destravamento nem registro, mas cada falha renova a quarentena de 1,5 s e o teclado ignora tudo calado.
* Tirar e recolocar a energia sempre resolve.

Testes pendentes:

* Abrir o portão pelo site várias vezes e ver se cada abertura gera ruído no registro. Repetir com o fio da fechadura solto do relé. Se a fechadura for a fonte, colocar diodo (fechadura DC) ou varistor/snubber RC (fechadura AC) nos terminais dela.
* Na próxima parada, anotar a situação do teclado no registro ("funcionando", "SEM COMUNICAÇÃO" ou "ignorando por ruído") antes de reiniciar, e testar o botão EN da placa antes de tirar a energia.

---

## 🧾 Autoria

**Leonardo Pascoalini de Souza**

O método de teclado 4×3 com 6 fios continua disponível na pasta [`arduino_teclado_6fios`](../arduino_teclado_6fios/).
