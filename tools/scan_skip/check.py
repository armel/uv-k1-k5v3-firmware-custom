#!/usr/bin/env python3
"""Run scan skip unit and scanner integration tests with no radio attached.
Requires Clang and an existing Fusion compile_commands.json for include paths.
"""
import json
import os
from pathlib import Path
import shlex
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'build/scan-skip-tests'
OUT.mkdir(parents=True, exist_ok=True)
commands = json.loads((ROOT / 'build/Fusion/compile_commands.json').read_text())
command = next(c for c in commands if c['file'].endswith('/App/app/chFrScanner.c'))
includes = [a.replace('/src/', str(ROOT) + '/') for a in shlex.split(command['command']) if a.startswith('-I')]
flags = ['-std=gnu11', '-O1', '-g', '-DENABLE_FEAT_F4HWN', '-DENABLE_FEAT_F4HWN_SCAN_SKIP', '-DENABLE_FEAT_F4HWN_SCAN_PROGRESS', '-DPRINTF_INCLUDE_CONFIG_H',
         '-DPY32F071x8', '-include', 'tools/scan_skip/host_gpio.h',
         '-Wno-pointer-to-int-cast', '-Wno-c23-extensions', '-ffunction-sections', '-fdata-sections',
         '-fsanitize=address,undefined', '-fno-sanitize-recover=all', *includes]
linker = '-Wl,-dead_strip' if sys.platform == 'darwin' else '-Wl,--gc-sections'
for name, sources, extra in [
    ('queue', ['tools/scan_skip/test.c'], ['-Wall', '-Wextra', '-Werror']),
    ('scanner', ['tools/scan_skip/scanner_test.c', 'App/misc.c'], []),
    ('scanner-no-progress', ['tools/scan_skip/scanner_test.c', 'App/misc.c'], ['-UENABLE_FEAT_F4HWN_SCAN_PROGRESS']),
    ('scanner-fast', ['tools/scan_skip/scanner_test.c', 'App/misc.c'], ['-DENABLE_FEAT_F4HWN_SCAN_FASTER']),
]:
    binary = str(OUT / name)
    subprocess.run([os.environ.get('CC', 'clang'), *flags, *extra, 'App/app/scan_skip.c',
                    *sources, linker, '-o', binary], cwd=ROOT, check=True)
    subprocess.run([binary], cwd=ROOT, check=True)
