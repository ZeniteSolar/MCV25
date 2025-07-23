
# Cross-compile e Deploy para Raspberry Pi 3 (64 bits) – Projeto Vosk

Este documento explica como realizar o cross-compilation do código para Raspberry Pi 3 (ARM64, aarch64) usando um Ubuntu como máquina host, e como enviar e executar o binário na Raspberry Pi.

---

## Requisitos

- Ubuntu com as ferramentas necessárias instaladas:

```bash
sudo apt update
sudo apt install gcc-aarch64-linux-gnu g++-aarch64-linux-gnu rsync
```

- Raspberry Pi 3 rodando sistema operacional 64 bits (verifique com `uname -m` que deve retornar `aarch64`).
- Estrutura `rpi-sysroot/` no Ubuntu contendo os headers e bibliotecas da Raspberry Pi, copiados via `rsync`.
- Vosk pré-compilado copiado para `rpi-sysroot/opt/vosk`.

---

## Preparação do Sysroot

Copie os arquivos da Raspberry Pi para o Ubuntu para montar o sysroot:

```bash
mkdir -p ~/rpi-sysroot/usr
rsync -avz --progress pi@<IP_RPI>:/usr/include/ ~/rpi-sysroot/usr/include/
rsync -avz --progress pi@<IP_RPI>:/usr/lib/aarch64-linux-gnu/ ~/rpi-sysroot/usr/lib/aarch64-linux-gnu/
rsync -avz --progress pi@<IP_RPI>:/lib/aarch64-linux-gnu/ ~/rpi-sysroot/lib/aarch64-linux-gnu/
```

Copie também o loader dinâmico

```
mkdir -p ~/rpi-sysroot/lib64
rsync -avz --progress pi@<IP_RPI>:/lib64/ld-linux-aarch64.so.1 ~/rpi-sysroot/lib64/
```

Substitua `<IP_RPI>` pelo IP da sua Raspberry Pi.

Copie o Vosk pré-compilado para o sysroot:

```bash
scp -r pi@<IP_RPI>:/caminho/para/vosk-linux-aarch64-0.3.45 ~/rpi-sysroot/opt/vosk
```

---

## Compilando o Projeto

Estrutura do projeto:

```
meuprojeto/
├── src/
│   ├── main.cpp
│   ├── can.cpp
│   ├── can.h
│   └── can_ids.h
├── Makefile
├── build.sh
└── rpi-sysroot/
```

Use o Makefile para compilar o projeto:

```bash
make
# ou
./build.sh
```

O binário `app_rpi` será gerado.

---

## Enviando o binário para a Raspberry Pi

Use o `scp` para copiar o arquivo compilado para a Raspberry Pi:

```bash
scp app_rpi pi@<IP_RPI>:/home/pi/
```

---

## Executando o binário na Raspberry Pi

Acesse via SSH:

```bash
ssh pi@<IP_RPI>
```

Permita execução e rode o programa:

```bash
chmod +x ./app_rpi
./app_rpi
```

---

## Resolução de Problemas Comuns

- **Erro**: `cannot find /lib/ld-linux-aarch64.so.1 inside sysroot`  
  Copie o loader dinâmico para o sysroot (`lib64/ld-linux-aarch64.so.1`).

- **Bibliotecas não encontradas**:  
  Verifique se `libvosk.so`, `libasound.so`, `libjsoncpp.so` estão presentes em `rpi-sysroot/opt/vosk/lib` e nas bibliotecas do sysroot.

---

## Referências

- [Cross compiling para Raspberry Pi](https://wiki.debian.org/RaspberryPiCrossCompiler)  
- [Vosk API](https://alphacephei.com/vosk/)

---
