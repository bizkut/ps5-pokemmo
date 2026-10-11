# PokeMMO PS5 technical notes

## Scope

This document records Windows x86-64 PokeMMO startup, visible 1920×1080 presentation, the earlier user-confirmed cursor alignment, and subsequently observed ROM loading in a PS5 Wine title. Final D-pad/touchpad UI acceptance, account-login/gameplay correctness, updater support, long-session stability, suspend/resume behavior, and Vulkan/OpenGL conformance are not claimed. The native filesystem Browse button crashed; staging ROMs replaces that workflow, not the dialog implementation.

## Current deployed artifacts

| Artifact | SHA-256 |
|---|---|
| Local `app/dist/PPSA99221/eboot.bin` (SELF) | `08457b842fe81410d440026916d86c48b9c59a70583a4ea16a1216611ba79866` |
| Local `app/build/ps5/eboot.elf` | `8f10cb94b7b27ea8411ab06db8ad6ff9b590a56d8fd623301ce9858c584a4e72` |
| FTP readback of installed `eboot.bin` (ELF) | `3f11837d988a2a09a1a1e42450f43ad380b931e970ba2e299c4c7eb429bef573` |
| Patched RADV archive | `14bf540e566960222a317abb3f30fb00118688975d3bef98f7fef127257c1c52` |
| Mesa WGL `opengl32.dll` | `1c41dae3157752809f95e4ee30a5c0871f3dae99bc9adfd59abcfca4af0025ea` |
| Mesa Zink `libgallium_wgl.dll` | `f509774d3961241371ab6b658465898e4a04778b6821234d8cc43b39139926dc` |
| User-supplied `PokeMMO.exe` revision `32920` | `127bd84a3010fa8cfc4b3a7816c9caa085d9035f819df092a42a9f152b679baf` |
| Menu/launch backgrounds (`pic0.dds`, `pic1.dds`) | `63966cfef5ebedb7cdc5878b9d2656a4cfe61289f7158ee2ec8c0de6aa65e19b` |

The console FTP readback exposes a 34,219,504-byte ELF, not the uploaded SELF container. Its loadable program-header entries and every `PT_LOAD` payload match the local ELF byte-for-byte. Non-load metadata differs, so neither whole-file equality nor equality with the SELF hash is claimed. The current receipt is `build/runs/pokemmo-input-correction-receipt.json`; the earlier alignment proof is retained separately as `pokemmo-measured-cursor-receipt.json`. Both installed background DDS files matched their generated files byte-for-byte; their receipt is `build/menu-art/deployment-receipt.json`.

## Observed startup receipt

The startup records and corrected run (`build/runs/pokemmo-measured-cursor-fix.klog`) establish these milestones:

1. The title started Wine with `C:\Games\PokeMMO\PokeMMO.exe`, `/data/pokemmo/wine`, and `/data/pokemmo/prefix`.
2. Wine loaded the main PE at `0x140000000`, the expected preferred base for the 64-bit image.
3. PokeMMO identified GraalVM Community Java 25.0.4, revision `32920`, Windows 10/amd64 and `C:\Games\PokeMMO` as client home.
4. The client wrote `config/main.properties` and its console/mod logs inside the staged client directory.
5. Wine selected the application-local native `opengl32.dll` and `libgallium_wgl.dll`, while `vulkan-1.dll` and `winevulkan.dll` remained Wine builtins.
6. PokeMMO reported:
   - vendor: Mesa;
   - renderer: `zink Vulkan 1.4 (RADV NAVI21)`;
   - version: OpenGL 4.6 Compatibility Profile, Mesa 26.2.0 at `15a5e9933e`;
   - eight MSAA samples and a 16384 maximum texture size.
7. Wine assigned the display to the real 1920×1080 window; RADV registered one 1920×1080 VideoOut buffer set. PokeMMO's viewport/backbuffer also reported 1920×1080.
8. Only successful presents to the physical display surface count toward removing the splash. A successful headless present is not evidence of a visible game frame.
9. PokeMMO detected one XInput controller (vendor `1356`, product `3302`), generated its default theme/font atlases, opened PS5 audio at 48 kHz stereo, and loaded the Main Feed from mirror 0.
10. No client `ERROR`, exception or fatal record appeared in `log/console.log` through the bounded first-frame run.

The user confirmed “Aligned and working” for the earlier left-stick/Cross cursor build. That mapping was later replaced at the user's request by an always-enabled right-stick cursor plus normal left-stick/Cross gamepad controls, with D-pad movement assist and touchpad tapping. Current native counters record three physical primary-click down/up pairs, 34,200 D-pad-assist samples, zero queue-rejected stick moves and no recognized taps during the captured session. These are event-path observations, not final UI acceptance; the user moved on to the menu-background request before confirming the corrected controls.

Clicking the filesystem Browse button on the earlier build terminated the client; the captured tail contains GraalVM fatal diagnostics and exit status 3, but not enough of the stack to identify its cause. The later input-correction run loaded ROMs without that dialog, as described below.

## Graphics path

`PokeMMO.exe` runs through Wine 11.19 inside the single PS5 title, without streaming or a Linux guest. PokeMMO uses LWJGL/OpenGL. Linking the separate native `ps5-opengl` fullscreen EGL stack would create a second presentation and allocator owner inside the Wine/Vulkan title. The selected path instead preserves one display backend:

```text
PokeMMO / LWJGL WGL
  -> application-local Mesa opengl32.dll
  -> libgallium_wgl.dll / Zink
  -> Wine builtin vulkan-1.dll / winevulkan
  -> PS5 win32u display bridge
  -> linked RADV
  -> VideoOut
```

`WINEDLLOVERRIDES=opengl32=n,b;vulkan-1=b` selects the application-local Mesa WGL DLL and Wine's Vulkan loader. `tools/wine-ps5/build-zink.sh` builds only the Windows x86-64 WGL frontend plus Zink from the pinned PS5 Mesa source. zlib is statically linked. The script rejects an extra DLL, Windows Vulkan loader or ICD in its install closure.

A Wine-internal OpenGL root-context attempt logs a non-fatal failure before the application-local Mesa DLLs load. PokeMMO subsequently creates its WGL context successfully and reports the Zink renderer above.

## Black-screen root cause and 1080p policy

The pre-fix window trace (`build/runs/pokemmo-window-trace.klog`) identifies a hidden **WineD3D fake window** with a 123×0 client surface. It claimed the sole PS5 display first and allocated a clamped 123×1 swapchain; the real SDL game window subsequently received a headless surface. This explains why the previous third-present marker could coexist with a black screen.

The Wine patch now keeps helper surfaces smaller than 320×200 headless and leaves the display available for the game. Display ownership is released when that surface is destroyed. This is a single-display policy, not general multi-window compositing.

`app/runtime/pokemmo_config.c` updates only five launch-owned properties: windowed mode, width 1920, height 1080, maximization off, and software cursor off. Other settings, including ROM paths, are preserved. The real 4K Wine desktop/output mode remains available; its 1080p client window is presented full-screen by VideoOut. Forcing a 1080p desktop instead made this client maximize to 1920×1054; the attempted 1928×1114 desktop produced 1928×1088 and a 1440p scanout set. Neither experiment is in the final build.

## Pointer coordinate contract

The cursor plane was adapted from the local `ps5-proton/ps5-wow` GPU-title implementation. It shares RADV's existing VideoOut handle and mutex; it does not open another presenter. Cursor coordinates are mapped through the active swapchain viewport into its registered scanout buffer. On the verified run, source `(960,540)` mapped to scanout `(960,540)` in a 1920×1080 set. The copied fixed-4K conversion had incorrectly doubled these coordinates.

Wine's **hardware-input** API takes absolute **screen pixels**. It must not be confused with `NtUserSendInput`'s normalized 0–65535 input. A real PS5 Win32 probe proved three positioned clicks at client `(960,540)`, `(100,100)` and `(1819,979)`, checking both mouse-message coordinates and `GetCursorPos`/`ScreenToClient`. Separate and combined move/button forms passed (`cursor-contract-raw-pixels.klog`, `cursor-contract-combined-clicks.klog`).

Native stick, touchpad and USB motion now share one client-space pointer. Native actions carry their pointer position with each event; Wine maps it through the foreground client's origin. The queue publishes a coherent atomic coordinate pair before dispatch, and Wine serializes the drain across message-pump threads. There is no post-dispatch global cursor readback to overwrite newer queued motion. Native raw deltas are preserved with `SEND_HWMSG_RAWINPUT`, and button releases survive loss of the foreground client rectangle.

Right-stick cursor velocity uses a 24/128 dead zone, squared response, fractional accumulation and a 100 ms elapsed-time cap. A diagnosed signed/unsigned multiplication previously converted negative movement into huge positive deltas; the signed-time fix and both negative axes are covered by the host regression. The initial left-stick/Cross mouse mapping consumed gameplay controls. An R3 mode-switch variant was built and confirmed working, then explicitly superseded by the user's final policy: right stick always controls the cursor, left stick and Cross remain XInput, and R3 has no mode-toggle role. The existing L3+R3 keyboard shortcut remains.

The D-pad retains its native XInput button bits and supplies full-range digital left-stick axes while both physical left-stick axes are within 24 raw units of center. Opposing D-pad directions cancel on their axis; a deflected physical stick takes priority. This avoids synthesizing keyboard/mouse movement and supports a client movement path that ignores the D-pad button flags alone.

Tap-to-click recognizes a single-finger release within 200 ms, with no more than 12 touch units of excursion per axis. Movement beyond that bound, a replaced touch ID, multiple fingers or a physical press cancels the gesture. A single-finger physical press is primary anywhere on the pad; only a two-finger press selects secondary. OSK ownership, interception and disconnect discard pending synthetic presses, while an already-started release is drained. A separate aggregated mouse source holds each synthetic press for at least 80 ms so a polling client can observe it; interruption releases it early, and queue refusal retries the outstanding edge without releasing another source's held button.

Cursor visibility uses accepted native pointer activity and a 3-second idle deadline. It starts its grace period only once the VideoOut cursor is ready, stays visible for requested or delivered mouse-button holds, and wakes for motion, clicks or wheel events. Left-stick/D-pad/gamepad-button input is not pointer activity. The cursor plane is actually disabled while hidden and re-enabled on activity; there is no second presenter.

Host verification passed the native input-mapping regression (including right-stick separation, all D-pad directions alongside the left stick, tap cancellation/aggregation/queue pressure and idle boundaries), all 115 OSK checks, and the client-config test. These do not replace console evidence for actual devices, game interaction or raw-input consumers.

## ROM setup without Browse

For client revision `32920`, the observed console run automatically discovered supported uncompressed ROMs placed in the client's `roms/` directory. The title must be closed while files or configuration are changed. The filesystem Browse button is not a substitute: it crashed the client during qualification.

The locally supplied `pokemmo-port/README.md` (ROM section) and `pokemmo/main_config.properties` use explicit relative `client.roms.*` paths. The same keys exist in this Windows client's generated configuration and remain available as an optional expert configuration:

| ROM | Property | Example relative path |
|---|---|---|
| Black **or White**, version 1 (required) | `client.roms.nds` | `roms/pokemon_black.nds` |
| FireRed | `client.roms.fr` | `roms/pokemon_firered.gba` |
| Emerald | `client.roms.em` | `roms/pokemon_emerald.gba` |
| Platinum | `client.roms.nds2` | `roms/pokemon_platinum.nds` |
| HeartGold **or SoulSilver** | `client.roms.nds3` | `roms/pokemon_heartgold.nds` |

Substitute the actual filenames when using White or SoulSilver. The reference does not define a mapping for `client.roms.nds4`; do not invent one.

PokeMMO starts with `C:\Games\PokeMMO` as its working directory, so a value such as `roms/pokemon_black.nds` resolves inside the installed game's own `roms/` directory. No Linux launcher, Java replacement, ROM conversion or native file picker is involved. On the PS5, the relevant locations are:

- ROMs: `/data/pokemmo/prefix/dosdevices/c:/Games/PokeMMO/roms/`
- Configuration: `/data/pokemmo/prefix/dosdevices/c:/Games/PokeMMO/config/main.properties`

Set only properties for files that are actually present. Configure the portable client's `config/main.properties` before staging it, or edit the installed file over FTP while the title is closed. Do not replace the full Windows-client configuration with the handheld port's file: it selects handheld-specific graphics, themes and mods. The PS5 launch-config updater preserves explicit ROM entries while enforcing the five launch-owned graphics settings described under [the 1080p policy](#black-screen-root-cause-and-1080p-policy).

A host smoke invoked the actual launch-config updater on a configuration populated with all five reference ROM paths and verified that they survived unchanged alongside the 1080p settings. Initially the supplied ROM folders were empty. The later `pokemmo-input-correction-live/klog-20261011-142554.log` records six candidates and successful loads of White (`IRAO`), Emerald (`BPEE`), FireRed (`BPRE`), SoulSilver (`IPGE`) and Platinum (`CPUE`). FTP inspection confirmed the supplied files were in the game's `roms/` directory and all six `client.roms.*` properties were still empty. This establishes automatic directory discovery and those loads for revision `32920`; it does not qualify every ROM variant, archive handling or gameplay.

## Reproducible build

Third-party source revisions are pinned in `dependencies.json`; generated sources and client data remain under ignored directories. `PS5_Mesa` pins the Windows WGL/Zink source, `PS5_Mesa_RADV` pins the separate Mesa revision consumed by the PS5 Vulkan RADV archive, and `PS5_PayloadSDK` pins the SDK revision. The commit IDs and isolated host-package checksums in `dependencies.json` are the authoritative versions.

The verified macOS host used Python 3, Git, LLVM/Clang and LLD 23, matching LLVM 23 SPIR-V tools, llvm-mingw, CMake, Meson, Ninja, `pkg-config`, `glslangValidator`, `bsdtar`, modern Bison/Flex/GNU Make, and LLVM 19 target builtins selected through `PS5_CLANG`. Bootstrap and build from the repository root:

```sh
python3 tools/bootstrap.py
cp .env.example .env                 # set POKEMMO_CLIENT
bash tools/wine-ps5/bootstrap-wine.sh
bash tools/wine-ps5/build-zink.sh
bash tools/wine-ps5/build-console.sh --no-deploy
python3 tools/wine-ps5/stage-game.py --dry-run
```

`bootstrap-wine.sh` prepares Wine host tools and its PS5 cross tree. `build-zink.sh` emits exactly `opengl32.dll` and `libgallium_wgl.dll`; it does not stage a Windows Vulkan loader or ICD. `build-console.sh` builds the ordered RADV cursor/mailbox patch stack before linking the title, WGL/Zink and TLS, and removes the overlays in reverse order afterward. The resulting RADV project archive is required at title link time. Wine's regenerated patch reproduces its source tree, excluding only Git and local indexing metadata.

The verified macOS console-build invocation keeps LLVM 19 include/library flags out of the LLVM 23 Mesa host-tool build while selecting LLVM 19 only for the PS5 title's available x86-64 builtins:

```sh
env -u LDFLAGS -u CPPFLAGS \
  LLVM_CONFIG=/opt/homebrew/opt/llvm/bin/llvm-config \
  LLVM_MINGW="$HOME/llvm-mingw" \
  CC=/opt/homebrew/opt/llvm/bin/clang \
  CXX=/opt/homebrew/opt/llvm/bin/clang++ \
  PS5_CLANG=/opt/homebrew/opt/llvm@19/bin/clang \
  PATH="/opt/homebrew/opt/llvm/bin:/opt/homebrew/opt/lld/bin:$HOME/llvm-mingw/bin:$PATH" \
  bash tools/wine-ps5/build-console.sh --no-deploy
```

The repeated generated LLVM shim was exercised without rewriting itself. The native input/OSK test translation units still report unused-static-helper warnings; their behavioral checks pass.

## Bundle export

After building and qualifying the title and runtime on a console, close the title so the prefix registry is complete, then create the installable bundle:

```sh
python3 tools/make-bundle.py
```

`tools/make-bundle.py` reads the tested `/data/pokemmo` Wine runtime and prefix from the console, sanitizes credentials, temporary contents, owner labels and host paths, and combines them with `app/dist/PPSA99221` and the two WGL/Zink DLLs. It writes `build/release/PokeMMO-PS5.zip`, copies the optional FTP helper as `install.py`, and generates the manifest. Component notices must already be staged with the title; additional source archives and root licence notices must be assembled separately in the bundle staging directory before repacking with `--no-fetch`. The exporter includes that staged material but does not generate it. The portable bundle calls the console C drive `prefix/drive_c`; deployment must map its contents back to `prefix/dosdevices/c:`.

The current local `build/release/PokeMMO-PS5.zip` is 385.5 MiB. It has not been uploaded to GitHub and is not a public release asset.

The bundle excludes the official PokeMMO client, ROMs, installed `Games` content and private session data. Release inputs must also exclude `PokeMMO.exe`, `PokeMMO-Debug.exe`, native Linux/macOS client binaries, older client JARs, downloaded client archives, private prefixes, logs and credentials. The official Windows portable client must always be downloaded independently from [PokeMMO](https://pokemmo.com/en/downloads/portable/).

### Optional automated FTP helper

The [user installation guide](../README.md#install) documents manual FTP installation. For maintainers who deliberately choose the bundled helper, `install.py` automates the same path mapping, selects binary FTP transfers, disables supported FTP-server SELF conversion, skips matching files, and excludes the portable client's top-level `bin/` directory and `PokeMMO.sh`:

```sh
python3 install.py --host YOUR_PS5_IP --client '/path/to/PokeMMO-Client'
```

After updating the portable client on a computer, the helper can restage only that client:

```sh
python3 install.py --host YOUR_PS5_IP --client '/path/to/PokeMMO-Client' --skip-app
```

Its local transfer record contains file size and modification-time metadata, not credentials. If the initial client copy was made outside the helper, `--since "YYYY-MM-DD HH:MM"` can be supplied once so same-size files changed after that copy are resent.

## Automated source-release workflow

The hosted `.github/workflows/release.yml` runs only for a newly pushed tag. It runs the offline packaging regressions, rejects recognized official PokeMMO client content in the checkout, and creates a GitHub release with generated notes. It does **not** build or upload the PS5 title, Wine runtime, installer-ready bundle or official client. Its only downloads are GitHub's automatic **Source code (zip)** and **Source code (tar.gz)** archives, which cannot be installed on a PS5.

To publish a prepared source revision, create and push a new tag containing that workflow:

```sh
git add -A
git commit -m "Prepare PokeMMO PS5 public source"
git push -u origin main
git tag -a v0.1.0 -m "PokeMMO PS5 v0.1.0"
git push origin v0.1.0
```

Use a new version tag for each release. A `main` push alone does not trigger the workflow, and there is no manual **Run workflow** button. Follow **Source release** in the repository's [Actions tab](https://github.com/bizkut/ps5-pokemmo/actions); after its checks pass, the source release appears under [Releases](https://github.com/bizkut/ps5-pokemmo/releases). Reruns preserve an existing release rather than replacing its contents. An installable `PokeMMO-PS5.zip`, if one is ever attached, is a separate manually produced and console-qualified asset.

## One-process Wine constraints

- The PS5 title cannot start normal Wine helper processes; the Wine server runs on a thread.
- The title has 16 KiB host pages. `ps5_mman.c` presents Windows-compatible 4 KiB commit/protection semantics and direct-memory executable mappings.
- Unix libraries (`win32u`, `winevulkan`, networking, TLS, XInput and audio) are linked into `eboot.bin` and selected from a generated table rather than `dlopen`.
- Native title bridges provide files, DNS/IPv4, TLS entropy, audio, XInput, keyboard/mouse and the on-screen keyboard.
- The title supplies an emulated working directory because title `chdir`/relative-path behavior is restricted. PokeMMO is started with its install directory as cwd.
- DNS is IPv4-backed. PokeMMO's observed Main Feed request succeeded, but broader network behavior is not qualified.
- Client self-update may require a second process and is not claimed. Update the portable client on the computer and restage it.

## Startup-link failure found during qualification

The first locally built title faulted before `main` at a null `pthread_mutex_lock` import in libc++ static initialization. The old title link passed every SDK stub, so the symbol bound to `libkernel_web`; that module advertises names not usable by a game title on this console.

The permanent fix is in `app/ps5/tools/link-title.sh`: both the LLVM link and PS5 title conversion consume `radv_link_stubs`, the explicit list produced by the pinned PS5 Vulkan link recipe. It excludes `libkernel_web` and weak fallback stubs. After relink/deploy, the title reached Wine, PokeMMO, Zink, audio, controller detection, network data and presentation.

## Runtime layout

```text
/data/homebrew/PPSA99221/                         native title
/data/pokemmo/ca-certificates.crt                 TLS trust bundle
/data/pokemmo/wine/                              Wine PE modules and NLS data
/data/pokemmo/prefix/                            registry and drive C
/data/pokemmo/prefix/dosdevices/c:/Games/PokeMMO/
  PokeMMO.exe
  PokeMMO-Debug.exe
  opengl32.dll
  libgallium_wgl.dll
  data/
  config/
  log/
  roms/
```

The prefix user is `pokemmo`. Release sanitization removes credentials, temporary contents, owner labels and host-only paths before packaging. In an extracted bundle, the contents of `runtime/pokemmo/prefix/drive_c/` must be copied to the console's `/data/pokemmo/prefix/dosdevices/c:/`; `drive_c` is not the deployed directory name.

Advanced runtime settings are `NAME=value` lines in `/data/pokemmo/launch.txt`. Diagnostic variables use the `POKEMMOPS5_` prefix. Delete `launch.txt` to restore defaults.

## Repository layout

| Path | Purpose |
|---|---|
| `app/` | PS5 title, Wine host integration, controller/audio/OSK bridges, and diagnostic probe |
| `runtime/wine-ps5/` | Pinned Wine patch, PS5 memory layer, and TLS compatibility |
| `runtime/radv-ps5/` | PS5 RADV presentation patch inherited from the template |
| `tools/wine-ps5/` | Wine, Zink, TLS, staging, console, and input tools |
| `ARTWORK.md` | Artwork provenance, redistribution confirmation, and attribution/licence limits |
| `docs/technical-notes.md` | Architecture, qualification receipts, limitations, and diagnosed failures |

## Reference boundary

The PortMaster handheld project was used for portable layout, OpenGL requirements and the explicit ROM-path method. Its launcher, patches, themes and assets are not part of the PS5 implementation; its full handheld configuration must not replace the Windows client's settings.

The native cursor's 12×19 arrow and GPU-plane approach came from the user-supplied local `ps5-proton/ps5-wow` tree (`ps5/title/cursor-arrow.h`, `ps5/probes/psmkit-wine/runtime-input.c`, and the RADV cursor patch). Those local cursor sources did not declare a separate licence for the PS5 addition, so this provenance does not establish an MIT grant. The maintainer has explicitly confirmed permission for this repository to redistribute the copied arrow artwork; its original artist and open-content licence remain undocumented. That artwork is outside the root MIT licence, and the confirmation does not supply a licence for separately copied implementation code. See the [artwork notice](../ARTWORK.md).
