#!/usr/bin/env bash
# Everything from the patched Wine source to a deployed title with Wine inside.
#
#   tools/wine-ps5/build-console.sh [--no-deploy]
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
export PS5_VULKAN_DIR="$root/vendor/PS5_Vulkan" PS5_PAYLOAD_SDK_FORK="$root/vendor/PS5_PayloadSDK"
export PKG_CONFIG_PATH="$root/.deps/host-tools/usr/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
export LD_LIBRARY_PATH="$root/.deps/host-tools/usr/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export JOBS=1
make_command=$(command -v gmake || command -v make)
export LLVM_CONFIG=$(bash "$root/tools/wine-ps5/host-llvm-config.sh")
llvm_bin=$("$LLVM_CONFIG" --bindir)
PATH="$llvm_bin:$PATH" CC="${CC:-$llvm_bin/clang}" CXX="${CXX:-$llvm_bin/clang++}" \
    bash "$root/tools/wine-ps5/build-radv.sh"
bash "$root/tools/wine-ps5/build-zink.sh"
bash "$root/tools/wine-ps5/build-tls.sh"
if [[ ! -f $root/work/wine-ps5-build-check/Makefile ]]; then
    bash "$root/tools/wine-ps5/bootstrap-wine.sh"
fi
grep -q 'SONAME_LIBGNUTLS "static-ps5"' "$root/work/wine-ps5-build-check/include/config.h" || {
    echo "Wine cross tree lacks static GnuTLS; remove work/wine-ps5-build-check and run bootstrap-wine.sh" >&2
    exit 2
}
bash "$root/tools/wine-ps5/sync-memory-layer.sh" "$root/vendor/wine"
bash "$root/tools/wine-ps5/build-native-server.sh" "$root/work/claude-partner/wineserver" | tail -3
# Build every Unix-library object. The standalone win32u.so link is expected to
# miss app callbacks and RADV entry points; the final title link supplies them.
"$make_command" -C "$root/work/wine-ps5-build-check" -j1 -k dlls/ws2_32/ws2_32.so dlls/win32u/win32u.so dlls/winevulkan/winevulkan.so dlls/crypt32/unixlib.o dlls/secur32/schannel_gnutls.o dlls/xinput1_3/unixlib.o include/audioclient.h include/mmdeviceapi.h > "$root/work/wine-unixlibs-build.log" 2>&1 || true
errors=$(grep -E "error:" "$root/work/wine-unixlibs-build.log" |
    grep -Ev "error: (unable to find library -lm|linker command failed|undefined symbol: (pokemmops5_input_read|pokemmops5_cursor_sync|pokemmops5_first_frame|vk_icdGetInstanceProcAddr|vk_common_GetDeviceProcAddr))" || true)
[[ -z $errors ]] || { printf '%s\n' "$errors" | head -20; exit 1; }
bash "$root/tools/wine-ps5/build-title-object.sh"
bash "$root/app/ps5/tools/build.sh" > "$root/work/wine-title-build.log" 2>&1 || { grep -nEi 'error|undefined|missing|failed' "$root/work/wine-title-build.log" | head -30; exit 1; }
title="$root/app/dist/PPSA99221"
mkdir -p "$title/licenses/tls"
cp -R "$root/build/ps5-tls/licenses/." "$title/licenses/tls/"
cp "$root/build/ps5-tls/PROVENANCE.txt" "$title/licenses/tls/"
cp "$root/tools/wine-ps5/build-tls.sh" "$title/licenses/tls/build-sources.sh"
bash "$root/tools/build-pe-smoke.sh"
cp "$root/build/windows/pe-smoke.exe" "$title/pe-smoke.exe"
sha256sum "$title/eboot.bin"
[[ ${1:-} == --no-deploy ]] || bash "$root/tools/console.sh" deploy | tail -1
