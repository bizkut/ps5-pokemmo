#!/usr/bin/env python3
"""Make the release bundle from a console where the port works, and this tree's title.

  tools/make-bundle.py [--out build/release]

The console's /data/pokemmo is the tested state of Wine's Windows side and of
the prefix. The bundle is a copy of it without the user's portable client, test
programs, temporary files or anything private a session wrote:

  PokeMMO-PS5/title/PPSA99221     app/dist/PPSA99221 of this tree
  PokeMMO-PS5/runtime/pokemmo     /data/pokemmo: wine/, prefix/ (no Games), ca-certificates.crt;
                                  the console's prefix/dosdevices/c: is prefix/drive_c here, a name
                                  every PC can hold (install.py puts it back)
  PokeMMO-PS5/zink                application-local opengl32.dll and libgallium_wgl.dll
  PokeMMO-PS5/install.py          tools/install.py
  PokeMMO-PS5/README.txt, MANIFEST.sha256

Close the title first: the prefix's registry is written when it ends.
"""
import argparse
import hashlib
import os
import shutil
import sys
from ftplib import FTP
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / 'tools'))
from release_client_guard import OFFICIAL_PORTABLE_URL, reject_official_client_content  # noqa: E402
sys.path.insert(0, str(ROOT / 'vendor/PS5_Vulkan/tools'))
import ps5_console  # noqa: E402

TITLE_ID = 'PPSA99221'
BUNDLE_NAME = 'PokeMMO-PS5'
RUNTIME_NAME = 'pokemmo'
REMOTE = '/data/pokemmo'
TOP = {'wine', 'prefix', 'ca-certificates.crt'}                  # not the client, tests or launch settings
SKIP_PATHS = {'prefix/dosdevices/c:/games', 'prefix/drive_c/games'}  # the client is the user's own
ZINK_DLLS = ('opengl32.dll', 'libgallium_wgl.dll')


def walk(ftp, remote, relative=''):
    rows = []
    ftp.retrlines('LIST ' + remote, rows.append)
    for row in rows:
        fields = row.split(None, 8)
        name = fields[8]
        if name in ('.', '..'):
            continue
        path = f'{relative}/{name}'.lstrip('/')
        if ((not relative and name not in TOP) or
                path.casefold() in SKIP_PATHS):
            continue
        if row.startswith('d'):
            yield from walk(ftp, f'{remote}/{name}', path)
        else:
            yield path, int(fields[4])


def local_name(path):
    return path.replace('prefix/dosdevices/c:/', 'prefix/drive_c/', 1)


def remove_private_contents(directory):
    if not directory.is_dir():
        return
    for path in sorted(directory.rglob('*'), key=lambda item: len(item.parts), reverse=True):
        if path.is_symlink() or path.is_file():
            path.unlink()
        elif path.is_dir():
            path.rmdir()


def sanitize(runtime):
    """Remove credentials, temporary state, account labels and host-only paths."""
    import re
    users = runtime / 'prefix' / 'drive_c' / 'users'
    if users.is_dir():
        for user in (path for path in users.iterdir() if path.is_dir()):
            private = (
                user / 'AppData' / 'Local' / 'Temp',
                user / 'AppData' / 'Local' / 'Microsoft' / 'Credentials',
                user / 'AppData' / 'Roaming' / 'Microsoft' / 'Credentials',
                user / 'AppData' / 'Roaming' / 'Microsoft' / 'Protect',
            )
            for directory in private:
                remove_private_contents(directory)
            temp = private[0]
            temp.mkdir(parents=True, exist_ok=True)
            (temp / '.keep').write_text('')
    for name in ('system.reg', 'user.reg', 'userdef.reg'):
        path = runtime / 'prefix' / name
        text = path.read_text(encoding='utf-8', errors='surrogateescape')
        kept, skipping = [], False
        for line in text.split('\n'):
            if line.startswith('['):
                skipping = bool(re.match(r'\[Software\\\\(?:PokeMMO|PokeMMOPS5)InprocTest\]', line))
            if skipping:
                continue
            if re.match(r'"(?:RegisteredOwner|RegisteredOrganization)"=', line, re.I):
                continue
            if (re.search(r'[Zz]:\\\\(?:home|Users)\\\\', line) or
                    re.search(r'/(?:home|Users)/', line)):
                continue
            kept.append(line)
        path.write_text('\n'.join(kept), encoding='utf-8', errors='surrogateescape')


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--out', type=Path, default=ROOT / 'build/release')
    parser.add_argument('--no-fetch', action='store_true', help="keep the runtime already in --out (the console is not read): only the title, Zink DLLs and archive are made again")
    args = parser.parse_args()
    bundle = args.out / BUNDLE_NAME
    runtime = bundle / 'runtime' / RUNTIME_NAME
    title = ROOT / 'app/dist' / TITLE_ID
    mesa_install = Path(os.environ.get('MESA_INSTALL', ROOT / 'build/mesa-wgl-zink-install')).expanduser()
    zink_source = mesa_install / 'bin'
    reject_official_client_content(title, 'the title staging folder')
    reject_official_client_content(bundle, 'the bundle staging folder')
    if not (title / 'eboot.bin').is_file():
        sys.exit('build the title first (tools/wine-ps5/build-console.sh)')
    missing = [zink_source / name for name in ZINK_DLLS if not (zink_source / name).is_file()]
    if missing:
        sys.exit('missing WGL/Zink artifact(s): ' + ', '.join(str(path) for path in missing) +
                 '; run tools/wine-ps5/build-zink.sh first (or set MESA_INSTALL)')
    fetched = 0
    if not args.no_fetch:
        settings = ps5_console.load_settings()
        ftp = FTP()
        ftp.connect(settings['host'], settings['ftp_port'], timeout=600)
        ftp.login(settings['ftp_user'], settings['ftp_password'])
        ftp.voidcmd('TYPE I')
        files = list(walk(ftp, REMOTE))
        print(f'{len(files)} files, {sum(size for _, size in files) / 2**20:.0f} MiB on the console', flush=True)
        for path, size in files:
            local = runtime / local_name(path)
            if local.is_file() and local.stat().st_size == size and not path.endswith('.reg'):
                continue
            local.parent.mkdir(parents=True, exist_ok=True)
            with local.open('wb') as stream:
                ftp.retrbinary(f'RETR {REMOTE}/{path}', stream.write, blocksize=1 << 20)
            if local.stat().st_size != size and not path.endswith('.reg'):
                sys.exit(f'{path}: {local.stat().st_size} of {size} bytes')
            fetched += 1
        wanted = {runtime / local_name(path) for path, _ in files}
        for local in sorted(p for p in runtime.rglob('*') if p.is_file() and p not in wanted):
            local.unlink()                                       # gone from the console since the last bundle
        for name in ('system.reg', 'user.reg', 'userdef.reg'):
            text = (runtime / 'prefix' / name).read_bytes()
            if not text.startswith(b'WINE REGISTRY') or not text.endswith(b'\n'):
                sys.exit(f'{name} is not whole: close the title and run this again')
        sanitize(runtime)
        reject_official_client_content(runtime, 'the fetched runtime')
    elif not (runtime / 'prefix' / 'user.reg').is_file():
        sys.exit('--no-fetch needs a runtime from an earlier run')
    shutil.rmtree(bundle / 'title', ignore_errors=True)
    shutil.copytree(title, bundle / 'title' / TITLE_ID)
    zink = bundle / 'zink'
    shutil.rmtree(zink, ignore_errors=True)
    zink.mkdir(parents=True)
    for name in ZINK_DLLS:
        shutil.copy2(zink_source / name, zink / name)
    shutil.copy(ROOT / 'tools/install.py', bundle / 'install.py')
    (bundle / 'README.txt').write_text(
        "PokeMMO PS5: Wine, WGL/Zink and RADV in one PS5 app. The official client is not included.\n\n"
        "Independently download and extract the portable client from the official page:\n"
        f"  {OFFICIAL_PORTABLE_URL}\n\n"
        "Then install it, with the console on, jailbroken, its FTP server running and ShadowMount loaded:\n\n"
        "  python3 install.py --host <the console's address> --client \"<your extracted PokeMMO-Client folder>\"\n\n"
        "The guide, the source of everything here and the licences: see the project page.\n"
        f"Licence notices of what the app contains: title/{TITLE_ID}/licenses/.\n")
    lines = []
    for path in sorted(p for p in bundle.rglob('*') if p.is_file() and p.name != 'MANIFEST.sha256'):
        lines.append(f'{hashlib.sha256(path.read_bytes()).hexdigest()}  {path.relative_to(bundle).as_posix()}')
    (bundle / 'MANIFEST.sha256').write_text('\n'.join(lines) + '\n')
    archive = args.out / f'{BUNDLE_NAME}.zip'
    import zipfile
    with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED, compresslevel=6) as bundle_zip:
        for path in sorted(p for p in bundle.rglob('*') if p.is_file()):
            bundle_zip.write(path, Path(BUNDLE_NAME) / path.relative_to(bundle))
    print(f'{fetched} fetched; {len(lines)} files in the bundle; {archive} is {archive.stat().st_size / 2**20:.0f} MiB')
    return 0


if __name__ == '__main__':
    sys.exit(main())
