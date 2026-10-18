# Contributing

Keep reusable Allegrex/runtime changes in the framework and title-specific
behavior in the relevant profile. Preserve existing license and attribution
notices; see [source provenance](docs/SOURCE_PROVENANCE.md).

The asset-free check below matches continuous integration on Linux, macOS, and
Windows. It includes the FF3 software host tests. A VCS tools build that
configures without the local generated corpus is still sequenced in
[the Windows and Linux build plan](docs/WINDOWS_LINUX_BUILDS.md).
Windows and Linux FF3 probe instructions are in
[profiles/ff3/WINDOWS_LINUX.md](profiles/ff3/WINDOWS_LINUX.md).

Build and test changes without commercial inputs first:

```sh
cmake -S . -B out/check -DPSPRECOMP_PROFILE=ff3 -DCMAKE_BUILD_TYPE=Release
cmake --build out/check --parallel 2
ctest --test-dir out/check --output-on-failure
```

This covers framework, bootstrap, synthetic instruction, tooling and ISO/setup
tests.
Native FF3 host/media tests additionally require the local generated build and
FFmpeg. Do not describe a passing synthetic test as proof of full-game behavior.

For bug reports, include OS/architecture, compiler, profile, commit, reproduction
steps, and expected versus actual behavior. For game-specific bugs, provide the
release ID and input SHA-256, not the executable or asset files. Review logs for
personal paths before attaching them.

Do not commit game executables, disc images, extracted assets, saves, translated
commercial game code, captures, local analysis output, credentials or build
products. The synthetic fixtures in `tests/` are appropriate to commit. Before
submitting a change, stage the intended files and run:

```sh
python3 tools/check_repository.py
git diff --cached --check
git diff --cached --stat
```

Describe the problem, the resulting behavior, and the checks you actually ran.
Call out untested platforms and remaining limitations.
