# Native audio, intro playback and pacing

The macOS player sends guest PCM through a bounded eight-channel stereo mixer to
AudioToolbox AudioQueue at 44.1 kHz. Channel release clears queued samples, and
mixing saturates only after summing channels. M toggles mute; fast forward,
scripted startup and modal dialogs flush/mute audio to avoid stale playback.
Master volume is adjustable in 10% steps, with a bounded gain ramp to avoid abrupt
volume changes. Wide intermediate arithmetic preserves eight-channel sums before
final clipping. The headless diagnostic keeps its optional PCM capture without opening a device.

The opening movie previously alternated a decoded picture with a black buffer:
GetVideoData consumed its ready flag while the guest alternated two display
buffers at 60 Hz for a 30 fps movie. It now copies the current decoded picture on
repeated reads, retaining its timestamp while paused or at EOF. This follows the
[PSP hardware test and expected output](https://github.com/hrydgard/pspautotests/tree/master/tests/video/psmfplayer).
RGBA copies use rows and preserve destination stride padding.

Presentation deadlines are anchored to guest time and the monotonic host clock.
Vblank scheduling preserves its 60 Hz phase instead of accumulating handler-time
drift. Identical images avoid redundant AppKit redraws. The title shows changed
frames per second and guest speed, so a static image can show zero fps even when
execution continues normally. `FF3_PERF=1` adds presentation and audio counters
to the launcher's log. A long stall resets the clock instead of producing a burst
of catch-up frames.

## Validation on 2026-09-27

- The user confirmed the live intro audio was **clear and in sync** on the Mac.
- Live intro measured about 60 presentations/sec, 30 changed frames/sec and
  100% guest speed, with no mixer overflow drops.
- After scripted startup, the cave ran at about 30 changed fps and 100% guest
  speed, with nonzero device audio and no mixer overflow drops.
- A focused capture interval produced 61 changed pictures and zero black frames.
- The full movie check reached EOF after 5,442 reads, including 5,321 nonblack
  frames; it decoded 8,005,632 audio frames with 15,763,307 nonzero samples.
- Seven asset-free CTest suites passed. Added coverage includes signed PCM volume,
  mono expansion, channel resets, mixing/clipping, silence, mute and overflow.

The optional real-asset check tests repeated reads into alternating buffers,
matching timestamps, pause behavior, EOF and nonzero audio:

```sh
cmake --build out/ff3 --target ff3_movie_check --parallel 1
out/ff3/profiles/ff3/ff3_movie_check profiles/ff3/game/disc
```

It requires the locally prepared retail assets and is intentionally separate from
CTest. Local evidence is under `analysis/intro-fixed/` and the live play logs;
these ignored files contain game-derived data and are not distribution assets.

These checks cover the intro and opening gameplay, not full-game timing or audio
accuracy. Approximate SAS effects, unsupported ATRAC second-buffer streaming and
limited movie seek/trick modes remain. See [native play](native-play.md) for controls.

## Presentation polish

Smooth enlargement uses [AppKit image interpolation](https://developer.apple.com/documentation/appkit/nsgraphicscontext/imageinterpolation).
F switches to unfiltered integer scaling in backing pixels, with centered
letterboxing to avoid uneven pixel widths at fractional window sizes. Neither
mode changes the guest framebuffer or increases internal rendering resolution.
Additional mixer tests verify monotonic stereo gain ramps, zero volume, gain
clamping and eight-channel accumulation.
