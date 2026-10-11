#!/usr/bin/env python3
"""Copy the portable Windows PokeMMO client to the console.

The client is not part of this project: POKEMMO_CLIENT names the folder that
holds your own PokeMMO.exe.

  tools/wine-ps5/stage-game.py [--dry-run] [--only PATTERN] [--shard I/N]

Uploads over the console's FTP server to drive C: of the console's prefix,
  /data/pokemmo/prefix/dosdevices/c:/Games/PokeMMO
which the client sees as C:\\Games\\PokeMMO. The installation on the PC is
only read. A file whose size on the console already matches is skipped, so an
interrupted run is continued by running it again; a file cut short is sent
again from its start.

After the client has updated on the PC, run this again: a file is also sent
when it has changed on the PC since this script last sent it (its size and time
are kept in work/pokemmo-client-staged.json). The first time after a copy made
before that record existed, give --since "YYYY-MM-DD HH:MM" (when that copy
ended) to send what changed after it.

The complete portable client is copied, including data, config, roms, logs and
user additions. Only the top-level bin directory, PokeMMO.sh, and platform
metadata are omitted. The built WGL/Zink DLL pair is placed beside PokeMMO.exe.

A title cannot ask the console how much space is free, and neither can this
script: a failed upload (a full disk, most likely) removes the file that
failed, stops, and says how much had been sent, so that it can be removed.
"""
import argparse
import datetime
import fnmatch
import json
import os
import sys
import time
from ftplib import FTP, all_errors, error_perm
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent



def install_folder():
    """The client folder on this PC: POKEMMO_CLIENT from the environment or project .env."""
    value = os.environ.get('POKEMMO_CLIENT')
    env_file = ROOT / '.env'
    if not value and env_file.is_file():
        for line in env_file.read_text(encoding='utf-8').splitlines():
            if line.strip().startswith('POKEMMO_CLIENT='):
                value = line.split('=', 1)[1].strip().strip('"\'')
    client = Path(value).expanduser() if value else None
    if client is None or not (client / 'PokeMMO.exe').is_file():
        sys.exit('set POKEMMO_CLIENT (environment or .env, see .env.example) to the portable Windows '
                 'client folder that holds PokeMMO.exe')
    return client


CLIENT = install_folder()
TARGET = '/data/pokemmo/prefix/dosdevices/c:/Games/PokeMMO'
RECORD = ROOT / 'work/pokemmo-client-staged.json'
ZINK_DLLS = ('opengl32.dll', 'libgallium_wgl.dll')
MESA_INSTALL = Path(os.environ.get('MESA_INSTALL', ROOT / 'build/mesa-wgl-zink-install')).expanduser()
METADATA_NAMES = {'.DS_Store', 'Thumbs.db', 'desktop.ini'}
METADATA_DIRS = {'__MACOSX', '.Spotlight-V100', '.Trashes', '.fseventsd'}


def is_platform_metadata(relative):
    return (relative.name in METADATA_NAMES or relative.name.startswith('._') or
            any(part in METADATA_DIRS for part in relative.parts))


def wanted():
    files = {}
    skip_bin = (CLIENT / 'bin').is_dir()
    for path in sorted(CLIENT.rglob('*')):
        relative = path.relative_to(CLIENT)
        if ((skip_bin and relative.parts[0] == 'bin') or relative == Path('PokeMMO.sh') or
                is_platform_metadata(relative)):
            continue
        if path.is_file():
            files[relative] = path
    zink = MESA_INSTALL / 'bin'
    missing = [zink / name for name in ZINK_DLLS if not (zink / name).is_file()]
    if missing:
        sys.exit('missing WGL/Zink artifact(s): ' + ', '.join(str(path) for path in missing) +
                 '; run tools/wine-ps5/build-zink.sh first (or set MESA_INSTALL)')
    for name in ZINK_DLLS:
        files[Path(name)] = zink / name
    yield from sorted(files.items(), key=lambda entry: entry[0].as_posix())


def connect(settings):
    ftp = FTP()
    ftp.connect(settings['host'], settings['ftp_port'], timeout=600)
    ftp.login(settings['ftp_user'], settings['ftp_password'])
    ftp.voidcmd('TYPE I')
    return ftp


def ensure(ftp, directory, made):
    path = ''
    for part in directory.strip('/').split('/'):
        path += '/' + part
        if path in made:
            continue
        try:
            ftp.mkd(path)
        except error_perm:
            pass
        made.add(path)


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--dry-run', action='store_true', help='list what would be sent')
    parser.add_argument('--only', help='only files whose path matches this pattern, e.g. "data/*"')
    parser.add_argument('--shard', default='0/1', help='I/N: every Nth file starting at the Ith, to run N uploads side by side')
    parser.add_argument('--since', help='also send files changed on the PC after this time ("YYYY-MM-DD HH:MM"), where there is no record of what was sent')
    args = parser.parse_args()
    shard, shards = (int(part) for part in args.shard.split('/'))
    files = [(relative, path) for relative, path in wanted()
             if not args.only or fnmatch.fnmatch(relative.as_posix(), args.only)]
    files = files[shard::shards]
    total = sum(path.stat().st_size for _, path in files)
    print(f'{len(files)} files, {total / 2**30:.1f} GiB, to {TARGET}', flush=True)
    if args.dry_run:
        for relative, path in files:
            print(f'{path.stat().st_size:>12}  {relative.as_posix()}')
        return 0
    sys.path.insert(0, str(ROOT / 'vendor/PS5_Vulkan/tools'))
    import ps5_console
    settings = ps5_console.load_settings()
    ftp = connect(settings)
    record = json.loads(RECORD.read_text()) if RECORD.is_file() else {}
    since = datetime.datetime.strptime(args.since, '%Y-%m-%d %H:%M').timestamp() if args.since else None
    made = set()
    sent = kept = sent_bytes = 0
    started = time.time()
    for relative, path in files:
        target = f'{TARGET}/{relative.as_posix()}'
        size, changed = path.stat().st_size, int(path.stat().st_mtime)
        known = record.get(relative.as_posix())
        stale = relative.as_posix() in ZINK_DLLS or (known != [size, changed] if known else
                                                     since is not None and changed > since)
        try:
            if ftp.size(target) == size and not stale:
                if not known:
                    record[relative.as_posix()] = [size, changed]
                kept += 1
                continue
        except all_errors:
            pass
        ensure(ftp, target.rsplit('/', 1)[0], made)
        began = time.time()
        try:
            with path.open('rb') as stream:
                ftp.storbinary('STOR ' + target, stream, blocksize=4 * 1024 * 1024)
            if ftp.size(target) != size:
                raise OSError(f'the console has {ftp.size(target)} of {size} bytes')
        except all_errors + (OSError,) as error:
            print(f'FAILED at {relative.as_posix()}: {error}', flush=True)
            RECORD.write_text(json.dumps(record))
            try:
                ftp = connect(settings)
                ftp.delete(target)
                print('the file that failed was removed from the console', flush=True)
            except all_errors as second:
                print(f'and it could not be removed: {second}', flush=True)
            print(f'sent before that: {sent} files, {sent_bytes / 2**30:.2f} GiB; already there: {kept}', flush=True)
            return 1
        sent += 1
        sent_bytes += size
        record[relative.as_posix()] = [size, changed]
        if sent % 20 == 0:
            RECORD.write_text(json.dumps(record))
        if size >= 64 << 20:
            seconds = max(time.time() - began, 0.001)
            print(f'{relative.as_posix()}: {size / 2**20:.0f} MiB at {size / 2**20 / seconds:.0f} MiB/s; '
                  f'{sent_bytes / 2**30:.1f} GiB sent in {(time.time() - started) / 60:.0f} min', flush=True)
    RECORD.write_text(json.dumps(record))
    print(f'done: {sent} sent ({sent_bytes / 2**30:.2f} GiB), {kept} already there', flush=True)
    return 0


if __name__ == '__main__':
    sys.exit(main())
