# PokeMMO on PS5

Run the official Windows PokeMMO client directly on a jailbroken PS5. The game runs locally, not through streaming.

This project does **not** include the PokeMMO client, ROMs, credentials, or account data. You must download the client from PokeMMO and supply your own legally obtained ROMs.

## Package availability

GitHub's automatic **Source code (zip)** and **Source code (tar.gz)** downloads are source only. They are not PS5 install packages and do not contain the title, Wine runtime, or installer-ready files.

Look for an installable package named **`PokeMMO-PS5.zip`** in the release assets. It has **not yet been uploaded to GitHub**. Until it is available, obtain the package from the maintainer or follow the [build and packaging instructions](docs/technical-notes.md#bundle-export).

The official PokeMMO client is never bundled in either kind of download.

## Requirements

- A jailbroken PS5 with an FTP server, normally on port `2121`.
- ShadowMount or an equivalent way to register titles under `/data/homebrew`.
- An FTP client that can use binary transfer mode.
- An extracted installable `PokeMMO-PS5.zip`, containing `title/PPSA99221`, `runtime/pokemmo`, and `zink`.
- The separately downloaded official Windows portable PokeMMO client.
- Your legally obtained ROMs. Pokémon Black or White, version 1, is required by PokeMMO.

## Install

1. **Extract the PS5 package.**

   Extract `PokeMMO-PS5.zip` on your computer. A GitHub **Source code** archive cannot be used in its place.

2. **Download and extract the PokeMMO client.**

   Download the official Windows portable client from [PokeMMO's portable download page](https://pokemmo.com/en/downloads/portable/). Keep this download separate from the project. The extracted directory must contain `PokeMMO.exe`, `PokeMMO-Debug.exe`, `data/`, and the rest of the Windows client.

3. **Add your ROMs with PokeMMO closed.**

   Put supported, uncompressed ROM files in the portable client's `roms/` directory:

   - Pokémon Black or White, version 1 (`.nds`) — required.
   - FireRed (`.gba`), Emerald (`.gba`), Platinum (`.nds`), and HeartGold or SoulSilver (`.nds`) — optional regions.

   Client revision `32920` discovered supported files in this directory automatically during the observed PS5 run. No ROM download links or ROM files are provided here.

4. **Close PokeMMO and the PS5 title, then connect by FTP.**

   Use binary transfer mode (`TYPE I`). If the FTP server supports the `SELF` command, issue it and, if the response says `enabled`, issue it once more so the final response says `disabled`. Keep automatic SELF conversion disabled while uploading the title, especially `eboot.bin`.

5. **Create the destination directories and copy these files.**

   | From the extracted package or client | PS5 destination |
   |---|---|
   | Contents of `title/PPSA99221/` | `/data/homebrew/PPSA99221/` |
   | Contents of `runtime/pokemmo/wine/` | `/data/pokemmo/wine/` |
   | `runtime/pokemmo/ca-certificates.crt` | `/data/pokemmo/ca-certificates.crt` |
   | `runtime/pokemmo/prefix/*.reg` | `/data/pokemmo/prefix/` |
   | Contents of `runtime/pokemmo/prefix/drive_c/` | `/data/pokemmo/prefix/dosdevices/c:/` |
   | Contents of the official Windows portable client directory | `/data/pokemmo/prefix/dosdevices/c:/Games/PokeMMO/` |
   | `zink/opengl32.dll` and `zink/libgallium_wgl.dll` | `/data/pokemmo/prefix/dosdevices/c:/Games/PokeMMO/` |

   The packaged `prefix/drive_c` directory is only a portable representation of the console's C drive. Copy its **contents** to `prefix/dosdevices/c:`; do not leave them under a `prefix/drive_c` directory on the PS5.

   Copy the complete Windows client, not just `PokeMMO.exe`: this includes `PokeMMO-Debug.exe`, `data/`, `config/`, `roms/`, the revision file, and the client's other files. The top-level `bin/` directory containing Linux/macOS binaries and `PokeMMO.sh` are not needed on PS5 and may be omitted. Confirm that both Zink DLLs sit beside `PokeMMO.exe` after the upload.

   Skip desktop metadata such as `.DS_Store`, `Thumbs.db`, `desktop.ini`, `._*` files, and `__MACOSX/` folders.

6. **Register and launch the title.**

   Use ShadowMount or its equivalent to register `/data/homebrew/PPSA99221`, then launch **PokeMMO PS5** from the home screen.

If you did not stage ROMs before installing, close PokeMMO, copy them over FTP to:

```text
/data/pokemmo/prefix/dosdevices/c:/Games/PokeMMO/roms/
```

Restart PokeMMO after copying them. Do not use the client's **Browse** button on PS5; it is known to crash. Expert ROM property settings and paths are in the [technical notes](docs/technical-notes.md#rom-setup-without-browse).

## Updating the client

Update the portable client on your computer, close the PS5 title, and copy the complete Windows client directory to `/data/pokemmo/prefix/dosdevices/c:/Games/PokeMMO/` again over FTP. Keep the two Zink DLLs beside `PokeMMO.exe`.

Client-initiated updating on PS5 is not qualified; updating on a computer and restaging the client is the supported method.

## Controls

| Input | Action |
|---|---|
| Left stick | Normal in-game movement |
| D-pad | Movement assist while the left stick is centered; the left stick takes priority when moved |
| Cross/X | Normal gamepad A button; it does not click the mouse |
| Right stick | Always moves the cursor |
| Touchpad movement | Relative pointer movement |
| Short one-finger touchpad tap | Primary click |
| Physical touchpad press | One finger is primary click; two fingers is secondary click |
| R3 | Normal stick-click button; there is no cursor-mode toggle |
| L3 + R3 | Opens the PS5 on-screen keyboard for the focused text field |
| USB keyboard and mouse | Standard Windows keyboard and mouse input |

The cursor hides after 3 seconds without pointer activity. Moving the right stick, using the touchpad or mouse, clicking, or scrolling shows it again. Normal gamepad movement does not keep the cursor visible.

## Known limitations

- This port is experimental. Startup and 1080p display have been tested, but login, gameplay, suspend/resume, and long sessions have not been fully verified.
- Cursor alignment was confirmed, but the latest D-pad movement and touchpad clicks still need final in-game verification.
- The filesystem **Browse** button crashed the client. Stage ROMs in the `roms/` directory instead.
- Automatic discovery was observed for supported uncompressed Black/White, FireRed, Emerald, Platinum, and HeartGold/SoulSilver files. Other variants and archive formats are not qualified.
- Updating the client on PS5 is not fully tested. Update it on your computer, then copy the updated files over FTP as described above.

For measured receipts, architecture, graphics details, build instructions, packaging, source-release behavior, and advanced ROM configuration, see the [technical notes](docs/technical-notes.md).

## Credits and licences

- [WoWPS5](https://github.com/BeLikeBrett/WoWPS5), used as the title/Wine template.
- [Wine](https://www.winehq.org/) (LGPL-2.1-or-later).
- [Mesa](https://www.mesa3d.org/) WGL, Zink, and RADV (MIT), including Mihawk's PS5 Mesa/Vulkan work.
- [PS5 Payload SDK](https://github.com/ps5-payload-dev/sdk).
- [PokeMMO Port for PortMaster](https://github.com/lowlevel-1989/pokemmo-port), used only as a behavioral reference.

Project-owned code, scripts, and documentation are covered by the root [MIT licence](LICENSE). Included menu, icon, and cursor artwork is not; redistribution permission is recorded, but the original artists and open-content licences remain unknown. See the [artwork notice](ARTWORK.md) and [title artwork notes](app/README.md#ps5-menu-artwork). Built packages carry component notices under `title/PPSA99221/licenses/`.

This project is independent and is not affiliated with PokeMMO, the Pokémon rights holders, or Sony Interactive Entertainment.
