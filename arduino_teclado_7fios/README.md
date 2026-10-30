# 📟 Porteiro Eletrônico – Arduino + teclado 3×4 (7 fios via RJ45)

Primeira versão, com a biblioteca `Keypad`.

> 🔑 Antes de compilar, copie o `segredos.exemplo.h` para `segredos.h` e coloque a senha de administrador (`SENHA_ADM`). O `segredos.h` não vai para o git.

Relação de conexão entradas cabo RJ45

| Posição Teclado | Matricial | Cor            | Entrada Arduino |
|-----------------|-----------|----------------|-----------------|
| 1               | Coluna 2  | Azul           | D5              |
| 2               | Linha 1   | Azul+Branco    | D7              |
| 3               | Coluna 1  | Laranja        | D4              |
| 4               | Linha 4   | Laranja+Branco | D10             |
| 5               | Coluna 3  | Marrom         | D6              |
| 6               | Linha 3   | Marrom+Branco  | D9              |
| 7               | Linha 2   | Verde+Branco   | D8              |
