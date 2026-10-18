# Native opening-gameplay checkpoint

The native NPUH10125 build starts New Game with the default name Luneth, runs the
opening cave sequence, and accepts Attack commands in the first battle. A local
run on 2026-09-27 defeated the goblins, displayed **Received 3 EXP!**, and returned
to the cave. A follow-up run also moved Luneth across the cave, scrolled the
camera and entered a random encounter with the battle command menu visible.
This executes the recompiled guest game with host services and the
software renderer; it does not substitute a host recreation of the game scene.

## Reproduce

Use the same local game, disc, font and build dependencies as
[the menu checkpoint](native-menu.md), then run from the repository root:

```sh
python3 profiles/ff3/tools/run_gameplay.py
```

Each run creates a fresh directory under `analysis/gameplay-runs/` containing the
trace, host/input hashes, exact controller script, framebuffer captures and four
PNG observation points plus the final frame. The runner uses a
100-million-dispatch ceiling, a 180-second host timeout, and an 11,040-vblank limit.
Vblanks are paced at roughly 60 Hz, but scheduler time jumps mean frame numbers
are not exact controller-script timestamps.
The higher dispatch ceiling permits the opening battle to finish; it changes no
guest instructions or game state.

| Guest time | Scripted action |
| --- | --- |
| 25 and 32 seconds | Start, to advance startup/movie |
| 41 seconds | Cross, to choose New Game |
| 50–68 seconds | Cross every three seconds for dialogue |
| 72 seconds | Down, to select OK below the default name |
| 74–170 seconds | Cross every three seconds for dialogue, Attack and target selection |
| 175–175.8 seconds | Hold Right in the cave |
| 178–179 seconds | Hold Up in the cave |

The name screen initially selects its editable field. Cross on that field calls
`sceUtilityOskInitStart`, which needs the interactive player's native editor.
Selecting the existing OK
button with Down preserves Luneth's default name and follows the game's normal
controller flow.

The runner checks that execution reaches its requested frame limit and that the
observation captures exist. **It does not automatically recognize a battle,
victory or movement.** Inspect `battle.png`, `battle-result.png`,
`before-movement.png`, `after-movement.png` and `last-frame.png` to assess those
states. Movement may trigger a random encounter, as it did in the reviewed run.
The generic
probe keeps `boot_validated: false` because a bounded stop alone proves none of
them. Script timing is specific to this pinned build and its current HLE timing.

## Scope

This runner is a headless diagnostic with scripted controller input. A separate
[macOS player](native-play.md) provides a live window and keyboard controls.
The macOS player supplies device audio. Gameplay
save/load and progression beyond the initial cave encounters remain unverified
or unimplemented. The existing
renderer, kernel, audio and utility limitations in the menu setup still apply.
Opening gameplay does not establish full-game compatibility or hardware accuracy.
