#!/usr/bin/env bash
# Prepare the patched Wine source, native build tools and PS5 cross-build tree.
set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
src=${WINE_SRC:-$root/vendor/wine}
tools=${WINE_TOOLS:-$root/work/wine-ps5-tools}
cross=${WINE_BUILD:-$root/work/wine-ps5-build-check}
sdk_fork=${PS5_PAYLOAD_SDK_FORK:-$root/vendor/PS5_PayloadSDK}
vulkan=${PS5_VULKAN_DIR:-$root/vendor/PS5_Vulkan}
revision=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["researchOnly"]["wine"]["revision"])' "$root/dependencies.json")
url=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["researchOnly"]["wine"]["url"])' "$root/dependencies.json")
patch=$root/runtime/wine-ps5/wine-ps5.patch
jobs=${JOBS:-8}

fail() { echo "bootstrap-wine.sh: $*" >&2; exit 2; }
for directory in /opt/homebrew/opt/bison/bin /opt/homebrew/opt/flex/bin /opt/homebrew/opt/make/libexec/gnubin; do
    [[ ! -d $directory ]] || PATH="$directory:$PATH"
done
for command in git python3 bison flex; do command -v "$command" >/dev/null || fail "missing host tool: $command"; done
make_command=$(command -v gmake || command -v make || true)
[[ -n $make_command ]] || fail "missing GNU make"

# Scripts below invoke `make`; on macOS put modern gmake under that name so its
# pattern-rule selection matches the Linux build host.
host_bin=$root/work/host-tools/bin
mkdir -p "$host_bin"
ln -sf "$make_command" "$host_bin/make"
export PATH="$host_bin:${LLVM_MINGW:+$LLVM_MINGW/bin:}$PATH"
unset CPPFLAGS LDFLAGS CFLAGS CXXFLAGS PKG_CONFIG_PATH

mingw_cc=$(command -v x86_64-w64-mingw32-clang || command -v x86_64-w64-mingw32-gcc || true)
mingw_cxx=$(command -v x86_64-w64-mingw32-clang++ || command -v x86_64-w64-mingw32-g++ || true)
[[ -n $mingw_cc && -n $mingw_cxx ]] || fail "set LLVM_MINGW or add an x86_64-w64-mingw32 compiler to PATH"

if [[ ! -d $src/.git ]]; then
    mkdir -p "$src"
    git -C "$src" init -q
    git -C "$src" remote add origin "$url"
    git -C "$src" fetch --depth 1 origin "$revision"
    git -C "$src" checkout -q --detach FETCH_HEAD
fi
[[ $(git -C "$src" rev-parse HEAD) == "$revision" ]] || fail "$src is not at $revision"
if git -C "$src" apply --reverse --check "$patch" 2>/dev/null; then
    :
elif git -C "$src" apply --check "$patch" 2>/dev/null; then
    git -C "$src" apply "$patch"
else
    fail "$patch does not cleanly apply and is not already applied"
fi
bash "$root/tools/wine-ps5/sync-memory-layer.sh" "$src"

PS5_PAYLOAD_SDK_FORK="$sdk_fork" bash "$root/app/ps5/tools/setup-sdk.sh" >/dev/null
sdk=$root/app/.deps/native/ps5-payload-sdk
radv_include=$vulkan/.deps/native/radv-release/include
[[ -d $radv_include/vulkan ]] || fail "missing RADV Vulkan headers: $radv_include"
tls=$root/build/ps5-tls
[[ -f $tls/lib/libgnutls.a ]] || bash "$root/tools/wine-ps5/build-tls.sh"

without=(--without-x --without-pulse --without-alsa --without-oss --without-sdl
    --without-opencl --without-cups --without-sane --without-gphoto --without-v4l2
    --without-usb --without-netapi --without-krb5 --without-gssapi --without-pcap
    --without-coreaudio --disable-tests --without-inotify --without-dbus --without-capi
    --without-wayland --without-ffmpeg --without-gstreamer --without-fontconfig
    --without-opengl --without-udev --without-xinerama --without-unwind)

mkdir -p "$tools" "$cross" "$root/build/ps5-lib"
if [[ ! -f $tools/Makefile ]]; then
    (cd "$tools" && "$src/configure" "${without[@]}" --without-vulkan --without-gnutls --without-freetype \
        >configure.log 2>&1) || { tail -40 "$tools/configure.log" >&2; exit 1; }
fi
"$make_command" -C "$tools" -j"$jobs" __tooldeps__ >"$tools/make.log" 2>&1 \
    || { tail -40 "$tools/make.log" >&2; exit 1; }
mkdir -p "$tools/nls"
ln -sf "$src/nls/locale.nls" "$tools/nls/locale.nls"
[[ -f $root/build/ps5-lib/libm.a ]] || "$sdk/bin/prospero-ar" rc "$root/build/ps5-lib/libm.a"

if [[ ! -f $cross/Makefile ]]; then
    (cd "$cross" && "$src/configure" --host=x86_64-unknown-freebsd9 --with-wine-tools="$tools" \
        --enable-archs=x86_64 --with-vulkan --with-gnutls --without-freetype \
        "${without[@]}" \
        CC="$sdk/bin/prospero-clang" CXX="$sdk/bin/prospero-clang++" \
        AR="$sdk/bin/prospero-ar" RANLIB="$sdk/bin/prospero-ranlib" STRIP="$sdk/bin/prospero-strip" \
        CROSSCC="$mingw_cc" CROSSCXX="$mingw_cxx" PKG_CONFIG=false \
        CFLAGS="-g -O2 -DWINE_INPROC_SERVER" \
        CROSSCFLAGS="-g -O2 -DWINE_PS5 -DWINE_PS5_MEMORY_MODEL" \
        GNUTLS_CFLAGS="-I$tls/include" GNUTLS_LIBS="-L$tls/lib -lgnutls" \
        ac_cv_lib_soname_gnutls=static-ps5 \
        VULKAN_CFLAGS="-I$radv_include" ac_cv_header_vulkan_vulkan_h=yes \
        ac_cv_lib_soname_vulkan=libvulkan.so \
        LDFLAGS="-L$root/build/ps5-lib -lunwind -lps5platform" >configure.log 2>&1) \
        || { tail -60 "$cross/configure.log" >&2; exit 1; }
fi

echo "Wine source, tools and PS5 cross tree ready"
