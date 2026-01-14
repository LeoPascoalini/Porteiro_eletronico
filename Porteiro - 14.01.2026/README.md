# 📟 Porteiro Eletrônico com Teclado Matricial 4×3 (6 fios via RJ45)

## 📌 Visão geral

Esta é a **nova versão do porteiro eletrônico**, adaptada para utilizar:

* 🔔 **Buzzer**
* ⌨️ **Teclado matricial 4×3**
* 🔌 **Apenas 6 pinos de I/O**, permitindo que **toda a comunicação seja feita por um único cabo RJ45**

O teclado matricial utilizado neste projeto foi **desenvolvido por mim, Leonardo Pascoalini de Souza**, e utiliza um método próprio de varredura elétrica que explora **todas as combinações distintas possíveis** entre colunas e linhas para identificar cada tecla de forma **determinística**, mesmo com um número reduzido de fios.

---

## 🧠 Conceito do teclado 4×3 com 6 fios

Diferente de um teclado matricial tradicional, neste modelo:

* **Colunas (C) e Linhas (L)** podem atuar tanto como **emissoras** quanto como **receptoras de sinal**
* As **linhas permanecem como receptoras**
* Em algumas etapas da varredura, **linhas também atuam como emissoras**
* Cada combinação única **(emissor → receptor)** corresponde a **uma única tecla**

Isso permite reduzir o número total de conexões físicas sem perda de funcionalidade.

---

## ⌨️ Layout físico do teclado

```
[ 1 ]  [ 2 ]  [ 3 ]
[ 4 ]  [ 5 ]  [ 6 ]
[ 7 ]  [ 8 ]  [ 9 ]
[ * ]  [ 0 ]  [ # ]
```

---

## 🔁 Mapeamento lógico de varredura

### 🔹 Colunas enviando, linhas recebendo

| Emissor | Receptor | Tecla |
| ------- | -------- | ----- |
| C1      | L1       | 3     |
| C1      | L2       | 6     |
| C1      | L3       | 9     |
| C2      | L1       | 2     |
| C2      | L2       | 5     |
| C2      | L3       | 8     |
| C3      | L1       | 1     |
| C3      | L2       | 4     |
| C3      | L3       | 7     |

---

### 🔹 Linhas enviando, linhas recebendo (fase extra)

| Emissor | Receptor | Tecla |
| ------- | -------- | ----- |
| L1      | L2       | #     |
| L1      | L3       | *     |
| L2      | L3       | 0     |

> 💡 Essa “fase extra” é o que permite identificar `*`, `0` e `#` sem adicionar novos fios.

---

## 🔌 Conexões via cabo RJ45 (⚠️ **necessário reajuste**)

A tabela abaixo descreve a relação atual entre teclado, cabo RJ45 e pinos do Arduino.
*(Observação: os nomes “Linha 4” referem-se à posição física do teclado, não a uma linha elétrica adicional.)*

| Posição no teclado | Matricial | Cor do cabo RJ45 | Pino Arduino |
| ------------------ | --------- | ---------------- | ------------ |
| 1                  | Coluna 2  | Azul             | D5           |
| 2                  | Linha 1   | Azul + Branco    | D7           |
| 3                  | Coluna 1  | Laranja          | D4           |
| 4                  | Linha 4   | Laranja + Branco | D10          |
| 5                  | Coluna 3  | Marrom           | D6           |
| 6                  | Linha 3   | Marrom + Branco  | D9           |
| 7                  | Linha 2   | Verde + Branco   | D8           |

---

## ✅ Vantagens do método

* ✔ Apenas **6 fios** para um teclado 4×3 completo
* ✔ Compatível com **cabo RJ45**
* ✔ Elimina dependência da biblioteca `Keypad`
* ✔ Controle total do hardware e do firmware
* ✔ Fácil integração com sistemas embarcados (porteiros, fechaduras, controle de acesso)

---

## 🧾 Autoria

**Desenvolvimento do método de teclado matricial 4×3 com 6 fios:**
**Leonardo Pascoalini de Souza**

---