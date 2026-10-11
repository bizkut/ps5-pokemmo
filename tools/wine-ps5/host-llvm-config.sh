#!/usr/bin/env bash
# Give the payload-SDK wrappers one LLVM bin directory containing clang and lld.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
shim="$root/work/host-llvm/bin"

prefer_homebrew_lld=false
if [[ -n ${LLVM_CONFIG:-} ]]; then
    real_config=$(command -v "$LLVM_CONFIG" 2>/dev/null || true)
    [[ -n $real_config && -x $real_config ]] ||
        { echo "host-llvm-config.sh: LLVM_CONFIG is not executable: $LLVM_CONFIG" >&2; exit 2; }
elif [[ -x /opt/homebrew/opt/llvm/bin/llvm-config ]]; then
    real_config=/opt/homebrew/opt/llvm/bin/llvm-config
    prefer_homebrew_lld=true
else
    real_config=$(command -v llvm-config-23 || command -v llvm-config-22 ||
        command -v llvm-config-21 || command -v llvm-config-20 ||
        command -v llvm-config-19 || command -v llvm-config || true)
    [[ -n $real_config ]] ||
        { echo "host-llvm-config.sh: no llvm-config found" >&2; exit 2; }
fi
real_config=$(realpath "$real_config")
real_bin=$("$real_config" --bindir)
real_bin=$(realpath "$real_bin")
if [[ -d $shim && $real_bin -ef $shim ]]; then
    [[ -e $shim/llvm-config && $real_config -ef $shim/llvm-config &&
        -x $shim/clang && -x $shim/clang++ && -x $shim/llvm-ld.lld ]] ||
        { echo "host-llvm-config.sh: LLVM_CONFIG points to an incomplete generated shim; select the underlying llvm-config" >&2; exit 2; }
    printf '%s\n' "$real_config"
    exit 0
fi
if [[ -x $real_bin/llvm-lld ]]; then
    printf '%s\n' "$real_config"
    exit 0
fi

if $prefer_homebrew_lld && [[ -x /opt/homebrew/opt/lld/bin/ld.lld ]]; then
    lld=/opt/homebrew/opt/lld/bin/ld.lld
else
    lld=$(command -v ld.lld || true)
fi
[[ -n $lld ]] || { echo 'host-llvm-config.sh: LLVM bin directory has no linker and no ld.lld is in PATH' >&2; exit 2; }
lld=$(realpath "$lld")
mkdir -p "$shim"
for tool in "$real_bin"/*; do
    [[ -x $tool && ! -d $tool ]] || continue
    name=${tool##*/}
    [[ $name == llvm-config ]] || ln -sfn "$tool" "$shim/$name"
done
ln -sfn "$lld" "$shim/llvm-ld.lld"
printf -v shim_q '%q' "$shim"
printf -v real_config_q '%q' "$real_config"
config_tmp="$shim/.llvm-config.$$"
trap 'rm -f "$config_tmp"' EXIT
cat > "$config_tmp" <<EOF
#!/usr/bin/env bash
if [[ \${1:-} == --bindir ]]; then
    printf '%s\\n' $shim_q
else
    exec $real_config_q "\$@"
fi
EOF
chmod +x "$config_tmp"
mv -f "$config_tmp" "$shim/llvm-config"
trap - EXIT
printf '%s\n' "$shim/llvm-config"
