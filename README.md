### Compilação do módulo de controle por voz: Zenira

### Guia de intalação
Baixe o modelo de linguagem pré compilado da vosk compativel com a arquitetura do sistema alvo:
  armv7    -> Raspberry Pi OS 32-bit
  aarch64  -> Raspberry Pi OS 64-bit
```
wget https://github.com/alphacep/vosk-api/releases/download/v0.3.45/vosk-linux-aarch64-0.3.45.zip
unzip vosk-linux-aarch64-0.3.45.zip
```
```
wget https://github.com/alphacep/vosk-api/releases/download/v0.3.45/vosk-linux-armv7l-0.3.45.zip
unzip vosk-linux-armv7l-0.3.45.zip
```

Isso criará uma pasta chamada com o nome do modelo pré-compilado. Agora instale em /opt/vosk:
```
sudo mkdir -p /opt/vosk
sudo cp -r vosk-linux-armv7l-0.3.45/* /opt/vosk/
sudo ldconfig
```

No Ubuntu, execute do seu PC, no diretório do projeto:
```
rsync -avz --delete --safe-links \
  zenite@10.228.62.59:/lib/arm-linux-gnueabihf rpi-sysroot-armv7/lib/

rsync -avz --delete --safe-links \
  zenite@10.228.62.59:/usr/lib/arm-linux-gnueabihf rpi-sysroot-armv7/usr/lib/

rsync -avz --delete --safe-links \
  zenite@10.228.62.59:/usr/include rpi-sysroot-armv7/usr/

rsync -avz --delete --safe-links \
  zenite@10.228.62.59:/usr/lib/gcc/arm-linux-gnueabihf rpi-sysroot-armv7/usr/lib/gcc/

rsync -avz --delete --safe-links \
  zenite@10.228.62.59:/opt/vosk rpi-sysroot-armv7/opt/
```

Depois, confirme se o libvosk.so existe em:
rpi-sysroot-armv7/opt/vosk/lib/libvosk.so

Rode o script de fix-sysroot para corrigir os symlinks que devem apontar para /lib do ubuntu
```
python3 fixup-sysroot.py rpi-sysroot-armv7
```

### Compilação para diferentes arquiteturas da raspberry pi
Uso: `./build.sh <arch>`

Arquiteturas válidas:
-  armv7    -> Raspberry Pi OS 32-bit
-  aarch64  -> Raspberry Pi OS 64-bit 