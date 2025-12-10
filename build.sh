#!/bin/bash

# Este script compila o aplicativo para Raspberry Pi, usando a toolchain adequada
# para a arquitetura especificada (armv7 ou aarch64).
# Uso: ./build.sh <arch>

set -e

ARCH="$1"

# ============================================
# Função de ajuda
# ============================================
show_help() {
    echo "Uso: ./build.sh <arch>"
    echo "Arquiteturas válidas:"
    echo "  armv7    → Raspberry Pi OS 32-bit"
    echo "  aarch64  → Raspberry Pi OS 64-bit"
    exit 1
}

# Sem argumento → mostra ajuda
[ -z "$ARCH" ] && show_help

# ============================================
# Seleção dos parâmetros conforme arquitetura
# ============================================
if [[ "$ARCH" == "aarch64" ]]; then
    TOOLCHAIN="toolchains/aarch64.cmake"
    OUTDIR="build/build-aarch64"
    LOGFILE="build/build-aarch64/build.log"
    LABEL="Compilando app para Raspberry Pi 64-bit (aarch64)..."

elif [[ "$ARCH" == "armv7" ]]; then
    TOOLCHAIN="toolchains/armv7.cmake"
    OUTDIR="build/build-armv7"
    LOGFILE="build/build-armv7/build.log"
    LABEL="Compilando app para Raspberry Pi 32-bit (armv7)..."

else
    echo "Arquitetura inválida: $ARCH"
    show_help
fi

# ============================================
# Criar pastas
# ============================================
echo "$LABEL"

mkdir -p "$OUTDIR"

# Limpa build antigo
rm -rf "$OUTDIR"/*
cd "$OUTDIR"

# ============================================
# CMake
# ============================================
echo "Configurando projeto com CMake..."
cmake ../.. \
    -DCMAKE_TOOLCHAIN_FILE=../../"$TOOLCHAIN" \
    -DCMAKE_BUILD_TYPE=Release

# ============================================
# Compilação
# ============================================
echo "-- Compiling... (You can check the log at $LOGFILE)"
make -j$(nproc) > "../../$LOGFILE" 2>&1

# ============================================
# Resultado
# ============================================
echo -e "\nBinary compiled successfully!"
echo "-- Binary: $(pwd)/app"
echo "-- Full log: ../../$LOGFILE"