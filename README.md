# PokeMMO on PS5

The Windows x86-64 PokeMMO client running locally in a single PS5 homebrew title through Wine. Rendering uses Mesa's Windows WGL frontend and Zink, then Wine Vulkan and the title's linked PS5 RADV driver. No streaming and no Linux guest.

## Status

| Surface | Observed result |
|---|---|
| Client startup | PokeMMO revision `32920` started from `C:\Games\PokeMMO`; GraalVM 25 initialized and persisted `config/main.properties` |
| Graphics | Application-local Mesa `opengl32.dll` loaded; PokeMMO reported OpenGL 4.6 Compatibility Profile on Zink → Vulkan 1.4 → RADV NAVI21 |
| Presentation | Visible PokeMMO UI; game viewport and VideoOut buffers are `1920×1080`, independently of the TV's output mode |
| Input | Native cursor presentation verified; current right-stick/tap/D-pad changes pass host checks, but final D-pad/touchpad UI acceptance remains pending |
| Audio | The PS5 audio port opened at 48 kHz stereo and Wine's event-driven mixer started |
| Network | The client loaded its Main Feed from mirror 0 |
| ROMs | Startup discovered staged ROM files with empty `client.roms.*` settings and loaded White, Emerald, FireRed, SoulSilver and Platinum |
| Known failure | The filesystem Browse button crashed the client; use the ROM-directory setup below instead |
| Not yet qualified | Final input UI acceptance, login/gameplay correctness, updates initiated by the client, suspend/resume, and long sessions |

PokeMMO, ROMs, credentials, and account data are not included. Evidence covers startup, 1080p presentation, the earlier confirmed pointer alignment, and the observed ROM loading—not general gameplay correctness; see [technical notes](docs/technical-notes.md).

## Install

### Source releases are not installable bundles

For every newly pushed tag, the hosted workflow runs the offline packaging regressions, rejects recognized official PokeMMO client content in the checkout, and creates a GitHub release with generated notes. Its only artifacts are GitHub's automatic **Source code (zip)** and **Source code (tar.gz)** archives. It does not build or upload a PS5 title, Wine runtime, or installer-ready package; those source archives cannot be installed on a PS5.

An installable `PokeMMO-PS5.zip`, when one is provided, is a separate maintainer artifact produced after the built title and runtime have been qualified on a PS5; it incorporates the sanitized Wine runtime/prefix retrieved from that console through `tools/make-bundle.py`. Hosted Actions do not produce it, and it is not promised for every tag. Unpack that bundle before following the installation steps below.

### Download the PokeMMO client yourself

Download the official client from **[PokeMMO's portable download page](https://pokemmo.com/en/downloads/portable/)** and extract it on your computer. This separate download is required for every installation. Use the extracted folder containing `PokeMMO.exe` and `data/` as the installer's `--client` directory.

**The official PokeMMO binaries are not included in this repository, the automatic source releases, or an installable `PokeMMO-PS5.zip`.** Keep your downloaded client outside the repository; do not commit it or attach it to a GitHub release. The installer reads that separately downloaded client from your computer and copies it to your PS5.

### Requirements

- A jailbroken PS5 with an FTP server (normally port 2121) and ShadowMount or equivalent `/data/homebrew` registration.
- Python 3 on a computer on the same network.
- An unpacked installable `PokeMMO-PS5.zip` containing `title/PPSA99221`, `runtime/pokemmo`, `zink`, and `install.py`. A GitHub **Source code** archive does not satisfy this requirement.
- Your separately downloaded and extracted official portable PokeMMO client directory containing `PokeMMO.exe` and `data/`.
- Your legally obtained ROMs for the regions you intend to play. PokeMMO requires a supported Pokémon Black or White ROM before gameplay.

With the target title closed:

```sh
python3 install.py \
  --host 192.168.1.50 \
  --client "/path/to/PokeMMO-Client"
```

The installer is resumable. It copies the Wine title/runtime, the Windows portable client, and the two WGL/Zink DLLs. Native Linux/macOS binaries under the client's top-level `bin/` directory and `PokeMMO.sh` are intentionally excluded. Existing files with matching size and unchanged source metadata are skipped.

The installed layout is:

| Local bundle/client | PS5 destination |
|---|---|
| `title/PPSA99221` | `/data/homebrew/PPSA99221` |
| `runtime/pokemmo/wine` | `/data/pokemmo/wine` |
| `runtime/pokemmo/prefix` | `/data/pokemmo/prefix` (`drive_c` becomes `dosdevices/c:`) |
| portable Windows client | `/data/pokemmo/prefix/dosdevices/c:/Games/PokeMMO` |
| `zink/opengl32.dll`, `zink/libgallium_wgl.dll` | beside `PokeMMO.exe` |

After ShadowMount registers the folder, launch **PokeMMO PS5** from the home screen.

The selection and launch backgrounds use the supplied sky artwork at 3840×2160; the tile icon is unchanged. Source images and conversion details are in [the title artwork notes](app/README.md#ps5-menu-artwork), and provenance, permission, and attribution limits are in [the artwork notice](ARTWORK.md).

### ROMs without the file browser

For client revision `32920`, simply placing supported uncompressed ROMs in the client's `roms/` folder and restarting is sufficient in the observed console run. The client found six candidates and loaded the supported regions while all `client.roms.*` settings remained empty.

1. Close PokeMMO before staging files.
2. Put your supported `.nds` and `.gba` ROMs in the client's `roms/` directory.
3. Start PokeMMO normally—do not use Browse.

The reference port instead supplies explicit paths using the following settings. They are available as an optional configuration method; set only entries for files you have, without replacing the full client configuration.

| ROM | Property | Example relative path |
|---|---|---|
| Black **or White**, version 1 (required) | `client.roms.nds` | `roms/pokemon_black.nds` |
| FireRed | `client.roms.fr` | `roms/pokemon_firered.gba` |
| Emerald | `client.roms.em` | `roms/pokemon_emerald.gba` |
| Platinum | `client.roms.nds2` | `roms/pokemon_platinum.nds` |
| HeartGold **or SoulSilver** | `client.roms.nds3` | `roms/pokemon_heartgold.nds` |

Substitute the actual filenames if using White or SoulSilver. The reference does not establish a mapping for `client.roms.nds4`; do not guess one.

On the PS5, these locations are:

- ROMs: `/data/pokemmo/prefix/dosdevices/c:/Games/PokeMMO/roms/`
- Configuration: `/data/pokemmo/prefix/dosdevices/c:/Games/PokeMMO/config/main.properties`

The installer copies ROMs placed in your local portable client's `roms/` folder. If supplying explicit paths, configure local `config/main.properties` before staging or edit the installed file over FTP with the title closed. Do not replace the whole configuration with the handheld port's file: it selects handheld-specific graphics, themes and mods.

The PS5 launcher preserves explicit ROM entries while enforcing its graphics settings; this was exercised on the host. Automatic discovery and the listed ROM loading are now observed on PS5, but other variants, archives, and full gameplay remain outside that receipt.

### Client updates

Update the portable client on the computer, close the PS5 title, then rerun:

```sh
python3 install.py --host 192.168.1.50 --client "/path/to/PokeMMO-Client" --skip-app
```

The transfer record is local and excludes credentials. If the initial client copy was made outside this installer, add `--since "YYYY-MM-DD HH:MM"` once so same-size changed files are resent.

## Input

| Input | Behavior |
|---|---|
| Left stick | Normal gamepad movement |
| D-pad | Movement through left-stick axes when the physical left stick is centered; a deflected stick takes priority |
| Cross/X | Normal gamepad A button; does not also click the mouse |
| Right stick | Always moves the native cursor, with a dead zone and fine-control response |
| Touchpad movement | Moves the same cursor |
| Short one-finger touchpad tap | Left click without mechanically pressing the touchpad |
| Physical touchpad press | Single-finger press anywhere is left click; two-finger press is secondary click |
| R3 alone | Normal gamepad stick-click button; **no mode toggle** |
| L3 + R3 | Opens the PS5 on-screen keyboard for the focused Windows text field |
| USB keyboard and mouse | Windows keyboard/mouse input using the same pointer |

There is no gamepad/cursor mode switch. Right-stick axes are reserved for the mouse; left-stick movement, D-pad directions, Cross and the other gamepad controls remain available to PokeMMO. D-pad input is not duplicated as keyboard or mouse events.

The cursor auto-hides after **3 seconds without pointer activity**. Pointer movement, a mouse click or scrolling makes it visible again. It stays visible while a mouse button is held; ordinary gamepad movement/buttons do not keep it on screen. A physical touchpad press, dragging, a long touch or a multi-finger gesture does not also generate a tap click.

Advanced runtime settings are `NAME=value` lines in `/data/pokemmo/launch.txt`. Diagnostic variables use the `POKEMMOPS5_` prefix. Delete `launch.txt` to restore defaults.

Normal game launches set `1920×1080`, disable maximization and leave PokeMMO's software cursor off to avoid a second pointer. The VideoOut cursor uses the active framebuffer's viewport, not the advertised 4K display size.

## Architecture

1. `PokeMMO.exe` runs through Wine 11.19 in the title process. Wine's server is a thread because a PS5 title cannot create the normal Wine process tree.
2. `WINEDLLOVERRIDES=opengl32=n,b;vulkan-1=b` selects the application-local Mesa WGL DLL and Wine's Vulkan loader.
3. `libgallium_wgl.dll` selects Gallium Zink. Zink emits Vulkan through Wine's `vulkan-1.dll`/`winevulkan` bridge.
4. Wine's PS5 `win32u` path creates the display surface; linked RADV drives the GPU and presents through VideoOut.
5. Native title bridges provide files, DNS/IPv4, TLS entropy, audio, XInput, keyboard/mouse, and the on-screen keyboard.

The handheld PortMaster project was used only as behavioral reference for the portable client layout and its OpenGL requirement. No code, assets, themes, configuration, or scripts were copied from it.

## Build from source

Third-party revisions are pinned in `dependencies.json`; generated sources and client data stay under ignored directories.
`PS5_Mesa` pins the Windows WGL/Zink build; `PS5_Mesa_RADV` pins the separate Mesa revision consumed by PS5 Vulkan's RADV archive.

```sh
python3 tools/bootstrap.py
cp .env.example .env                 # set POKEMMO_CLIENT
bash tools/wine-ps5/bootstrap-wine.sh
bash tools/wine-ps5/build-zink.sh
bash tools/wine-ps5/build-console.sh --no-deploy
python3 tools/wine-ps5/stage-game.py --dry-run
```

`bootstrap-wine.sh` prepares Wine host tools and its PS5 cross tree. `build-console.sh` builds the project's RADV cursor/mailbox patch stack before linking the title, as well as WGL/Zink and TLS. `build-zink.sh` emits exactly `opengl32.dll` and `libgallium_wgl.dll`; it does not stage a Windows Vulkan loader or ICD. The verified macOS build used matching LLVM/LLD and SPIR-V host tools at version 23, llvm-mingw, Meson/Ninja, modern Bison/Flex/GNU Make, and `PS5_CLANG` pointing to LLVM 19 for the title's available x86-64 builtins. See the exact environment in the technical notes.

After building the title and runtime, qualifying them on a console, and closing the title, create the installable bundle with:

```sh
python3 tools/make-bundle.py
```

This reads and sanitizes the console-derived Wine runtime/prefix, combines it with the built title and WGL/Zink DLLs, and writes `build/release/PokeMMO-PS5.zip`. It excludes the official client and private session data, and copies `tools/install.py` into the bundle as `install.py`. The included installer uploads that bundle plus the user's separately downloaded client. This manual, console-qualified path is distinct from the source-only tag workflow described above.

Release inputs must exclude `PokeMMO.exe`, `PokeMMO-Debug.exe`, native Linux/macOS client binaries, older client JARs, downloaded client archives, and installed `Games` content. ROMs, private runtime prefixes, logs, and credentials must not be committed either.

### Trigger a GitHub source release

Commit and push the prepared source, then create and push a new tag containing `.github/workflows/release.yml`:

```sh
git add -A
git commit -m "Prepare PokeMMO PS5 public source"
git push -u origin main
git tag -a v0.1.0 -m "PokeMMO PS5 v0.1.0"
git push origin v0.1.0
```

Use a new version tag for each release. A `main` push alone does not trigger this workflow, and there is no manual **Run workflow** button. Follow **Source release** in the repository's [Actions tab](https://github.com/bizkut/ps5-pokemmo/actions); after its checks pass, the release appears under [Releases](https://github.com/bizkut/ps5-pokemmo/releases). Reruns preserve an existing release rather than replacing its contents.

## Repository layout

| Path | Purpose |
|---|---|
| `app/` | PS5 title, Wine host integration, controller/audio/OSK bridges, and diagnostic probe |
| `runtime/wine-ps5/` | Pinned Wine patch, PS5 memory layer, and TLS compatibility |
| `runtime/radv-ps5/` | PS5 RADV presentation patch inherited from the template |
| `tools/wine-ps5/` | Wine, Zink, TLS, staging, console, and input tools |
| `ARTWORK.md` | Artwork provenance, redistribution confirmation, and attribution/licence limits |
| `docs/technical-notes.md` | Architecture, qualification receipt, limits, and diagnosed failures |

## Credits and licences

- [WoWPS5](https://github.com/BeLikeBrett/WoWPS5), used as the title/Wine template.
- [Wine](https://www.winehq.org/) (LGPL-2.1-or-later).
- [Mesa](https://www.mesa3d.org/) WGL, Zink and RADV (MIT), including Mihawk's PS5 Mesa/Vulkan work.
- [PS5 Payload SDK](https://github.com/ps5-payload-dev/sdk).
- [PokeMMO Port for PortMaster](https://github.com/lowlevel-1989/pokemmo-port), behavioral reference only.

Project-owned code, scripts, and documentation are covered by the root [MIT licence](LICENSE); included menu, icon, and cursor artwork is not. See the [artwork notice](ARTWORK.md) for its provenance, redistribution confirmation, and unresolved attribution/licence details. A built installable bundle carries component notices under `title/PPSA99221/licenses/`. This project is independent and is not affiliated with PokeMMO, the Pokémon rights holders, or Sony Interactive Entertainment.
