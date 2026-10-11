#!/usr/bin/env python3
"""Install PokeMMO PS5 on a console, and copy your own portable client.

  python3 install.py --host 192.168.1.50 --client "/path/to/PokeMMO-Client"

Run it from the unpacked release bundle (the folder that holds title/, runtime/
and zink/), or name that folder with --bundle. It uploads over the console's
FTP server:

  title/PPSA99221   ->  /data/homebrew/PPSA99221   the app (ShadowMount shows it on the home screen)
  runtime/pokemmo   ->  /data/pokemmo              Wine's Windows side and an empty Windows prefix
                                                     (prefix/drive_c goes to prefix/dosdevices/c:)
  your client       ->  /data/pokemmo/prefix/dosdevices/c:/Games/PokeMMO
  zink/*.dll        ->  beside PokeMMO.exe

Your client is only read. A file already on the console with the same size is
skipped, so an interrupted run is continued by running it again.

After the client has updated on the PC, run this again the same way (--skip-app
copies only the client). An update can change files without changing their
size, so what was sent is remembered in pokemmo-client-sent.json beside this
script, and a file that has changed on the PC since then is sent again. If the
client was copied by hand or by an earlier version, there is nothing remembered
yet: add --since "YYYY-MM-DD HH:MM", the time of that copy, to send what
changed after it.
"""
import argparse
import datetime
import json
import sys
import time
from ftplib import FTP, all_errors, error_perm
from pathlib import Path

TITLE_ID = 'PPSA99221'
RUNTIME_NAME = 'pokemmo'
CLIENT_TARGET = 'prefix/dosdevices/c:/Games/PokeMMO'
ZINK_DLLS = ('opengl32.dll', 'libgallium_wgl.dll')
METADATA_NAMES = {'.DS_Store', 'Thumbs.db', 'desktop.ini'}
METADATA_DIRS = {'__MACOSX', '.Spotlight-V100', '.Trashes', '.fseventsd'}


class Console:
    def __init__(self, args):
        self.args = args
        self.made = set()
        self.connect()

    def connect(self):
        self.ftp = FTP()
        self.ftp.connect(self.args.host, self.args.port, timeout=600)
        self.ftp.login(self.args.user, self.args.password)
        try:
            reply = self.ftp.sendcmd('SELF')
            if 'enabled' in reply:
                reply = self.ftp.sendcmd('SELF')
            if 'disabled' not in reply:
                raise RuntimeError(f'FTP SELF conversion was not disabled: {reply}')
        except all_errors as error:
            if not str(error).startswith('502'):
                raise
        self.ftp.voidcmd('TYPE I')

    def size(self, path):
        try:
            return self.ftp.size(path)
        except all_errors:
            return None

    def ensure(self, directory):
        path = ''
        for part in directory.strip('/').split('/'):
            path += '/' + part
            if path not in self.made:
                try:
                    self.ftp.mkd(path)
                except error_perm:
                    pass
                self.made.add(path)

    def send(self, local, remote, stale=False):
        """True when sent, False when it was there already. stale: send it though the size matches."""
        size = local.stat().st_size
        # The console's FTP servers answer for an executable with another size than
        # the file's (they read it as the system would): those are sent every time.
        signed = local.name == 'eboot.bin' or local.suffix in ('.prx', '.sprx', '.self', '.elf')
        if not signed and not stale and self.size(remote) == size:
            return False
        self.ensure(remote.rsplit('/', 1)[0])
        problem = 'the size on the console does not match'
        for attempt in (1, 2, 3):
            try:
                with local.open('rb') as stream:
                    self.ftp.storbinary('STOR ' + remote, stream, blocksize=1024 * 1024)
                if signed or self.size(remote) == size:
                    return True
            except all_errors as error:
                problem = str(error) or type(error).__name__
            time.sleep(2)
            try:
                self.connect()
            except all_errors as error:
                problem = f'the console stopped answering ({error})'
        sys.exit(f'could not send {local.name} to {remote}: {problem}\n'
                 'Is the console still on, with free space? If the app is running on the console, close it first.')


def upload_tree(console, source, target, what):
    files = sorted(p for p in source.rglob('*') if p.is_file())
    total = sum(p.stat().st_size for p in files)
    print(f'{what}: {len(files)} files, {total / 2**20:.0f} MiB, to {target}', flush=True)
    sent = done = 0
    began = time.time()
    for path in files:
        relative = path.relative_to(source).as_posix()
        if relative.startswith('prefix/drive_c/'):       # the console's name for it, which a Windows PC cannot hold
            relative = 'prefix/dosdevices/c:/' + relative[len('prefix/drive_c/'):]
        sent += console.send(path, f'{target}/{relative}')
        done += path.stat().st_size
        if time.time() - began > 15:
            began = time.time()
            print(f'  {done * 100 // max(total, 1)}%', flush=True)
    print(f'  done: {sent} sent, {len(files) - sent} already there', flush=True)


def is_platform_metadata(relative):
    return (relative.name in METADATA_NAMES or relative.name.startswith('._') or
            any(part in METADATA_DIRS for part in relative.parts))


def client_files(client, zink):
    files = {}
    skip_bin = (client / 'bin').is_dir()
    for path in sorted(client.rglob('*')):
        relative = path.relative_to(client)
        if ((skip_bin and relative.parts[0] == 'bin') or relative == Path('PokeMMO.sh') or
                is_platform_metadata(relative)):
            continue
        if path.is_file():
            files[relative] = path
    for name in ZINK_DLLS:
        files[Path(name)] = zink / name
    return sorted(files.items(), key=lambda entry: entry[0].as_posix())


def validate_client(client, zink):
    if not (client / 'PokeMMO.exe').is_file():
        sys.exit(f'{client} does not hold PokeMMO.exe: --client is the portable Windows client folder')
    missing = [zink / name for name in ZINK_DLLS if not (zink / name).is_file()]
    if missing:
        sys.exit('the release bundle is missing WGL/Zink artifact(s): ' +
                 ', '.join(str(path) for path in missing))


def upload_client(console, client, zink, target, since=None):
    files = client_files(client, zink)
    total = sum(path.stat().st_size for _, path in files)
    print(f'the PokeMMO client: {len(files)} files, {total / 2**20:.0f} MiB, to {target}', flush=True)
    record_file = Path(__file__).resolve().parent / 'pokemmo-client-sent.json'
    record = json.loads(record_file.read_text()) if record_file.is_file() else {}
    sent_bytes = sent = 0
    started = time.time()
    for relative, path in files:
        now = [path.stat().st_size, int(path.stat().st_mtime)]
        known = record.get(relative.as_posix())
        stale = relative.as_posix() in ZINK_DLLS or (known != now if known else
                                                     since is not None and now[1] > since)
        was_sent = console.send(path, f'{target}/{relative.as_posix()}', stale)
        record[relative.as_posix()] = now
        if was_sent:
            record_file.write_text(json.dumps(record))
            sent += 1
            sent_bytes += path.stat().st_size
            if path.stat().st_size >= 64 << 20:
                minutes = (time.time() - started) / 60
                print(f'  {relative.as_posix()}: {sent_bytes / 2**20:.0f} MiB sent in {minutes:.0f} min', flush=True)
    record_file.write_text(json.dumps(record))
    print(f'  done: {sent} sent ({sent_bytes / 2**20:.0f} MiB), {len(files) - sent} already there', flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--host', required=True, help="the console's address on your network")
    parser.add_argument('--port', type=int, default=2121, help="the console's FTP port (default 2121)")
    parser.add_argument('--user', default='anonymous')
    parser.add_argument('--password', default='')
    parser.add_argument('--bundle', type=Path, default=Path(__file__).resolve().parent,
                        help='the unpacked release bundle (default: where this script is)')
    parser.add_argument('--client', type=Path, help='your portable Windows client folder (the one with PokeMMO.exe)')
    parser.add_argument('--skip-app', action='store_true', help='only copy the client')
    parser.add_argument('--since', help='after a client update, with no record of an earlier copy: also send client '
                                        'files changed on the PC after this time ("YYYY-MM-DD HH:MM")')
    parser.add_argument('--title-dir', default=f'/data/homebrew/{TITLE_ID}', help=argparse.SUPPRESS)
    parser.add_argument('--root', default='/data/pokemmo', help=argparse.SUPPRESS)
    args = parser.parse_args()
    since = datetime.datetime.strptime(args.since, '%Y-%m-%d %H:%M').timestamp() if args.since else None
    title = args.bundle / 'title' / TITLE_ID
    runtime = args.bundle / 'runtime' / RUNTIME_NAME
    zink = args.bundle / 'zink'
    if not args.skip_app and not ((title / 'eboot.bin').is_file() and runtime.is_dir()):
        sys.exit(f'{args.bundle} is not an unpacked release bundle '
                 f'(no title/{TITLE_ID}/eboot.bin and runtime/{RUNTIME_NAME})')
    if not args.skip_app or args.client:
        missing = [zink / name for name in ZINK_DLLS if not (zink / name).is_file()]
        if missing:
            sys.exit('the release bundle is missing WGL/Zink artifact(s): ' +
                     ', '.join(str(path) for path in missing))
    if args.client:
        validate_client(args.client, zink)
    try:
        console = Console(args)
    except all_errors as error:
        sys.exit(f'no FTP server at {args.host}:{args.port} ({error}): is the console on, jailbroken, and its FTP server running?')
    if not args.skip_app:
        upload_tree(console, title, args.title_dir, 'the app')
        upload_tree(console, runtime, args.root, 'Wine and the Windows prefix')
    if args.client:
        upload_client(console, args.client, zink, f'{args.root}/{CLIENT_TARGET}', since)
    else:
        print('no --client given: the PokeMMO client was not copied')
    print('finished. On the console: "PokeMMO PS5" on the home screen '
          '(ShadowMount adds it within a minute).')
    return 0


if __name__ == '__main__':
    sys.exit(main())
