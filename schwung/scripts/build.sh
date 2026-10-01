#!/usr/bin/env bash
# Cross-compile the Schwung module for Ableton Move (aarch64 Linux) and package it as build/schwung/clementine-xt-module.tar.gz.
# Needs Docker; nothing on a device is touched. Run schwung/scripts/test.sh first (host simulation under ASan).
set -euo pipefail
cd "$(dirname "$0")/../.."
IMAGE=clxt-schwung-build
if ! docker image inspect "$IMAGE" &>/dev/null; then
    docker build -q -t "$IMAGE" - <<'EOF'
FROM debian:bookworm
RUN apt-get update && apt-get install -y gcc-aarch64-linux-gnu binutils-aarch64-linux-gnu file && rm -rf /var/lib/apt/lists/*
EOF
fi
python3 tools/gen_schwung.py
OUT=build/schwung/modules/sound_generators/clementine-xt
rm -rf build/schwung/modules build/schwung/clementine-xt-module.tar.gz
mkdir -p "$OUT"
cp schwung/module.json "$OUT/"
SRC="src/engine.c src/out.c src/patch.c src/syx.c src/waves.c src/wavedata.c src/filter.c src/fx.c src/mod.c src/presets.c schwung/src/schwung_plugin.c"
CFLAGS="-O2 -g -shared -fPIC -Wall -Wno-format-truncation -Isrc -Isrc/vendor/schwung -Isrc/vendor/mpc-vst-plugins -Ischwung/src"
docker run --rm -v "$PWD":/w -w /w "$IMAGE" bash -c "
    set -e
    aarch64-linux-gnu-gcc $CFLAGS $SRC -o $OUT/dsp.so -lm -lpthread
    aarch64-linux-gnu-strip --strip-unneeded $OUT/dsp.so
    file $OUT/dsp.so
    aarch64-linux-gnu-objdump -T $OUT/dsp.so | grep -o 'GLIBC_[0-9.]*' | sort -uV | tail -1
    tar --owner=0 --group=0 -czf build/schwung/clementine-xt-module.tar.gz -C build/schwung/modules/sound_generators clementine-xt
    tar -tzf build/schwung/clementine-xt-module.tar.gz
"
echo "Built: build/schwung/clementine-xt-module.tar.gz"
