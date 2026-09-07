#!/bin/sh
set -e

echo "Compiling dshuf to freestanding WebAssembly..."
clang --target=wasm32 -O3 -nostdlib -Wl,--no-entry -Wl,--export-all dshuf_wasm.c -o dshuf.wasm
echo "Created dshuf.wasm: $(wc -c < dshuf.wasm) bytes"