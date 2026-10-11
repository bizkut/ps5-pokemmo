# PokeMMO PS5 title

`app/` is the native title (`PPSA99221`) that hosts one-process Wine and the PokeMMO Windows client. It is based on PS5 Vulkan Template and the upstream title/Wine integration.

A normal home-screen launch bypasses the sample menu when this file exists:

```text
/data/pokemmo/prefix/dosdevices/c:/Games/PokeMMO/PokeMMO.exe
```

The fallback `pokemmops5probe` sample remains a native diagnostic for the Vulkan, PE-loading, memory and Wine contracts; it is not PokeMMO compatibility proof.

## Build

From the repository root:

```sh
bash tools/wine-ps5/bootstrap-wine.sh
bash tools/wine-ps5/build-zink.sh
bash tools/wine-ps5/build-console.sh --no-deploy
```

The package is written to `app/dist/PPSA99221/` with `eboot.bin`, `sce_module/libc.prx`, metadata, shaders, assets and component licences.

For the native diagnostic sample only:

```sh
app/ps5/tools/compile-shaders.sh pokemmops5probe
app/ps5/tools/run.sh
```

`app/ps5/tools/link-title.sh` consumes the PS5 Vulkan link recipe's explicit safe stub list. It must not pass every SDK stub to the title converter: `libkernel_web` exposes names whose function pointers are null in a game title.

## PS5 menu artwork

`ps5/sce_sys/pic0.dds` is the selected-tile/menu background; `pic1.dds` is the matching launch background. Both use the user-supplied `background-sky.af44bb53.jpg`. The 2800×900 source was center-cropped to `(600, 0, 2200, 900)` and uniformly resized to 3840×2160 with Lanczos filtering. No text, logo, stretching or icon replacement was added.

The editable 4K PNGs are `ps5/sce_sys/background-source.png` and `launch-background-source.png`. The deployable files are single-level DX10 `BC7_UNORM` DDS textures, as required by the PS5 Vulkan presentation validator. `link-title.sh` copies them into the packaged `sce_sys/` directory.

From the repository root, re-encode the retained composition using the existing native converter:

```sh
bash vendor/PS5_Vulkan/tools/prepare-assets.sh \
  --background app/ps5/sce_sys/background-source.png \
  --bc7enc vendor/PS5_Vulkan/.deps/native/bc7enc_rdo/build-macos/bc7enc \
  --output-directory app/ps5/sce_sys
cp app/ps5/sce_sys/pic0.dds app/ps5/sce_sys/pic1.dds app/dist/PPSA99221/sce_sys/
bash vendor/PS5_Vulkan/tools/validate-assets.sh app/dist/PPSA99221/sce_sys
```

The macOS encoder was built from the template's pinned `bc7enc_rdo` revision with Homebrew LLVM 23 and `libomp`; Apple Clang's lack of `-fopenmp` support prevents using it for that build. On other hosts, use the encoder path produced by `setup-asset-dependencies.sh`.

The installed backgrounds were read back over FTP and matched the generated DDS files byte-for-byte. Shell/loader caching can delay their visible refresh. The maintainer has explicitly confirmed permission for this repository to redistribute the supplied background and existing icon, but their original artists and open-content licences remain undocumented. They are not covered by the root MIT licence; see the [artwork notice](../ARTWORK.md).


## Runtime boundary

- `app/runtime/wine_title.c`: product paths, launch settings, filesystem/syscall wrappers and title shutdown.
- `app/runtime/wine_devices.c`, `wine_input.c`, `wine_osk.c`: XInput, keyboard/mouse and PS5 on-screen keyboard.
- `app/runtime/wine_audio.c`: PS5 AudioOut sink for Wine's event-driven mixer.
- `runtime/wine-ps5/wine-ps5.patch`: Wine server/thread, memory, window, Vulkan, networking and linked-Unix-library changes.
- `build/ps5-wine/wine-unix.o`: generated Wine Unix side linked into `eboot.bin`.

Internal symbols and diagnostic settings use `pokemmops5` / `POKEMMOPS5`; no compatibility aliases for the template namespace remain.

## Licences

Project-owned title source is covered by the root MIT licence except for the artwork identified in [the artwork notice](../ARTWORK.md). Wine-derived files remain LGPL-2.1-or-later. The linked PS5 platform layer is GPL-3.0-or-later, so distributed title binaries are governed by that licence. Build-generated component notices are staged into the title package.
