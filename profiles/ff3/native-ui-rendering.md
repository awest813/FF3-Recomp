# Sprite and UI rendering checks

The software rasterizer now assigns a shared triangle edge to exactly one
triangle. Previously both triangles included boundary samples, so a translucent
quad could blend twice along its diagonal or a shared strip edge. Edge ownership
is normalized for either winding; the scalar and x86 SIMD paths use the same
half-open coverage rule. Interpolation weights are unchanged.

The software scissor decoder also preserves an endpoint of (0, 0). It previously
interpreted zero as an unset register and expanded it to the full framebuffer,
allowing sprites to escape a one-pixel clip rectangle.

Asset-free regressions submit real GE lists through DisplayHost and check:

- An alpha-blended panel has exactly one blend per pixel, for both windings and
  integer/half-pixel vertex positions, with no pixels outside the panel.
- A sprite clipped to (0, 0) writes only that pixel.
- Textured sprites preserve transparent backgrounds, mirror UVs correctly, and
  stay within their bounds.

The edge and scissor tests failed against the old implementation before fixes.
The host tests pass after the changes. The x86 SIMD branch was updated but is not
executed by the local ARM64 test run. No real-hardware pixel comparison has been
performed, and these checks do not validate every PSP rasterization detail.

The [top-left coverage reference](https://learn.microsoft.com/en-us/windows/uwp/graphics-concepts/introduction-to-rasterization-rules)
explains the shared-edge rule. PSP sprite vertex formats are documented in
[PSPSDK](https://pspdev.github.io/pspsdk/group__GU.html). This change uses the shared
MIT software rasterizer; no emulator implementation was copied.

## Half-pixel sprite coverage

Rectangle bounds now use the same pixel-center convention as UV sampling. A
2-pixel sprite shifted left by half a pixel must leave the next pixel untouched;
the old bounds included it. The new regression fails with the old implementation
and passes after the change, including mirrored UVs. Its coverage expectation
follows the PSP hardware [nearest-filter precision test](https://github.com/hrydgard/pspautotests/blob/master/tests/gpu/filtering/precisionnearest2d.expected)
and [test setup](https://github.com/hrydgard/pspautotests/blob/master/tests/gpu/filtering/precisionnearest2d.cpp).

## Party-menu panel gaps

A deterministic party-menu capture isolated a one-pixel gap in submitted panel
geometry: the lower-right x was 112 while the neighboring panel began at 113.
Memory-write tracing located the source vertices at 0x09E51E30, written after
FF3's UI rotation routine at 0x0893AE08. That routine receives the float full-turn
angle 0x40C90FDB, converts it to VFPU quarter-turn units (exactly 4), and uses
VROT before truncating positions to integers.

The host implementation evaluated sin(4 * float(pi/2)), leaving a small positive
residual. A full rotation could therefore turn x=113 into 112.99999, which FF3
truncated to 112. VROT now preserves exact cardinal sine/cosine endpoints and
signed zeros for representable integral angles up to 2^24 quarter turns. Other
angles retain the existing approximation; this is not a claim of bit-exact VFPU
transcendental math.

The endpoint expectations follow the PSP hardware [vector test output](https://github.com/hrydgard/pspautotests/blob/master/tests/cpu/vfpu/vector.expected)
and [input vectors](https://github.com/hrydgard/pspautotests/blob/master/tests/cpu/vfpu/vector.c).
The regression checks positive and negative cardinal rotations and the exact
FF3 corner/truncation case. It failed before the math fix and passes after it.
No game assets or submitted vertices are patched.

Headless diagnostics can set `FF3_TRACE_DISPATCH=0` to suppress dispatch logging.
Use `PSPRECOMP_NO_CHAIN=1` and `PSPRECOMP_RASTER_THREADS=1` for this menu capture,
especially when another player is running.

Reproduce the party-menu checkpoint with:

```sh
python3 profiles/ff3/tools/run_gameplay.py --party-menu
```

The runner saves `party-menu.png` and a probe report under
`profiles/ff3/analysis/gameplay-runs/`. Reaching its diagnostic frame limit does
not by itself assert visual correctness; inspect the image.

Validation: all seven CTest suites passed. The rebuilt probe completed the
opening-battle-to-party-menu sequence in 113.8 seconds. Visual inspection of
`analysis/gameplay-runs/20260927-131102-2c4bc50e/party-menu.png` confirms that all
four vertical panel seams and the bottom-bar mark are gone compared with
`analysis/ui-pixel-check/frame_010800.png`. These local captures contain game
assets and remain under the ignored analysis directory.
