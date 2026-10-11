#!/usr/bin/env bash
# Build the application-local Windows WGL frontend backed only by Gallium Zink.
set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
fail() { echo "build-zink: $*" >&2; exit 2; }
abspath() { python3 -c 'import pathlib,sys; print(pathlib.Path(sys.argv[1]).expanduser().resolve())' "$1"; }

for command in git python3 meson ninja; do
    command -v "$command" >/dev/null || fail "missing required tool on PATH: $command"
done
if command -v brew >/dev/null; then
    for formula in bison flex; do
        formula_prefix=$(brew --prefix "$formula" 2>/dev/null || true)
        [[ ! -x $formula_prefix/bin/$formula ]] || PATH="$formula_prefix/bin:$PATH"
    done
fi
for command in bison flex; do
    command -v "$command" >/dev/null || fail "missing required tool on PATH: $command"
done

# Apple's old Bison is not sufficient for current Mesa. Resolve both generators
# from PATH and put their directories first so Meson's native generators use the
# same checked tools on fresh and reconfigured builds.
check_version() {
    local command=$1 required_major=$2 required_minor=$3 required_patch=$4 line major minor patch
    line=$("$command" --version 2>/dev/null | command head -n 1) || fail "cannot run $command from PATH"
    [[ $line =~ ([0-9]+)\.([0-9]+)(\.([0-9]+))? ]] || fail "cannot read $command version: $line"
    major=${BASH_REMATCH[1]}
    minor=${BASH_REMATCH[2]}
    patch=${BASH_REMATCH[4]:-0}
    if (( major < required_major ||
          (major == required_major && minor < required_minor) ||
          (major == required_major && minor == required_minor && patch < required_patch) )); then
        fail "$command $major.$minor.$patch is too old; need at least $required_major.$required_minor.$required_patch from PATH"
    fi
}
bison=$(command -v bison)
flex=$(command -v flex)
check_version "$bison" 3 0 0
check_version "$flex" 2 6 4
export PATH="${bison%/*}:${flex%/*}:$PATH"

MESA_SRC=$(abspath "${MESA_SRC:-$root/vendor/PS5_Mesa}")
LLVM_MINGW=$(abspath "${LLVM_MINGW:-$HOME/llvm-mingw}")
MESA_BUILD=$(abspath "${MESA_BUILD:-$root/build/mesa-wgl-zink-win64}")
MESA_INSTALL=$(abspath "${MESA_INSTALL:-$root/build/mesa-wgl-zink-install}")
cross_file="$root/build/win64-mingw.ini"

[[ -f $MESA_SRC/meson.build ]] || fail "no PS5_Mesa source at $MESA_SRC (run python3 tools/bootstrap.py or set MESA_SRC)"
pin=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["PS5_Mesa"])' "$root/dependencies.json")
revision=$(git -C "$MESA_SRC" rev-parse HEAD 2>/dev/null) || fail "$MESA_SRC is not the pinned vendor PS5_Mesa checkout"
[[ $revision == "$pin" ]] || fail "PS5_Mesa is at $revision; expected pinned revision $pin"
git -C "$MESA_SRC" diff --quiet && git -C "$MESA_SRC" diff --cached --quiet ||
    fail "PS5_Mesa has tracked changes; use a clean checkout at $pin"

triplet=x86_64-w64-mingw32
for tool in clang clang++ ar strip windres; do
    [[ -x $LLVM_MINGW/bin/$triplet-$tool ]] || fail "missing llvm-mingw tool: $LLVM_MINGW/bin/$triplet-$tool"
done
[[ -x $LLVM_MINGW/bin/llvm-readobj ]] || fail "missing llvm-mingw tool: $LLVM_MINGW/bin/llvm-readobj"
[[ $LLVM_MINGW != *"'"* ]] || fail "LLVM_MINGW cannot contain a single quote"
[[ $MESA_BUILD != / && $MESA_BUILD != "$root" && $MESA_BUILD != "$MESA_SRC" ]] ||
    fail "unsafe MESA_BUILD: $MESA_BUILD"
[[ $MESA_BUILD/ != "$MESA_SRC/"* && $MESA_SRC/ != "$MESA_BUILD/"* ]] ||
    fail "MESA_BUILD and MESA_SRC must be separate trees"
[[ $MESA_INSTALL != / && $MESA_INSTALL != "$root" && $MESA_INSTALL != "$MESA_SRC" && $MESA_INSTALL != "$MESA_BUILD" ]] ||
    fail "unsafe MESA_INSTALL: $MESA_INSTALL"
[[ $MESA_INSTALL/ != "$MESA_SRC/"* && $MESA_INSTALL/ != "$MESA_BUILD/"* &&
    $MESA_SRC/ != "$MESA_INSTALL/"* && $MESA_BUILD/ != "$MESA_INSTALL/"* ]] ||
    fail "MESA_INSTALL, MESA_SRC and MESA_BUILD must be separate trees"

if [[ -d $MESA_BUILD && ! -f $MESA_BUILD/meson-private/coredata.dat ]]; then
    rm -rf -- "$MESA_BUILD"
fi
mkdir -p "$root/build" "$MESA_BUILD"
cat > "$cross_file" <<EOF
[binaries]
c = '$LLVM_MINGW/bin/$triplet-clang'
cpp = '$LLVM_MINGW/bin/$triplet-clang++'
ar = '$LLVM_MINGW/bin/$triplet-ar'
strip = '$LLVM_MINGW/bin/$triplet-strip'
windres = '$LLVM_MINGW/bin/$triplet-windres'

[properties]
needs_exe_wrapper = true

[host_machine]
system = 'windows'
cpu_family = 'x86_64'
cpu = 'x86_64'
endian = 'little'
EOF

export LC_ALL=C TZ=UTC SOURCE_DATE_EPOCH
SOURCE_DATE_EPOCH=$(git -C "$MESA_SRC" show -s --format=%ct "$pin")
unset CC CXX AR STRIP WINDRES CFLAGS CXXFLAGS CPPFLAGS LDFLAGS OBJCFLAGS OBJCXXFLAGS \
    PKG_CONFIG PKG_CONFIG_PATH PKG_CONFIG_LIBDIR PKG_CONFIG_SYSROOT_DIR CMAKE_PREFIX_PATH \
    CPATH C_INCLUDE_PATH CPLUS_INCLUDE_PATH LIBRARY_PATH LD_LIBRARY_PATH

config_stamp="$MESA_BUILD/.pokemmo-zink-config"
configuration=$(printf '%s\n' pokemmo-wgl-zink-v1 "$pin" "$MESA_SRC" "$LLVM_MINGW" "$MESA_INSTALL" \
    "$bison" "$flex" "$(meson --version)" 'windows-x86_64;zink;no-vulkan;static-zlib;no-zstd;no-video')
setup=(meson setup)
if [[ -f $MESA_BUILD/meson-private/coredata.dat ]]; then
    if [[ -f $config_stamp && $(<"$config_stamp") == "$configuration" ]]; then
        setup+=(--reconfigure)
    else
        setup+=(--wipe)
    fi
fi
"${setup[@]}" "$MESA_BUILD" "$MESA_SRC" \
    --cross-file "$cross_file" \
    --prefix "$MESA_INSTALL" \
    --buildtype release \
    --default-library=static \
    --force-fallback-for=zlib \
    -Dplatforms=windows \
    -Dgallium-drivers=zink \
    '-Dvulkan-drivers=[]' \
    -Dglx=disabled \
    -Degl=disabled \
    -Dgles1=disabled \
    -Dgles2=disabled \
    -Dgbm=disabled \
    -Dllvm=disabled \
    -Dshared-glapi=enabled \
    -Dgallium-wgl-dll-name=libgallium_wgl \
    -Dzlib=enabled \
    -Dzstd=disabled \
    '-Dvideo-codecs=[]' \
    -Dbuild-tests=false
printf '%s\n' "$configuration" > "$config_stamp"
meson compile -C "$MESA_BUILD"
rm -rf -- "$MESA_INSTALL"
meson install -C "$MESA_BUILD"

outputs=("$MESA_INSTALL/bin/opengl32.dll" "$MESA_INSTALL/bin/libgallium_wgl.dll")
for output in "${outputs[@]}"; do
    [[ -s $output ]] || fail "Mesa did not emit ${output##*/} at $output"
done
python3 - "$LLVM_MINGW/bin/llvm-readobj" "${outputs[@]}" <<'PY'
import re
import struct
import subprocess
import sys
from pathlib import Path

readobj = sys.argv[1]
system_dlls = {
    'advapi32.dll', 'bcrypt.dll', 'cfgmgr32.dll', 'comdlg32.dll',
    'crypt32.dll', 'dbghelp.dll', 'dwmapi.dll', 'gdi32.dll', 'imm32.dll',
    'kernel32.dll', 'msvcrt.dll', 'normaliz.dll', 'ntdll.dll', 'ole32.dll',
    'oleaut32.dll', 'psapi.dll', 'rpcrt4.dll', 'secur32.dll', 'setupapi.dll',
    'shell32.dll', 'shlwapi.dll', 'ucrtbase.dll', 'user32.dll', 'userenv.dll',
    'version.dll', 'vulkan-1.dll', 'winmm.dll', 'wintrust.dll', 'ws2_32.dll',
}
companions = {'libgallium_wgl.dll'}
for name in sys.argv[2:]:
    data = Path(name).read_bytes()
    if len(data) < 0x40 or data[:2] != b'MZ':
        raise SystemExit(f'build-zink: {name} is not a PE DLL')
    pe = struct.unpack_from('<I', data, 0x3c)[0]
    if pe + 26 > len(data) or data[pe:pe + 4] != b'PE\0\0':
        raise SystemExit(f'build-zink: {name} has an invalid PE header')
    machine = struct.unpack_from('<H', data, pe + 4)[0]
    characteristics = struct.unpack_from('<H', data, pe + 22)[0]
    magic = struct.unpack_from('<H', data, pe + 24)[0]
    if machine != 0x8664 or magic != 0x20b or not characteristics & 0x2000:
        raise SystemExit(f'build-zink: {name} is not a Windows x86-64 DLL')
    imports = {
        match.group(1).casefold()
        for match in re.finditer(r'^\s*Name:\s+(\S+\.dll)\s*$', subprocess.check_output(
            [readobj, '--coff-imports', name], text=True), re.MULTILINE | re.IGNORECASE)
    }
    if not imports:
        raise SystemExit(f'build-zink: could not inspect the import table of {name}')
    if Path(name).name.casefold() == 'opengl32.dll' and 'libgallium_wgl.dll' not in imports:
        raise SystemExit('build-zink: opengl32.dll does not import its staged libgallium_wgl.dll companion')
    unexpected = sorted(
        dependency for dependency in imports
        if dependency not in system_dlls
        and dependency not in companions
        and not dependency.startswith(('api-ms-win-', 'ext-ms-win-')))
    if unexpected:
        raise SystemExit(
            f'build-zink: {name} requires non-system DLLs that are not staged: {", ".join(unexpected)}')
PY

forbidden=$(find "$MESA_INSTALL" -type f \( -iname 'vulkan-1.dll' -o -path '*/vulkan/icd.d/*' \) -print -quit)
[[ -z $forbidden ]] || fail "unexpected Windows Vulkan loader/ICD artifact: $forbidden"
unexpected=$(find "$MESA_INSTALL/bin" -maxdepth 1 -type f -name '*.dll' ! -name 'opengl32.dll' ! -name 'libgallium_wgl.dll' -print -quit)
[[ -z $unexpected ]] || fail "unexpected DLL in the WGL/Zink install: $unexpected"
printf 'WGL Zink DLLs: %s, %s\n' "${outputs[@]}"
