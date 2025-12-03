# Este arquivo corrige links simbólicos absolutos no sysroot para serem relativos.
# Isso é necessário para garantir que o sysroot funcione corretamente em diferentes ambientes.
# Altere o valor da variável 'root' para o path do sysroot que você deseja corrigir.

#!/usr/bin/env python3
import os

# Definindo path para sysroot da arquitetura que precisa de fixup
root = "rpi-sysroot-armv7"

for path, dirs, files in os.walk(root):
    for f in files:
        full = os.path.join(path, f)
        if os.path.islink(full):
            target = os.readlink(full)
            if target.startswith("/"):
                new = os.path.relpath(os.path.join(root, target.lstrip("/")), path)
                os.unlink(full)
                os.symlink(new, full)
                print(f"Fixed symlink: {full} -> {new}")