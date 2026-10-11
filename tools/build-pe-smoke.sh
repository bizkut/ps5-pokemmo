#!/usr/bin/env bash
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
mkdir -p "$root/build/windows"
if [[ -n ${LLVM_MINGW:-} && -x $LLVM_MINGW/bin/x86_64-w64-mingw32-clang ]]; then
    cc=$LLVM_MINGW/bin/x86_64-w64-mingw32-clang
else
    cc=$(command -v x86_64-w64-mingw32-gcc || command -v x86_64-w64-mingw32-clang)
fi
compiler_runtime=$("$cc" --print-libgcc-file-name)
"$cc" -O2 -nostdlib -fno-stack-protector \
    -Wl,--entry,PokeMMOPS5Smoke -Wl,--enable-reloc-section \
    "$root/tools/pe-smoke.c" -lkernel32 -o "$root/build/windows/pe-smoke.exe"
"$cc" -O2 -nostdlib -fno-stack-protector \
    -Wl,--entry,PokeMMOPS5Win32Test -Wl,--enable-reloc-section \
    "$root/tools/win32-runtime-test.c" -lkernel32 -o "$root/build/windows/win32-runtime-test.exe"
"$cc" -O2 -nostdlib -fno-stack-protector -fno-builtin \
    -Wl,--entry,PokeMMOPS5MemoryTest -Wl,--enable-reloc-section \
    "$root/tools/win32-memory-test.c" -lkernel32 "$compiler_runtime" -o "$root/build/windows/win32-memory-test.exe"
"$cc" -O2 "$root/tools/win32-crt-test.c" -ladvapi32 -o "$root/build/windows/win32-crt-test.exe"
"$cc" -O2 "$root/tools/win32-net-test.c" -lws2_32 -o "$root/build/windows/win32-net-test.exe"
"$cc" -O2 -Wall -Wextra "$root/tools/win32-tls-test.c" -lwinhttp -o "$root/build/windows/win32-tls-test.exe"
"$cc" -O2 -Wall -Wextra "$root/tools/win32-input-test.c" -luser32 -o "$root/build/windows/win32-input-test.exe"
"$cc" -O2 -Wall -Wextra "$root/tools/win32-selfconnect-test.c" -lws2_32 -o "$root/build/windows/win32-selfconnect-test.exe"
"$cc" -O2 "$root/tools/win32-gdi-test.c" -lgdi32 -luser32 -o "$root/build/windows/win32-gdi-test.exe"
# Vulkan headers are any platform's copy; only vulkan/ is put on the include path.
vulkan_headers="${VULKAN_HEADERS:-$root/vendor/PS5_Vulkan/.deps/native/radv-release/include}"
mkdir -p "$root/build/windows/.vulkan-include"
ln -sfn "$vulkan_headers/vulkan" "$root/build/windows/.vulkan-include/vulkan"
[[ ! -d $vulkan_headers/vk_video ]] || ln -sfn "$vulkan_headers/vk_video" "$root/build/windows/.vulkan-include/vk_video"
"$cc" -O2 -isystem "$root/build/windows/.vulkan-include" "$root/tools/win32-vulkan-noscreen-test.c" -o "$root/build/windows/win32-vulkan-noscreen-test.exe"
"$cc" -O2 "$root/tools/win32-d3d12-test.c" -ldxguid -o "$root/build/windows/win32-d3d12-test.exe"
"$cc" -O2 "$root/tools/win32-d3d12-present-test.c" -ld3d12 -ldxgi -ldxguid -luser32 -o "$root/build/windows/win32-d3d12-present-test.exe"
"$cc" -O2 -Wall -Wextra "$root/tools/win32-audio-test.c" -lole32 -lwinmm -lntdll -o "$root/build/windows/win32-audio-test.exe"
"$cc" -O2 "$root/tools/win32-file-test.c" -o "$root/build/windows/win32-file-test.exe"
"$cc" -O2 "$root/tools/win32-sync-bench.c" -o "$root/build/windows/win32-sync-bench.exe"
