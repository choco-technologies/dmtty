#!/usr/bin/env python3
"""Run host tests using headers from a configured dmtty module build.

Usage: python3 tests/run_output_test.py build/crlf-stm32f7
Only the generated include paths are reused; the test itself uses the host CC.
"""
import json
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile

build = Path(sys.argv[1]).resolve()
commands = json.loads((build / 'compile_commands.json').read_text())
command = next(c for c in commands if c['file'].endswith('/src/dmtty.c'))
includes = [arg for arg in shlex.split(command['command']) if arg.startswith('-I')]
with tempfile.TemporaryDirectory(prefix='dmtty-output-') as tmp:
    exe = str(Path(tmp) / 'output_test')
    subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-Wall', '-Wextra',
                    '-Wno-unused-parameter', '-ffunction-sections', '-fdata-sections',
                    '-DDMOD_MODULE_NAME="dmtty"', '-DDMOD_MODULE_VERSION="0.1"',
                    '-DDMOD_LIBRARY_MODULE=1', '-DDMOD_APPLICATION_MODULE=0', '-DDMOD_MODULE=1', '-DDMOD_SYSTEM=0', '-DDMOD_dmtty', *includes,
                    str(Path(__file__).with_name('output_test.c')),
                    '-Wl,--gc-sections', '-o', exe], check=True)
    subprocess.run([exe], check=True)
