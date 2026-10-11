#!/usr/bin/env python3
"""Exercise actual relocated Windows code and reject malformed PE fixtures."""
import json
import os
import shutil
import struct
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / 'build/windows'
WORK = ROOT / 'work/pe-loader-check'
BUILD.mkdir(parents=True, exist_ok=True)
WORK.mkdir(parents=True, exist_ok=True)
mingw = os.environ.get('MINGW_CC') or shutil.which('x86_64-w64-mingw32-gcc') or shutil.which('x86_64-w64-mingw32-clang')
if not mingw:
    raise SystemExit('missing x86_64-w64-mingw32 compiler (or set MINGW_CC)')
subprocess.run([mingw, '-O2', '-nostdlib', '-fno-stack-protector',
                '-Wl,--entry,PokeMMOPS5Smoke', '-Wl,--enable-reloc-section',
                str(ROOT / 'tools/pe-smoke.c'), '-lkernel32', '-o', str(BUILD / 'pe-smoke.exe')], check=True)
subprocess.run(['g++', '-std=c++20', '-O2', '-Wall', '-Wextra', '-DPOKEMMOPS5_PE_HOST_TEST',
                str(ROOT / 'app/runtime/pe_probe.cpp'), '-o', str(BUILD / 'pe-host-test')], check=True)
original = (BUILD / 'pe-smoke.exe').read_bytes()
pe = struct.unpack_from('<I', original, 0x3c)[0]
opt = pe + 24
sections = opt + struct.unpack_from('<H', original, pe + 20)[0]
cases = [('valid', original, True), ('truncated', original[:80], False)]
bad = bytearray(original)
struct.pack_into('<H', bad, pe + 4, 0x14c)
cases.append(('wrong-architecture', bad, False))
bad = bytearray(original)
struct.pack_into('<I', bad, sections + 12, 0xffff0000)
cases.append(('section-outside-image', bad, False))
bad = bytearray(original)
struct.pack_into('<I', bad, sections + 20, len(bad) - 1)
cases.append(('section-outside-file', bad, False))
bad = bytearray(original)
struct.pack_into('<I', bad, sections + 40 + 12, struct.unpack_from('<I', bad, sections + 12)[0])
cases.append(('overlapping-sections', bad, False))
results = []
for name, data, expected in cases:
    exe, report = WORK / (name + '.exe'), WORK / (name + '.json')
    exe.write_bytes(data)
    result = subprocess.run([str(BUILD / 'pe-host-test'), str(exe), str(WORK / 'absent-client'), str(report)],
                            capture_output=True, text=True, timeout=10)
    actual = json.loads(report.read_text())['smokePassed']
    if actual != expected or result.returncode != (0 if expected else 1):
        raise SystemExit(f'{name}: unexpected result {result.returncode}, smokePassed={actual}')
    results.append({'case': name, 'passed': True})
print(json.dumps({'checks': results, 'platform': 'Linux host; PS5 tested separately'}, indent=2))
