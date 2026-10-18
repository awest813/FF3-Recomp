# PSPRecomp

PSPRecomp is a static recompilation framework for PSP software. It reads an Allegrex/MIPS executable, analyzes guest code, emits C++ translation units, and runs them through a native host runtime instead of shipping a PSP interpreter or JIT.

The repository is split between a reusable framework and game-specific profiles. Profiles include GTA: Vice City Stories (`profiles/vcs`) and an experimental Final Fantasy III native player for macOS (`profiles/ff3`).

## Repository layout

```text
include/psprecomp/   Public runtime and Allegrex interfaces
src/                 ELF/PRX loading, decoder, memory, runtime and support code
tools/               Generic analyzer, recompiler and reverse-engineering helpers
tests/               Framework regression tests
configs/             Generic examples and PSP NID data
profiles/            Game-specific hosts, configuration and tests
  vcs/               GTA: Vice City Stories profile
  ff3/               Final Fantasy III native player and tooling
```

Game-specific addresses, HLE behavior, native fast paths, renderer integration and generated AOT code belong under a profile. The framework should remain usable without any profile selected.

## Requirements

- CMake 3.20 or newer
- A C++20 compiler
- Visual Studio 2022 for the current Windows/DX12 VCS build

## Build the framework only

```bash
cmake -S . -B out/framework -DPSPRECOMP_PROFILE=""
cmake --build out/framework --config Release
ctest --test-dir out/framework -C Release --output-on-failure
```

This builds `psprecomp_core`, `psp_analyze`, `psp_recomp`, `dump_function` and the framework tests.

## Build a profile

Profiles are selected with `PSPRECOMP_PROFILE`:

```bash
cmake -S . -B out/vcs -DPSPRECOMP_PROFILE=vcs
cmake --build out/vcs --config Release
```

Windows users working on the VCS profile can use the maintained scripts in `profiles/vcs/scripts`.

The [Final Fantasy III profile](profiles/ff3/README.md) supports title-menu and
opening gameplay, intro video, audio, keyboard controls, and native save/load on
macOS. Saving and loading after a full restart have been verified past the opening
cave. Full-game compatibility is unverified. See the [current status](profiles/ff3/STATUS.md)
and [player quickstart](profiles/ff3/QUICKSTART.md).

To build FF3's asset-free tooling and tests:

```bash
cmake -S . -B out/ff3-tools -DPSPRECOMP_PROFILE=ff3 -DCMAKE_BUILD_TYPE=Release
cmake --build out/ff3-tools --parallel 2
ctest --test-dir out/ff3-tools --output-on-failure
```

Building the player additionally requires locally prepared game files, locally
generated C++/font code, and FFmpeg; follow the profile setup instructions. Use
one compiler job for the large generated units on machines with limited memory.
The VCS player additionally requires its local generated corpus and Windows
dependencies; those binaries and the commercial-game corpus are not published here.

## Create another profile

See [`docs/PROFILE_GUIDE.md`](docs/PROFILE_GUIDE.md). A new title normally provides its own generated corpus, HLE/profile host, configuration, tests and optional native fast paths without modifying the framework for game-specific addresses.

## Game files

No commercial game executable or asset is included. PSPRecomp does not ship an EBOOT decryption implementation. Profiles expect files obtained from the user's own copy in the format documented by that profile.

## Source provenance

See [`docs/SOURCE_PROVENANCE.md`](docs/SOURCE_PROVENANCE.md) for the project rules around independently written code, profile boundaries, decryption and third-party source.

## Third-party code

The framework is MIT licensed. Individual profiles may include separately licensed dependencies or assets; their notices stay beside those files. The VCS profile lists its bundled dependencies in `profiles/vcs/THIRD_PARTY.md`.

## License

PSPRecomp framework code is distributed under the MIT License. See [`LICENSE`](LICENSE).

## Contributing and publishing

See [CONTRIBUTING.md](CONTRIBUTING.md) for asset-free checks and contribution
scope, and [repository preparation](docs/REPOSITORY.md) for the source-only
publication boundary. CI builds the framework and FF3 tooling without game files.
