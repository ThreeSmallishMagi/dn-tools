# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

It is the **measurement record**: what was tried, what it measured, and what
that rules out. Four companion documents carry the rest and are not duplicated
here — `doc/GLOSSARY.md` defines every term of art, `doc/DESIGN.md` the design
rationale and the references, `doc/ARCHITECTURE.md` the module graph, and
`doc/DEFAULTS.md` the parameter inventory. Prefer adding a finding here and a
definition there.

## Build

Dependencies come from two manifests at the repo root — `apt-packages.txt` (system)
and `requirements.txt` (Python, for the verification tools only). The `aliases`
file drives both, and `build`/`deploy` live there too — there are no build or
deploy scripts:

```sh
source aliases
install_deps          # apt (interactive) + the dither_env virtualenv
build                 # cmake -B build && cmake --build build, then show README
install_dither        # bash completion, seed ~/.config/dither/{samples,crops}, calibrate them
deploy                # copy the git-tracked files to ../../public/dn-tools/dither
```

`install_dither` is `install_completion` plus `install_samples`, which ends by
running `calibrate_samples`: every installed film, for dft, inpaint and markov,
at the default settings, one at a time since each writes the same config. About
two minutes a film the first time, nearly all of it markov; a film already
calibrated is read back. Every later build from that film takes the
calibration from the config — `maybe_calibrate` looks it up on every sample
build, before `--explain` so it reports what will be used, and measures only
under `--mask-calibrate` — and `--no-config` is the way back to the rules.
`install_samples --no-calibrate` skips it, which `test/workflowcheck` uses.
The completion and the samples are separate because they install different
kinds of thing: the completion is a
program artefact, the samples are the user's own data. Seeding the library
**never overwrites** — a film directory may already hold the user's scans under
the same name, and adding to that is a convenience where replacing it is not.

`examples/films.tsv` is the manifest: one row per scan, copied whole into
`samples/<film>/` for markov and inpaint, which pool them, and on one row per
film a window that `test/findcrops --install` cuts into `crops/<film>.png` for
`direct` and `dft`, which take a film name to mean its crop (see dft below). Those crops are derived from the manifest, so they *are* replaced when
the window changes, with the old one kept as `<film>.png~`. A user's own film
gets a crop by saving one there under the film's name; nothing else is needed.
The installer reports a library file that differs from the shipped scan, or
sits beside it under the same stem, because the library used to hold crops
rather than scans and a crop left beside its scan is pooled with it.

Whole scans cost build time: on Kodak-4X's five scans (37.9 Mpx against the old
crops' 5 Mpx) a default build takes dft 32 s against 3.5, markov 10 s against
1.5 and inpaint 14 s against 4.7, and a cache hit 0.65 s against 0.1, because
the set is decoded before the size is known. dft no longer pays it for a film
name, which now means the film's crop: 3.3 s cold on Kodak-4X.

`install_deps` also exports `dither_env/bin` onto `PATH`, which is what lets
`test/coltest` reach the venv's `cv2` (see Verification). By hand:

```sh
cmake -B build        # configure (defaults to Release / -O3)
cmake --build build   # compile
```

To see the actual compiler flags used: `cat build/CMakeFiles/dither.dir/flags.make`

To rebuild with verbose output: `cmake --build build --clean-first -- VERBOSE=1`

Four executables are produced: `dither`, `maketexture`, `srgbtest`, `temporaldither`.

## Source files

- `main.cpp` — the process entry point and nothing else; everything below it is
  the `dither_lib` static library, so a test can link it rather than shelling out.
- `dither.cpp` — the pipeline and `dither_main`: decode onto the ladder, call the
  engine, encode what comes back.
- `engine.cpp` / `engine.h` — the dither itself: the `DitherFn` threshold classes,
  `SpatialFn`, `ditherPass`, and — private to the .cpp — `Space`, `for_serpentine`
  and `quantize_scan`. Depends on `srgb.h` and nothing else.
- `cli.cpp` / `cli.h` — option parsing, the usage text, `config_path`, and the
  reporting that goes with resolving defaults.
- `mask.cpp` / `mask.h` — mask policy: how big a tile, what it costs, and
  `obtain_mask`. Names no generator.
- `generators.cpp` / `generators.h` — the generator table: one row each for blue,
  dft, markov, inpaint and direct, carrying the name, whether it imitates a
  sample, whether `reconcile()` may assume it, whether it pools several scans
  (`combines`), whether a film name means the film's crop (`prefers_crop`), its
  default `--mask-highpass`, its registration, work formula, build, validation,
  sample limit and calibration. The only file that knows all five exist; adding
  a generator is a new module plus a row.
- `bluenoise.cpp` / `bluenoise.h` — void-and-cluster mask construction; exposes a
  single function `build_blue_noise_mask(...)`, reached through the table.
- `spectralmask.cpp` / `spectralmask.h` — `build_spectral_mask(...)`, the
  `--mask-sample --mask-method dft` generator: synthesises a mask matching a
  sample's spectrum.
- `markov.cpp` / `markov.h` — `build_markov_mask(...)`, the `--mask-method markov`
  generator: fits a pairwise Markov random field to the sample and Gibbs-samples it.
  Also `register_markov_params(...)`, its default rules (see Defaults below), and
  the `--markov-report` diagnostics, which stay here because they report on
  `fit_markov_model`'s local state.
- `markovcalib.cpp` / `markovmodel.h` — markov's `--mask-calibrate`, and the
  fit-and-draw pair it reaches the model through. `markovmodel.h` is shared with
  `markov.cpp` and nothing else.
- `inpaintcalib.cpp`, `dftcalib.cpp` — the other two calibrations: inpaint's
  sweep count and dft's spectrum smoothing.
- `calib.cpp` / `calib.h` — what the three share: the level-set, ridge and
  spectrum measures they score with, `inputs_for` (the sample read as the build
  reads it, and the film's crop as the reference), and the cache checks. Every
  calibration is cached in the config and used by every later run on that
  sample (`maybe_calibrate` in `mask.cpp`).
- `inpaint.cpp` / `inpaint.h` — `build_inpaint_mask(...)`, the `--mask-method inpaint`
  generator: fills each pixel from the sample neighbourhood most like its own,
  matched through a principal component analysis (PCA) subspace and a
  `cv::flann` k-dimensional tree.
- `directmask.cpp` / `directmask.h` — `build_direct_mask(...)`, the
  `--mask-method direct` generator: takes a tile of the sample itself rather
  than synthesising one, makes it join, ranks it flat. The only generator that
  cannot be toroidal by construction, which is what its seam handling is for.
- `samples.cpp` / `samples.h` — what `--mask-sample` names. A file, a directory
  of scans of one film, or a film in the library at
  `$XDG_CONFIG_HOME/dither/samples/<Manufacturer-FilmName>/`; a path the user
  typed always wins over a library name, since letting the library shadow
  `./Kodak-TriX` would make the same command mean different things on different
  machines. Several scans are kept **whole**, as a `samples::Set`, and every consumer
  iterates. A single Mat has to be rectangular, so flattening them forces a
  common width — and **cropping each to the narrowest threw away most of a film
  shot in mixed orientations**: 52% of Kodak-4X and 80% of Kodak-2484, where one
  portrait scan pulled the width down to 900 and a 5669-wide frame lost six
  columns in seven. Banding each scan into `round(W/C)` bands recovered that to
  94% and 96% but added a join per band, and no arrangement of one rectangle
  avoids both. The three generators that take several scans all pool *additive*
  statistics — a spectrum, a table of pair counts, a neighbourhood database — so
  none of them ever needed one rectangle. Not padding either, which would put a
  constant block in the data that every generator would harvest as grain.

  **What the class owns is what more than one consumer repeats**: reading,
  `primary()`, the shared high-pass, the pooled ordering, `pixels()` and
  `smallest()`. What it deliberately does not own is any choice one generator
  makes for itself — inpaint's coarse-level block average stayed in inpaint, and
  the one time it did not every inpaint mask changed, because `cv::resize` is
  not that average and inpaint's own comment says so.

  **The common-mean shift survives with a different justification, and the old
  one was the clue.** It existed to stop a join carrying a tone step; needing it
  at all was the signal that the single-Mat framing was wrong. There are no
  joins now, but `prepare_texture_sample` only removes DC when the high-pass is
  on — so at `--mask-highpass 0` markov's pooled rank would sort one whole scan
  above another, making its bins mean "which scan" rather than "which grain
  level", and inpaint's neighbourhoods from different scans could never match.

  **What it is worth, and it is not what was predicted.** Four Kodak-4X scans at
  256², patchfit over five seeds against the film's own crop, dft by spectral
  fit:

  | | markov | dft | inpaint |
  |---|---|---|---|
  | crop to narrowest (48% kept) | **34.31** | **0.1152** | 47.79 |
  | banded (94%) | 35.76 | 0.1238 | 43.08 |
  | whole scans (100%) | 36.24 | 0.1253 | **41.02** |

  `inpaint` gains 14%, monotonically, which is its database being the binding
  constraint — though two things changed for it: more data, and a rank pooled
  across scans rather than taken over a composite, which also decides what its
  coarse levels average. `markov` and `dft` lose 6% and 9%, also monotonically — and that
  ordering is the finding. The joins were the suspect: banding added one per
  band, and removing *every* join, including the scan-to-scan ones that predate
  banding, should have recovered them. It does not. What tracks the loss is how
  much non-centre content is admitted, so the cause is the frame edges a centred
  crop used to exclude, not any seam. That is a fact about the scans rather than
  about the code, and it means the capacity gain genuinely costs those two
  generators something.

  **Two caveats on that table.** Every reference available is itself a centre
  crop, so the metric cannot be neutral: a mask built from a whole film is
  legitimately less like one centred crop of one scan. And duplication is
  separately expensive — seven bands drawn from the scans' centres, which
  repeats content, measured 42.5 on markov against crop-to-narrowest's 34.0 and
  0.1268 on dft against 0.0929. That is why banding rounded rather than covering
  every column, and why `ceil` was wrong.

  **dft's loss is the content and not the new method**, which had to be checked
  because two things changed at once: it now averages N periodograms (Bartlett's
  method) where it used to take one DFT of a composite. Three samples through
  one binary — the four centre crops vstacked into one image, the same four
  crops as a set, and the four whole scans as a set — read **0.0836, 0.0834 and
  0.1253**. The method is worth 0.2%; everything else is what the scans contain.
  `dft.smooth_cells` pinned at 1 reproduces all three to the digit, since the
  area rule floors at one mask cell here. Averaging N periodograms is still more
  variance reduction than that parameter was fitted for, which is recorded
  rather than re-fitted because it demonstrably costs nothing.

  markov's held-out report splits **each scan** independently, there being no
  single column to split at across scans of different widths: on one scan it
  reads 0.991 nats learned with 2.9% noise, on two 0.961 and 2.8%.

  `direct` is exempt and the generator table says so with a `combines` column.
  Its tile *is* the sample, so a tile spanning two scans would cross a join
  between unrelated frames — the one defect that generator works hardest to
  avoid. It takes `Set::primary()`, the largest single scan, chosen by area with
  ties broken by sorted path so a cache hit reproduces, and the run says out
  loud that it is using one of several.

  That is now the fallback. A film name given to `direct` resolves first to
  `crops/<film>.png`, a window chosen to hold grain and nothing else, since the
  largest scan's middle is wherever the photograph put its subject. `dft` does
  the same, for its own reason (see the dft section): the table's
  `prefers_crop` column, separate from `combines` because dft without a crop
  still pools the scans. The choice is `samples::resolve(spec, crop_first)`,
  and the build and the mask cache's key both call it with the generator
  table's answer, so the key hashes the file the mask is made from — keyed on
  the scans instead, an edited crop is served the old crop's mask. A typed path
  is used as given, crop or no crop.

  `samples::reference_for` is the other half: what a synthesis is *held to* and
  scored against, which is the film's crop for every generator, including the
  two that learn from the scans. markov and inpaint hold their band power to it
  and every calibration scores against it, so the crop is in the mask cache's
  key for those two as well.
- `maskutil.cpp` / `maskutil.h` — the pieces more than one generator needs and
  none of them owns: `prepare_texture_sample` (the high-pass dft, markov and
  inpaint apply), `local_quantile` (the neighbourhood rank `direct` applies
  instead), `highpass_toroidal`, `periodic_component` (Moisan, no longer called),
  `rank_order`, and `threshold_mask` with `threshold_byte` — the flat-histogram
  ranking every generator ends with, and the one place the brightest pixel is
  made to light first (see Orientation). Also `band_power`, `band_reference` and
  `impose_band_power`, which hold markov's and inpaint's sweeps to the sample's
  power at every scale. Depends on no generator, which is what keeps them
  independent of each other.
- `defaults.cpp` / `defaults.h` — `params::Params`, the lazy parameter registry
  behind `--explain` and `--set`. Knows nothing about dithering: each generator
  registers its own rules, so a rule sits beside the code it configures.
- `srgb.h` — the `vision::` namespace: inline sRGB curve primitives
  (`srgb_to_linear`, `linear_to_srgb`, `decode_pixel`, `encode_pixel`,
  `level_to_pixel`) plus declarations of the Mat-level helpers.
- `srgb.cpp` — Mat-level implementations: `to_gray`, `decode_gray`,
  `encode_levels`, `average_gray`, and the 3-channel converters used by `srgbtest`.
- `app.h` / `options.h` — the process-boundary types: `dither_main`'s signature,
  and the read-only `Options` cache filled once the whole command line has been
  applied. Anything held in `Params` must be read from `Params`; `Options` is a
  cache, never a second writable copy.
- `maketexture.cpp` — `maketexture <image> --sample WxH+X+Y --size WxH`, which
  synthesises a texture from a patch using `synthesise_inpaint_field`, the same
  function `build_inpaint_mask` calls. The two differ only in the finishing step:
  a mask maps the synthesised ordering onto a flat 0..254 scale, a texture maps
  it onto the sample's own sorted values. `--paste` writes the input back with
  the region replaced, which is the check every statistic here cannot make — if
  the synthesis works, the region cannot be found. On Kodak-4X-1 at
  `--highpass 32` it cannot; on X570-TriX80-274a it can, faintly, even at one
  sweep, which is the anisotropy finding seen directly rather than inferred.
- `srgbtest.cpp`, `temporaldither.cpp` — small independent test/demo executables.
- `unittest.cpp` — the only test that links `dither_lib` and calls the functions
  rather than running the binary and reading back a PNG. See Verification.

## Architecture

All dithering operates in **linear light**. `main()`'s pipeline is:

1. `vision::decode_gray(image, useSrgb)` — coerce any input to single-channel
   *at its own bit depth* and decode to `CV_32F` linear `[0, 1]` (folded into
   step 2 by `decode_display`). One path serves 8- and 16-bit files: the depth
   decides only how many distinct codes there are, and so how big the lookup
   table is.
2. `vision::decode_display(image, ladder, useSrgb)` — decode, then place each value
   on the `LevelSet` ladder built by `make_levels(levels, useSrgb, srgb_spaced)`,
   giving display units in `[0, levels-1]`.
3. `ditherPass(target, dither_fn, spatial_fn, spatial, levels, frames, debug_row, debug_col)`
   — returns a `std::vector<cv::Mat>` of `frames` images, each `CV_8U` raw level
   indices in `[0, levels-1]`.
4. `vision::encode_levels(f, ladder)` per frame — LUT-encode level indices to the ladder's pixel values, then `vision::resize_gray(f, size, useSrgb)` brings each frame to the output size in linear light. Unset, that size is the input's and the call is `downsample_gray`'s block average. `--out-size <side>` fits the result in a square instead, and **resamples once**: with `--supersample` the dither grid is the only place the sub-pixel information exists, so the target is reached from there rather than via the input's size, which would quantise to 8 bits in between. Shrinking a 4096 grid to 512 measures 21.1/1000 linear against the truth in one step and 36.4 through 1024 — and the two-step path comes back with *more* distinct tones, 206 against 64, which is interpolation inventing values rather than detail surviving. At exactly input × supersample the target *is* the dither grid, so `resize_gray` short-circuits and the raw dither comes out whole — a 256² input at `--supersample 8 --out-size 2048` writes 2 tones, not 256. That is supersampling without reducing, which is the main thing the option is for. Enlarging further is allowed and costs tone: the resolution here is how many dithered pixels each output pixel averages, so a bigger target averages fewer.
5. `vision::average_gray(frames, useSrgb)` — linear-light mean, then re-encode. With
   `--save-frames` the individual frames are also written as `<stem>-N.png`.

**Output** is `CV_8U` (single-channel) throughout — level indices travel in a
`CV_8U` mat, which is why `--levels` caps at 256, and `LevelSet::px` is
`uint8_t`. **Input** is 8- or 16-bit, and the difference matters: see
"Input depth" below. The sRGB curve is applied exactly at the encode/decode
boundaries and never inside the dither loop.

## Input depth

Input may be 8- or 16-bit; output is always 8-bit. The point of accepting 16 bits
is not tidiness — it is that **an 8-bit target cannot express anything below one
output level, so error diffusion and the multi-frame budget have nothing to
distribute.** Both mechanisms exist to render a value that no single pixel can
show, and against an 8-bit input in an 8-bit ladder there is no such value.

Measured on a gradient spanning four 8-bit codes, rendered at `--levels 256`
(8-bit frames) with diffusion on, scored as RMS of the column means against what
the picture really is, in units of 1e-6 linear:

| frames | 16-bit in | 8-bit in | gain |
|---|---|---|---|
| 1 | 61.9 | 793.0 | 12.8x |
| 4 | 25.0 | 793.0 | 31.7x |
| 16 | 12.7 | 793.0 | 62.7x |

The 8-bit column does not move. More exposures buy exactly nothing, because the
target was quantised before the budget ever saw it — which is the whole argument
for this feature, and why `test/unittest`'s budget matrix now carries 16-bit
rows. `--spatial 0` shows the same thing from the other side: without diffusion
a 16-bit input reads 793.0 too, since each pixel rounds independently and the
sub-level information is discarded rather than spread.

Two mechanical notes. `cv::imread` needs `IMREAD_ANYDEPTH` or it silently
returns 8 bits; verified to give single-channel native depth for 16-bit gray
*and* 16-bit colour. And `to_gray` keeps the depth rather than normalising it:
the function it replaced, `to_gray8`, min-max normalised anything that was not
8-bit, so the scale came from the image's own extremes and **the same scene
decoded to different tone depending on its brightest pixel**. That was a tone
bug, not merely a loss of precision, and every density guarantee in this file
was void for such an input.

Float inputs are taken as `[0,1]` and widened to 16-bit — by a constant, never
by content.

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

### Units: display levels vs linear light

A threshold rule has to work in **display levels** — `frac(v)` only means anything
on a scale whose rungs sit at the integers. What `average_gray` and
`downsample_gray` add up is **linear light**. Under a uniform ladder the two are
proportional and the distinction is invisible; under `--level-spacing srgb` with
more than two levels it is not, because the rungs are evenly spaced in *pixel*
value and so each step spans a different amount of light.

`Space` is the conversion, and it decides which quantity the ledgers conserve.
Diffusing the residual in display units is exact only while the levels a pixel
gets pushed to stay inside the rung interval its target sits in. Beyond that the
linear value of a level is a *convex* function of its index, so spreading the
levels around the target raises the mean however exactly the display-unit ledger
balances — a positive bias, peaking at the intermediate rungs, that measured
+49/1000 linear at `--levels 3 --spatial 1` and +23 at `--levels 4`. Both
ledgers therefore run in linear light, and `Space::of` returns the identity for
a uniform ladder so that output stays bit-identical rather than merely equal.

Two things about `Space` are not optional:

- **Both ledgers must agree.** Converting only the spatial one and leaving the
  temporal budget in display levels is worse than the original bug, not better:
  the two pull against each other and `--levels 3 --frames 8 --spatial 1` reads
  +101/1000 against the bug's +73.
- **`to_display` snaps to a rung within `kSnap`.** The budget pays itself down to
  a whole number of rungs and asks for that number back; in display units that
  was integer arithmetic and returned exact, but summing rung values and dividing
  does not. Any residue at all then moves the threshold decision: against a
  mask cell of `0` it buys a whole extra level, and the same residue shifts
  `lround` for `round` and the comparison point for `random`. The mask case used
  to be the sharpest illustration and was quoted here as such; it is now blunted
  slightly by the half-step centring below, which is a reason to restate the
  argument rather than to drop `kSnap` — the budget still pays itself down to a
  whole number of rungs and still has to be able to ask for that number back.

The CLI exposes these as two orthogonal axes:

| `--threshold` | DitherFn |
|--------|----------|
| `round` (default) | `RoundDither` (`lround(v)`) |
| `random` | `RandomDither` (stochastic threshold) |
| `mask` | `MaskDither` (threshold against a mask; `--mask-method` picks which) |

`--threshold blue` is the old spelling of `mask`, from when void-and-cluster was
the only generator. `parseOptions` rewrites it to `mask` at the point of parsing,
so only one spelling ever reaches the rest of the program — including the
`Note:` logic and `main()`'s dispatch, which is why neither tests for `blue`.
It is deliberately silent: `test/coltest` and `test/base.txt`
all predate the rename, and a deprecation note on stderr would change what they
capture. `test/impliedtest` holds both halves of that — the two spellings
produce the same bytes, and nothing is said about it.

`test/base.txt` and `test/basefull.txt` are saved `coltest` transcripts kept for
reading, not baselines anything compares against — no tool reads them, and
`test/run` does not produce them. `base.txt` is `test/coltest` and
`basefull.txt` is `test/coltest --full`, which sweeps threshold × frames ×
levels × spatial × `--linear` instead of a hand-picked list.

Refresh them whenever the dither output changes, and do it in two steps, because
a single regeneration cannot say whether a moved number came from the code or
from the test. First check out the `coltest` that produced the current file and
run it against the new binary: every difference is then the code's. Then restore
the current `coltest` and run again for the formatting and any new rows. The
last such refresh moved three rows, all improvements — two `--levels 4` mask
rows the linear-light ledger fixed, and `mask=32x32` from 20 failures to 19 from
the `frac * 254` fix — and nothing regressed.

Their header records the paths of the machine that generated them, so it moves
with whoever refreshes them.

`--spatial <frac>` is the fraction of the residual handed to `spatial_fn`.
`parseOptions` resolves its default after the whole command line is parsed: `0`
if `--threshold` was named, `1` otherwise, and an explicit `--spatial` always
wins — so the two flags are order-independent. `--mask-method` is resolved the
same way and for the same reason: `blue` if no `--mask-sample` was given, `dft`
if one was.

`main()` picks `spatial_floydsteinberg` when the resolved value is above 0 and
`spatial_none` at exactly 0 — the two are equivalent there, but the no-op skips
the pointless deposit. The axes stay independent: every threshold works at any
diffusion strength.

## Multi-frame budget

Temporal accumulation is expressed as a **budget**, not a carried residual. Each
pixel gets `goal = target * frames`, and for `frames > 1` that goal is itself
dithered (by the same `dither_fn`, `spatial_fn` and `--spatial`) to an integer in
`[0, frames*(levels-1)]`. Every frame then aims at

```
ideal = (goal - displayed) / (frames - f) + spatial_err
```

where `displayed` is the running total already shown. Because the shortfall
stays in `goal - displayed`, the total converges *exactly* to the goal on the
final frame — no separate accumulator is needed, and stochastic thresholds need
no special-casing.

The goal pass is the one place display levels are still the right scale, and that
is deliberate rather than left over. Snapping `target * frames` to an integer
there is snapping it to the nearest *achievable* total: a pixel that spends every
frame on one of the two rungs bracketing its target produces totals spaced
exactly one level-step apart. The integer is then converted back through the
ladder — `goal = frames * to_conserved(goal / frames)` — which puts the budget in
the same units as everything else without giving up the exact close.

For `frames == 1` the goal pass is skipped, `goal = target * 1` stays a float, and
the formula collapses to the classical `ideal = target + spatial_err`.

`--spatial` scales how much of the accumulated error the *threshold rule sees*,
not how much is diffused. `quantize_scan` computes two values per pixel:
`decide = target + spatial * accumulated` goes to the threshold rule, while
`account = target + accumulated` is what the residual is measured against, and
that residual is always diffused in full.

It used to scale the deposit instead, which discarded a fraction of every
residual — and with a mask threshold those residuals are not zero-mean, because
the mask is a fixed function of position and so correlates with the error field
feeding back into it. At `--spatial 0.5` that lifted the shadows by 27/1000 in
linear light and crushed the highlights by 19/1000; a random threshold showed
none of it, which is what identified the correlation as the cause. Keeping the
ledger exact makes the output average to the target for any `spatial`, whatever
the residuals correlate with. `--spatial 0` and `1` are unaffected either way and
remain byte-identical: at 0 nothing is deposited at all, and at 1 the two values
coincide.

`--spatial` applies to the goal pass as well as the frames, and it has to. The
goal decides each pixel's total over all frames, and so the average image; the
frames only divide that total between them. The goal pass used to run at full
strength whatever `--spatial` said, so at 2 frames `--spatial 0.01` produced the
same bytes as `--spatial 1`, frames included — only `0` differed, because it
selects `spatial_none` rather than a scale. `unittest`'s "--spatial scales the
diffusion of a multi-frame run's totals" holds it: the mean departure of a total
from `round(target * frames)` reads 0 / 0.129 / 0.229 / 0.246 / 0.249 at
`--spatial` 0 / 0.01 / 0.1 / 0.5 / 1, where it was flat at 0.249. `0` and `1` are
byte-identical to before.

With `--spatial 0` the goal is just `round(target * frames)` per pixel, and
anything below `1/(2*frames)` of a level shows black: the boundary lands at
column 49 of the ramp at 16 frames and column 21 at 64. That is `round`'s hard
threshold at budget granularity, and it is why `--spatial 0` multi-frame runs
clip the shadows.

## Masks

`--mask-method` selects the generator: `blue`, `dft`, `markov`, `inpaint` or
`direct`. All five return a histogram-flat `CV_8U` mask spanning 0..254, which is
what `MaskDither` requires and what `test/maskstats` checks; past that they share
nothing but the indexing below. Four of them are also toroidal by construction;
`direct` is the exception and says so in its own section.

`MaskDither` thresholds `frac(v) * 254` against
`mask[(row + sy) % rows][(col + sx) % cols]`. 254 is the scale `threshold_byte`
ranks onto — a cell holding `t` is the rank at quantile `t/254` — so firing on
`frac > t/254` fires with probability `frac` and the level averages to the
target. Against 255 it did not, and every mask-dithered image came out half a
mask step light-side: +1.06/1000 linear at the defaults, +1.87 at `--levels 3
--level-spacing linear`. Error diffusion absorbs it, so only `--spatial 0` shows
it. What remains is the mask's own resolution, ±1/508 of a level and not
removable by any offset. The modulo indexing handles both cases
uniformly:
- `--mask-size` unset: derived by `mask_side()` in `mask.cpp` (see
  `doc/DEFAULTS.md`) from the dither grid, `--mask-highpass`, `--mask-scale` and two
  values each generator declares for itself — `<method>.max_size`, the tile it
  can afford, and `<method>.max_repeats`, how often its tile may repeat before
  the repetition shows. **The result is square and a power of four: 256, 1024 or
  4096.** It used to be derived per axis, which made it a different size for
  almost every picture — and since a mask is cached under its own parameters,
  a size that tracks the image means a rebuild for each new one and a cache that
  never hits. Five images of different shapes now share two masks where they
  would have had five.

  The ladder is also a ceiling, which it needed to be. The anti-repetition floor
  is `grid / max_repeats` and grid is the image times `--supersample`, so a
  3877×2869 scan at `--supersample 10` asked for **9693×7173** — seventy
  megapixels, from a generator whose `max_size` is 256, because floors override
  the cap. That is not a large mask, it is one that never finishes. Repetition
  past what a generator would like is a visible defect; a build that does not
  terminate is not an alternative to it.

  `direct`'s sample limit still beats the ladder and is deliberately not snapped
  back onto it: rounding a sample that can supply 938 down to 256 would discard
  most of a usable tile, and it buys nothing, because that limit tracks the
  *sample* rather than the image. One sample gives one size however many
  pictures it is used on, which is what the cache needs.
- `--mask-size WxH`: taken as given, including above the cap.

How much repetition shows is a property of the generator and nothing else.
A markov tile repeated 16 times reads as regular banding in flat areas and 4 does
not, so `markov.max_repeats` is 4. Blue noise is locally featureless, so a repeat
of it is still featureless — 16 repeats is visually indistinguishable from 4, and
applying markov's limit to it would have cost 30s a run instead of 1s for no
visible gain. `blue.max_repeats` is 64 and never binds.

A mask *larger* than the dither grid is a trap: only a window of it is ever
indexed, and a window of a clustered mask is not histogram-flat even when the
whole mask is. Blue noise survives this (0.6% window deviation), a grain mask
does not (up to 6%), which is why the default size tracks the grid.

`MaskDither::new_frame()` draws a fresh toroidal offset per frame (`sx` before `sy`)
from the global `rng`, which `main()` seeds with `--seed`. The goal pass runs before
the first `new_frame()`, so it always uses offset `(0, 0)`.

### Orientation

**A mask built from a sample lights the sample's brightest grain first**, so the
dither shows the grain as it is. Until this was fixed every sample-based mask did
the opposite and dithered as the sample's negative: a scan's dark clumps came
out as light ones. A `direct` tile correlated +0.95 with its sample's
brightness; it now reads −0.89 at the defaults, and −1 for the plain tile.

**It is one convention, stated in one function.** Every generator that takes
a sample builds a field in the sample's own sense — bright where the grain is
bright — and every generator ends in `maskutil::threshold_mask`, which takes
the field's ascending order. That function now says which end lights first:
the highest value gets threshold 0, because `MaskDither` lights the lowest
cells first. Nothing else changed. No generator knows about it, the builders
return usable threshold masks whoever calls them, and calibration needed no
change, since its reference (`calib::reference_mask`) ends in the same function
as the probes it is compared with. Blue noise's order is the order its dots are
placed, which is the order they should light, so it hands that over reversed —
the one line that is not a field. dft cannot show either orientation: random
phase makes its field symmetric.

The first fix turned the finished mask round at the generator table's dispatch
instead, and the scoring tools turned masks back to compensate. That was a
correction applied on one path to an error made on every path: a builder called
directly still returned the negative, and a tool comparing a mask with itself
read them differently. Measured against that version, moving the convention
here is byte-identical for every generator at even mask areas (where
`threshold_byte`'s rounding cannot meet an exact half) and for the dft and
inpaint calibrations. markov's calibration moves its temperature in the fourth
place (0.302669 → 0.302922), because its level-set measure cuts the lowest `k`
cells out of a tied value with `partial_sort`, and a reflected mask is cut
elsewhere in the tie.

**Why nothing caught it.** Every scoring tool built its reference by ranking the
sample the same way the generators did, so mask and reference were inverted
together and every comparison came out right. The tools now build the reference
as the mask the sample would be — brightest lowest — and compare masks as they
are: `levelfit` ranks the sample descending (and takes `--reference-is-mask` for
a reference that already is one, instead of the old advice to pass
`--highpass 0`), and `patchfit` and `grainfit` negate the sample's field before
ranking it. `grainfit` measures the cells a mask lights at each density; its
densities are symmetric, so its features are those of before relabelled and
every recorded figure reproduces to the digit. `patchfit` agrees to within 0.3%
and `levelfit` to within 3%, both cutting tied values at fixed densities.
`test/refmask` and `test/inpaintmask` write masks the same way.

Two checks hold it. `unittest` asserts the convention itself — a field's
brightest pixel gets 0 — and that `build_direct_mask`'s plain tile, taken
straight from the builder, reads −1 against its sample. `test/masktest`'s
`oriented` property renders: a flat sample whose dark values are horizontally
smeared noise and whose bright values vertically smeared (darkest tenth +0.49,
brightest −0.50, in log10 of row power over column power), then flat 10% and
90% greys dithered through it. White at 10% must be vertical and black at 90%
horizontal: direct −0.53 / +0.52, markov −0.23 / +0.21, inpaint −0.65 / +0.73.
With the old mapping every one reads as its mirror image.

**What changes in what was recorded here.** Every comparison between a mask and
its sample is unchanged. What changes is reading a level set as a *tone of the
dither*: the level set at `g` in the older figures here is the sample's darkest
`g`, which a dither shows as the dark minority at tone `1 − g`. So "the film
clumps 6× harder in the shadows" was a statement about the scan's darkest
grain, which a dither shows in its highlights, as dark clumps on light.

**A sample's regional tone is not something any generator can learn**, with
or without the high-pass. A sample that changes texture from one region to
another — coarse grain in its dark third, fine elsewhere — cannot ask for
coarse grain in the dither's shadows. At 32 the high-pass removes each region's
tone before anything is ranked, so both ends of the value distribution come
from whichever region has the most contrast. At 0, markov and inpaint's
default, the tone is kept and they reproduce it: the strongest relation between
neighbouring values is that they share a tone, so the field they build is a
layout of near-constant regions, and ranked flat each region switches on at
about one tone. A flat grey then shows which regions are below it — blotches,
measured on `repro.sh`'s composite — not a pattern of grain at that density.

The reason is what a mask value is: the tone at which that pixel turns on. Grain
that depends on tone is a statement about how the lit pattern grows as the tone
rises, at every place in the mask, each pattern inside the next. A sample shows
each place at one tone only, so the growth is never in the data, and generators
that model values rather than densities have no way to infer it. A texture that
varies with value inside a flat patch — film's dark grain clumping harder than
its light grain, or the streak sample `masktest` uses — is reproduced. One that
varies with the tone of the region it was found in would need a generator that
builds the mask a density at a time, taking each density's pattern from the
sample regions at that tone.

### blue (default)

`build_blue_noise_mask` in `bluenoise.cpp` runs progressive void-and-cluster:
repeatedly place a pixel at the lowest-energy empty location and update a
toroidal Gaussian energy map with a min-heap (`O(N·K·log N)` where `K` is the
sparse kernel size). Takes no sample; `--blue-sigma` is its only option. The
kernel cutoff `blue.eps` and half-width `blue.radius` are internal parameters
(`--explain` / `--set`), and the half-width is *derived*: `makeSparseGaussian`
scans a box of `ceil(radius·sigma)` and then drops every tap under `eps`, so the
only correct half-width is where the Gaussian reaches `eps`, namely
`sqrt(-2 ln eps)` — 4.29 sigma at the default. The old fixed 6.0 was documented
as inert and this makes that structural: the kernel is byte-identical, and the
parameter can no longer be set somewhere that silently truncates it.

The other four imitate a `--mask-sample` instead. Naming `blue` with one is an
error — before the method was nameable the sample's presence was itself the
switch, so that case could not be expressed. Naming one of the four without a
sample takes `kDefaultFilm`, Kodak-4X, as `Origin::Implied` with a `Note:`, and
is an error only when the library lacks it. Kodak-4X because the derived
defaults were fitted on it. The film is set after `reconcile`, which may be what
chose the method, and before anything reads `mask.sample`: `Params::set` does
not re-resolve dependents, and `mask.method`'s own rule must never see it, or a
bare `--threshold mask` would turn from blue into dft. `test/samplecheck` holds
both, and that the implied film is byte-identical to naming it.

### dft

`build_spectral_mask` alternates two projections on a random field — impose the sample's amplitude
spectrum, then re-rank to a flat histogram — over `dft.iters` (20) rounds. It
converges in distribution but never to an exact fixed point: 30 rounds still
differ from 20 on 1.6% of pixels by one threshold level, so there is nothing for
a convergence test to detect. 20 is where quality plateaus (spectral fit 0.0367 at
5 rounds, 0.0073 at 10, 0.0030 at 15, 0.0023 at 20 and at 30). The full 2D spectrum is used, not a radial average, so an
oriented sample keeps its direction; the spectrum is first smoothed over some of the
frequency cells a mask bin covers, because imposing a raw periodogram copies one
realisation's speckle: on 4Xcrop at 256² the spectral fit is 0.0170 raw and
0.0023 smoothed. Smoothing much harder converges on a radial average, which fits
isotropic grain slightly better and erases orientation altogether.

**How much to smooth depends on how much sample there is**, and `dft.smooth_cells`
used to be a flat 8 cells at every size. The count of *sample* bins under one
mask cell rises with the sample's area, so a window of fixed width in mask cells
already averages more of them and the estimate is quieter — smoothing it as hard
then trades bias for variance that is no longer there. The rule is
`cells / sqrt(sample_area / mask_area)`, floored at one mask cell (below that the
estimate is already sharper than the mask can represent), with `cells` quoted for
a sample the size of the mask and now 4 rather than 8.

Measured on three films with the sample area swept over 64× at a fixed 256² mask
and every mask scored against the *full* sample at a fixed `--highpass 32`, the
best value tracks `r^-0.5` across the whole range; a free fit lands at `r^-0.35`
with a coefficient within 3% of 4. The old flat 8 is the value for a sample a
quarter the mask's area, and much too much for a real scan: across five films at
34× to 326× the mask's area it was the *worst* setting tried, 0.0120 against
0.0023 on 4Xcrop and the same 4–5× on three others. Over the whole domain — five
full scans and a four-point crop ladder — the rule means 0.0263 against the flat
8's 0.0331. A flat 1 is better than 8 at real sample sizes but worse than either
at small ones (0.0630 against 0.0423 on a 128² sample), which is the bias half of
the trade showing up from the other side. Phase is randomised, so nothing
higher-order survives: a weave comes back as woven-looking noise, and capturing
more than that needs a patch-based or Markov synthesiser rather than a spectral
one. `--mask-highpass` (32 px for dft and direct, and 0 disables it — which for
markov reopens the DC blowup below) keeps a full-frame sample's subject out of
the statistics, so what is imitated is the grain and not the picture.

**markov and inpaint default to 0**, the generator table's `default_highpass`
column, and that is a choice to see what the filter was hiding rather than a
measured improvement. Removing tone before the model ever sees it also removes
everything that depends on tone, and a filter that makes masks look like grain
can make a generator's defects look like grain too. What 0 costs is recorded
and expected: from a film's whole scans both build maps of the photographs'
tones — blotches tens of pixels across, with the grain inside them — and markov
loses the field high-pass that holds its DC down. On a flat crop of one tone
the two are much as before. Every figure for markov and inpaint elsewhere in
this file was measured at 32. The scoring tools take `--method` and prepare
their reference the way that generator prepares its sample.

**32 is a measured constant, not a derived one, and the obvious derivation was
tried and does not work.** A three-segment fit of the sample's radial spectrum
in log-log cleanly separates subject (slope −2.4 to −6.3), grain (−0.14 to
−1.07) and rolloff (−3.7 to −5.5), and the grain→rolloff knee is stable at
4.4–5.2 px across six films. The subject→grain knee is not: it runs 39 px to
443 px. The measured optimum does not follow it — 4Xcrop's knee is 443 px and
it wants 64, Kodak-2484's is 395 px and it wants 32 — nor does it track the mask
size. Two traps are worth knowing. A *two*-segment fit finds the rolloff instead
and reports the slopes backwards. And of the three metrics only `test/patchfit`
can judge this parameter at all: `test/maskfit` builds its reference by
high-passing the sample at the evaluation figure, so a mask built at that same
figure matches by construction and it returns 32 whatever the truth is, while
`test/levelfit` runs monotone to whichever end of the range is tested.

**`--mask-scale` must band-limit, not fold.** `amplitude_grid` reads the sample's
spectrum at `f * scale`, and `wrapped()` indexes it modulo — right for negative
frequencies, which is what the upper half of a DFT row is, and wrong for a
frequency genuinely off the end. At `scale 2` a mask bin at 0.4 cycles/px asked
for 0.8 and got 0.2, near the peak: the main lobe was mirrored back above
0.5/scale as a second lobe carrying 15.8% of peak power, which looked like fine
noise on grain that was asked to be coarse. At `scale 4` it folded repeatedly.
Frequencies past the sample's Nyquist are now zero, because a sample says
nothing about them; the residual 0.02% is the harmonics of the final rank, which
is a nonlinear transform. `test/run`'s `scalecheck` and `maskfit --scale` guard it.

Two things about this are worth keeping straight. The support is a *square* in
frequency, so scaled it is a square of half-width `0.5/scale`, and only radii
below that are fully defined — a radial metric that compares further out
penalises correctly-zeroed corners. And the band-limit does cost local density:
coarser grain clumps harder, so a 16-column band of the ramp reads 16/39/86 per
1000 at scale 1/2/4 with `--spatial 0`. That is the grain, not the fix — an
independently built coarse mask (a half-size mask upsampled and re-ranked) reads
22 — and diffusion removes it entirely, 86 → 1.6 at `--spatial 1`.

The remap beats the obvious alternative of building a smaller mask and
interpolating it up. Over the band the sample defines, `maskfit --scale 2` reads
0.011 against the interpolated mask's 0.112, and the remap keeps the histogram
flat by construction where interpolation destroys it (47% deviation) and needs a
re-rank to repair it.

It is far cheaper than void-and-cluster (0.2s
against 10s for 256²), so its default size cap is 1024 rather than 256.

**A film name gives dft the film's crop, not its scans**, as it does `direct`,
and for a reason dft's own construction supplies. It imposes the sample's whole
2D amplitude spectrum, so a straight edge anywhere in a scan — a door frame, a
horizon, a frame border — is a line of power through the origin, and it comes
back in the mask as streaks along that direction with no picture to explain
them. The 32 px high-pass does not remove an edge, only the smooth tone either
side of it. A crop is a window chosen by `test/findcrops` to hold grain and
nothing else, and the generator table's `prefers_crop` column says which
generators resolve a name that way; markov and inpaint still pool every scan.

Measured on the six shipped films at 1024², by the angular profile of the
mask's power in 5° bins over 0.03–0.45 cycles/px against that of a held-out
window of the same scan (`test/findcrops`' next-best non-overlapping window),
RMS log10:

| | 4X | 2484 | TriX | OldTriX | PanX | Delta |
|---|---|---|---|---|---|---|
| scans | 0.095 | **0.006** | 0.010 | 0.049 | 0.021 | 0.029 |
| crop | **0.012** | 0.014 | **0.009** | **0.024** | **0.019** | **0.009** |

Five of six, Kodak-4X by 8x — its five scans mix portrait and landscape frames,
so pooling averages the grain's real orientation away as well as adding the
pictures'. On Delta 3200 the scan-based mask shows vertical bands and broad
blotches by eye and the crop-based one does not. `test/grainfit` at block 48
against the same held-out windows is mixed — the crop wins on 2484 (2.89 →
2.42) and OldTriX (4.09 → 3.99), loses slightly on TriX and PanatomicX and
clearly on 4X (3.30 → 5.21) and Delta (2.43 → 4.11) — and is biased towards
the scans, since each held-out window is part of what the
scan-based mask was built from and none of the crop's.

### markov

`build_markov_mask` is the answer to what dft gives up. It fits a pairwise
Markov random field: for each of `--markov-pairs` (64) neighbour pairs inside
`--markov-radius` (±5) — drawn from a shortlist of offsets sized by
`markov.shortlist`, which is derived from the pair count so that the greedy pick
always has about 12 candidates per pair it must choose — a table of
`P(centre | the two neighbours)` quantised to
`--markov-bins` (16) conditioning bins and 2× that many centre bins, counted off
the sample and smoothed toward the marginal. The field is then Gibbs-sampled
from the pooled verdict of those tables; after each sweep it is held to the
film's power in every band (spectrum matching, `markov.bands`, see "Holding the sweeps to the
sample's band power") and re-ranked flat. Costs seconds rather than tenths, so
its size cap is 256 like void-and-cluster.

Both the sample and the field are quantised by *rank*, not by value, so the
model's marginal is the flat histogram a threshold mask is required to have —
the model constraint and the mask constraint are the same object.

Five things were established by measurement — `test/maskfit` and `test/levelfit` reproduce
every figure below, so re-check rather than trusting the quoted numbers if
anything here changes:

- **Pairs are selected in symmetry orbits, and it is the smallest of the three
  anisotropy levers.** Greedy MI alone returns ~90% horizontal pairs on a film
  scan (whose horizontal MI genuinely runs 2× the vertical) — the selection
  printout shows this plainly. Taking orbits instead moves the finished mask from
  +5.7 dB to +4.0 dB, all else held. Flat pooling then takes it to +2.6, and
  temperature 0.1 → 0.4 takes it to −0.0 against the sample's −0.4. Do not read
  any one of those three in isolation; all three were confounded on the first
  pass, and temperature dominates.
- **Pooling by MI (`--mask-attend`) is a small loss, not a large one.** Weighting
  each expert by what it knows arguably double-counts strength (a table that
  knows little already votes flatly) and lands the double count on one axis. But
  compare it at *matched* temperature or it looks far worse than it is: at each
  scheme's own optimum, MI weighting reads level-set fit 0.238 / +0.2 dB against
  flat's 0.190 / −0.0 dB, and slightly better on the spectrum. Flat 1/K is the
  default on that margin; the flag exists because the margin is thin.
- **`--markov-temp` is the scalar that decides the outcome**, and 0.4 is the
  measured optimum **for 4Xcrop**. It is not a constant across samples: the
  measured optimum runs 0.34 to 0.60 over four scans, and the rule has no sample
  term, so it answers 0.40 for all of them. A command-line `--markov-temp` outranks
  the measurement, which the guard has to ask `Params::user_set` for: a
  command-line value is `Origin::User`, and testing `Params::measured` — which is
  `Origin::Measured`, what the *config* loads as — never fired, so
  `--markov-temp 0.99 --mask-calibrate` produced a mask byte-identical to
  `--mask-calibrate` alone. `--mask-calibrate` measures it per
  sample and is much better wherever the sample is unlike 4Xcrop (level-set fit
  0.535 → 0.387 and 0.304 → 0.063 on two Kodak samples). See doc/DEFAULTS.md,
  including the criterion that did *not* work — a fixed share of the sample's
  neighbour mutual information, which is itself a property of the texture. Most of what looks like a difference between pooling schemes
  is temperature: MI weighting at 0.1 reads +5.3 dB, flat at 0.1 reads +2.9 dB,
  and both are simply over-coupled there.
- **The field must be high-passed each sweep.** The model is silent past its
  radius; left alone, a locally-agreeing MRF random-walks at long wavelength and
  the mask's spectrum peaks at DC (power 377 against the sample's 0.015). Since
  `--mask-highpass` is already the claim that the sample has no structure coarser
  than that, the field is held to the same claim. That was measured at 32; at
  markov's default of 0 this filter is off, and `markov.bands` is what holds the
  long wavelengths down, to the film's own power there rather than to zero.
- **`--mask-scale` is not symmetric with dft's.** Here it resamples the sample,
  so above 1 the interpolator's own smoothness enters the statistics and the
  model learns it as though it were grain. dft's scale is a pure frequency
  remap, which has no such artefact but had a worse one of its own until the
  band-limit went in — see the dft section. It also means the field high-pass has to be
  `highpass * scale`, not `highpass`: the resample moves the sample's cutoff
  too, and cutting at the unscaled figure silently removes the coarse band a
  user asking for coarser grain wanted. With that, the mask's spectral peak
  tracks `scale` cleanly (6.7, 12.1, 25.9 px at 0.5, 1, 2).

Two metrics below, both RMS of log10 ratio, both against the film's own ranked
128² crop as the reference mask — *not* comparable to the bare "film fit"
figures in the dft section above, which include the sub-high-pass bins these
exclude:

- **level-set fit** (`test/levelfit`) — maskstats' `low` across g = 0.1…0.9, i.e. how
  much the pattern shown at each density clumps. This is the higher-order view.
- **spectral fit** (`test/maskfit`) — radial power above 1/highpass cycles/px.

The film runs `low` = 12.5, 7.7, 4.2, 2.5, 2.1 from its darkest tenth to its
brightest: its dark grain clumps 6× harder than its light grain, which a dither
shows in its highlights (see Orientation). dft reproduces none of that asymmetry (2.5, 2.7,
2.8, 2.8, 2.4; slope 1.04) because phase randomisation destroys it. markov
reaches slope 2.2 and a level-set fit of 0.19 against dft's 0.385. What it costs
is the spectrum: 0.74 against dft's 0.04 — and note the film's *own* 128² crop
scores 0.30 there, because ranking a crop flat is itself a nonlinear transform,
so dft is more spectrally faithful to the film than the film is.

**`--mask-calibrate` chooses the weighting as well as the temperature**, and the
two had to be chosen together or neither is chosen at all. The weighting moves
the useful temperature range by a factor of three — `flat` measures its optimum
between 0.28 and 0.43 across six films, `ml` between 0.06 and 0.16 — so every
comparison that fixed one and searched the other judged `ml` at two to four
times its own temperature. That is what the entry above records as "flat
measures best": true at flat's temperature, and not a comparison of the schemes.

Each scheme therefore gets its own bracket, and a scheme whose best lands on one
of its own ends is *dropped from the comparison* rather than entered into it: its
score is a lower bound on a curve that never turned over, and letting it compete
on that would let it win for not having finished. That guard is doing real work
— on Panatomic-X, `ml` runs to its floor and is dropped, which is the one film
where `ml` is 41% *worse* than flat.

**`--markov-radius` is searched too**, and it is the odd one of the three: the
weighting and the temperature are properties of the *draw*, so one fitted model
serves every value of them, while the radius decides which offsets the pairs may
be drawn from and so needs a refit per value. That is affordable only because
fits are cheap — a radius-8 fit measures within 10% of a radius-3 one. Three
rungs, 3/5/8, because that is where the evidence is and nothing between them
separated.

Measured on six full scans, against the temperature-only calibration all of this
replaces:

| | temp only | all three | picked |
|---|---|---|---|
| Kodak-4X-1 | 20.87 | **15.27** | ml, r3 |
| Kodak-4X-2 | 19.61 | **12.37** | ml, r3 |
| AutoRex | 10.31 | **9.80** | flat, r3 |
| Kodak-2484 | 10.76 | **10.69** | flat, r3 |
| Delta3200 | 18.38 | **15.68** | mi, r3 |
| Panatomic-X | 11.67 | **10.02** | ml, r8 |

Every film better, mean 13.29 → 12.30. Panatomic-X picking radius 8 is an
independent agreement with the parameter study, which found the same for that
film and no other.

**The draw count has to grow with the search, and finding that out is the
instructive part.** Each distance is an average over stochastic draws, so taking
the minimum over many candidates picks partly on noise, and the bias grows with
the number of candidates. At three draws the two-way search was sound and the
nine-way one was not: adding the radius ladder made Kodak-4X-2 22% worse and
AutoRex 13% worse, which reads exactly like radius genuinely not helping them. It
was not — at six draws both come back *better* than before. A wider search needs
a quieter measurement, and a regression under a wider search is worth suspecting
of that before it is believed.

The count is therefore set from the search actually being run, so pinning buys
back the time rather than paying for precision no longer needed: 22s for the full
search, 8s with `--markov-radius` given, 2s with both that and a weighting. Once per
sample and cached, and all three values come back — a warm run reproduces the
cold one's mask byte for byte.

`--markov-report` prints the fitting diagnostic: raw counts per conditioning cell,
and a held-out log-loss (fit on the left half of the sample, scored on the right
with a `radius` gap). It is inert — it never touches the builder's `rng`, so the
mask is byte-identical with or without it. Two things it settles, measured on
4Xcrop at 256²:

- **Data is not the binding constraint.** At the defaults the model learns 0.894
  nats against a 3.466-nat no-model baseline, and only 0.025 of that is noise —
  2.8% overfit, and that figure is an upper bound since the report fits on half
  the data. `step` auto-thins to hold ~24 counts per table *entry*, so raising
  `--markov-bins` shrinks `step` rather than starving the cells; `step` only
  reaches 1 at bins 28, and the CLI caps bins at 32 where occupancy is still
  43 counts/entry. Unweighted cell occupancy looks alarming on paper because
  neighbours are correlated and off-diagonal cells are empty by construction —
  which is why the report weights by how often a cell is actually visited.
- **The diversity penalty has to fall as pairs rise.** `markov.reuse` divides a
  candidate pair's mutual information by `1 + reuse·(uses of its two offsets)`,
  so an offset already spent is worth less next time. Because the shortlist rule
  pins `C(shortlist,2)` at `12·pairs`, uses-per-offset already grows as √pairs —
  3.2 at 64 pairs, 6.5 at 256, 12.8 at 1000 — so a fixed coefficient makes the
  *total* penalty grow and the pick starts refusing repeats it should take. The
  rule holds `reuse × uses` constant. Measured on three films at 64 and 256
  pairs by both `levelfit` and `patchfit`, all six film-metric cells agree the
  optimum falls, by about half for four times the pairs. doc/DEFAULTS.md had posited
  the opposite dependence.

- **Pooling is — but `--markov-pairs` is not the reason.** Adding pairs looks like
  it degrades the texture, and the held-out *learned* nats do fall (0.940 →
  0.877 going 128 → 256). That is dilution, not waste: flat weights are
  `1/(K·temp)` and their sum is pinned at `1/temp`, so every pair added takes
  vote away from the rest and the pooled conditional broadens toward white.
  Temperature is the compensation, and with it the extra pairs pay — but
  recalibrate it rather than nudging it by hand (see below). `--markov-radius 8` is
  genuinely worse. The film's own slope is 6.63; nothing here reaches it.
- **Past ~256 pairs, more pairs stop helping at any temperature.** Fixing the
  `markov.shortlist` cap made `--markov-pairs` above 780 actually deliver, which
  exposed the ceiling: level-set fit reads 0.190 / 0.158 / 0.172 / 0.241 at 64 /
  256 / 512 / 1000 pairs on the derived temperature. Part of that is the temp
  rule under-compensating as it extrapolates — 1000 pairs wants about 0.32, not
  the 0.341 the rule gives, and at 0.32 it recovers to 0.186 — but 0.186 is still
  worse than 256 pairs manages. The relationship is not linear in log2(pairs)
  either (the optimum falls ~0.0125/octave from 64 to 256, then ~0.03/octave from
  256 to 1000), so re-fitting the rule on the far point would spoil the near
  ones. 256 is the useful maximum; the rule is fitted where it matters and
  `--mask-calibrate` is the proper fix for the rest.
- **Fewer Gibbs sweeps is better, and finding that needed an `iters` term in
  the `markov.temp` rule.** The default was 12 on the strength of a level-set
  sweep at a *fixed* temperature, which is the wrong comparison: the temperature
  that suits the field depends on how long it has been swept — `--mask-calibrate`
  picks 0.347, 0.435 and 0.452 at 3, 12 and 40 sweeps on 4Xcrop — so every sweep
  count was being judged at 12's temperature. That confound is the one flagged
  three bullets up, and it made `markov.iters` unfittable on its own.

  With both free — a 7×8 grid over `iters` 2…20 and `temp` 0.20…0.50 on two
  films, each column read at its own best temperature — the ordering reverses:

  | iters | 2 | 3 | 4 | 5 | 8 | 12 | 20 |
  |---|---|---|---|---|---|---|---|
  | patchfit 4Xcrop | 17.5 | 20.5 | 22.7 | 24.7 | 28.1 | 30.0 | 31.4 |
  | patchfit AutoRex | 10.3 | 12.2 | 13.4 | 13.9 | 15.9 | 18.0 | 18.5 |
  | levelfit 4Xcrop | 0.18 | 0.19 | 0.20 | 0.19 | 0.20 | 0.20 | 0.20 |
  | levelfit AutoRex | 0.26 | 0.19 | 0.18 | 0.18 | 0.18 | 0.19 | 0.19 |

  patchfit falls monotonically as sweeps are removed and would take 2; levelfit
  on AutoRex was the dissent, and 3 was where both were satisfied.

  **It is 2 now**, on a wider measurement: six films rather than two, the three
  extra ones (Kodak-2484, Delta 3200, Panatomic-X) being samples no rule here was
  fitted on. At the *derived* temperature — the path a run without
  `--mask-calibrate` takes — patchfit reads

  | | 4X-1 | 4X-2 | AutoRex | 2484 | Delta | PanX |
  |---|---|---|---|---|---|---|
  | iters 3 | 31.3 | 54.1 | 26.4 | 26.8 | 21.9 | 34.8 |
  | iters 2 | 27.6 | 34.2 | 18.8 | 25.0 | 13.9 | 28.4 |

  every film, three of them by a wide margin, and a third faster. Under
  `--mask-calibrate` it is 5 of 6; the exception is 2484, whose calibration pins
  at the bracket floor of 0.25 for *both* sweep counts, so that is the one film
  where the comparison is not really being made. The AutoRex levelfit dissent
  does not survive the wider set either — 2 reads 0.303 there against 3's 0.237
  while winning the aggregate 18.8 to 26.4, and wins clumping outright on three
  of the six.

  **Two things this sweep established that are not about `iters`.** The
  calibration bracket was `[0.25, 0.60]`, and it bound: on Kodak-2484 the answer
  was 0.25 at every setting tried, which is the floor rather than an optimum.

  It was `[0.10, 0.60]` then, at the same 0.05 step (see "Calibration under the
  no-high-pass default" for what it is now and why), and **the width was the
  smaller half of the fix**. 2484 calibrates to 0.148 and its patchfit goes 25.8
  to 20.3; every other film measured is unchanged to the digit, which is what
  says the floor was costing one sample and constraining nobody.

  The larger half is that *a winner at either end is not a measurement*. The
  objective was still falling when the search ran out of room, so what came back
  was where the looking stopped — and it was then cached against the sample for
  ever. `calibrate_markov_temp` now declines, and the derived rule stands: a
  stated guess rather than a number dressed as evidence. Both ends are
  reachable and both are real — white noise pins at 0.60 because it wants no
  coupling at all, a blurred blob field pins at 0.10 because it wants as much as
  it can get.

  That guard is why the bracket could not simply be widened further. Scored
  against a level-set proxy, two of the six films run monotonically to whatever
  floor they are given, and following AutoRex down would take its patchfit from
  17.8 to 27.2. A bracket cannot be made wide enough to contain an optimum that
  does not exist; it can only be honest about not having found one.

  And that is how `--markov-weights ml` came to look like a large win and is
  not one. Swept on three films at its own best temperature it beat flat on both
  patchfit and clumping by 25–30% — but its optimum is 0.06 to 0.13, *below the
  bracket*, so every previous comparison judged it at two to four times its own
  temperature. Extended below the bracket it still wins on those three films and
  then fails on held-out ones: Panatomic-X reads 76.8 at temp 0.10 and 113.3 at
  0.06, against flat's 31.1. The recorded conclusion that flat is the right
  default survives — but it survives because ml does not generalise, not because
  ml is weak at its own temperature, and the distinction is worth keeping.

  All four film-metric surfaces agree the best temperature rises with
  `log2(iters)` — at 0.072 / 0.060 / 0.054 / 0.048 per octave — so the rule
  gained a `0.059·log2(iters/12)` term, anchored at 12 where it still returns
  0.40. It is a strict generalisation of the rule it replaces and would have been
  byte-identical had the sweep count stayed put.

  What improves is the worming, which is exactly what this parameter buys and
  spends. Against the old default on 4Xcrop, patchfit's coarse ridgeness —
  the clumps stretching into connected ridges, measured rather than eyeballed —
  goes from +10.0 to +1.7, near inpaint's −1.2, and the fine-mid envelope
  correlation from −15.2 to −8.6, better than the −11 to −15 that used to hold on
  every setting tried. `test/levelfit` cannot see either, since it asks how much
  each level set clumps and not whether the clumps are round or stringy.

  Validated on a third film that was not in the fit. patchfit / levelfit /
  maskfit against the old default: 30.1→20.4, 0.190→0.191, 0.722→0.634 on
  4Xcrop; 23.6→17.9, 0.252→0.255, 0.755→0.664 on AutoRex; and 14.3→12.4,
  0.613→0.502, 0.707→0.620 on the held-out Kodak-2484-1. Everything improves or
  holds, and builds are 1.3–1.7× faster.

### What the worming is, and how iters, temp and bins connect

The clumps stretching into connected ridges is not a quirk of the sampler. The
Gibbs pass proposes each pixel from its neighbours' pooled verdict and the sweep
ends by re-ranking the field flat, so the marginal is *conserved* exactly while
the field is free to rearrange. A locally-agreeing field under a conserved
marginal is the textbook setup for phase separation, and the equilibrium of that
is a labyrinth, not a field of blobs. Four measurements, all on 4Xcrop at 256²,
against the film's own patches:

- **It peaks at the median.** Excess level-set coherence over the film runs
  +0.050 / +0.127 / **+0.179** / +0.169 / +0.142 across quantiles 0.1 … 0.9 at 40
  sweeps. A phase-separating system at 50:50 has no choice but to form a
  labyrinth; at 10:90 the minority phase can form droplets, and there the excess
  nearly vanishes. The film's own trend is the opposite way, falling 0.219 → 0.137
  from its darkest quantile to its brightest, and dft is flat at ~0.16 — it does not worm at all.
- **There is a transition temperature, T_c ≈ 0.48.** Excess coherence at 16
  sweeps reads +0.247 / +0.153 / +0.029 / +0.001 / −0.008 at temp 0.32 / 0.40 /
  0.50 / 0.65 / 0.80. Below T_c the field orders and keeps ordering; above it,
  32 sweeps do nothing at all.
- **Below T_c it coarsens.** The characteristic scale grows +37% / +38% / +29%
  from 2 to 32 sweeps at temp 0.25 / 0.32 / 0.40, and +9% / +2% at 0.50 / 0.65.
  A growing length scale under conserved dynamics is what coarsening *is*.
- **The coarsening is also what sizes the grain**, which is the bind. The field
  starts as white noise with no scale, so at temp 0.40 it is 18% finer than the
  film at 2 sweeps, matches it at 8, and overshoots to +6% by 32. Above T_c it
  never coarsens and so is stuck 18–25% too fine however long it runs. Sweeps buy
  the grain size and the worming together.

So the three parameters are one control and two ways of spending it:

| | what it does |
|---|---|
| `markov.temp` | the control parameter. T_c ≈ 0.48, and the distance below it sets how fast the field orders |
| `markov.iters` | time spent below T_c. Nothing else in the model has a clock |
| `markov.bins` | does **not** move T_c — measured 0.479 / 0.483 / 0.472 at bins 8 / 16 / 32 — but the `markov.temp` rule moves temp with it, at +0.030 per octave, so it changes how far below T_c the default sits: about 0.11 at bins 8, about 0.05 at bins 32 |

This is why `--mask-calibrate` raises the temperature as sweeps are added — 0.347,
0.435, 0.452 at 3, 12 and 40 sweeps — rather than holding it. It is walking
towards T_c to buy back the extra ordering time, which is also why a fixed-temp
`markov.iters` sweep measures the wrong thing.

Two attempted fixes, both instructive, neither kept:

- **Seeding the field at the right scale** instead of white noise, so the sweeps
  need not grow it. It fixes the size immediately, but the worming still climbs
  +0.038 → +0.147 over 2 to 16 sweeps against white noise's +0.002 → +0.153.
  Ordering is per-sweep and does not care where the field started.
- **Re-ranking less often than every sweep**, which is the constraint that makes
  the marginal conserved and therefore forbids droplets. It works exactly as
  predicted and is useless: at 16 sweeps the worming falls from +0.153 to +0.017
  (every 2 sweeps) or +0.005 (only at the end), and everything else collapses
  with it — `Efine` +23, `Emid` −25, patchfit 32.8 → 47.6, levelfit clump 0.205
  → 0.486. The field goes to white noise, because the bin index `field * Qc`
  assumes the flat marginal the re-rank maintains.

That second result is the useful one: **the clumping and the worming are the
same conserved dynamics**, so no amount of tuning separates them. Getting one
without the other needs a different generator, not a different constant — which
is what `test/inpaintmask` is.

### direct

`build_direct_mask` does not imitate the sample — it *is* the sample. Equalise
it, take a centred tile, rank it flat — at `--direct-equalise 0`, `test/refmask` in
the program. Everything true
of the grain is true of the mask, including whatever no model has captured, so it
is the upper bound the other three sample-based generators are measured against.
It measures like one:

| | patchfit 4Xcrop | patchfit 2484 | clump | spectrum |
|---|---|---|---|---|
| direct | **9.0** | **1.8** | 0.235 | 0.271 |
| inpaint | 9.4 | 4.5 | **0.197** | 0.678 |
| dft | 15.5 | 4.7 | 0.431 | **0.002** |
| markov | 19.9 | 12.1 | 0.192 | 0.616 |

A real patch of 4Xcrop scores 2.8 on patchfit; direct's 1.8 on Kodak-2484 is
below that film's own floor, which is what "it is the sample" means.

That row, and every `patchfit` figure quoted for `direct` anywhere in this file,
was measured at `--direct-equalise 0` and is **not** re-measured below. `patchfit`
cannot judge a change of preparation — its reference *is* the subtractive
high-pass, so it scores a differently-prepared mask against a reference that
embodies the choice being tested, which is why `test/grainfit` exists. Re-running
it here would produce a worse number that means nothing. The comparison between
generators is still sound, since the other three are unaffected.

**It is the one generator that does not high-pass its sample**, and the reason is
the sentence above. A ranked tile shows, at threshold g, the pixels below the
tile's own g-quantile, so its local density is flat only where each
neighbourhood's *distribution* matches the whole tile's — in mean, in amplitude
and in shape. The other three fit a model with no term for a neighbourhood that
differs, so whatever a preparation leaves never reaches their output. Here it is
ranked straight onto the threshold scale.

So `--direct-equalise <sigma>`: each pixel becomes its **quantile within a
Gaussian neighbourhood** (`maskutil::local_quantile`), ties shuffled from the
seed — the randomised probability integral transform, against the local
distribution rather than the global one. Every neighbourhood of the result is
uniform by construction, it depends only on order and so is unchanged by any
monotone remapping of the sample, and below the neighbourhood's scale it
preserves the grain's ordering exactly. `0` restores the subtractive high-pass,
byte-identically.

**The failure it fixes was visible as lines at every tile seam**, at
`--supersample 16` on a ramp, on all but two of the twelve per-scan crops
`examples/crops` then held — including the flat fields DemiEE17-319d and Kodak-4X-2 that should
have worked. Read off the masks, the tile was *denser at one edge than the other*: PanatomicX 0.05 at the top and
0.15 at the bottom at g = 0.1, Kodak-2484 0.18 against 0.06. Tiled, a density
gradient is a step at the seam.

**What caused it was the preparation before this one**, a polynomial surface
fitted to the tile and divided out, on the premise that grain is multiplicative
— amplitude proportional to the mean, quoted as r = +0.57. On these scans it is
not. PanatomicX's grain amplitude is 4.8 at the top of its crop and 4.4 at the
bottom while the mean falls 47 → 27, so dividing by the mean *made* an amplitude
gradient of 3.1 → 5.2 that the sample never had; Kodak-2484's went 45 → 24.
Ranking turns amplitude into tail density, and that was the gradient. The
subtractive high-pass has the converse fault: it keeps whatever amplitude
variation the sample has (2484: 24 → 29), because it only ever touches the mean.

That preparation also needed its degree derived from the tile size, since a
fixed degree is a cutoff that moves with the tile, and choosing it showed the
lesson that applies again below: density metrics alone say "more is always
better" and win by destroying the grain, so a preparation can only be chosen by
density *and* fidelity together.

**Why the earlier attempt at a local rank failed, and this does not.** The
seamlab `localrank` variant was rejected for "huge areas of all zeros in the
shadows", and it was applied to the *high-passed* field. Kodak-4X-1 holds 22% of
its pixels at code 0 — clipped, and so one tie group carrying no information
about where in the tail each member lies. Subtracting anything smooth gives each
of them a different value, ordered by the surface, and the darkest output levels
then land wherever the surface was highest. That is the shadow void, and it is
why every subtractive preparation produced it on this film while `plain` did
not. The same local quantile measures `void/ideal` **5.56** at g = 0.10 on the
high-passed field and **2.02** on the raw one, so this runs on the sample as
recorded.

**Choosing sigma**, over those same twelve crops, by `test/grainfit`'s `raw` — the
mask's grain against the unfiltered sample, which no preparation is circular
against, and where the raw sample itself reads 2.03 — and by the tile's density:
the spread of 64-px row and column bands across thresholds, which is the seam
line, in units of the binomial sd:

| | grain | seam lines (worst of 12) |
|---|---|---|
| high-pass | 2.57 | 0.17 |
| polynomial ÷ mean | 2.14 | 0.19 |
| σ 6 | 2.70 | 0.03 |
| σ 8 | 2.27 | 0.03 |
| σ 10 | 2.14 | 0.03 |
| **σ 12** | **2.10** | **0.04** |
| σ 16 | 2.08 | 0.04 |
| σ 24 | 2.08 | 0.06 |

That table is the Python prototype, which computes the neighbourhood exactly.
The shipped code, coarse grid and all, reads grain **2.18** at σ 12 against its
own high-pass's 2.67 on the same twelve, better on eleven; the grid widens the
weight a few per cent and costs about 0.08 of that. On the fourteen per-scan
crops that followed — four of the twelve replaced and two frames added; see
`examples/README.md` — σ 12 reads **1.90**
against 2.48, better on thirteen; X570-Tri-X-017 is the exception, 1.93 against
1.87.

σ 6 — the high-pass's own sigma for the default 32 px — wins on density by
removing the grain's own clumping, which is the trap from the paragraph above.
That table read a plateau at 12, and 12 was the default until it was noticed
that **`grainfit` measures shape in 12-px blocks, so it could not see the scale
being chosen**. Equalising makes every σ-sized neighbourhood the same density,
which removes whatever clumping the grain has above σ along with the drift, and
a 12-px block is blind to exactly that. Re-measured on those fourteen crops with
the block widened, 256 masks against their own crops:

| grainfit `raw` | high-pass | σ 12 | σ 16 | σ 24 | σ 32 |
|---|---|---|---|---|---|
| block 12 | 2.48 | 1.91 | 1.86 | 1.86 | 1.89 |
| block 24 | 3.53 | 1.88 | 1.74 | 1.64 | 1.63 |
| block 48 | 3.88 | 2.14 | 1.81 | 1.62 | 1.60 |

At block 48, σ 24 beats σ 12 on thirteen of fourteen, Delta3200 level. 24
against 32 is split — the flat fields prefer 32, the photographs 24 or 16 — so
the rule is **four times the high-pass's sigma** for `--mask-highpass ×
--mask-scale`, 24 px at the default, and 0 at `--mask-highpass 0`.

**What it costs is picture content, not seams.** A wider window lets through
more of the structure it is smaller than. The tiled mask's level-set anomaly
(`judge`'s A, blurred at 8 px, tiled so a seam counts) means 4.3 at σ 12, 9.1
at 24, and 10.2 for the high-pass; part of that is the grain's own clumping,
which is the point, and part is picture — X570-Tri-X-032a's window grows
vertical bands at 24 and -017's a grid of lines. The 64-px band spread, which is
the seam line, means 0.014 at 12 and 0.030 at 24 against the high-pass's 0.18,
and `colstats --group 16/32` on the `--supersample 16` ramp fails no column at
any σ from 12 to 32 where the high-pass fails 20. So the seams stay fixed, and
the crop has to be chosen for the σ it will be equalised at — which is why
`test/findcrops` models it and was re-run when it moved.

**Looked at**, at `--supersample 16`, the seam lines are gone on every crop.
What is left is picture content sharper than the neighbourhood, which no
neighbourhood transform removes — and that turned out to be a property of where
each crop was cut rather than of the transform. On the original crops Delta3200
kept its door frames and X570-Tri-X-017 and -032a the outlines of their
cloned-out subjects, and σ 24 lets more of each through. So each film now has
one crop, chosen at σ 24 from all its scans by `test/findcrops`, `grainfit` at
block 48 and the tiled mask's level sets by eye; `examples/README.md` has the
table. AutoRex (Kodak-4X) and X570-TriX80-274a (Kodak-OldTriX) come out clean,
Mamiya-2484-154 at 2048 (Kodak-2484) clean — equalise's fifth window rather
than its first, since the four it ranks above it each carry a line, an arc or
a blotch the residual barely registers — X570-Tri-X-032a (Kodak-TriX)
and Delta3200 at 768 with one faint line each, and PanatomicX keeps its dust
specks: equalise's alternative there sits at tone 17, scores worse and carries
a line. The bands in the outer 2σ of the tile,
where the neighbourhood is truncated, are *better* than before on every sample
(Kodak-2484 0.145 → 0.005), since the neighbourhood is normalised over what lies
inside the sample rather than mirrored.

Three implementation points, each a way to get it wrong:

- **The tie-share guard runs first, on what the high-pass would have left.** The
  transform shuffles every tie, so its output has almost none whatever went in,
  and a flat sample would come out as pure shuffle and pass a guard measured
  afterwards. Whether a sample has texture finer than `--mask-highpass` is a
  property of the sample, and `unittest` asserts the refusal under both
  preparations.
- **It is affordable only on a coarse grid.** One weighted count per grey level,
  and a full-resolution Gaussian at σ 12 is 38 ms on a 1024² field, so 256 of
  them is ten seconds. The counts are smooth at that scale, so they are taken on
  cells of σ/3 and read back bilinearly — 7 ms at a 256 tile, 0.2 s at a 1012
  one. Above 256 distinct values, a 16-bit scan, the levels are pooled into 256
  equal-count groups of the same strict order.
- **It runs over the crop plus 4σ of the sample round it**, before the seam cut:
  nothing may filter once the tile joins, and the tile's edges should see the
  sample beyond them where there is any.

`unittest` holds the property itself: a field whose mean ramps one way, whose
amplitude ramps the other and a third of which is clipped at zero must come out
with every 32-px block's quantiles at mean 1/2 and spread 1/√12. The high-pass
fails that by 0.187 on the spread against a bound of 0.04.

`--mask-highpass` is still read under equalising — it sets σ, and `direct.overlap`
is two sigma of it.

**What it gives up is the seam, and that is structural.** The other four
synthesise a field and can make it periodic by construction. A crop of a
photograph has a left edge that has never met its right, so it has to make a
join it was never born with.

The join is made by Efros & Freeman's minimum error boundary cut (2001). Take
the tile out of a crop `direct.overlap` larger on each axis, so the leading band
has two candidates — the crop's own start, and the continuation of what is about
to become the far edge — and switch between them along the path where they
differ least, found by dynamic programming. Once per axis, both ends of each
path pinned to the same index. Measured at 256² on Kodak-4X-1, the wrapping
pairs go from a rank-sum z of 13.79 and 8.65 uncut to **0.04 and 0.50**, and
`test/patchfit` from 3.04 to **2.606** — under that film's own real-patch floor
of 2.83. `test/masktest` reads 12.5x and 17.9x better than uncorrected.

**Why a cut rather than anything cleverer: every output pixel comes from exactly
one place in the sample.** Nothing is averaged, so nothing about the grain is
altered anywhere, and this generator's whole claim survives. That rules out the
band blend as well as the solve, and it is not a close call — histogram-
preserving blending has the right marginal *and* the right autocovariance and
still measures 3.42 and 3.76 on patchfit against the cut's 2.64 and 2.35.

`direct.overlap` defaults to two sigma of `--mask-highpass`, scaled with
`--mask-scale` — the scale over which the field stays correlated, and so the scale
over which the cut has independent places to go. The sweep found the cut sound at
every width from 4 px to 64, with no knee, so the floor of 8 is only giving the
search somewhere to go; at `--mask-highpass 0` it is an admitted guess, since the
field then keeps the photograph's low frequencies and two sigma is zero. Where the sample cannot
supply tile + overlap the overlap shrinks to what it can and says so, rather
than refusing — `mask_side` derives the mask from the *image*, so a sample
exactly mask-sized is routine.

**It used to be Moisan's periodic + smooth decomposition (2011)**, and that is
worth recording rather than deleting, because it is a correct implementation of
a published method that lost on the measurements above. Any image splits
uniquely as `u = p + s` with `s` smooth and carrying the whole boundary jump, so
`p = u - s` is periodic; one Poisson solve in the frequency domain, nothing
blurred and nothing mirrored. It made the join — mean edge mismatch on the
sample fell from 64.4 to 25.4 against an interior of 29.8 — but it lost twice.
It **over-corrected**, leaving the wrapping pairs 0.84x and 0.51x as *different*
as the interior ones, which is a soft seam rather than a hard one and no less
visible. And `s` came out of the whole tile rather than its edges, leaving a
broad tilt that needed a second filter to remove. `maskutil::periodic_component`
is still there and still tested; nothing calls it.

That implementation is checked against the paper's own criterion, in
`unittest`. Treating a crop as periodic — which a DFT and a tiled mask both do
— puts a jump all along its frame, and a straight-line discontinuity spreads
power along the perpendicular frequency axis: the "cross structure" of the
paper's Figure 1. So: build a field with no jump (white noise, whose pixels are
as different across the wrap as anywhere else, and whose broadband spectrum
makes on-axis and off-axis power alike), add a ramp to create a jump without
adding texture, and measure mean on-axis power over mean off-axis power. It
reads 0.99 for the reference, **224** with the ramp, and **1.18** after the
decomposition. Two further properties are asserted because the cross test alone
can pass for the wrong reason: `s` is harmonic in the interior to 3e-7 of its
own range — its defining property, and what says the decomposition removes a
jump rather than texture — and `u = p + s` holds with the mean untouched, since
`ŝ(0,0)` is pinned at zero. The harmonic test also refuses a trivial `s`: a
decomposition that returned `u` unchanged would leave `s` identically zero,
which is harmonic in the most useless possible way.

**The solve had to be cleaned up after, and forgetting that was a density bug.**
This is the second of the two losses above, in full, because the shape of it
generalises past the method that had it. `s` was subtracted from the whole tile
rather than just its edges, so it left a broad, gentle tilt. Rank-flattening
does not remove such a thing — ranking is a *global* monotone map, so it
flattens the histogram over the whole tile while preserving the tilt as a
low-frequency swing in the **local** mean. The result is a mask that is flat
overall and not flat anywhere in particular, which is a density defect rather
than a cosmetic one, and `--supersample` samples exactly that:

```
dither ramp.png --mask-method direct --mask-sample Kodak-4X-1.png \
       --mask-highpass 8 --mask-size 256x256 --supersample 8
```

failed three columns of `colstats` with the solve on and passed with it off; the
local mean's spread over a 16px Gaussian went 1.55 → 8.01. The fix was one more
high-pass after the repair, which brought it back to 1.19 and improved the seam
too (8.47 → 6.59), since the tilt was part of what made the join stand out.

**The cut has no such step and needs none**, which is the point: it moves pixels
the sample already held rather than adding a field to them, so there is nothing
to take back out. The same command now passes `colstats` at group 1, 16 *and*
32. Nothing may filter after the cut, and that is the inverse of the same rule —
a filter there would put values *between* the sample's own back in, which is the
sub-level ordering the high-pass is placed before the crop to avoid.


**Why the solve could not reach the interior level, and the cut can.** Film grain
is anisotropic — the sample's adjacent rows differ by 41 and adjacent columns by
30, and the difference grows smoothly with separation, so that is real texture
rather than scanner row noise. A solve drives *both* jumps toward zero, so the
axis with the larger interior difference is necessarily over-corrected and its
join comes out smoother than the interior rather than rougher; no single
correction strength fixes that, which is what killed the per-axis damping in the
list below. A cut has no strength to get wrong. It selects a route through
existing pixels, so the join it makes is as different as whatever it joined, on
each axis independently: 0.99/1.03 and 1.11/0.83 of the interior difference on
the two films.

`test/masktest` consequently holds `direct` to the same absolute line as
everything else. It used to be exempted, and held instead to improving on its
own uncorrected tile by 1.5x, precisely because the solve could not land both
axes. Removing that exemption also subjected this generator to the broken-copy
self-check for the first time — every other generator has to prove the detector
can fail — which has teeth here, because a tile of film grain has strong
column-to-column structure and destroying its join is loudly visible.

Six alternatives were tried and are worse, Moisan's decomposition above being
the sixth. Mirroring is exactly periodic and
exactly wrong — the eye finds the symmetry at once. Blending a band across the
seam destroys texture in a cross at the same fixed period. Keeping only low
frequencies is periodic by construction and discards the fine detail, which for
grain is the whole content. Damping the correction per axis to land on the
interior level helps three samples of five and breaks the others, whether fitted
on the mean or the median. Searching 30k crop positions for one whose edges
happen to match does nothing, because the edges are independent.

**A high-pass manufactures detail, and ranking draws it at full contrast.**
Subtracting a blur of integers leaves non-integers, so a two-valued sample gives
a tile with 15713 distinct values — none of which the sample recorded. Since
rank-flattening is scale-free, that residue becomes a full-scale swing in the
local mean. The cure is two steps that only work together: **quantise** the tile
to just under one input code (`direct.quantise`, 0.9), then rank it with the
resulting **ties broken at random** rather than by pixel index.

Neither half works alone, which is why neither was ported from `test/seamlab` on
its own. On a flat dithered sample the finished mask's local-mean spread reads
5.35 untouched, **36.40** quantised and ranked stably, and **0.70** quantised
with the ties shuffled — quantising by itself is seven times worse than doing
nothing, because a tie group in pixel order is a gradient across the tile.
Shuffling by itself measures *identical* to stable on both a flat sample and
film, because it only permutes within equal-value groups and the damage is in
which group a region falls into.

Film is indifferent: 0.1% of its pixels tie, and patchfit reads 3.040
unquantised against 3.074 at 0.9 and 3.050 at 0.3. That is why this went
unnoticed for so long, and why the step is a flat constant rather than a fitted
rule. The one thing a constant cannot express: `prepare_texture_sample`
high-passes and *then* resizes, so below `--mask-scale 1` the interpolator
averages already-high-passed values and the true floor drops under one code.

**The refusal that guards this was testing the wrong quantity.** It counted
distinct values in the tile and demanded 255; the flat sample passes with 13277.
What matters is the share of pixel pairs that tie *after* quantising, which is
exactly the fraction of the mask's ordering decided by the coin flip. Measured
over nineteen images, every real film scan lands between **0.006 and 0.065** —
including a fine-grained Panatomic-X surviving with only 50 distinct levels,
*fewer than several synthetic patterns that are refused*, which is why no
threshold on a count can work — while flat fields, ramps and line patterns land
between **0.39 and 1.00**. Nothing measured falls between 0.16 and 0.39, and the
bar sits at 0.25.

**The order of the high-pass was questioned and is only partly settled.** It
looks as though cropping first and then high-passing the tile wrap-padded — so
the filter sees a torus — should leave the periodisation less to do. On film the
two are indistinguishable: edge mismatch over interior goes 1.83 → 1.85 on
4Xcrop, the seam statistic moves by 1–3% with no consistent sign, and the tiled
masks look the same. The current order is kept because it is the one in place,
not because it was shown better.

One difference is definite, and it is why wrap-padding is not obviously safe:
**wrap-padding a sample that has a gradient leaves the gradient behind.** It puts
the dark end next to the bright end, so the blur errs in opposite directions at
the two edges and a residual tilt survives the high-pass — on a test image of
alternating black and ramp rows the column mean runs −23.8 to +23.8 where the
reflecting high-pass leaves 0.0 to 0.0. Film has no gradient, which is why this
never shows on a grain sample, but it is a real hazard for anything else.

The principle the rest of the program follows: **wrap the filter when the input
is a torus, reflect when it is not one yet.** `markov`'s field high-pass
wrap-pads because the Gibbs field really is toroidal and a reflecting filter
would break it every sweep; `spectralmask`'s spectrum smoothing wrap-pads because
a spectrum wraps. `prepare_texture_sample` reflects because a photograph does
not.

**There is no validated measure of seam *visibility* here, and the construction
choice rests partly on the eye.** Five proxies have been tried: the one-pixel
rank test `test/masktest` uses, that test on blurred copies, the local-contrast
profile across the join, the DFT cross artifact Moisan's method is named for,
and edge mismatch at several scales. They disagree with each other and with the
tiled images. The one-pixel test sees a hard discontinuity and is blind to a
broad band of altered texture; the contrast-profile test is the reverse, and
rates an uncorrected crop *better* than a corrected one on film, which the
images do not support. `test/seamlab --write` emits each construction's mask and
a 2×2 tiling, and that tiling is the check — the same standing this file already
gives markov's worming, which was checked by eye against the film's own crop
before `test/patchfit` existed.

`direct.overlap=0` takes the plain crop, which with `--direct-equalise 0` is
`refmask`'s own behaviour and what the correction is measured against. It replaced
`direct.periodic`, of which it is a superset.

The other thing it gives up is that it cannot invent, and the mask size has to
know that. The tile comes out of the sample, so the sample must supply the tile
*plus* the overlap after `--mask-scale`. `mask_side` derives the size from the
**image**, so this binds constantly — `--supersample 4` on a 256x256 image asks
for a 1024 tile, which a 512x512 sample has not got.

So a derived size is clamped to what the sample can supply, per axis, through
the generator table's `sample_limit` column (null for the three that synthesise
and are bounded by nothing). **The clamp comes last and beats the floors**, which
is the distinction worth keeping: `direct.max_size` is a cost preference and the
anti-repetition floor may override it, but no floor can conjure pixels the
sample has not got — at `--supersample 16` that floor alone would demand 1024.
A non-square sample gives a correctly non-square mask: 1800x950 in, 1024x938
out, seam 0.39/0.05.

A **pinned** `--mask-size` still refuses, and that is deliberate rather than
left over. The rule never runs when the user gave a size, so asking for a tile
the sample has not got stays an error that says so — a statement about what was
typed, not a default to be quietly improved.

### inpaint

`build_inpaint_mask` is what the worming investigation ended at. Efros-Leung,
coarse to fine — to decide a pixel, find the neighbourhoods in the sample that
most resemble the one around it and copy what they had in the middle. Matching is
done in a `inpaint.dims` (12) PCA subspace through a `cv::flann` k-d tree over
`inpaint.db` (250000) sample neighbourhoods.

**It was recorded here as the answer to the bind that section describes — no
temperature, so no ordering transition to fall below, so no sweep count worms —
and that is false.** Mid-octave ridgeness on Kodak-4X-2 reads 12.7 / 16.5 /
32.2 / 44.1 at 2 / 3 / 6 / 12 sweeps, with patchfit following it 23.4 / 29.7 /
53.4 / 69.7. Monotone in the sweep count is what worming is, and the argument
from the absent temperature was reasoning about a mechanism where a measurement
was needed.

The absent temperature does change its *shape*, which is why the claim survived:
with no transition to sit below and no coarsening length to grow, the ridges
build steadily from the first sweep rather than switching on at a threshold.
Nothing announces itself. What hid it beyond that is that `inpaint.iters` was
fitted on one film — at 6 sweeps Kodak-4X-1 reads 9.95 against 3 sweeps' 10.78,
and it is one of only three films in seven where more sweeping wins at all.
Where fewer wins it wins by 31–68%; where more wins it wins by 6–31%.

### The sweep loop is closed, and what that does and does not fix

The loop was **open**: each sweep makes every pixel more consistent with its
neighbours, which can only add structure, and nothing ever compared the field
with the sample. `iters` was a count, not a criterion. Worse, **the gain scales
with the structure already present**, so it is positive feedback. Over eleven
films the finished mask's directionality — structure-tensor coherence, the
single-number form of patchfit's ridgeness — sits within ±25% of its sample's
for the six the generator was tuned on, and 45% and 50% above it for the two
whose grain carries a strong direction. What that looks like is clumps stretched
into ridges *along* that direction, and no parameter removed it.

`inpaint.cap` (1.25) stops the sweeping once the field is that much more
directional than the sample. Measured across those eleven films it is
**byte-identical on nine** — only the two oriented ones trip it, after one and
two sweeps, and patchfit reads 8.9 against 16.2 on one and 15.5 against 18.8 on
the other. It bounds the feedback rather than guessing a count, so unlike
lowering `iters` it cannot regress a sample it does not fire on.

**It applies to the finest level only**, which is measured rather than tidy: a
coarse level's reference is the sample *downsampled*, which destroys the grain
it is meant to describe. Kodak-4X-1's coarse reference reads 0.020 against 0.216
at full resolution while its field reads 0.042 at both, so the coarse ratio is
2.13 on a film with nothing wrong with it, and 2.07 on another. Ratios against a
near-zero reference fire on exactly the samples that are fine.

**Two things it does not do.** It does not reach the coarse level, so it recovers
less than cutting `iters` outright does — `--mask-calibrate` picks 2 sweeps for
274a and reads 11.7 where the cap reads 15.5, because the calibrated value
applies to both levels and the cap truncates only the last. Calibration remains
the better tool; the cap is the floor under a run that does not use it. And it
does not remove the cause: **Efros-Leung resynthesis has no fixed point at the
sample's statistics**, so iterating always drifts towards something more regular
than the film.

**The published remedy was tried and does nothing.** This implementation takes
the strict argmin — `knnSearch(..., 1, ...)` — where Efros and Leung sample
uniformly among matches within 1.1x of the best, precisely to avoid this
collapse. Implemented with k=8 and a tolerance sweep, patchfit moves within noise
with no trend and the coherence not at all: 0.559 at the argmin against
0.547–0.562 over tolerances 1.05 to 1.3. With 250000 neighbourhoods in a
12-dimensional subspace the nearest eight are near-identical, so the mode-seeking
is in the iteration and not in the tie-break. The prototype's own rejected list
already carried this ("sampling among the 8 best, 28.8") from a film with no
anisotropy to protect; it now has a second measurement from one that has.

`inpaint.iters` was therefore 3 rather than 6, worth about 13% on the mean across
seven films (and is 2 now, measured without the high-pass — see "Calibration
under the no-high-pass default"), and it is the parameter `--mask-calibrate` measures for this
generator — it trades one defect against another and has no convergent value,
which is exactly `markov.temp`'s situation. `inpaint.checks` is 64 rather than
128 at the same time: across six films that is a wash on quality, the largest
move either way being 4%, and about a quarter off the search.

**The index had to be seeded before any of this could be measured, and that was
a reproducibility bug rather than a tuning one.** cvflann builds a kd-forest by
picking each split dimension with `rand_int` among the top-variance candidates
and shuffling the point order with `random_shuffle`, and both land on
`std::rand()`. So the tree, and every approximate answer it gives, depended on
how many indexes the process had already built. A mask was a pure function of
its inputs only *by accident* — which is the property the whole mask cache rests
on — and the accident held only because a run built one index per level in a
fixed order. `--mask-calibrate` put four probe builds in front of the real one
and it stopped holding: the same settings gave a different mask depending on
whether calibration had run, even when calibration decided nothing.
`PatchDatabase` now seeds cvflann from its own generator, so the trees still
differ between levels and between `--seed` values and are identical across
anything else the process did. Calibrating a sample whose answer is the default
is now byte-identical to not calibrating, which is the check.

### The speckle on Kodak-2484

Inpaint masks of Kodak-2484 look speckled, and that is measurable: the share of
a level set's pixels with no neighbour in it, over what white noise of that
density would have, reads **0.30 / 0.20 / 0.20** at g = 0.05 / 0.1 / 0.2
against the film's own **0.11 / 0.10 / 0.08** — the film being its `direct`
mask at `--direct-equalise 0`, so prepared by the same subtractive high-pass
inpaint's input gets. Single pixels the film does not have, in the shadows.

That preparation matters for the reference. The default, equalised `direct`
agrees within 0.02 on four films and reads Kodak-4X at 0.76 / 0.55 / 0.21
against the high-pass's 0.10 / 0.10 / 0.14, because 22% of that crop is clipped
at code 0 and equalising shuffles a tie group into isolated pixels.

It is not 2484's clipped shadows: Mamiya-2484-154 alone, which has none, reads
the same 0.30 / 0.20 / 0.18. Nor is it 2484's at all — every film measured
carries it, Kodak-TriX 0.35 against 0.15, Delta 3200 0.42 against 0.21, 4X 0.38
against 0.10, PanatomicX 0.53 against 0.32. Why it was noticed on 2484 first is
not measured here. Search effort does not move it — `checks` 256,
`iters` 1 or 6, `dims` 24, `scales` 1 or 3 and `--mask-highpass` 16 or 64 all
land within the seed-to-seed spread of ±0.02 — and the database only a little:
1M neighbourhoods against 250k reads 0.26 / 0.19 / 0.18, for twice the time.

**The lever is `inpaint.radius`, and it trades the speckle for the worming.**
Radius 2 cuts the excess by a third on every film (2484 0.20 / 0.14 / 0.11),
and `test/grainfit` at block 48 likes it on four of five, by a lot — 2484 7.32
→ 2.74, TriX 6.53 → 2.49, Delta 13.2 → 5.2, PanatomicX 8.1 → 5.1, 4X level.
`test/patchfit` says the opposite on four of five — 2484 21.8 → 31.0, TriX
11.7 → 25.6, 4X 36.6 → 58.0, PanatomicX 19.6 → 21.8; Delta 38.7 → 19.2 is the
exception — and at radius 2 its worst feature is `ridgeness_mid` on all five,
which is the worming. Radius 3 was settled against 4 and 5, not 2, so this is
the first measurement below it. The default stays, and the speckle is
recorded as what radius 3 costs. A plausible reading, not a measured one, is
that it is the same shape as markov's white-noise floor — a centre the
matched neighbourhood does not pin down — and a fix that kept both would need
to constrain the centre without shrinking the neighbourhood.

### Holding the sweeps to the sample's band power

Both markov and inpaint sweep a field towards local agreement with the sample,
and nothing in either compared the field's *scale* with the sample's. With the
high-pass gone that showed: on film crops, inpaint ended at 1.85× Kodak-TriX's
power coarser than 32 px and 0.57× Kodak-2484's, markov at 0.39–0.76× on all
six. Each sweep now rescales the field's power, band by band, to the
reference's before the field is re-ranked (`maskutil::impose_band_power`): 16
log-spaced radial bands from 1/size to the corner of the frequency plane. The
sweeps decide the arrangement and the reference decides how much structure
there is at each scale. This is *spectrum matching*; `--markov-bands` and
`--inpaint-bands` set the number of bands, 16 by default, and 0 turns it off.
It used to be registered as a 0/1 switch, which left the flags unable to do
anything but set the default and needed `--set` to turn it off; the count is
what the code always had as a constant, so it is the parameter now.

**Five ways were tried, on three crops and two seeds**, and this was the one
that moved every film to the sample's scale:

| | coarse share TriX / 4X / 2484 (1 is right) | patchfit |
|---|---|---|
| none | 1.85 / 1.02 / 0.57 | 16.8 / 9.6 / 31.0 |
| stop when coarser than the sample | 1.53 / 1.18 / 0.57 | 15.8 / 12.2 / 31.0 |
| **band power after each sweep** | **1.03 / 1.08 / 1.03** | **6.7 / 6.9 / 13.8** |
| each band's histogram (Heeger–Bergen) | 1.36 / 1.22 / 1.08 | 9.2 / 10.2 / 25.6 |
| random choice among the 8 nearest | 1.97 / 1.10 / 0.71 | 18.7 / 13.3 / 35.9 |
| coarse level as a constraint (Wei–Levoy) | 0.96 / 0.88 / 0.54 | 12.3 / 17.2 / 35.7 |

Stopping can only halt growth, and 2484 needs more structure, not less. The
random choice reconfirms the old result that the tie-break is not where the
drift is.

**Three things had to be right, and each was found by measurement.**

- **The bands must reach the corners.** Stopping at the axis Nyquist left the
  corners uncorrected, and once the bands inside were scaled down the re-rank
  gave them 10–40× the sample's power — a fine speckle.
- **The reference is the film's crop**, `samples::reference_for`, not the scans
  the model learns from: held to whole scans, the photographs' blobs set the
  scale. The crop's content is in the mask cache key for the same reason.
- **The reference must sample every scan and every part of each.** Its 16 tiles
  are shared between the scans by area, at least one each, and sit on an even
  grid across each scan. They used to be taken in raster order until sixteen,
  so a second scan counted for nothing, the bottom of a tall scan was never
  read, and swapping the files moved the target by 10^4 in a band.
- **Calibration holds its probes to the same reference set as the build.**
  `calib::Inputs` keeps the set the bands are held to (`reference`) apart from
  the one image a probe is scored against (`scoring`); collapsing the first to
  the second made calibration tune a different synthesiser for any sample of
  several scans with no crop.
- **The crop is equalised first**, at `kReferenceEqualise` = 24 px, per tile.
  The crops were chosen for direct's equalising and keep soft shading; held to
  that, markov's dither of `m039crop` from Delta 3200 wandered by +3.3 ± 5–9 per
  1000 over six seeds, worst +15. Equalised, +0.39 ± 0.13 (markov) and +0.27 ±
  0.18 (inpaint), against +0.45 ± 0.29 and −0.33 ± 0.70 unheld.

Measured on the six crops, unheld → held:

| | patchfit mean | grainfit 48 mean |
|---|---|---|
| inpaint | 22.9 → 15.1 | 6.72 → 3.34 |
| markov | 37.4 → 17.1 | 16.80 → 3.50 |

Better on every film for both. `test/levelfit` at `--method` (no high-pass)
reads worse on average, 0.11 → 0.31 and 0.17 → 0.30, and that is the crops'
shading in its reference: scored against the crop high-passed at 32 the held
masks clump 2–6× closer (PanatomicX inpaint 0.97 → 0.16, markov 0.57 → 0.18).
With the bands on, markov's temperature matters far less — six-film patchfit
means 38.6 / 37.7 / 36.4 / 35.8 at 0.35 / 0.5 / 0.7 / 0.9 — so the default stays
at 0.50. `unittest` holds the property: every band but the lowest within 0.15
log10 of the reference when held (0.03 markov, 0.12 inpaint), against 0.49 and
0.61 unheld.

### Calibration under the no-high-pass default

When markov and inpaint stopped high-passing, `--mask-calibrate` stopped
working for markov, and three defects in the calibrators showed up that the
filter had been hiding.

**The reference was a photograph.** Every calibrator scored its probes
against the centre of the largest scan. A high-pass took the subject out; at
0 it stays, and on Kodak-4X every one of nine weighting-and-radius
combinations ran to the *lowest* temperature, the most clumped field markov
can make, because that is what a photograph looks like at level-set scale.
The reference is now the film's crop where it has one — the window
`test/findcrops` chose to hold only grain — and each calibrator says which it
used (`calibrating against the film's crop, …`). `calib::inputs_for` decides
it once for all three.

**dft calibrated on a different sample from the one it built from.** Its
build has taken the film's crop since 2b81703, and its calibration still read
every scan, which also made it take 35 to 233 seconds. It now reads the sample
through `samples::resolve` with the generator's own crop preference, as the
build does: 1 to 4 seconds.

**The level sets were cut through tied values.** `level_set_low` took the
lowest `round(g·N)` cells with `partial_sort`, which cuts a tie group wherever
it happens to, so a reflected mask measured differently and markov's answer
moved in the fourth place. It now measures the cells the dither lights,
`g·254 > t`, normalised by the density that really is.

**Then the ranges were wrong.** With the crop as reference, the opposite end
bound: almost every combination wanted the top of its bracket, because a model
fitted with tone in it is over-coupled and needs a higher temperature to
match grain. The brackets were measured at 32. They are now `flat`
0.10–2.0, `mi` 0.05–2.0, `ml` 0.03–2.0, ten rungs each **spaced evenly in log
temperature** — each spans 20 to 67 times, and the old even step was 50% of
the bottom rung for `flat` and 100% for `ml` while being finer than needed at
the top — with the parabolic refinement done in log temperature to match.
Measured on the six films (calibrated temperature, weighting, radius; patchfit
against the crop for the rule's mask and the calibrated one, `--method markov`):

| | temp | weights, radius | patchfit rule | patchfit calibrated |
|---|---|---|---|---|
| Kodak-4X | 0.880 | mi, 5 | 96.1 | **49.8** |
| Kodak-2484 | 0.372 | mi, 8 | **43.4** | 48.6 |
| Kodak-TriX | 0.885 | mi, 8 | 68.1 | **38.3** |
| Kodak-OldTriX | 0.347 | ml, 8 | 87.6 | **47.8** |
| Kodak-PanatomicX | 1.075 | flat, 3 | 41.3 | **29.4** |
| Ilford-Delta3200 | 0.725 | flat, 8 | 41.8 | **33.3** |

Every search found an interior optimum, and the calibrated mask is better on
five of six, by up to half. It costs 76 to 119 seconds a film, once, cached.

**What this said about the rule, and what changed.** Uncalibrated, markov
took the `markov.temp` rule's 0.247 — a rule fitted at a high-pass of 32 —
while the measured optima at 0 run 0.35 to 1.08. The rule's anchor now depends
on the high-pass: 0.40 above 1 px, as before, and 0.65 without one, which gives
0.50 at the defaults. That is the best single guess for a film not yet seen,
chosen by sweeping a fixed temperature over the six with the default flat
weighting and radius 5 and scoring with `test/patchfit` against each crop:

| temp | 4X | 2484 | TriX | OldTriX | PanX | Delta | mean | worst |
|---|---|---|---|---|---|---|---|---|
| 0.247 (old) | 96.1 | 43.4 | 68.1 | 87.6 | 41.3 | 41.8 | 63.0 | 96.1 |
| 0.45 | 46.7 | 56.7 | 52.9 | 79.2 | 37.0 | 31.7 | 50.7 | 79.2 |
| 0.55 | 42.5 | 64.9 | 47.9 | 85.5 | 35.0 | 27.6 | 50.6 | 85.5 |
| 0.65 | 45.2 | 73.1 | 44.0 | 95.0 | 32.9 | 25.5 | 52.6 | 95.0 |
| 0.905 | 56.7 | 91.6 | 35.1 | 124.6 | 29.4 | 24.4 | 60.3 | 124.6 |
| calibrated | 49.8 | 48.6 | 38.3 | 47.8 | 29.4 | 33.3 | 41.2 | 49.8 |

The per-film optimum spans more than 2×, so no single value is close to every
film, and the calibrated row is what a film installed by `install_samples` gets.
The weighting and radius defaults stay: at each film's own best temperature
flat/5 averages 0.113 on the calibration's distance against ml/8's 0.111 and
flat/8's 0.114, and ml has not generalised before. `inpaint.iters` moved from
3 to 2 on the same six (see its rule in inpaint.cpp), and `InpaintParams` and
`MarkovParams` now default `highpass` to 0, matching the table, so a direct
caller of either API gets what the command line does.

**dft's ladder now steps in effective smoothing.** Its rungs were
coefficients `{1, 2, 4, 8, 16}` which the area rule divides by
√(sample area / mask area) and floors at one cell, so on a 1024 crop against
a 256 probe three of five were one point. The ladder is now the effective
width the build uses — 1, 1.5, 2, 3, 4, 6, 8 cells at the probe — turned into
the coefficient that produces it on this sample, so every rung is a different
mask. On the six crops the rungs now differ, and by less than the 10% margin,
so it still declines: the area rule is doing its job there.

### Calibrating inpaint

`inpaintcalib.cpp`. Four builds at 2, 3, 4 and 6 sweeps on a 256² probe, scored
against the sample's own ranked crop, and the default stands unless some rung
beats it by 10%.

**The objective is not markov's, and the difference is the point.** markov scores
level-set clumping alone, which is right for a temperature because over- or
under-clumping *is* what a temperature gets wrong. `inpaint.iters` gets *ridging*
wrong, and this file already says of `test/levelfit` that it "asks how much each
level set clumps and not whether the clumps are round or stringy". So the score
adds a ridge term — the structure-tensor coherence per octave that `test/patchfit`
carries for the same reason — with each term normalised by its own mean over the
ladder, because the two are log-ratios with different spreads and an unweighted
sum is a clumping-only search wearing a second term.

**The 10% margin is the load-bearing part.** Two terms stand in for patchfit's
eleven, and the proxy is good enough to rank but not to trust at close range:
where it calls the rung patchfit would it wins by 12–26%, and where it is wrong
it is wrong by 3–8%. Without the margin it moved one film to a rung patchfit
scores *worse than the default*, and a calibration that can do that is not worth
having at any mean. With it, measured over four films: 0.96, 0.77, 1.00 and 1.06
of the default, mean 0.95, worst regression 6%. Weighting the terms differently
was tried and cannot close the gap — neither term alone nor any mixture picks the
disputed rung — so the way forward is the other nine features, which means
patchfit in process.

### Calibrating dft

`dftcalib.cpp`, searching `dft.smooth_cells`, and it declines on every full scan
tried — which is the correct answer and worth understanding rather than
shrugging at.

**`dft.smooth_cells` is a coefficient, not the smoothing.** `smooth_power`
divides it by the square root of the sample-to-mask area ratio and floors the
result at one mask cell. On a 2.9-megapixel sample against a 256² mask the
divisor is about 6.6, so coefficients 1, 2 and 4 all land on the floor and
produce *the same mask*: three of the ladder's five rungs are one point. The
search reads 0.0473 / 0.0473 / 0.0473 / 0.0472 / 0.0468 on Kodak-4X-1 and
declines. The area rule is already doing the adapting that calibration would
otherwise do.

**Where it does fire is the other end of that rule.** The area term is an
extrapolated measurement, and a sample near the mask's own size is where the
periodogram is noisiest — not *4Xcrop*, which this section used to say was a
128² crop and is the whole 1300×2200 Kodak-4X-1 scan, so its divisor against a
256² mask is about 6.6. On a real 128² crop of Kodak-4X-1 the divisor is near 1,
the rungs separate and there is a real interior optimum: the search reads 0.1174 / **0.0769** / 0.0943 / 0.1386 / 0.1991 and takes 2,
18% better than the 4 the rule would have used. The probe size
therefore follows the sample rather than being fixed, since a large probe would
refuse exactly the samples this can help.

Its objective is spectral and nothing else, because dft randomises phase: a
clumping or ridging score of a dft mask measures its noise rather than its fit,
which is the same fact as its 0.385 level-set fit against 0.002 spectral.

**Two other dft parameters were considered and rejected.** `dft.iters` plateaus
at 20 by the measurement in the dft section above, with no sample term in sight.
`mask.highpass` does have a real per-sample optimum — 4X wants 64 where four
other films want the default 32, which is exactly what this file already records
— but it is a shared `--mask-*` parameter rather than dft's, and calibrating it
under one generator would make the same flag on the same sample mean different
things depending on `--mask-method`. Doing it properly means a calibration step
shared by every sample-based generator; on this evidence it buys one film 27%.

|  | patchfit 4Xcrop | patchfit 2484 | clump | slope | spectrum |
|---|---|---|---|---|---|
| inpaint | **9.4** | **4.5** | 0.234 | **2.97** | 0.671 |
| dft | 14.0 | 4.7 | 0.419 | 1.06 | **0.012** |
| markov | 20.4 | 12.4 | **0.189** | 2.27 | 0.634 |

A real patch of the film scores 2.8. It wins `test/patchfit` on both films and has
no worming at all (`Rcoar` −1.2, against markov's +1.4 now that the sweep count
has come down, and +10.0 before it); dft still owns the spectrum by construction
and is far faster. It costs 4.6/6.5/14.0 s at 128²/256²/512²
on a 64-core machine, so `inpaint.max_size` is 256 like markov's, as a cost
limit rather than a quality one.

**Ranking is the other place worth threading.** Every generator ends by ranking a
field flat, markov, inpaint and dft rank once per sweep or iteration, and markov
and inpaint rank every scan of a film together (`Set::ordering`). All of it goes
through `maskutil::stable_order`: a parallel `std::sort` (TBB, which OpenCV
already loads) of (value, index) pairs, which orders exactly as a stable sort by
value does, so every mask is byte-identical. It was a serial `std::stable_sort`
of indices reading each value from a scattered address. On Kodak-4X's five scans
at `--mask-scale 4` — 606 Mpx — the ranking took 136 s and takes 10 s, most of
that allocating the pairs; with the rank-to-pixel loops after it split by
`cv::parallel_for_`, `--mask-method markov --mask-scale 4 --supersample 12` went
from 163 s to 20 s, inpaint at scale 4 from 208 s to 32 s, and a 4096² dft mask
from 77 s to 24 s. The pairs cost 8 bytes a pixel while they live, beside the
values and the indices: that run peaked at 8 GB and now at 16.

Within inpaint, the nearest-neighbour search is what is worth threading,
and it is about eighty per cent of a build — turning `inpaint.checks` down to 8
takes a sweep from 4.6 s to 0.6 s. It parallelises with a `cv::parallel_for_`
around the `knnSearch` call and nothing else: a query's answer depends only on
itself and on a tree nobody writes to, and each output row has exactly one
writer, so the mask is byte-identical to the single-threaded build rather than
merely equivalent. That is checked, not assumed. Concurrent search on one index
is supported rather than got away with — cvflann's kd-tree keeps its visited-set
on the stack and takes its scratch heap from a pool keyed by thread id. It took
256² from 31 s to 9 s and 512² from 146 s to 23 s.

**But that pool is guarded by one process-wide mutex taken once per query, and
the search stops scaling well before the core count because of it.**
`Heap::getPooledInstance` in `opencv2/flann/heap.h` locks a function-static
`cv::Mutex`, finds the calling thread's heap in a shared map, and walks the
whole map evicting stale entries — all under the lock. Every query from every
core queues there, so past the point where the lock saturates, another core is
another waiter. Uncapped on 64 cores the search was **slower than on 16**:
sweeping the stripe count at a fixed 393216 queries reads 4.7 s at 8 stripes,
2.7 at 16, 2.0 at 24, 2.0 at 32, 2.7 at 48 and 3.2 at 64, and it was taking the
last of those. Capping the stripes at 24 takes a build from 8.1/21.1 s to
6.5/14.0 s at 256²/512².

**What is capped is concurrency, not chunk size, and the two were separated by
measurement rather than assumed.** Run the same sweep at two group sizes with
the query count held fixed and the optimum stays at 24–32 stripes while the rows
each stripe gets moves by 4x — 170 against 680. A granularity rule would have
tracked the rows. It is the lock that saturates, and it saturates at a number of
simultaneous callers.

Two things this does not license. The cap must not be `cv::setNumThreads`, which
is process-global and would throttle every other parallel region in the program
— including the gemms in the query projection a few lines above it. Shrinking
the *pool* to 24 as well is worth a further 7–13% (5.9/12.3 s), because
OpenCV's pthread pool synchronises all its threads at each of the 192 parallel
regions whether they have work or not; that is real and is deliberately not
taken, because the side effect is global and the gain is not. And byte-identity
survives all of it, which is the claim that had to be checked rather than
reasoned: the 76-run hash matrix is unchanged, and eleven configurations —
stripe counts 8 to 64 and pool sizes 16 to 64 — produce one mask hash at each of
256² and 512².

**Two candidates measured and rejected.** `KDTreeSingleIndex` — cvflann's exact
single-tree index for low dimensions — takes no lock at all and scales to 36x,
but it is 3–4x *slower* in absolute terms at the concurrency actually used (3.9 s
against 1.4 s at 24 stripes) and it agrees with the current index on only 74% of
queries, so it would change every mask byte and re-open four fitted constants.
`inpaint.checks` is the reason not to want it: 32/128/512 measure 11.9/9.4/10.8,
so quality is *non-monotone* in search accuracy and a more exact answer is not a
better one here.

**The remaining floor is the database build, and it is serial.** Of a 256² build's
6.5 s, 2.9 s is `PatchDatabase`'s constructor — 1.4 s of `cv::PCA` over the
250000×48 feature matrix and 1.2 s of kd-tree construction, neither of which
threads, and both of which are a *fixed* cost: they measure the same at 512²,
where they are 21% of the build, and they are most of a 128² one. Attacking it
means changing mask bytes. Fitting the PCA basis on a subsample would cut the
first — a 48×48 covariance does not need 250000 rows — and giving each level its
own seeded generator would let the two levels' databases build concurrently, or
let level 1's overlap level 0's sweeps. Both are real and neither is free.

`calibrate_markov_temp` runs its draws in parallel too, which took
`--mask-calibrate` at 256² from 10 s to 4 s — about 4x on the calibration
itself, the remaining 2 s being the fit and the real build. It was blocked until
the pooling weights stopped living on the model: `sample_markov_mask` took it by
non-const reference and wrote each call's `1/temp` weights into the shared
experts, so probes at different temperatures raced. With the weights call-local
the probes share nothing but a model nobody writes to.

The split is per temperature *and* per realisation, 24 tasks rather than 8,
since averaging over realisations is what makes a probe worth trusting and
splitting at the coarser grain leaves two thirds of the cores idle. Determinism
does not depend on scheduling: a draw's distance depends only on its index and
seed, each writes one slot, and the pick runs in index order rather than as a
race to update a running best. Pinned to one core it takes 10 s and produces the
same mask, byte for byte, as it does on sixty-four.

One thing nearby is *not* worth it: the query-gathering loop beside the inpaint
search measured no gain at all.

Four things about it are load-bearing, and each was measured wrong first:

- **The field is updated in place**, `inpaint.groups` (16) passes to a sweep.
  Deciding every pixel from the previous sweep leaves each individually
  plausible and neighbouring pairs inconsistent — patchfit 19.6 against 15.6,
  for free. The C++ Gibbs sampler had been doing this all along.
- **The database is the binding constraint.** 40k/100k/250k neighbourhoods
  measure 15.6/12.9/10.7 in the prototype. What looks like model noise is search
  error: a closer match is one whose centre is consistent with the neighbourhood
  that asked for it.
- **`inpaint.checks` (128) is the same constraint again**, since the k-d tree is
  approximate: 32/128/512 measure 11.9/9.4/10.8. Too few checks and the search
  misses; the flatness past 128 says the exact answer has been reached.
- **A bigger radius does not help** — 4 and 5 measure 31.8 and 50.9 — because the
  database thins out in a bigger space. Same constraint, seen from the other end.

`test/inpaintmask` is the Python prototype this came from. It is slower by a factor
of about fifty (brute-force search rather than a k-d tree) and is kept because
its docstring records the six variants that measure worse, including both
readings of a 2×2 hole.

### Choosing the weights and the temperature

Both were settled by measurement; `--markov-weights` and `--markov-report` exist so
the work can be redone rather than trusted.

**Weights: flat 1/K, and likelihood is the wrong guide.** `--markov-weights ml`
fits them by maximum likelihood on the sample (convex; projected gradient ascent
holding `Σw = 1/temp`, so only the direction is learned). It predicts the
sample's own pixels **0.09 nats better** than flat and concentrates ~68% of the
vote on the better half of the pairs — almost exactly what `mi` weighting does at
70.5%. Both then lose on texture. At each scheme's own best temperature:

| weights | level-set fit | aniso dB (sample −0.2) |
|---|---|---|
| flat | **0.198** | −0.18 |
| mi | 0.226 | +0.20 |
| ml | 0.239 | +0.24 |

A pair scores low exactly when it is distant or off-axis, which is also when its
information is least redundant. Prediction rewards concentrating on the strong,
mutually redundant near pairs; the texture needs the long-range and off-axis
constraints only the weak pairs carry. This is why `--mask-attend` lost, and the
ML result shows it was not a defect of MI as a score — it is the objective that
is wrong.

**Temperature: likelihood is wrong here too, in a quantifiable way.** Held-out
log-likelihood peaks at temp ≈ 0.21 while the texture optimum is 0.41, so the
log-opinion pool over-sharpens by about **2×**. Sharpening every conditional
always improves one-step prediction — the true neighbourhood is highly
informative — but the Gibbs chain then feeds on its own samples and the errors
compound into over-coupling.

**Moment matching on mutual information was tried next, and is superseded.** It
is recorded because the measurements are sound and the reason it fails is the
useful part. Quantise the finished mask and the high-passed sample to 16 rank
bins and compare mean single-offset mutual information over a few near offsets;
at the texture optimum the mask reads a consistent-looking fraction of the
sample:

| config | optimal temp | MI ratio there |
|---|---|---|
| bins 16, pairs 64 | 0.41 | 0.68 |
| bins 16, pairs 256 | 0.38 | 0.62 |
| bins 32, pairs 64 | 0.44 | 0.65 |

which reads as an invariant of **MI ratio ≈ 0.65**, and it validated as a
predictor on a config never tuned by hand (bins 24, pairs 128): the rule picked
0.42 against a measured optimum of 0.45, with 0.42 a tie (0.182 vs 0.186)
against 0.31 and 0.29 either side. Matching the sample's MI *exactly* over-couples,
because the field's MI profile is the wrong *shape* — at the matching point the
near offsets undershoot (0.74) while the far ones overshoot (2.27).

**But all three rows above are one film.** Across samples the ratio at the
optimum is itself a property of the texture: a film scan reads 0.63 and a
coarser sample 0.79, and targeting 0.65 on the coarse one returned 0.64 where
the truth was 0.37 — worse than the closed-form rule it was meant to improve on.
A constant cannot stand in for a quantity that is not constant, however carefully
it was measured on one texture.

**What `--mask-calibrate` actually does** is compare the thing being judged. A
threshold mask is used one level set at a time, so it draws probe masks across a
range of temperatures and takes the one whose level-set clumping best matches
the sample's own ranked crop — `test/levelfit`'s measure, computed in process by
`markovcalib.cpp`, and sample-relative by construction. The circularity is
acknowledged there: scoring the result with `levelfit` afterwards only confirms
the search ran, so the independent check is agreement with hand sweeps the
calibrator cannot see. `markovcalib.cpp`'s header comment is the primary source
for any of this.

**What transfers and what does not.** The optimal temperature is stable across
`--mask-size` — 0.41 at 64², 128² and 256² — so calibration can be done on a
small cheap mask and applied at full size. It is *not* stable across
`--markov-pairs` or `--markov-bins` (0.38 to 0.44 above), so those must trigger a
recalibration. That is the shape any future auto-default should take.

The residual defect is a white-noise floor: pooled pairwise conditionals are
broader than the true full-neighbourhood conditional, so every pixel carries
extra independent noise, visible as ~8× excess power above 0.3 cycles/px and as
a crunchy edge on the clumps. Lowering `--markov-temp` removes it and over-clumps
instead; the two cannot be had together from a log-opinion pool. Fixing it
properly means fitting the joint rather than pooling conditionals.

How much a clumped mask costs local density depends entirely on the mask size,
and at the derived default it costs nothing measurable: on the 256×256 ramp all
three generators get a 256² mask and `test/colstats` reports 0 (blue), 1 (dft)
and 1 (markov) of 256 column means outside 3σ at `--spatial 0`, dropping to 0 at
`--spatial 1`. Forcing `--mask-size 64x64` reads 15 / 38 / 37 instead. The
histogram is flat to 0.6% either way, so density is preserved in expectation;
what the z-test measures is that it assumes independent columns and a mask
repeated four times over does not supply them. An earlier note here quoted
17 / 4 / 3 as a property of the generators, which was really a property of the
smaller size derived at the time. None of it is in `test/coltest`'s matrix —
adding it would cost seconds per row.

Two findings from fitting real film that are worth not rediscovering. A greedy
placement loop cannot be retargeted at a broadband spectrum by swapping its
kernel: `argmin` is scale-invariant, so there is no temperature term, the
linear-response inversion `K̂ = 1/S - 1` does not apply, and any kernel with net
low-frequency attraction collapses into a single clump. A difference-of-Gaussians
kernel does produce clusters, but only at one characteristic scale. Separately,
grain coarser than the picture detail destroys it, and scaling the target finer
just converges on white noise — film's look needs `--supersample`, not a finer
mask.

The mask is written only when `--mask-out <path>` is given, and `--mask-in <path>`
loads one instead of building it (`cv::IMREAD_GRAYSCALE`, raw values — `MaskDither`
compares against `frac(v) * 254`, so no sRGB decode applies). Every builder uses its
own `std::mt19937_64(seed)` and none touches the global `rng`, so loading a mask
leaves the per-frame offsets identical to the run that produced it — `--mask-out`
then `--mask-in` reproduces a result byte for byte, whichever builder made it.

## Key invariants

- `--linear` disables the sRGB curve entirely (input treated as raw linear, output written raw). Everything else in the pipeline is unaffected.
- `--levels 2` (default) produces only `0` and `255`, which are sRGB-invariant — the choice of `useSrgb` doesn't affect the pixel values, only the linear-light interpretation of intermediate values (which are absent). `--level-spacing` is likewise a no-op there.
- `--level-spacing` says which display the ladder describes, and there are three
  answers. `srgb` puts the rungs at even 8-bit pixel values and takes each rung's
  light to be what that pixel decodes to: right when the file *is* the display
  signal. `linear` puts them at even steps of light. `device` puts them at the
  even steps of a display that has exactly `--levels` of its own — a 3-bit mono
  panel — where the file only carries level indices and a rung emits the curve
  applied to `k/(levels-1)`, not what its rounded 8-bit stand-in decodes to.
  Those differ by up to 2% of a rung's light, and only when `levels-1` does not
  divide 255: an 8-level dither viewed on a 3-bit panel is 0.39/1000 dark under
  `srgb` and 0.01/1000 under `device`, while at 4 or 16 levels the two ladders
  are the same object. Every other tone check here reads the output as an 8-bit
  sRGB image, which is the wrong question for `device`, so `test/devicetest`
  simulates the panel instead — and asserts both directions, since if `srgb`
  ever stops being worse there the mode has been silently unified away.
- `--level-spacing srgb` moves the rungs, and it does change the maths — the claim
  that it does not is what the bias above came from. `LevelSet::lin` holds each
  level's true linear value and `to_display` interpolates between rungs *in linear
  light*, so a fraction `f` between levels averages to `(1-f)·lin[k] + f·lin[k+1]`;
  interpolating in sRGB instead would look right and quietly break density
  preservation. But that only makes a *single* interval exact. Anything that
  accumulates error across intervals — which both ledgers do — has to run in
  linear light, via `Space`. For a uniform ladder `to_display` short-circuits to
  `linear * (levels-1)` and `Space` to the identity, which is the arithmetic the
  pipeline used before ladders existed — keeping default output bit-identical.
- `goals` and `displayed` (`CV_64F`) live for the whole `ditherPass` and carry the
  temporal budget across frames. Double is load-bearing, not caution: in linear
  light this ledger accumulates `frames` rung values, and the residue that leaves
  is measured against the *smallest* span on the ladder — 0.017 of full scale at
  8 levels, 0.0056 at 16 — so at high frame counts a float32 rounding error
  exceeds a whole level. `spatial_err` (`CV_32F`) is freshly zeroed inside every
  `quantize_scan` — it's a within-scan diffusion channel only, never accumulates
  across frames, and single precision is right for it.
- Output frames are `CV_8U` grayscale. `average_gray` averages them in linear light (decode → mean → encode) — averaging raw pixel means would be perceptually wrong. Note this is a *preview*: it re-encodes the mean to 8 bits, so it understates what a display showing the frames in sequence actually emits. Measure the frames, not the average, when the frame sequence is the product.

## Defaults

`doc/DEFAULTS.md` is the design for deriving parameter defaults from whichever
options the user actually supplied, and all six of its steps are implemented:
the registry, `--explain`, `--set`, derived rules such as `markov.temp` (from
`markov.bins`, `markov.pairs`, `markov.iters` and `mask.highpass`), and values
measured and cached as `Origin::Measured` — build speeds, which only
`--budget` reads, and calibrations, which every run on their sample takes.
Ask for a parameter with `P.num(key)` and the
rule runs once, recording what it depended on; a user-supplied value is pinned as
`Origin::User` and never recomputed. Adding a rule means calling `P.add(...)` in
the owning module — never editing the registry. A rule also declares *when* it
applies (`mask::generator("markov")` and friends in `maskparams.h`), which is
what produces the "applies to ..., and is ignored here" notes and the
"(does not apply to this run)" rows in `--explain`; applicability governs
reporting only, never resolution.

`mask.calibrate` is a parameter rather than a bool in `Options` for the same
reason: as a bool, `--mask-calibrate` against a non-markov method printed a note
and carried on, which is the silently-dropped-option shape again. It is declared
`mask::calibratable()`, which asks the generator table whether the generator in
use has a `calibrate` entry — dft, markov and inpaint do — so naming it against
blue or direct is refused, and naming it alone implies the first of those the
reconcile candidates offer, which is dft. Anything else that reads as a flag but
belongs to one generator should be a parameter too.

Applicability also drives `Params::reconcile()`, which runs once in
`parseOptions` before anything reads a value. An option the user actually
supplied should not be ignored because some *other* parameter was left at its
default: `--mask-sample grain.png` is a request for a mask, so the threshold
default has no business staying on `round` and then reporting the sample as
inapplicable. A rule that gates others offers `candidates` — `threshold` offers
`mask`, `mask.method` offers every generator that takes a sample — and
`reconcile` searches the
product of those lists for the assignment leaving the fewest supplied options
stranded, breaking ties towards moving nothing. What it moves becomes
`Origin::Implied`, is printed as a `Note: assuming …` line, and shows up in
`--explain`.

Whatever `reconcile` cannot rescue is, by construction, blocked by a parameter
the *user* pinned — had it been a default, it would have moved. So it is not a
misplaced option, it is two requests that cannot both be honoured, and
`Params::conflicts()` reports it as an error that names both sides:

```
--mask-scale applies to building a mask from a sample, which --mask-in rules
out; drop one of them
```

It names the flags the user typed, not the registry's keys, so the message can
be acted on without translating it back to the command line.

`blocked_by` is found by giving the applicability test a stack frame of its own
so the existing dependency recorder captures what it reads, then keeping the
keys the user set. That names the actual other side rather than restating the
condition, and needs nothing declared per parameter.

Between the two, an option the user typed is now never silently dropped: either
the defaults move to accommodate it, or the run stops. That replaced a `Note:
… is ignored here` printed alongside a zero exit status, which is the shape of
"your option had no effect and everything is fine".

Two properties are worth keeping. The search is exhaustive rather than greedy
because the moves are not independent: `--markov-temp 0.3` needs
`mask.method=markov` *and* `threshold=mask` together, and neither alone reduces
the count, so a one-at-a-time pass finds nothing. And nothing in `reconcile`
knows what a mask is — the conditions are the ones each parameter already
declared, so a new generator with its own gating parameter needs no change here.
Listing the implying options in the `threshold` rule instead would restate every
one of those conditions in a second place.

**Anything held in `Params` must be read from
`Params`** — `Options` is a read-only cache filled after the whole command line
is applied, because keeping a second writable copy silently broke `--set`. A rule carries its own permitted
range, checked on every write and on every derived result: bounds belong to the
parameter, not to whichever option exposes it, so demoting an option to internal
cannot quietly take its validation with it (it did once — `--set markov.bins=0`
dumped core before the range moved onto the rule). The rest of the design — a lazy resolution graph, with each module
registering its own rules so the mechanism stays ignorant of the generators.
`--budget <time>` is opt-in: unset, every
generator keeps its fixed size ceiling and output is byte-identical to before it
existed, which is why the test suites need no insulation and `test/coltest`'s
baselines still mean something. Given, it picks the mask size and markov pair
count to fit the time, using per-generator work formulas divided by a throughput
measured from real builds and cached in `~/.config/dither/cache.conf`. Which
rates get written there is `P.keys_ending(".rate")`, discovered from the
registry: it used to be the literal list `{"blue.rate", "dft.rate",
"markov.rate"}` at two call sites, a fourth generator was added without the
fourth string, and `--budget` could never learn inpaint's speed — it announced
that it was working from a guess on every run, for ever. `test/run`'s
`budgetcheck` holds the behaviour. The
allocation deliberately stops at 256 pairs, because past that the mask measurably
gets worse — spending the whole budget and getting the best result are not the
same thing. Turning `--budget` on by default would make output machine-dependent
and has a price listed under Reproducibility in that document; it has not been
paid. It also carries the full parameter inventory
(command-line vs internal, dependencies, and the rule for each), so read it
before adding a new tuning knob or changing a `constexpr` in `markov.cpp` or
`spectralmask.cpp`.

## Mask cache

Built masks are kept in `~/.config/dither/masks/` and reused; `maskcache.cpp`.

**The key hashes every file `--mask-sample` resolves to, in sorted order, with
the count mixed in.** A directory of scans means the mask is a function of all
of them, so hashing only the first would be this cache's worst failure — not a
miss but a wrong hit, a mask built from one film served for another. The count
is there because content alone cannot tell one grouping of the same bytes from
another. The version string is `maskcache/5`; an entry written by `/2` recorded
a single file and is the answer to a different question, and one written by
`/4` holds a sample-based mask in the sample's order rather than the threshold
order (see "Orientation" under Masks).
A mask is a pure function of its inputs, so a hit is byte-identical to a build —
measured 4.70s to 0.06s on a 256² inpaint mask, and `coltest` fell from 34s to
27s on within-run reuse alone.

**The failure mode is not a miss, it is a wrong hit**, so the key is walked out
of the parameter registry rather than listed in the cache: every key where
`Params::applies()` is true, at the value `--explain` prints. Three inputs are
not parameters and are added explicitly — the sample's *contents* (hashed;
`mask.sample` is only a path), the executable's identity (so rebuilding a
generator invalidates every mask it made), and formerly `seed`, which was the
one input to a mask living outside the registry and is now in it.

**What is left out is every parameter a rule declares an *intermediate*** — one
that reaches the mask only by deciding another parameter, which is then in the
key in its own right. Ten qualify: `image.width`, `image.height`,
`supersample`, `budget`, the five `<generator>.rate` values, and `mask.out`,
which says where the mask goes rather than what it is — in the key, every run
that saved its mask missed. Leaving them in
is not a correctness bug but pure waste, and it was one: `image.width` is an
attribute of the *picture*, so dithering a second image of a different size
rebuilt a byte-identical mask and stored it again.

`budget` is the one that has to be measured rather than reasoned about, because
with markov it does change the mask even at a pinned `--mask-size`. It moves
`markov.pairs` from 64 to 256, and `markov.pairs` is in the key, so the miss
still happens — for the right reason.

The declaration defaults to the safe direction: a parameter counts until a rule
says otherwise, so forgetting it costs a rebuild and never a stale hit. What
makes the exclusion trustworthy is a unit test that *builds* — the registry
cannot check the claim, since what it asserts is about which values the C++
reads. For each intermediate it varies the value and requires either that the
key moved or that the mask is byte-identical.

Two things need no code. `conflicts()` already refuses `--markov-bins 8`
against `--mask-method blue`, and `applies()` keeps a markov setting out of a
blue mask's key — so "compatible parameters" falls out of the registry. And a
larger cached mask deliberately cannot serve a smaller request, for the reason
under Masks above: a window of a clustered mask is not histogram-flat.

`test/run` exports its own `XDG_CONFIG_HOME`, so the suite neither evicts the
developer's masks nor relies on the build identity being right. `impliedtest` is
the check that most needed this and already had it — it passes `--no-config` on
every invocation, so its byte-identity assertion still tests the generators
rather than the cache.

## Verification

`test/run` runs everything and prints one summary; `test/run <substring>` runs a
subset. It resolves its own paths, so it works from any directory, and it puts
`dither_env/bin` on `PATH` itself — which is what the checks need, since they
shell out to each other by name and would otherwise find a `python3` without
`cv2`. It refuses with a clear message if the binary or `cv2` is missing rather
than reporting a wall of failures.

Everything under `test/` is runnable by hand from anywhere too: paths are
resolved relative to the script, not the working directory.

Four of its checks exist because the others each pin something to keep their
question cheap, which is how a default can break while every one of them
passes. `workflowcheck` runs the README's commands as typed, from a fresh
install with no size pinned and the cache on, and holds each output to its
size, its tones and the input's mean in linear light. `findcropscheck`
requires every window in `examples/films.tsv` to come back out of its own
search. `seamparams` holds `test/seamlab`'s model of `direct` to the program's
parameters, and `findcropscheck` holds `findcrops`' to the same, since both
benches once drifted from the program unnoticed. And `masktest`'s `oriented`
property renders flat greys through a mask built from a sample whose dark and
bright grain run in different directions (see Orientation).

`test/coltest` runs a matrix of dither invocations against `examples/ramp_256x256.png` and, for each output, calls `test/colstats` at every group size in its `GROUPS` tuple (1, 16 and 32). Grouping pools adjacent columns, so a row is identified by its configuration *and* its group: the two ask genuinely different questions, and a configuration can legitimately pass one and fail the other. `expected_failure` is keyed on that pair and states the most failing groups tolerated. An allowance there is a claim that the configuration cannot preserve density for a known reason, with the measurement that establishes it — not a way to quieten a result that has not been explained.

The matrix now includes partial `--spatial`, which it did not before. That omission is how the `--spatial` tone shift reached a release: `0` and `1` are both clean and neither exposes it. Reintroducing that bug now fails nine rows.

`test/colstats` decodes both the reference and output columns to linear (unless `--linear` is passed), computes means in linear space, and reports the sRGB-encoded linear mean in the `ref`/`out` columns. Column-mean deviations near mid-grey with `N=256` rows can hit ~5 sRGB pixels of noise even for a perfectly density-preserving dither, so passes are z-score based rather than raw pixel diff.

`--group N` pools N adjacent columns per test. Every term of the standard error
falls as 1/√samples — including the half-LSB floor, which is divided by √N — so
pooling tightens the test by √N and brings out a small bias applied everywhere
that per-column noise buries. This is not a refinement, it is the difference
between catching a bug and not: the `--spatial` tone shift passed all 256
columns at `--group 1` and fails four of sixteen groups at `--group 16`. Reach
for `--group 16` or `32` whenever a result looks wrong but every column passes.
`--group 1` is the default and is byte-identical to the ungrouped tool, trailer
included, so `coltest` is unaffected.

A grouped FAIL is strong evidence; a grouped PASS is weaker than its z-score
looks. The standard error assumes independent samples, which holds for a
stochastic threshold with no diffusion (measured at 1.07–1.21× the true SE) but
not once diffusion or a mask is in play — both cancel error actively, so the
regional mean converges nearer 1/n than 1/√n and the model runs 1.5× conservative
at group 1 rising to 8× at group 32. The √N gain in z for a fixed bias is real,
which is why grouping catches things; the false-negative rate is not 0.3%.
`colstats`' docstring records the two estimators tried instead and why both are
worse.

One implementation note: the noise estimate is the spread of `out - ref` over
the slab, not of `out` alone. A reference that varies across the slab then
contributes its structure to neither the mean nor the noise. For a single ramp
column the reference is constant and the two are identical, which is what keeps
`--group 1` exact.

`test/tonetest` asks whether the whole image averages to the input, across
thresholds, `--spatial` values, frame counts, level counts and level spacings. `colstats` asks the
same question per *column* and answers with a z-score against counting noise, which
is right for local density but nearly blind to a small bias applied everywhere: a
few parts per thousand vanishes into per-column noise of thirty. Averaging the
whole image cuts that noise by the square root of the pixel count. It exists
because exactly such a bug shipped and every `coltest` column passed.

`--levels` is in that matrix because a second bug then shipped in the first one's
blind spot. Every check swept thresholds and diffusion strengths at the default
`--levels 2`, where the ladder is uniform and display units cannot disagree with
linear light — so the unit bug above was invisible to all of them. Anything added
here should ask whether it is testing a uniform ladder by accident.

`--level-spacing` joined it for the same reason one turn later. The matrix swept
`--levels` but only ever on the default `srgb` ladder, whose rungs land on exact
8-bit codes; `linear` and `device` put them where 8 bits cannot represent them.
That has two consequences and they pull opposite ways. The rows are more
sensitive — a third mask bug was found there — but reading the output file
charges the dither for the *file format's* rounding: the mid rung of a 3-level
linear ladder is written as pixel 188 and reads back as 0.5030, a standing
+1.1/1000 that would leave a real bias of 0.6 nowhere to show. So tonetest reads
each output back to its level index and maps that to the ladder's exact linear
value, and reads multi-frame runs frame by frame rather than trusting the
averaged file, which is `devicetest`'s trick applied to every spacing. srgb rows
are unaffected by the change, which is the point: it corrects the measurement
only where it was wrong.

Its tolerances are per threshold rather than shared, because the three have
genuinely different floors and one number for all of them is as loose as the
loosest. `random` is the only stochastic rule and a band mean carries real
counting noise (5.5/1000 of a standard error over 8192 pixels); `round` is
deterministic but keeps a small residual under partial diffusion; `mask` is
deterministic *and* error-cancelling, so it has no excuse for any offset and is
held ten times tighter. That split is what separates a bias from noise: the
`frac * 255` bug read 1.17 where `random` honestly reads 0.95, indistinguishable
under a shared bound and caught in six rows once the deterministic rule was held
to the bound its determinism earns.

`test/devicetest` is the only tone check that does not read the output as an
8-bit sRGB image: under `--level-spacing device` the file carries level indices
for a panel with its own levels, so it simulates that panel. See the invariant
above for why the distinction is worth a check of its own.

`test/impliedtest` covers `Params::reconcile` — that naming a mask option selects
`--threshold mask` on its own, and that the shorthand is byte-identical to the
spelled-out form. That equality is the property worth pinning: a note printed
with the wrong image behind it is worse than no note. It also holds the three
parts easiest to regress — the joint move a markov-only parameter needs, that a
pinned `--threshold` is never overridden, and that a plain run acquires nothing.
It also holds the refusal: two options that cannot both apply must stop the run,
because the failure being guarded against is a zero exit status with an option
quietly dropped.

`test/run`'s `shellcheck` greps the tracked sources for `std::system`, `popen`
and the `exec*p` pair. It is there because `save_config` built
`mkdir -p '<path>'` and handed it to `std::system`, and that path comes from
`--config` or from `XDG_CONFIG_HOME`/`HOME` — so a single quote in it closed the
quoting and ran the rest, making `--config "/tmp/x'; rm -rf ~; '/c.conf"`
arbitrary execution. `std::filesystem::create_directories` does the same job
with no shell. The check is structural because reproducing the original needs a
mask build slow enough to trip the >0.25s cache write, and because the rule
worth holding — never hand a path to a shell — is broader than the one call that
broke it.

`test/masktest` holds every generator to the four properties this file claims of
all of them, where only the first was checked before: **usable** (flat
histogram, endpoints spanning 0..254 — `maskstats` says so), **toroidal**,
**any shape**, and **reloadable** (`--mask-out` then `--mask-in` reproduces the
run byte for byte). It replaced a `maskcheck` that tested only usability.

Toroidality is the one that needs care, and two attempts did not work. Comparing
the wrapping pair against the spread of adjacent pairs has no teeth: a noise mask
has so little column-to-column structure that a deliberately broken mask scored
2.4 where a sound one scored 1.1. Scoring where the wrapping pair falls between
two references — the adjacent pairs, and the same columns permuted — has teeth
but almost no *power*, because it compares means and a 96×48 mask gives it 48
samples. Across seeds a sound markov mask scored −0.37 to +0.53 while a broken
blue one scored 0.46; it was passing on the luck of one seed, which only showed
up when a default changed underneath it.

What works is a rank sum. Take the per-row absolute differences across the
wrapping column pair and ask whether they look like a draw from the pooled
differences of every interior pair — all `H·(W−1)` of them rather than their
mean. The statistic is `|z|` of the Mann-Whitney U, near 0 when the mask joins
and large when it does not, and it needs no assumption about the sign of the
correlation. That last part matters: blue noise's adjacent columns are *less*
alike than unrelated ones, where every other generator's are more. Built masks
score 0.1–1.7 against a line at 3.0.

Every run re-scores a deliberately broken copy and requires it to fail, because a
toroidality test that cannot see a seam is worse than none. That self-check is
skipped for blue noise alone, and the reason is not a fudge: breaking the join of
a blue-noise mask is genuinely near-undetectable from local statistics, because
there is almost no column-to-column correlation to break — the same property that
makes a seam in it invisible, and the reason blue tiles well at all. The check is
kept where toroidality can fail visibly and honest about having no power where it
cannot.

The non-square size is there because nothing else in the suite used one, which
is where a row/column transposition hides. It is 160×96 rather than the 96×48
first used, because at 48 rows the seam test cannot separate a sound mask from a
broken one for any generator.

`unittest` (built from `unittest.cpp`, linking `dither_lib`) holds the checks
that are about an invariant of the code rather than a property of an image. It
is the only test that calls the functions instead of running the binary and
reading back a PNG, and it exists because that distinction cost real bugs.

It covers the **multi-frame budget** — with `--spatial 0`, `sum_f display_f[r,c]
== goal[r,c]` exactly for every threshold, across the same 22-case matrix
`test/budgettest` used to run as a subprocess — plus the **level ladder** and the
**ranking every generator shares**.

Converting budgettest is what the ladder cases are for. The Python version had
to reconstruct the ladder to say what it expected, assumed evenly spaced pixel
values, and so could not express `--level-spacing linear` at all; a probe built
on the same wrong assumption once read a bias of 32.5 out of a program that was
correct. Calling `make_levels` removes the reconstruction, and the conversion
also made it 10x faster — 8s to under 1s — because nothing writes a PNG.

Three things it pinned that were not previously stated anywhere:

- `LevelSet::uniform` is decided by the *spacing*, not by inspecting the rungs,
  so it is **conservative**: across 1530 ladders none is wrongly flagged, but 10
  even ones are missed — `levels 2`, and `srgb` spacing under `--linear`
  whenever `levels-1` divides 255, where `px[k]` is exactly `255k/(levels-1)`.
  Those take the ladder-search path instead of the fast one, which is slower and
  gives the same answer. Only the other direction would be a bug, and that is
  what the test asserts.
- Under `--level-spacing linear` two rungs can share one 8-bit code, first at
  **122 levels**, because near white an even step of light is finer than sRGB
  can carry. A limit of the format rather than an error, but anything reading a
  level back *from* a pixel value is ambiguous there.
- `threshold_byte` puts rank `r` at quantile `t/254`, which is what makes
  `MaskDither`'s `frac*254 > t` average to the target.

`maskstats <mask>` measures a blue-noise mask: the mandatory properties (flat value
histogram, endpoints spanning 0..254) and then, per level set, low-frequency energy,
spectral knee, anisotropy and largest void — all normalised so uncorrelated noise
reads 1.0. It classifies the arrangement as blue/white/clustered independently of
whether the mask is usable, which is the split that matters if the generator is ever
aimed at a non-blue target. `--field` reports the spectrum of an ordinary image, and
`--highpass <px>` removes structure coarser than that first — a photograph's spectrum
is otherwise dominated by its subject rather than its grain.

`maskfit <sample> <mask>…` and `levelfit <sample> <mask>…` score a synthesised
mask against the sample it imitates, and are the pair to reach for whenever
`--mask-sample` changes. Both report RMS log10 ratio, both prepare the sample
with the high-pass the mask's generator used — `--method` asks `dither
--explain` for it through `test/maskref.py`, `--highpass` overrides, and neither
guesses when given nothing, because a fixed 32 scored every markov and inpaint
mask against the wrong reference once those stopped high-passing. `--method
direct` is refused while `direct.equalise` is on: `direct` ranks its sample
within a neighbourhood rather than high-passing it, which these tools do not
model, and a shaded sample scored 1.43 against its own preparation's 0.00.
`test/grainfit` is the tool for `direct`. `patchfit`
works the same way. And they measure deliberately different things:
`test/maskfit` compares radial spectra, `test/levelfit` compares how much each level set
clumps. A method can win one and lose the other by an order of magnitude — dft
and markov respectively do — so quoting either alone misrepresents the result.
`test/levelfit` builds its own reference by high-passing, cropping and ranking the
sample flat, which is the same treatment the mask got; pass `--reference-is-mask` when
the reference already is a mask.

`maskfit --scale S` is what to use on a mask built with `--mask-scale S`, whose
grain is S times coarser than the sample's on purpose: it compares mask
frequency `f` against the sample at `f * S`, and without it such a mask is
scored against the wrong part of the spectrum entirely. It restricts the fit to
the fully-defined radius `0.5/S` and normalises both spectra over that band,
because the two fields' total powers are no longer the same quantity. The
`alias` column reports what the mask put where the sample defines nothing, which
is the number that catches a remap folding rather than stopping.

Neither is comparable to a fit quoted without the sub-high-pass exclusion, which
roughly doubles every figure — see the note in the markov section.

`test/inpaintmask <sample> <out.png>` is a prototype fourth generator, living
outside the C++ program while it is decided whether it earns a place inside it.
It writes a mask; `dither --mask-in` uses it. Where markov pools *pairwise*
conditionals, it conditions on the joint neighbourhood non-parametrically — to
decide a pixel, find the neighbourhoods in the sample most like its own and copy
what they had in the middle, coarse to fine. Efros-Leung.

|  | patchfit 4Xcrop | patchfit 2484 | clump | slope | spectrum |
|---|---|---|---|---|---|
| inpaint | **10.7** | 6.1 | **0.185** | **3.44** | 0.671 |
| dft | 14.0 | **4.7** | 0.419 | 1.06 | **0.012** |
| markov | 20.4 | 12.4 | 0.189 | 2.27 | 0.634 |

(clump/slope/spectrum on 4Xcrop, whose own slope is 6.63.) It wins the aggregate
on both films and owns the slope, and trades with dft: dft owns the spectrum by
construction, while inpaint has the clumping dft has none of. It passes
`colstats --group 16` where a markov mask does not, and no 5×5 tile of its
output appears anywhere in the sample, so it synthesises rather than copies.
This paragraph used to claim it cannot worm at any sweep count; see the inpaint
section above for the measurement that withdrew that.

It used to beat markov on *every* metric, and no longer does: since
`markov.iters` came down to 3 the two are tied on clumping (0.189 against 0.185)
and markov's spectrum is now the better of the two (0.634 against 0.671). The
aggregate and the slope are still inpaint's by a distance.

Two things carried it and both were wrong at first. **Updating in place**: deciding
every pixel from the previous sweep leaves each individually plausible and
neighbouring pairs inconsistent, which is what pinned `Cfm` at −8 in every early
variant; splitting the sweep into 16 in-place passes took the total 19.6 → 15.6
at no cost, which is what the C++ Gibbs sampler had been doing all along.
**Database size** is then the binding constraint by a distance — 40k/100k/250k
measure 15.6/12.9/10.7 with `Cfm` −5.8/−2.8/−2.6 — because a closer match is one
whose centre is consistent with the neighbourhood that asked for it. Six
variants that measure worse are listed in its docstring, including both obvious
readings of a 2×2 hole.

**If this is taken further, the case for it is that it wins the aggregate and
dissolves the tangle**: no temp, so no T_c, so no worming, so no
`iters`/`temp`/`bins` interaction to calibrate. (The margin is narrower than it
was — see the note under the table above — but the tangle is the durable part of
the argument.) The work is a `cv::flann` index
in place of the brute-force scan — the database trend says a million
neighbourhoods would be better still, and an approximate index makes that
cheaper than the 250k scan is now. What it would not replace is dft, which stays
the right answer whenever the spectrum matters more than the clumping.

`test/seamlab <sample>…` is the bench the `direct` generator's seam handling was
settled on, and the place to take that question if it is reopened — which it
has been. It builds the mask several ways in Python — plain crop, the shipped
pipeline (`equalise`; `direct` is the high-pass at `--direct-equalise 0`), bare Moisan, wrap-padded high-pass, high-pass after the repair,
mirrored, per-axis damped, histogram-preserving blend, minimum error boundary
cut — and reports, for each: `work`, the edge mismatch over the interior
difference *before* any repair; `seam c` and `seam r`, the rank-sum z of the
wrapping column and row pairs; the share of the field's power in three bands;
`swing`, the spread of its *local* mean; `tiebrk`, how far the treatment moved
the field; and `mask`, the swing of the finished ranked mask.

Everything is measured on the **field**, before any ranking, and that is
deliberate. Rank-flattening is scale-free, so a low-frequency component of any
amplitude at all becomes a full-scale density modulation — looking at the mask
tells you something is wrong without telling you how much was added or by what.

`swing` exists because a defect got past the other two. Ranking guarantees the
global histogram and says nothing about local flatness, so Moisan's broad tilt
survived it while `work` and `seam` both reported the mask as fine and a
dithered ramp failed `colstats`. Every construction that ends in a bare
periodisation shows it — moisan 9.6, wraphp 10.0, damped 7.6, against an
untouched crop's 1.8 — and the cure in each case is the final toroidal
high-pass the shipped pipeline has.

**`seam c`/`seam r` are signed, and the sign was the finding.** They returned an
absolute value until it was noticed that the then-shipped repair's 1.34 and 4.84 on
4Xcrop were not residual seams at all: its wrapping pairs are 0.84x and 0.51x as
*different* as its interior pairs, so Moisan plus the toroidal high-pass are
overshooting into a join smoother than the surrounding texture — a soft line
rather than a hard one, and no less visible. Positive is a hard seam, negative a
repair that went too far, and 0 is a field that simply continues. Under the
magnitude, the two were indistinguishable. `mirror` now reads −27.55 rather than
+27.55, which is the correct description of a wrap pair that is literally the
same column twice.

**Two constructions beat the decomposition, and one of them is now shipped.**

- `blendpre` — histogram-preserving blending (Heitz & Neyret 2018) of the tile
  with its own half-shifts, one separable pass per axis, in the production order
  (high-pass the sample, then blend; nothing may filter afterwards, or the
  sub-level ordering `--quantise` removes comes straight back). No global solve,
  so no mechanism for injecting a smooth field into the interior, which is the
  defect that rules out both `moisan` and `wraphp` however well they close the
  seam. One weight per axis is forced: the tile breaks on the whole cross
  {x=0} ∪ {y=0} and its half-shift on {x=W/2} ∪ {y=H/2}, those crosses
  intersect, and at the intersection a single 2D weight would have to be 0 and 1
  at once.
- `quilt` — Efros & Freeman's minimum error boundary cut (2001) against the
  tile's own continuation, one pass per axis, both ends of each path pinned to
  the same index. Pinning rather than allowing a step of 1 at the wrap is not
  fussiness: a step there splices a pixel from a whole tile away, which is
  arbitrary rather than a bounded near-miss.

`quilt` wins, and it wins for a structural reason: every output pixel comes from
exactly one place in the sample and nothing is averaged, so `direct`'s whole
claim — everything true of the grain is true of the mask — survives, where the
blend trades a slice of it away inside the band.

| | seam c | seam r | wrap/interior | `tiebrk` | patchfit |
|---|---|---|---|---|---|
| 4X-1 shipped | 1.34 | −4.84 | 0.84 / 0.51 | 2.380 | 3.12 |
| 4X-1 `blendpre` | −0.94 | 2.70 | 1.01 / 1.22 | 0.973 | 3.42 |
| 4X-1 `quilt` | **−0.51** | **0.12** | **0.99 / 1.03** | **0.408** | **2.64** |
| 4X-2 shipped | −10.69 | −7.34 | 0.57 / 0.73 | 2.513 | 2.74 |
| 4X-2 `blendpre` | 1.84 | 2.94 | 1.07 / 1.24 | 0.395 | 3.76 |
| 4X-2 `quilt` | 1.78 | −3.38 | 1.11 / 0.83 | 0.424 | **2.35** |

`quilt`'s patchfit scores are *below* those films' own real-patch floors of 2.83
and 3.13. On greyd — the flat dithered field that is `repro.sh`'s case — its
mask swing is 0.72 against the untouched crop's 0.70, i.e. indistinguishable
from doing nothing while actually closing the seam, where the shipped repair
reads 3.42 and its 2×2 tiling shows a dashed cross.

Three things `quilt` costs, all real, and the second is why `blendpre` is worth
keeping in the bench rather than deleting. It needs somewhere to cut, which
stochastic grain offers constantly and structured content does not — on linesr,
whose lines sit on a gradient so the two edges never agree anywhere, it reads
16.2 where the blend reads −5.5. It needs a sample larger than the tile by the
overlap, where every other construction needs only a tile; `build_direct_mask`
shrinks the overlap to what the sample can supply rather than refusing, which is
safe only because of the third point. And an overlap sweep over 4–64 px finds no
knee at all, so the default is 2σ because wider substitutes more pixels for no
gain, not because narrower fails.

If a sample ever turns up that `quilt` cannot serve — structured, on a gradient,
of the kind linesr stands in for — `blendpre` is the measured answer for it, and
`test/seamlab` is where to pick that up.

`--check` rebuilds the same mask with `build/dither` and prints its seam beside
the Python `moisan` row; they match to the digit, which is what makes the bench
worth trusting. `--score` skips the constructions and just scores finished masks,
so any generator's tiling can be compared. Every entry in `direct`'s rejected
list above was killed here, and `mirror` is kept as a variant because it makes
the argument against mirroring a number: its wrap pair is literally the same
column twice, so it scores 27 — perfectly periodic and maximally anomalous.

`test/patchfit <sample> <mask>…` asks the question those two cannot: would a
patch of this mask disappear if you dropped it into the film? Both fields get
the mask treatment (high-pass, crop, rank flat), each is cut into
non-overlapping blocks, each block becomes eleven features, and the mask's mean
feature vector is scored by Mahalanobis distance against the spread of *real*
patches of that film. The unit is therefore real-patch standard deviations, and
the floor is not zero — film is not stationary, so one real patch already
differs from the others by 2.8 on 4Xcrop. Calibration: real patch 2.8, dft 14,
markov 20 (35 at the old 12-sweep default), white noise 87,
void-and-cluster 362.

Its reason to exist is the two features the other two cannot express. **Ridgeness**
is the structure-tensor coherence of each octave — 0 for a field of blobs, 1 for
a field of ridges — and it is what makes worming a number instead of a thing to
check by eye. **Envelope correlation** across adjacent octaves lives entirely in
the phase, so a spectrum-matching generator gets it wrong for free: it is how
dft is caught, along with a level-set clumping of −9.2 at the midpoint.

Read the per-feature columns before the total. The total weights each feature by
how much real patches vary in it, not by how visible it is — a statistical
distance, not a perceptual one. One reading is worth knowing: markov's `Cfm` is
strongly negative — the white-noise floor described above, where pooled
conditionals add independent noise and decorrelate the fine octave from the mid.
It ran −11 to −15 at every setting tried until the sweep count came down, and
reads −8.8 at the current default; `Rfine` moved with it, from a ceiling of −5 to
−3.8. Both are the same effect easing, not going away.

`dither --debug-pixel <row,col>` prints per-frame `ideal`/`display`/`displayed` for
that pixel, which is the quickest way to see the budget converge.

These scripts find their interpreter via `#!/usr/bin/env python3` and invoke each
other by path, so putting the virtualenv on `PATH` is what makes them use it —
`dither_env/bin/python coltest` is not enough, because the `test/colstats` it spawns
would still pick up the system `python3`. Without `cv2`, `test/coltest` reports
`COLSTATS PARSE FAIL` for every row:

```sh
export PATH="$PWD/dither_env/bin:$PATH"
test/coltest
```
