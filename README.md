# HardStress

**HardStress** é uma aplicação desenvolvida em linguagem C com interface gráfica baseada em GTK3, destinada à realização de testes de estresse em sistemas multicore. O programa permite avaliar o desempenho de unidades de processamento (CPU) e memória por meio de carga computacional controlada, apresentando resultados em tempo real com suporte à exportação de dados.

---

## Objetivos

O objetivo principal do HardStress é prover uma ferramenta de diagnóstico e avaliação de desempenho que:

* Gere carga computacional intensa utilizando múltiplas threads.
* Permita visualização gráfica do uso de CPU e iterações por thread.
* Ofereça funcionalidades de monitoramento térmico do sistema.
* Exporte os resultados obtidos em formato CSV para posterior análise.

---

## Funcionalidades

* Execução de testes de estresse utilizando threads independentes.
* Suporte a operações sobre ponto flutuante, inteiros e alocação de memória.
* Visualização gráfica em tempo real:

  * Uso da CPU por núcleo.
  * Iterações realizadas por thread.
* Exportação dos dados em formato `.csv`.
* Monitoramento da temperatura do processador:

  * Em sistemas Linux, via biblioteca `lm-sensors`.
  * Em sistemas Windows, via interface WMI.
* Definição da duração do teste e afinidade de CPU (opcional).

---

## Compilação

### Em sistemas Linux

**Dependências:** `build-essential`, `libgtk-3-dev`

```bash
sudo apt install build-essential libgtk-3-dev
make
```

### Em sistemas Windows (via MSYS2)

**Dependências:** `mingw-w64-x86_64-toolchain`, `mingw-w64-x86_64-gtk3`

```bash
pacman -S mingw-w64-x86_64-toolchain mingw-w64-x86_64-gtk3
make
```

---

## Estrutura do Código

O código-fonte principal está contido em um único arquivo:
**`hardstress.c`**

---

## Dependências Adicionais

| Plataforma | Dependência  | Observação                            |
| ---------- | ------------ | ------------------------------------- |
| Linux      | `lm-sensors` | Necessário para monitoramento térmico |
| Windows    | `WMI`        | Recurso nativo do sistema operacional |

---

## Possíveis Extensões Futuras

* Inclusão de suporte a logs detalhados por thread.
* Interface gráfica com recursos interativos (pausar/resumir por thread).
* Suporte a monitoramento de consumo energético.
* Integração com bases de dados ou ferramentas de benchmark.

---

## Licença

Este projeto é de código aberto.

