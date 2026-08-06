# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build

Dependencies come from two manifests at the repo root — `apt-packages.txt` (system)
and `requirements.txt` (Python, for the verification tools only). The `aliases`
file drives both, and `build`/`deploy` live there too — there are no build or
deploy scripts:

```sh
source aliases
install_deps          # apt (interactive) + the dither_env virtualenv
build                 # cmake -B build && cmake --build build, then show README
deploy                # copy the git-tracked files to ../../public/dn-tools/dither
```

`install_deps` also exports `dither_env/bin` onto `PATH`, which is what lets
`coltest` reach the venv's `cv2` (see Verification). By hand:

```sh
cmake -B build        # configure (defaults to Release / -O3)
cmake --build build   # compile
```

To see the actual compiler flags used: `cat build/CMakeFiles/dither.dir/flags.make`

To rebuild with verbose output: `cmake --build build --clean-first -- VERBOSE=1`

Three executables are produced: `dither`, `srgbtest`, `temporaldither`.

## Source files

- `dither.cpp` — main entry point; the `ditherPass` driver, the `quantize_scan`
  helper, and all `DitherFn` threshold classes live here.
- `bluenoise.cpp` / `bluenoise.h` — void-and-cluster mask construction; exposes a
  single function `build_blue_noise_mask(...)` called from `dither.cpp`.
- `srgb.h` — the `vision::` namespace: inline sRGB curve primitives
  (`srgb_to_linear`, `linear_to_srgb`, `decode_pixel`, `encode_pixel`,
  `level_to_pixel`) plus declarations of the Mat-level helpers.
- `srgb.cpp` — Mat-level implementations: `to_gray8`, `decode_gray`,
  `encode_levels`, `average_gray`, and the 3-channel converters used by `srgbtest`.
- `srgbtest.cpp`, `temporaldither.cpp` — small independent test/demo executables.

## Architecture

All dithering operates in **linear light**. `main()`'s pipeline is:

1. `vision::decode_gray(image, useSrgb)` — coerce any input to `CV_8U` grayscale and decode to `CV_32F` linear `[0, 1]` (folded into step 2 by `decode_display`).
2. `vision::decode_display(image, ladder, useSrgb)` — decode, then place each value
   on the `LevelSet` ladder built by `make_levels(levels, useSrgb, srgb_spaced)`,
   giving display units in `[0, levels-1]`.
3. `ditherPass(target, dither_fn, spatial_fn, spatial, levels, frames, debug_row, debug_col)`
   — returns a `std::vector<cv::Mat>` of `frames` images, each `CV_8U` raw level
   indices in `[0, levels-1]`.
4. `vision::encode_levels(f, ladder)` per frame — LUT-encode level indices to the ladder's pixel values.
5. `vision::average_gray(frames, useSrgb)` — linear-light mean, then re-encode. With
   `--save-frames` the individual frames are also written as `<stem>-N.png`.

The output type is `CV_8U` (single-channel) end-to-end. The sRGB curve is applied exactly at the encode/decode boundaries and never inside the dither loop.

## Dither mechanism

`ditherPass` is a single driver parameterized by two callables:

- `DitherFn` — an abstract class. `operator()(float v, int row, int col)` returns a
  level index for the value `v`; the coordinates enable thresholds with per-pixel
  state (blue noise looks up a mask). `new_frame()` is called at the top of each
  output frame; thresholds with per-frame state override it, the rest inherit the
  no-op.
- `SpatialFn = void(*)(int row, int col, int dir, cv::Mat& spatial_err, float value)`
  — deposit `value` into neighbouring cells of `spatial_err` (or discard). `dir` is
  `+1` on left→right rows and `-1` on right→left rows. `spatial_none` is a no-op;
  `spatial_floydsteinberg` uses the 7/16, 3/16, 5/16, 1/16 pattern, mirrored
  by `dir` so it always pushes error towards not-yet-visited pixels.

Scanning is **serpentine**: `for_serpentine` walks even rows left→right and odd rows
right→left, which avoids the directional worming that a pure raster scan produces.

`quantize_scan` is the shared workhorse — one serpentine pass with its own private
`spatial_err` channel. Per pixel it computes `v = value_fn(row, col) + spatial_err`,
clamps `dither_fn(v, row, col)` to `[0, max_level]`, hands `spatial * residual`
to `spatial_fn`, and reports the chosen level to a `sink` callback. `ditherPass` uses
it twice: once for the goal pass and once per output frame; only `value_fn` and `sink`
differ.

The CLI exposes these as two orthogonal axes:

| `--threshold` | DitherFn |
|--------|----------|
| `round` (default) | `RoundDither` (`lround(v)`) |
| `random` | `RandomDither` (stochastic threshold) |
| `blue` | `BlueDither` (mask threshold) |

`--spatial <frac>` is the fraction of the residual handed to `spatial_fn`.
`parseOptions` resolves its default after the whole command line is parsed: `0`
if `--threshold` was named, `1` otherwise, and an explicit `--spatial` always
wins — so the two flags are order-independent.

`main()` picks `spatial_floydsteinberg` when the resolved value is above 0 and
`spatial_none` at exactly 0 — the two are equivalent there, but the no-op skips
the pointless deposit. The axes stay independent: every threshold works at any
diffusion strength.

## Multi-frame budget

Temporal accumulation is expressed as a **budget**, not a carried residual. Each
pixel gets `goal = target * frames`, and for `frames > 1` that goal is itself
dithered (by the same `dither_fn`/`spatial_fn`, at full `spatial = 1`) to an integer in
`[0, frames*(levels-1)]`. Every frame then aims at

```
ideal = (goal - displayed) / (frames - f) + spatial_err
```

where `displayed` is the running sum of levels already shown. Because the shortfall
stays in `goal - displayed`, the sum of displayed levels converges *exactly* to the
integer goal on the final frame — no separate accumulator is needed, and stochastic
thresholds need no special-casing.

For `frames == 1` the goal pass is skipped, `goal = target * 1` stays a float, and
the formula collapses to the classical `ideal = target + spatial_err`.

`--spatial` is the fraction of each pixel's quantization residual handed to
spatial diffusion (1 = full FS, 0 = none); it scales the per-frame passes only,
never the goal pass. The withheld remainder is not discarded in multi-frame runs:
it stays in `goal - displayed` and is therefore paid back by a later frame.

## Blue noise

`build_blue_noise_mask` in `bluenoise.cpp` runs progressive void-and-cluster: repeatedly place a pixel at the lowest-energy empty location and update a toroidal Gaussian energy map with a min-heap (`O(N·K·log N)` where `K` is the sparse kernel size).

`BlueDither` thresholds `frac(v) * 255` against
`mask[(row + sy) % rows][(col + sx) % cols]`. The modulo indexing handles both cases
uniformly:
- `--mask-size` unset: mask is the image size capped at 256 per side (building one
  is `O(N·K·log N)`, so an uncapped 1200×1600 mask costs ~60s against ~1s for the
  tile). At or below the cap the modulo is just a toroidal shift; above it, it tiles.
- `--mask-size WxH`: taken as given, including above the cap.

`BlueDither::new_frame()` draws a fresh toroidal offset per frame (`sx` before `sy`)
from the global `rng`, which `main()` seeds with `--seed`. The goal pass runs before
the first `new_frame()`, so it always uses offset `(0, 0)`.

The mask is written only when `--mask-out <path>` is given, and `--mask-in <path>`
loads one instead of building it (`cv::IMREAD_GRAYSCALE`, raw values — `BlueDither`
compares against `frac(v) * 255`, so no sRGB decode applies). Building uses its own
`std::mt19937_64(seed)` inside `bluenoise.cpp` and never touches the global `rng`, so
loading a mask leaves the per-frame offsets identical to the run that produced it —
`--mask-out` then `--mask-in` reproduces a result byte for byte.

## Key invariants

- `--linear` disables the sRGB curve entirely (input treated as raw linear, output written raw). Everything else in the pipeline is unaffected.
- `--levels 2` (default) produces only `0` and `255`, which are sRGB-invariant — the choice of `useSrgb` doesn't affect the pixel values, only the linear-light interpretation of intermediate values (which are absent). `--level-spacing` is likewise a no-op there.
- `--level-spacing srgb` moves the rungs but not the maths: `LevelSet::lin` holds each
  level's true linear value and `to_display` interpolates between rungs *in linear
  light*, so a fraction `f` between levels still averages to `(1-f)·lin[k] + f·lin[k+1]`.
  Interpolating in sRGB instead would look right and quietly break density preservation.
  For a uniform ladder `to_display` short-circuits to `linear * (levels-1)`, which is the
  arithmetic the pipeline used before ladders existed — keeping default output bit-identical.
- `goals` and `displayed` (`CV_32F`) live for the whole `ditherPass` and carry the
  temporal budget across frames. `spatial_err` (`CV_32F`) is freshly zeroed inside
  every `quantize_scan` — it's a within-scan diffusion channel only.
- Output frames are `CV_8U` grayscale. `average_gray` averages them in linear light (decode → mean → encode) — averaging raw pixel means would be perceptually wrong.

## Verification

`coltest` runs a matrix of dither invocations against `examples/ramp_256x256.png` and, for each output, calls `colstats` to check that per-column linear-light means match the reference within 3σ. A few configurations are inherently non-density-preserving (`round spatial=0`) or have known edge artifacts (`blue mask=32x32 spatial=0`); `coltest`'s `expected_failure` dict maps those labels to the maximum number of failing columns tolerated, and the run only fails overall if a test exceeds its allowance. `base.txt` / `basefull.txt` are saved runs from before the option rename, kept for comparison by hand — nothing reads them automatically.

`colstats` decodes both the reference and output columns to linear (unless `--linear` is passed), computes means in linear space, and reports the sRGB-encoded linear mean in the `ref`/`out` columns. Column-mean deviations near mid-grey with `N=256` rows can hit ~5 sRGB pixels of noise even for a perfectly density-preserving dither, so passes are z-score based rather than raw pixel diff.

`budgettest` checks the multi-frame invariant directly: with `--spatial 0` (per-frame
FS off), `sum_f display_f[r,c] == goal[r,c]` exactly for every threshold.

`dither --debug-pixel <row,col>` prints per-frame `ideal`/`display`/`displayed` for
that pixel, which is the quickest way to see the budget converge.

These scripts find their interpreter via `#!/usr/bin/env python3` and invoke each
other by path, so putting the virtualenv on `PATH` is what makes them use it —
`dither_env/bin/python coltest` is not enough, because the `colstats` it spawns
would still pick up the system `python3`. Without `cv2`, `coltest` reports
`COLSTATS PARSE FAIL` for every row:

```sh
export PATH="$PWD/dither_env/bin:$PATH"
./coltest
./budgettest
```
