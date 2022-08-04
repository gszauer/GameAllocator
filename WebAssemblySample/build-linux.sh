

clang -x c++ \
    --target=wasm32 \
    -nostdinc \
    -nostdlib \
    -O3 \
    -flto \
    -Wl,--allow-undefined \
    -Wl,--import-memory \
    -Wl,--no-entry \
    -Wl,--export-dynamic \
    -Wl,--lto-O3 \
    -Wl,-z,stack-size=4194304 \
    -D WASM32=1 \
    -D _WASM32=1 \
    -o GameAllocator.wasm \
    ../mem.cpp