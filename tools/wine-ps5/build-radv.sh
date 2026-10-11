#!/usr/bin/env bash
# Build the title's RADV with this project's changes to its display code.
#
#   tools/wine-ps5/build-radv.sh
#
# PS5_Vulkan builds RADV from a pinned revision of PS5_Mesa
# (vendor/PS5_Vulkan/tools/build-radv.sh release). This applies
# runtime/radv-ps5/*.patch to that revision's exported source, builds the
# archive again in the same build directory, puts it in build/radv/lib/ and
# takes the patches out again, so PS5_Vulkan's own tree and archive stay what
# their provenance says. app/ps5/tools/link-title.sh requires build/radv's
# archive by default.
#
# videoout-mailbox.patch: VK_PRESENT_MODE_MAILBOX_KHR beside FIFO. With the
# game's vertical sync off, vkd3d-proton presents in it and the game draws as
# fast as it can; the screen shows the latest finished frame at each refresh,
# without tearing. PS5_VIDEOOUT_MAILBOX=0 in the title's environment leaves the
# mode out.
# videoout-wsi-cursor.patch: exports the RADV-owned VideoOut cursor plane so
# the title's native input worker can place one cursor over the WGL/Zink frame
# without opening a second VideoOut presenter.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
# Host compiler selection remains caller-controlled. Ambient flags naming a
# different LLVM prefix must not leak into Mesa's native-tool configuration.
unset CPPFLAGS LDFLAGS
vulkan="$root/vendor/PS5_Vulkan"
source_tree="$vulkan/.deps/work/radv-src"
build="$vulkan/.deps/work/radv-build-ps5-release"
out="$root/build/radv"
ninja=${NINJA:-$(command -v ninja || echo "$HOME/.local/bin/ninja")}

[[ -f $build/build.ninja && -f $source_tree/.revision ]] || bash "$vulkan/tools/build-radv.sh" release
revision=$(<"$source_tree/.revision")
patches=(
    "$root/runtime/radv-ps5/videoout-mailbox.patch"
    "$root/runtime/radv-ps5/videoout-wsi-cursor.patch"
)

applied=()
restore() {
    local status=$? restore_status=0 p
    local i
    trap - EXIT
    for ((i=${#applied[@]} - 1; i >= 0; i--)); do
        p=${applied[i]}
        if ! patch -d "$source_tree" -p1 -R -s < "$p"; then
            echo "could not take out $p" >&2
            restore_status=1
        fi
    done
    (( restore_status == 0 )) || exit 1
    exit "$status"
}
trap restore EXIT
for p in "${patches[@]}"; do
    patch -d "$source_tree" -p1 -s --dry-run < "$p" > /dev/null ||
        { echo "$(basename "$p") does not apply to PS5_Mesa at $revision" >&2; exit 2; }
    patch -d "$source_tree" -p1 -s < "$p"
    applied+=("$p")
done
"$ninja" -C "$build" -j1 src/amd/vulkan/libvulkan_radeon.a > "$build.project.log" 2>&1 ||
    { grep -E "error|FAILED" "$build.project.log" | head -20 >&2; exit 1; }
# The layout and the provenance fields of PS5_Vulkan's own archive, which the
# title's link and notices read; the patches are named beside them.
mkdir -p "$out/lib"
cp "$build/src/amd/vulkan/libvulkan_radeon.a" "$out/lib/libvulkan_radeon.ps5.a"
{
    echo "RADV for the PlayStation 5, built by tools/wine-ps5/build-radv.sh"
    sed -n '/^fork: /p; /^sdk: /p; /^assertions: /p' "$vulkan/.deps/native/radv-release/PROVENANCE.txt"
    echo "revision: $revision"
    for p in "${patches[@]}"; do echo "patch: $(basename "$p") $(sha256sum "$p" | cut -d' ' -f1)"; done
    echo "archive sha256: $(sha256sum "$out/lib/libvulkan_radeon.ps5.a" | cut -d' ' -f1)"
} > "$out/PROVENANCE.txt"
echo "==> $out/lib/libvulkan_radeon.ps5.a (PS5_Mesa ${revision:0:12} + ${#patches[@]} patch(es))"
