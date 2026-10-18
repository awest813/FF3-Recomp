# Interactive native play on macOS

The native FF3 host now has a resizable macOS window, live keyboard controls and
device audio and frame pacing. The real guest framebuffer is scaled to the window with its aspect
ratio preserved. AppKit supplies presentation and input; no emulator process or
SDL installation is involved. AudioToolbox plays the mixed 44.1 kHz stereo PCM.

For a new checkout, use the [guided quickstart](QUICKSTART.md). After input
preparation and build configuration (guided or [manual](native-menu.md)):

```sh
cmake -S . -B out/ff3 -DFF3_BUILD_WINDOW=ON
cmake --build out/ff3 --target ff3_player --parallel 1
python3 profiles/ff3/tools/play.py
```

A normal launch plays the intro with sound; Start skips it.

You can also open `out/ff3/profiles/ff3/ff3_player.app` directly. This local app
build embeds the configured ELF path and depends on the existing local disc,
font and FFmpeg files; it is not a self-contained distributable.

To auto-start New Game and play through the initial sequence before handing over
to live input:

```sh
python3 profiles/ff3/tools/play.py --gameplay
```

The scripted opening runs without the presentation frame cap. At guest time
173 seconds it releases the scripted controls, enables live input and starts
normal pacing and sound. Fast forward and scripted startup mute and flush audio
to prevent delayed playback. Battle timing varies, so handoff may occur during the final
dialogue or battle; use Z to continue. This option starts a fresh New Game, not
a saved checkpoint. System messages return to interactive dialogs at handoff.

## Controls

Smooth scaling is the default for enlarged 3D scenes. F selects unfiltered scaling
at integer multiples of physical display pixels, with letterboxing where needed.
This changes presentation only; the guest still renders at 480 × 272. Volume
changes ramp over roughly 5 ms at most, and the title shows the selected mode and
volume. These controls apply to the current session.

Click the game window to focus it. Bindings use physical keyboard positions.

| Key | PSP control |
| --- | --- |
| Arrow keys | D-pad / movement and menu navigation |
| W A S D | Analog stick |
| Z | Cross / confirm |
| X | Circle / back |
| C / V | Square / Triangle |
| Q / E | Left / right shoulder |
| Return or Escape | Start |
| Backspace | Select |
| Tab | Toggle fast forward |
| M | Toggle mute |
| Minus / Plus (equals key) | Volume down / up by 10% |
| F | Toggle smooth / pixel-aligned scaling |
| Close window or Command-Q | Quit the native host |

On the name screen, Z opens a native text editor. Down then Z accepts the default
**Luneth** without editing. Name entry preserves the PSP field's UTF-16 length
limit and reports changed, unchanged and cancelled results to the game. This
implements the single-field name entry used here, not all PSP keyboard modes.
Message dialogs use native macOS buttons. Saving and loading use a native slot
picker with overwrite confirmation; see [save storage and validation](native-savedata.md).
Losing focus releases held keys. Short taps persist for three presentation ticks
so a press/release in the same event batch is not discarded. Normal presentation
follows guest time at about 60 Hz, with new intro pictures at about 30 fps. The
window title reports changed frames per second and guest speed; a static screen
can report zero fps. Set `FF3_PERF=1` when launching to log these measurements
and audio counters. Performance still depends on software rendering and this
unoptimized development build.

The launcher logs to a unique `analysis/play-runs/<run>/native.log`. Live play has
no diagnostic dispatch limit and does not continuously write framebuffer captures
or full dispatch traces. Headless `run_menu.py` and `run_gameplay.py` keep their
bounded runs and captures. The macOS window can be excluded with
`-DFF3_BUILD_WINDOW=OFF`; other platforms currently use the headless host.

## Verification and remaining work

The window rendered opening combat, victory rewards and the cave. The live app
also showed the startup message dialog. A close-button test stopped the host
cleanly with exit code zero after 105,948,477 dispatches. Seven CTest suites pass,
including live controller sampling/release, analog-vs-digital mode and interactive
dialog state transitions, name-entry result handling and output-buffer bounds.
Input timing and full-game correctness still need more
playtesting.

The intro audio was confirmed clear and in sync on the test Mac. See
[native media validation](native-media.md) for playback and pacing checks.
Gamepad support, full PSP save compatibility and
later progression remain unfinished. The software renderer and other HLE limits
listed in [native-gameplay.md](native-gameplay.md) still apply.

The window uses Apple's documented [event dispatch](https://developer.apple.com/documentation/appkit/nsapplication/nextevent(matching:until:inmode:dequeue:))
and native bitmap presentation APIs.
PSP keyboard structure offsets and states follow the local
[PSPSDK OSK declarations](https://github.com/pspdev/pspsdk/blob/master/src/utility/psputility_osk.h).
