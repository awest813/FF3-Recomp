# Source-only repository preparation

This working tree was supplied as a ZIP checkout, without Git history. The local
`main` branch begins a new repository; it does not recreate upstream history.
The existing MIT license and profile-specific third-party notices are retained.

Publication includes framework code, handwritten profile hosts, tools, tests,
configuration, documentation and attributed third-party source/resources.
Commercial executable translations, game files, saves, downloads, local analysis,
captures, build outputs and local dependency installs stay on disk under ignored
paths. In particular, `profiles/vcs/generated/` is local, just like FF3's
`profiles/ff3/analysis/generated/`. A fresh clone needs local generation before
building a game player. The VCS player also requires its Windows dependencies.

CI on Linux and macOS builds the framework and FF3 asset-free tooling/tests.
It does not build the commercial-game corpus or claim gameplay validation.
Windows CI and a corpus-free VCS tools build are sequenced in
[the Windows and Linux build plan](WINDOWS_LINUX_BUILDS.md).
FF3 player preparation is documented in [the profile README](../profiles/ff3/README.md).

Before the first commit or a push:

```sh
python3 tools/check_repository.py
git diff --cached --check
git diff --cached --stat
git status --short
```

The audit examines Git's index, including already tracked files that `.gitignore`
cannot exclude. It rejects known local-data paths, common game/container binaries,
large files, and a small set of credential patterns. It is a guardrail, not a
complete secret scanner or a license determination; review the staged content.

Create the GitHub repository with the intended owner, name and visibility, then
connect its URL as `origin` and push `main` after making the initial commit.
No remote URL, Git identity, repository visibility or publication destination is
assumed by this preparation. Do not attach native player binaries to a release:
they contain locally translated game code.
