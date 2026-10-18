#!/usr/bin/env python3
"""Audit the Git index for local game/build data and common credential patterns."""
import re
import subprocess
import sys
from pathlib import PurePosixPath


def git(*args):
    return subprocess.check_output(['git', *args])


def main():
    paths = [p for p in git('ls-files', '-z').decode().split('\0') if p]
    if not paths:
        print('No indexed files. Stage the intended source files first.', file=sys.stderr)
        return 1
    forbidden_suffixes = {'.elf', '.iso', '.cso', '.pbp', '.prx', '.exe', '.dll',
                          '.lib', '.o', '.obj', '.a', '.so', '.dylib', '.zip', '.7z', '.rar'}
    credentials = re.compile(rb'-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----|'
                             rb'gh[pousr]_[A-Za-z0-9]{36,}|'
                             rb'github_pat_[A-Za-z0-9_]{40,}|'
                             rb'AKIA[0-9A-Z]{16}')
    failures = []
    total = 0
    for name in paths:
        path = PurePosixPath(name)
        parts = path.parts
        reasons = []
        if (parts[0] in {'out', 'build', '.venv'} or
            parts[0].startswith('Final_Fantasy_III_') or
            any(p in {'PSP_DATA', 'SAVEDATA', '__pycache__'} for p in parts) or
            (len(parts) > 2 and parts[0] == 'profiles' and parts[2] in
             {'game', 'analysis', 'progress', 'captures', 'generated'})):
            reasons.append('local data/output path')
        if path.suffix.lower() in forbidden_suffixes:
            reasons.append('binary/container suffix')
        if path.name == '.env' or (path.name.startswith('.env.') and path.name != '.env.example'):
            reasons.append('local environment file')
        data = git('show', ':' + name)
        total += len(data)
        if len(data) > 10 * 1024 * 1024:
            reasons.append('file exceeds 10 MiB source limit')
        if data[:4] in {b'\x7fELF', b'~PSP', b'\x00PBP', b'CISO'}:
            reasons.append('executable/game container signature')
        if credentials.search(data):
            reasons.append('possible credential (content withheld)')
        if reasons:
            failures.append(name + ': ' + ', '.join(reasons))
    if failures:
        print('\n'.join(failures), file=sys.stderr)
        return 1
    print(f'Index audit passed: {len(paths)} files, {total / 1024 / 1024:.2f} MiB.')
    return 0


if __name__ == '__main__':
    sys.exit(main())
