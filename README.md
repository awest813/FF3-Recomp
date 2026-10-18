# PSPRecomp

PSPRecomp is a static recompilation framework for PSP software. It reads an Allegrex/MIPS executable, analyzes guest code, emits C++ translation units, and runs them through a native host runtime.

The repository has two layers:

- **Framework** — ELF/PRX loading, the Allegrex decoder, guest memory, the runtime, and the generic analyzer and recompiler (`psp_analyze`, `psp_recomp`).
- **Profiles** — one title's generated code, HLE, and host integration. The published profiles are GTA: Vice City Stories (`profiles/vcs`) and Final Fantasy III (`profiles/ff3`).

Game-specific addresses stay in the profile that measured them. The framework builds on its own.

## What builds today

| Build | Linux | Windows | macOS | Local game files |
| --- | --- | --- | --- | --- |
| Framework library, tools, and tests | Continuous integration | MSVC flags are in the CMake files; continuous integration does not run this host yet | Continuous integration | No |
| FF3 asset-free tooling and tests | Continuous integration | Same gap as the framework | Continuous integration | No |
| FF3 interactive player | No presentation backend | No presentation backend | Experimental Apple Silicon player | Yes |
| VCS player (`VCSNative`) | Configure stops without the local generated corpus. Window, audio, and DirectX 12 are Windows-only | Maintained DirectX 12 player when the local corpus is present | No player | Yes |

The [Windows and Linux build plan](docs/WINDOWS_LINUX_BUILDS.md) is the sequence for closing those gaps. macOS FF3 play stays in the profile docs.

## Repository layout

```text
include/psprecomp/   Public runtime and Allegrex interfaces
src/                 ELF/PRX loading, decoder, memory, runtime
tools/               Generic analyzer, recompiler, and helpers
tests/               Framework regression tests
configs/             Generic examples and PSP NID data
profiles/vcs/        Vice City Stories host and Windows build scripts
profiles/ff3/        Final Fantasy III host and asset-free tooling
docs/                Architecture, profile guide, and the build plan
```

## Requirements

Every build needs CMake 3.20 or newer and a C++20 compiler. The FF3 profile tests also need Python 3.9 or newer.

| Host | Maintained compiler |
| --- | --- |
| Linux | GCC or Clang. Continuous integration uses the `ubuntu-latest` default. GCC 13 passes the framework tests and the FF3 asset-free suite. |
| macOS | Apple Clang, as used by `macos-latest` and the FF3 player. |
| Windows | Visual Studio 2022 for the Vice City Stories DirectX 12 player. The framework CMake already passes MSVC flags; that configuration is not in continuous integration yet. |

## Build the framework

From the repository root, with no profile and no game files:

```bash
cmake -S . -B out/framework -DPSPRECOMP_PROFILE="" -DCMAKE_BUILD_TYPE=Release
cmake --build out/framework --parallel 2
ctest --test-dir out/framework --output-on-failure
```

This produces `psprecomp_core`, `psp_analyze`, `psp_recomp`, `dump_function`, and the framework tests. Ninja and Makefile builds take the configuration from `CMAKE_BUILD_TYPE`. A Visual Studio generator uses `--config Release` and `ctest -C Release`. The Windows plan keeps continuous integration on Ninja so those flags stay consistent across hosts.

## Build a profile

Select a profile with `PSPRECOMP_PROFILE`.

Asset-free FF3 tooling, the check continuous integration runs:

```bash
cmake -S . -B out/ff3-tools -DPSPRECOMP_PROFILE=ff3 -DCMAKE_BUILD_TYPE=Release -DFF3_BUILD_GENERATED=OFF
cmake --build out/ff3-tools --parallel 2
ctest --test-dir out/ff3-tools --output-on-failure
```

That suite covers the framework tests plus FF3 bootstrap, synthetic instruction, tooling, and setup tests. It leaves the player unbuilt.

The interactive FF3 player is a macOS application today. Opening gameplay through the world map, intro video, audio, keyboard controls, and native save/load have been exercised on Apple Silicon. Full-game compatibility is unverified. Follow the [profile README](profiles/ff3/README.md), [status](profiles/ff3/STATUS.md), and [quickstart](profiles/ff3/QUICKSTART.md). The player also needs a locally generated corpus, a prepared font module, and FFmpeg. Use one compiler job for the large generated units when memory is tight.

The Vice City Stories player is the Windows DirectX 12 application `VCSNative`. A fresh clone has no generated corpus, and configuring `-DPSPRECOMP_PROFILE=vcs` currently stops because profile-guided AOT expects `profiles/vcs/generated/generated_unit_0043.cpp`. With that corpus present, use the scripts in `profiles/vcs/scripts` or the [Ninja build notes](docs/VCS_NINJA_BUILD.md). See the [VCS profile README](profiles/vcs/README.md).

To add a title, follow [docs/PROFILE_GUIDE.md](docs/PROFILE_GUIDE.md).

## Game files

Profiles expect a decrypted ELF or PRX from the user's own copy, in the format that profile documents. PSPRecomp does not include commercial executables, disc images, or an EBOOT decryption implementation.

## Source provenance

[docs/SOURCE_PROVENANCE.md](docs/SOURCE_PROVENANCE.md) covers independently written code, profile boundaries, decryption, and third-party source.

The framework is MIT licensed. Profile dependencies keep their notices beside the files that need them. The VCS list is [profiles/vcs/THIRD_PARTY.md](profiles/vcs/THIRD_PARTY.md). See [LICENSE](LICENSE).

## Contributing

[CONTRIBUTING.md](CONTRIBUTING.md) describes the asset-free checks and what must stay out of Git. Continuous integration on Linux and macOS builds the framework and the FF3 tooling with `FF3_BUILD_GENERATED=OFF`. [docs/REPOSITORY.md](docs/REPOSITORY.md) describes the source-only publication boundary.
