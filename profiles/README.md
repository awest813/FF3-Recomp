# Profiles

Profiles contain everything PSPRecomp needs for a particular title without contaminating the reusable framework with game-specific addresses or behavior.

A profile is selected at configure time:

```bash
cmake -S . -B out/<profile> -DPSPRECOMP_PROFILE=<profile>
```

The repository includes [`vcs`](vcs/README.md) and [`ff3`](ff3/README.md).
FF3 has an experimental macOS player with rendering, audio, opening gameplay,
and native save/load. Commercial game files and translated game-code corpora
remain local and are excluded from Git. See [`docs/PROFILE_GUIDE.md`](../docs/PROFILE_GUIDE.md)
before adding another title.
