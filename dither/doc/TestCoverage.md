# Function test coverage plan

Snapshot: 2026-10-18. This is a source-based coverage map and test specification, **not a measured line/branch coverage percentage**. It records current callers and separates existing checks from tests that should be added. A successful invocation can bypass generators through caching; use the isolated scenarios below when testing execution paths.

## Scope and evidence

Includes every project `.cpp` and `.h` found under this directory, including inline methods, explicit constructors/destructors, function templates, macro-generated `CASE` functions, anonymous lambdas and the two archived experimental drivers. Compiler-generated special members and standard-library/OpenCV internals are not separate project functions. The installed `dither_env` dependencies are excluded; they are not this project's source. `MarkovJunior` contains no project C++ implementation in this inventory. Declaration-only headers are indexed separately and link to the full function entry; a declaration and definition of the same signature are one function, not two tests.

Caller lists combine source inspection with GCC 13.3.0 `-fdump-ipa-cgraph` output from a clean Debug build using OpenCV 4.6.0. Template instantiations are grouped by source definition. Calls inside a lambda are attributed to its enclosing named function and marked as such; each lambda also has its own subsection. Function pointers, virtual calls, lazy parameter rules, standard-algorithm comparators and process entry points are described explicitly. A callback's registration site is not its execution caller. Caller lists are limited to this source tree, not unknown external library clients.

Validation performed: all five CMake executable targets built successfully in `/tmp/dither-coverage-build`; `unittest examples/ramp_256x256.png` passed **60/60** cases. Representative commands for H, E, R, O, B, D, M, P, Q, L and T were smoke-tested successfully (including the corrected inpaint database minimum of 1000). These smoke checks validate command syntax and successful execution, not every proposed assertion. The full integration suite and expensive calibration searches were not run. Separate compiler probes confirmed the public calibration overload fails to link and the archived Markov driver fails syntax checking, as described below. Existing unit references establish a call path, not complete branch/assertion coverage. No production code was modified.

### Important gaps to address first

1. **Public API compile/link coverage:** `markov.h` declares `calibrate_markov(const cv::Mat&, ...)`, but `markovcalib.cpp` defines the `samples::Set` version. The header-only client signature has no implementation. The archived Markov driver also passes a `cv::Mat` to `fit_markov_model`, whose current interface requires `samples::Set`. Add tiny client translation units that include each public header and link representative API calls.
2. **Calibration decisions and persistence:** isolate scoring/selection from expensive generation to test pinned parameters, invalid cached measurements, failed probes, endpoint winners, the 10% decision margin and cache invalidation deterministically.
3. **Parameter graph and parser:** test precedence, dependency invalidation, failed resolution cleanup, reconciliation, generated flags and config pre-scan as contracts. Existing image tests do not fully cover these.
4. **Filesystem failures and caches:** malformed metadata, interrupted writes, full/unwritable destinations and concurrent access require fixtures; successful image generation alone proves none of these.
5. **Multiple scans and reproducibility:** unequal dimensions/exposures, stable ties, no cross-scan neighborhoods and seeded results independent of prior generator activity.
6. **Engine boundaries:** negative threshold inputs, exact rung snapping, rectangular toroidal indexing, edge diffusion and no-diffusion temporal budgets need small numerical oracles rather than screenshots.

## Reproducible command-line scenarios

Run from the project root. The following setup uses an isolated scratch directory and leaves the user's config and sample library alone. `B` must point to a freshly built directory; do not accidentally run an old root-level binary.

```bash
B=/tmp/dither-coverage-build
T=$(mktemp -d /tmp/dither-coverage.XXXXXX)
I=examples/ramp_256x256.png
S=examples/Kodak-4X-1-crop.png
export XDG_CONFIG_HOME="$T/config"
mkdir -p "$XDG_CONFIG_HOME" "$T/scans"
cp "$S" "$T/scans/a.png"
cp "$S" "$T/scans/b.png"
```

The duplicated sample above is only a smoke fixture. Multi-scan correctness tests must also generate unequal-size, unequal-mean/contrast scans with known values. Use native 16-bit, RGB/RGBA, constant, impulse, periodic sinusoid, checkerboard and non-contiguous ROI fixtures for the branches that require them. Small synthesis settings test mechanics, not production texture quality. No CLI option can force all private error branches; those require unit fixtures or injected failures.

| <a id="scenario-id"></a>ID | Invocation / variants | Observable assertion |
|---|---|---|
| <a id="scenario-h"></a>H | `"$B/dither" --help`; `"$B/dither"`; `"$B/dither" --list-options`; `"$B/dither" --list-samples` | Help/list routes and statuses; no output image or mask build. Bare invocation fails with brief usage. |
| <a id="scenario-e"></a>E | `"$B/dither" --no-config --explain`; `"$B/dither" "$I" --mask-sample "$S" --no-config --explain`; add `--markov-bins 8` | Values/origins/dependencies and implied method; explain does not calibrate or build masks. |
| <a id="scenario-r"></a>R | `"$B/dither" "$I" --no-config --out "$T/round.png"`; add `--threshold round --spatial 0`, then `--spatial 0.5` and `--spatial 1` | Default diffusion versus explicitly named threshold; output dimensions, palette and light/tone conservation. |
| <a id="scenario-n"></a>N | `"$B/dither" "$I" --threshold random --seed 42 --frames 8 --save-frames --no-config --out "$T/random.png"` | Repeated seed reproduces frames; different seed changes noninteger decisions; budget and average verified numerically. |
| <a id="scenario-f"></a>F | `"$B/dither" "$I" --threshold round --spatial 0 --frames 8 --levels 3 --level-spacing linear --save-frames --no-config --out "$T/frames.png"`; vary spacing `srgb/linear/device`, levels `2/3/8/256`, frames `1/2/32`, and `--linear` | Per-pixel raw level budget at spatial 0; correct encoded palette and linear-light average. Repeat with native 16-bit input. |
| <a id="scenario-o"></a>O | `"$B/dither" "$I" --supersample 2 --out-size 128 --frames 2 --save-frames --debug-pixel 0,0 --no-config --out "$T/scaled.png"`; omit out-size; set it to `512`; try debug pixel `99999,0` | Single resample, aspect ratio, frame naming and debug-coordinate validation on enlarged target. |
| <a id="scenario-b"></a>B | `"$B/dither" "$I" --mask-method blue --mask-size 32x24 --seed 42 --mask-out "$T/blue-mask.png" --no-config --out "$T/blue.png"`; vary `--blue-sigma 1.5` and `--blue-eps 0.001` | Byte range 0..254, rank histogram, deterministic mask and toroidal spatial statistics. |
| <a id="scenario-d"></a>D | `"$B/dither" "$I" --mask-method dft --mask-sample "$S" --mask-size 64x64 --dft-iters 4 --mask-out "$T/dft-mask.png" --no-config --out "$T/dft.png"`; vary `--mask-scale 1/2/4`, `--mask-highpass 0/32`, `--dft-smooth-cells 1/8` individually | Spectrum frequency locations/cutoff and lack of alias folding; rank histogram and seed repeatability. Slash-separated values here mean separate runs, not literal CLI values. |
| <a id="scenario-m"></a>M | `"$B/dither" "$I" --mask-method markov --mask-sample "$S" --mask-size 32x32 --markov-bins 4 --markov-pairs 8 --markov-radius 3 --markov-iters 2 --mask-out "$T/markov-mask.png" --no-config --out "$T/markov.png"`; repeat with `--markov-weights flat/mi/ml`, `--markov-temp 0.2/0.5`, `--markov-report` | Model fitting/drawing paths, diagnostics without changed pixels, finite statistics and repeatable rank masks. |
| <a id="scenario-p"></a>P | `"$B/dither" "$I" --mask-method inpaint --mask-sample "$S" --mask-size 32x32 --inpaint-db 1000 --inpaint-dims 4 --inpaint-scales 1 --inpaint-iters 2 --mask-out "$T/inpaint-mask.png" --no-config --out "$T/inpaint.png"`; vary scales `1/2`, groups, checks and `--inpaint-cap 0` versus positive cap | Patch matching, coarse-to-fine paths and early-stop logging; rectangular/tiny invalid geometry in separate negative runs. |
| <a id="scenario-q"></a>Q | `"$B/dither" "$I" --mask-method direct --mask-sample "$S" --mask-size 32x32 --direct-overlap 8 --mask-out "$T/direct-mask.png" --no-config --out "$T/direct.png"`; vary `--direct-quantise 0/1` and `--mask-highpass 0/32` | Seam-cut behavior, sample size limit and excessive-tie refusal. Flat/low-detail samples exercise failure, not successful masks. |
| <a id="scenario-l"></a>L | After B: `"$B/dither" "$I" --mask-in "$T/blue-mask.png" --frames 8 --save-frames --no-config --out "$T/loaded.png"`; use missing and corrupt mask paths | Loaded mask bypasses synthesis; correct offsets/budgets; clear read failure. |
| <a id="scenario-c"></a>C | Repeat D/M/P/B/Q **without** `--no-config`, with `--config "$T/cache.conf"`; run each twice, then change seed/sample contents/one applicable parameter | Miss builds, hit reuses identical mask, changed relevant input misses; corrupt/mismatched-size entry rebuilds; no build-rate update from a hit. |
| <a id="scenario-v"></a>V | `"$B/dither" "$I" --mask-method inpaint --mask-sample "$S" --budget 10 --config "$T/rates.conf" --out "$T/budget.png"`; compare first run, cached run and a forced fresh build; also `--budget 1m` | Budget influences derived work/size; sufficiently long fresh build records a rate, cache hit does not; unit tests cover exact 0.25-second boundary. |
| <a id="scenario-cd"></a>CD | `"$B/dither" "$I" --mask-method dft --mask-sample "$S" --mask-size 64x64 --mask-calibrate --config "$T/dcalib.conf" --out "$T/dcal.png"`; repeat; add `--dft-smooth-cells 4` | Search, cache reuse where a measurement is accepted, pinned-setting skip, no-improvement fallback; actual probe size is independent of requested mask. |
| <a id="scenario-cm"></a>CM | `"$B/dither" "$I" --mask-method markov --mask-sample "$S" --mask-size 64x64 --mask-calibrate --config "$T/mcalib.conf" --out "$T/mcal.png"`; separately pin `--markov-weights flat`, `--markov-radius 3`, or `--markov-temp 0.4` | Joint/unpinned and restricted searches; explicit temperature skips entire search; endpoint/failed-probe fixtures do not persist fake measurements. Expensive: run in a slow suite. |
| <a id="scenario-cp"></a>CP | `"$B/dither" "$I" --mask-method inpaint --mask-sample "$S" --mask-size 64x64 --mask-calibrate --config "$T/icalib.conf" --out "$T/ical.png"`; repeat; add `--inpaint-iters 3` | Sweep-ladder scoring, 10% margin, measured cache or retained default, explicit iteration skip. Expensive: run in a slow suite. |
| <a id="scenario-s"></a>S | Repeat D/M/P/Q using `--mask-sample "$T/scans"`; put a film directory under `$XDG_CONFIG_HOME/dither/samples/TestFilm` and repeat with `--mask-sample TestFilm`; run H list-samples | Pool complete scans for D/M/P; direct uses largest scan and says so; path precedence, stable ordering and library lookup. |
| <a id="scenario-x"></a>X | `"$B/dither" "$I" --levels 2.9 --no-config`; `"$B/dither" --no-config --explain --threshold nonsense`; `"$B/dither" "$I" --mask-method markov --no-config`; `"$B/dither" "$I" --no-config --out "$T/no-parent/out.png"`; vary missing values, incompatible flags and corrupt input | Nonzero exit and actionable diagnostic; no core dump, silent ignored user setting or misleading successful artifact. |
| <a id="scenario-t"></a>T | `"$B/maketexture" "$S" --sample 64x64+0+0 --size 64x64 --mask-method dft --dft-iters 4 --out "$T/texture.png" --paste "$T/paste.png"`; repeat for markov/inpaint using matching small settings from M/P; compare highpass omitted/0/32 | Texture histogram/geometry; paste changes only selected rectangle; default highpass zero; cross-generator/workflow flags rejected. |
| <a id="scenario-th"></a>TH | `"$B/maketexture" --help`; `"$B/maketexture" --list-options`; malformed `--sample`, excessive `--size`, `--mask-in` and mismatched paste/size | Help and option inventory reflect parser; validation fails before unsafe allocation or writing. |
| <a id="scenario-u"></a>U | `"$B/unittest" "$I"`; `"$B/unittest" "$T/missing.png"` | All registered checks execute; missing input fails; harness mutation fixtures verify failed/thrown cases produce nonzero overall status. |
| <a id="scenario-sr"></a>SR | `"$B/srgbtest" "$I"`; repeat with missing path and a suitably sized native 16-bit fixture | Diagnostic values match transfer formulas; document accepted input depth and invalid input behavior. |
| <a id="scenario-td"></a>TD | `"$B/temporaldither" 8 2 128 0`; repeat strategy `1/2`, seed `42` as fifth operand; strategy `9` and missing operands | Forty-row demonstration, seeded random sequence and invalid-strategy failure. Invalid depths/values need new validation tests. |
| <a id="scenario-ei"></a>EI | Archived thread-control driver: compile separately against dither_lib, then `thread_control --probe` and `thread_control 1 "$I" --no-config --out "$T/thread.png"`; compare `2` threads | Probe and shifted argv forwarding; matching pixels across thread counts. Not a CMake target. |
| <a id="scenario-em"></a>EM | Archived Markov driver: first repair its stale API call, compile separately, then `draw jobs.txt timings.csv` | Consecutive jobs with same fit key reuse model; changed fit inputs refit; malformed jobs, unreadable sample and failed output fail. Currently blocked by stale source interface, not a runnable production CLI. |

### Test implementation conventions

- **Unit tests should exist** describes assertions to add or preserve. Private helpers should be exercised through the nearest public boundary or a small internal test seam; do not expose implementation details as public API just to test them. Invalid inputs outside a documented precondition need validation/death tests, not an expectation that current code already handles them.
- **Integration tests should exist** references scenario IDs plus an oracle. A command exiting zero is insufficient: read image dimensions, type, palette, decoded light, masks, logs and persistence as appropriate. Error cases must check exit status and absence of misleading artifacts.
- Mask histogram uniformity means uniform **ranks**, with threshold bytes produced by `threshold_byte`: bins 0 and 254 have approximately half the population of an interior bin. Do not assert identical counts for all 255 byte values.
- For stochastic output, pin seeds and verify invariants. Use same-environment pixel identity for replay and thread-count tests; use numeric/statistical tolerances for cross-platform OpenCV changes. Calibration quality belongs in a slower corpus-based suite with independent reference metrics, not a brittle exact-temperature assertion.
- Add compiler coverage (`--coverage`, `gcov`/`gcovr`) and sanitizer jobs separately. The call map below is not proof that a branch was executed. Mutation checks are especially useful for the registry, budget and threshold tests.

### Existing integration checks

`test/run` invokes `coltest`, `unittest`, `tonetest`, `impliedtest`, `devicetest`, `masktest`, inline `scalecheck`, `budgetcheck`, `shellcheck`, `outsizecheck`, plus `flagcheck` and `samplecheck`. These cover density/tone, mask properties, CLI implications, output scaling, flags and sample handling. Tools such as `maskstats`, `maskfit`, `levelfit`, `patchfit` and `grainfit` provide measurements; do not count them as automated assertions unless a runner checks a stated tolerance. The per-function unit-call references below identify existing C++ checks that reach the function directly or transitively.

## Function inventory

## File index

- [app.h](#file-0)
- [bluenoise.cpp](#file-1)
- [bluenoise.h](#file-2)
- [calib.cpp](#file-3)
- [calib.h](#file-4)
- [cli.cpp](#file-5)
- [cli.h](#file-6)
- [defaults.cpp](#file-7)
- [defaults.h](#file-8)
- [dftcalib.cpp](#file-9)
- [dftcalib.h](#file-10)
- [directmask.cpp](#file-11)
- [directmask.h](#file-12)
- [dither.cpp](#file-13)
- [engine.cpp](#file-14)
- [engine.h](#file-15)
- [experiments/inpaint-20260927/thread_control.cpp](#file-16)
- [experiments/markov-20260927/draw.cpp](#file-17)
- [generators.cpp](#file-18)
- [generators.h](#file-19)
- [inpaint.cpp](#file-20)
- [inpaint.h](#file-21)
- [inpaintcalib.cpp](#file-22)
- [inpaintcalib.h](#file-23)
- [main.cpp](#file-24)
- [maketexture.cpp](#file-25)
- [markov.cpp](#file-26)
- [markov.h](#file-27)
- [markovcalib.cpp](#file-28)
- [markovcalib.h](#file-29)
- [markovmodel.h](#file-30)
- [mask.cpp](#file-31)
- [mask.h](#file-32)
- [maskcache.cpp](#file-33)
- [maskcache.h](#file-34)
- [maskparams.h](#file-35)
- [maskutil.cpp](#file-36)
- [maskutil.h](#file-37)
- [options.h](#file-38)
- [samples.cpp](#file-39)
- [samples.h](#file-40)
- [spectralmask.cpp](#file-41)
- [spectralmask.h](#file-42)
- [srgb.cpp](#file-43)
- [srgb.h](#file-44)
- [srgbtest.cpp](#file-45)
- [temporaldither.cpp](#file-46)
- [unittest.cpp](#file-47)

<a id="file-0"></a>

## `app.h`

### Declaration: `dither_main(int argc, char** argv)`

Source: [app.h:8](../app.h#L8).

Callers, CLI variants, unit tests and integration tests: [`dither_main`](#function-096). This is the declaration of that same function; do not count it as separate executed coverage.

<a id="file-1"></a>

## `bluenoise.cpp`

<a id="function-000"></a>

### `makeSparseGaussian` — line 27

Source: [bluenoise.cpp:27](../bluenoise.cpp#L27). Signature: `makeSparseGaussian(double sigma, double eps, double mul)`.

**Callers and how they call it**

- [`build_blue_noise_mask`](#function-003) ([bluenoise.cpp:77](../bluenoise.cpp#L77)): direct C++ call; `makeSparseGaussian(sigma, eps, mul)`.

**CLI invocations / variants:** [B](#scenario-b). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Compare taps against an independent Gaussian formula; epsilon/radius cutoffs include the intended boundary; symmetry, positive weights, and tiny sigma.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [B](#scenario-b) — Read the generated mask and check dimensions, byte range/histogram, wrap statistics and fixed-seed replay; compare blue-eps/sigma variants without relying on exact cross-platform pixels.

<a id="function-001"></a>

### `addKernelWrapped` — line 44

Source: [bluenoise.cpp:44](../bluenoise.cpp#L44). Signature: `addKernelWrapped(cv::Mat& E, const std::vector<Tap>& K, int y, int x)`.

**Callers and how they call it**

- [`build_blue_noise_mask`](#function-003) ([bluenoise.cpp:77](../bluenoise.cpp#L77)): direct C++ call; `addKernelWrapped(E, K, sy, sx)`, `addKernelWrapped(E, K, by, bx)`.

**CLI invocations / variants:** [B](#scenario-b). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Compare one deposit to a slow toroidal convolution on a non-square field; corners, multiple wraps, and overlapping taps.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [B](#scenario-b) — Read the generated mask and check dimensions, byte range/histogram, wrap statistics and fixed-seed replay; compare blue-eps/sigma variants without relying on exact cross-platform pixels.

<a id="function-002"></a>

### `pop_lowest_empty` — line 58

Source: [bluenoise.cpp:58](../bluenoise.cpp#L58). Signature: `pop_lowest_empty(Heap& pq, const cv::Mat& E, const cv::Mat& jitter, const cv::Mat& occupied, int W)`.

**Callers and how they call it**

- [`build_blue_noise_mask`](#function-003) ([bluenoise.cpp:77](../bluenoise.cpp#L77)): direct C++ call; `pop_lowest_empty(pq, E, jitter, occupied, W)`.

**CLI invocations / variants:** [B](#scenario-b). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Skip occupied and stale heap entries; return the lowest current energy; deterministic ties. Establish the nonempty-heap precondition; a deliberately exhausted heap is a validation/death-test candidate, not a supported successful call.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [B](#scenario-b) — Read the generated mask and check dimensions, byte range/histogram, wrap statistics and fixed-seed replay; compare blue-eps/sigma variants without relying on exact cross-platform pixels.

<a id="function-003"></a>

### `build_blue_noise_mask` — line 77

Source: [bluenoise.cpp:77](../bluenoise.cpp#L77). Signature: `build_blue_noise_mask(cv::Size sz, double sigma, double eps, double mul, uint64_t seed, std::ostream* log)`.

**Callers and how they call it**

- [`blue_build`](#function-119) ([generators.cpp:27](../generators.cpp#L27)): direct C++ call; `build_blue_noise_mask(q.size, sigma, eps, radius, q.seed, q.log)`.

**CLI invocations / variants:** [B](#scenario-b). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Fixed-seed repeatability; rectangular and minimum valid sizes; permutation-derived 0..254 histogram; null logging; changing seed changes arrangement.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [B](#scenario-b) — Read the generated mask and check dimensions, byte range/histogram, wrap statistics and fixed-seed replay; compare blue-eps/sigma variants without relying on exact cross-platform pixels.

<a id="function-004"></a>

### `blue_work` — line 156

Source: [bluenoise.cpp:156](../bluenoise.cpp#L156). Signature: `blue_work(int w, int h, double sigma)`.

**Callers and how they call it**

- [`blue_work_of`](#function-118) ([generators.cpp:22](../generators.cpp#L22)): direct C++ call; `blue_work(w, h, r.num(bluenoise::keys::Sigma))`.

**CLI invocations / variants:** [B](#scenario-b). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Positive finite work at valid bounds; area and sigma scaling agree with the stated cost formula.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [B](#scenario-b) — Read the generated mask and check dimensions, byte range/histogram, wrap statistics and fixed-seed replay; compare blue-eps/sigma variants without relying on exact cross-platform pixels.

<a id="function-005"></a>

### `register_bluenoise_params` — line 162

Source: [bluenoise.cpp:162](../bluenoise.cpp#L162). Signature: `register_bluenoise_params(params::Params& P)`.

**Callers and how they call it**

- [`usage_generators`](#function-031) ([cli.cpp:568](../cli.cpp#L568)): indirect: generators() row .register_params(P), once for every generator.

- [`dither_main`](#function-096) ([dither.cpp:105](../dither.cpp#L105)): indirect: generators() row .register_params(P), once for every generator.

- [`list_options`](#function-161) ([maketexture.cpp:102](../maketexture.cpp#L102)): indirect: generators() row .register_params(P), once for every generator.

- [`run`](#function-166) ([maketexture.cpp:239](../maketexture.cpp#L239)): indirect: generators() row .register_params(P), once for every generator.

- [`cache_registry`](#function-345) ([unittest.cpp:981](../unittest.cpp#L981)): indirect: generators() row .register_params(P), once for every generator.

**CLI invocations / variants:** [H](#scenario-h) [E](#scenario-e) [R](#scenario-r) [T](#scenario-t) [U](#scenario-u) (registration); selected-generator scenarios exercise lazy derived rules. IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Register every bluenoise rule once; assert defaults, range endpoints, option visibility and applicability. Resolve derived rules at dependency boundaries and after overrides, check origins/dependency edges, and verify parameter-struct defaults agree. Test each lambda below through the registry.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [H](#scenario-h) [E](#scenario-e) [R](#scenario-r) [T](#scenario-t) [U](#scenario-u) (registration); selected-generator scenarios exercise lazy derived rules — Read the generated mask and check dimensions, byte range/histogram, wrap statistics and fixed-seed replay; compare blue-eps/sigma variants without relying on exact cross-platform pixels.

#### Anonymous function: `register_bluenoise_params::callback@206`

Source: [bluenoise.cpp:206](../bluenoise.cpp#L206); capture/signature: `[](const params::Resolver& r) -> params::Value`.

- **Callers/how:** params::Params::resolve invokes Rule::derive(Resolver) lazily; register_bluenoise_params registers the callback.

- **CLI invocations:** [H](#scenario-h) [E](#scenario-e) [R](#scenario-r) [T](#scenario-t) [U](#scenario-u) (registration); selected-generator scenarios exercise lazy derived rules; same enclosing branch, not an independently named CLI operation.

- **Unit tests should exist:** Resolve this individual rule at each dependency boundary; compare the derived formula/default independently, validate numeric range and dependency invalidation. Vary each read dependency independently: `bluenoise::keys::Eps`.

- **Integration tests should exist:** [H](#scenario-h) [E](#scenario-e) [R](#scenario-r) [T](#scenario-t) [U](#scenario-u) (registration); selected-generator scenarios exercise lazy derived rules; verify the enclosing function's observable assertion above with the callback branch actually reached.

<a id="file-2"></a>

## `bluenoise.h`

### Declaration: `build_blue_noise_mask(cv::Size sz, double sigma, double eps, double mul, uint64_t seed, std::ostream* log)`

Source: [bluenoise.h:13](../bluenoise.h#L13).

Callers, CLI variants, unit tests and integration tests: [`build_blue_noise_mask`](#function-003). This is the declaration of that same function; do not count it as separate executed coverage.

### Declaration: `register_bluenoise_params(params::Params&)`

Source: [bluenoise.h:26](../bluenoise.h#L26).

Callers, CLI variants, unit tests and integration tests: [`register_bluenoise_params`](#function-005). This is the declaration of that same function; do not count it as separate executed coverage.

### Declaration: `blue_work(int w, int h, double sigma)`

Source: [bluenoise.h:31](../bluenoise.h#L31).

Callers, CLI variants, unit tests and integration tests: [`blue_work`](#function-004). This is the declaration of that same function; do not count it as separate executed coverage.

<a id="file-3"></a>

## `calib.cpp`

<a id="function-006"></a>

### `calib::level_set_low` — line 24

Source: [calib.cpp:24](../calib.cpp#L24). Signature: `level_set_low(const cv::Mat& mask8, double g)`.

**Callers and how they call it**

- [`calib::clump_profile`](#function-007) ([calib.cpp:61](../calib.cpp#L61)): direct C++ call; `level_set_low(mask8, kLevels[i])`.

**CLI invocations / variants:** [CM](#scenario-cm) [CP](#scenario-cp). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Constant, checkerboard, and manufactured low-frequency masks; density extremes; independent FFT energy ratio and excluded DC.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [CM](#scenario-cm) [CP](#scenario-cp) — Use constant, periodic and directional fixtures with independently calculated profiles; calibrator decisions must match a reference scorer, including unusable-band fallback.

#### Anonymous function: `calib::level_set_low::callback@33`

Source: [calib.cpp:33](../calib.cpp#L33); capture/signature: `[v](int a, int b)`.

- **Callers/how:** std::sort/stable_sort invokes the comparator supplied by calib::level_set_low.

- **CLI invocations:** [CM](#scenario-cm) [CP](#scenario-cp); same enclosing branch, not an independently named CLI operation.

- **Unit tests should exist:** Strict weak ordering, equal values, stable/deterministic ties as required; sorted output is a permutation.

- **Integration tests should exist:** [CM](#scenario-cm) [CP](#scenario-cp); verify the enclosing function's observable assertion above with the callback branch actually reached.

<a id="function-007"></a>

### `calib::clump_profile` — line 61

Source: [calib.cpp:61](../calib.cpp#L61). Signature: `clump_profile(const cv::Mat& mask8)`.

**Callers and how they call it**

- [`calibrate_inpaint_for`](#function-156) ([inpaintcalib.cpp:107](../inpaintcalib.cpp#L107)): direct C++ call; `clump_profile(ref)`, `clump_profile(m)`.

- [`calibrate_markov`](#function-200) ([markovcalib.cpp:116](../markovcalib.cpp#L116)): call in enclosing function / its lambda callback; `clump_profile(ref)`, `clump_profile(probeMask)`.

**CLI invocations / variants:** [CM](#scenario-cm) [CP](#scenario-cp). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Each configured threshold maps to the corresponding independently computed low-frequency ratio; finite output for flat and sparse masks.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [CM](#scenario-cm) [CP](#scenario-cp) — Use constant, periodic and directional fixtures with independently calculated profiles; calibrator decisions must match a reference scorer, including unusable-band fallback.

<a id="function-008"></a>

### `calib::blur_toroidal` — line 72

Source: [calib.cpp:72](../calib.cpp#L72). Signature: `blur_toroidal(const cv::Mat& f, double sigma)`.

**Callers and how they call it**

- [`calib::ridge_profile`](#function-009) ([calib.cpp:83](../calib.cpp#L83)): direct C++ call; `blur_toroidal(f, 1.4 * (1 << s))`.

**CLI invocations / variants:** [CP](#scenario-cp). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Compare to explicit periodic padding, including corners and sigma larger than the image; constant field remains constant.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [CP](#scenario-cp) — Use constant, periodic and directional fixtures with independently calculated profiles; calibrator decisions must match a reference scorer, including unusable-band fallback.

<a id="function-009"></a>

### `calib::ridge_profile` — line 83

Source: [calib.cpp:83](../calib.cpp#L83). Signature: `ridge_profile(const cv::Mat& mask8)`.

**Callers and how they call it**

- [`calibrate_inpaint_for`](#function-156) ([inpaintcalib.cpp:107](../inpaintcalib.cpp#L107)): direct C++ call; `ridge_profile(ref)`, `ridge_profile(m)`.

**CLI invocations / variants:** [CP](#scenario-cp). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Isotropic noise versus horizontal/vertical stripes; rotation invariance of scalar coherence; flat field, octave indexing, and finite values.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [CP](#scenario-cp) — Use constant, periodic and directional fixtures with independently calculated profiles; calibrator decisions must match a reference scorer, including unusable-band fallback.

<a id="function-010"></a>

### `calib::spectrum_profile` — line 130

Source: [calib.cpp:130](../calib.cpp#L130). Signature: `spectrum_profile(const cv::Mat& mask8, double lowCut)`.

**Callers and how they call it**

- [`calibrate_dft_for`](#function-088) ([dftcalib.cpp:95](../dftcalib.cpp#L95)): direct C++ call; `spectrum_profile(ref, lowCut)`, `spectrum_profile(m, lowCut)`.

**CLI invocations / variants:** [CD](#scenario-cd). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Known sinusoid occupies the expected frequency band; DC exclusion, lowCut boundary, empty usable band, odd and rectangular sizes.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [CD](#scenario-cd) — Use constant, periodic and directional fixtures with independently calculated profiles; calibrator decisions must match a reference scorer, including unusable-band fallback.

<a id="function-011"></a>

### `calib::profile_distance` — line 171

Source: [calib.cpp:171](../calib.cpp#L171). Signature: `profile_distance(const double* a, const double* ref, size_t n)`.

**Callers and how they call it**

- [`calib::clump_distance`](#function-014) ([calib.h:77](../calib.h#L77)): direct C++ call; `profile_distance(a.data(), ref.data(), a.size())`.

- [`calib::ridge_distance`](#function-015) ([calib.h:81](../calib.h#L81)): direct C++ call; `profile_distance(a.data(), ref.data(), a.size())`.

- [`calib::spectrum_distance`](#function-016) ([calib.h:85](../calib.h#L85)): direct C++ call; `profile_distance(a.data(), ref.data(), a.size())`.

**CLI invocations / variants:** [CD](#scenario-cd) [CM](#scenario-cm) [CP](#scenario-cp). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Identical profiles give zero; symmetry and known log-ratio result; zeros, near-zero values, and no valid entries.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [CD](#scenario-cd) [CM](#scenario-cm) [CP](#scenario-cp) — Use constant, periodic and directional fixtures with independently calculated profiles; calibrator decisions must match a reference scorer, including unusable-band fallback.

<a id="function-012"></a>

### `calib::reference_mask` — line 188

Source: [calib.cpp:188](../calib.cpp#L188). Signature: `reference_mask(const cv::Mat& sample, double highpass, double scale, cv::Size sz)`.

**Callers and how they call it**

- [`calibrate_dft_for`](#function-088) ([dftcalib.cpp:95](../dftcalib.cpp#L95)): direct C++ call; `reference_mask(set.primary(), highpass, scale, cv::Size(side, side))`.

- [`calibrate_inpaint_for`](#function-156) ([inpaintcalib.cpp:107](../inpaintcalib.cpp#L107)): direct C++ call; `reference_mask(set.primary(), ip.highpass, ip.scale, probe)`.

- [`reference_mask`](#function-199) ([markovcalib.cpp:65](../markovcalib.cpp#L65)): direct C++ call; `reference_mask(set.primary(), p.highpass, p.scale, sz)`.

**CLI invocations / variants:** [CD](#scenario-cd) [CM](#scenario-cm) [CP](#scenario-cp). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Too-small sample returns empty; exact-size and oversized crops; highpass off/on and scale changes; returned byte range and uniform ranks.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [CD](#scenario-cd) [CM](#scenario-cm) [CP](#scenario-cp) — Use constant, periodic and directional fixtures with independently calculated profiles; calibrator decisions must match a reference scorer, including unusable-band fallback.

<a id="function-013"></a>

### `calib::usable` — line 198

Source: [calib.cpp:198](../calib.cpp#L198). Signature: `usable(const params::Params& P, std::string_view key, double v, bool wholeNumber)`.

**Callers and how they call it**

- [`calibrate_dft_for`](#function-088) ([dftcalib.cpp:95](../dftcalib.cpp#L95)): direct C++ call; `usable(P, SmoothCells, *hit, false)`.

- [`calibrate_inpaint_for`](#function-156) ([inpaintcalib.cpp:107](../inpaintcalib.cpp#L107)): direct C++ call; `usable(P, inpaint::keys::Iters, *hit, true)`.

- [`cached_calibration`](#function-202) ([markovcalib.cpp:355](../markovcalib.cpp#L355)): direct C++ call; `usable(P, markov::keys::Temp, *t, false)`, `usable(P, markov::keys::Radius, *ri, true)`.

**CLI invocations / variants:** [CD](#scenario-cd) [CM](#scenario-cm) [CP](#scenario-cp). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** NaN/infinity rejected; both range endpoints; fractional integer rejected; legitimate doubles accepted; missing range behavior.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [CD](#scenario-cd) [CM](#scenario-cm) [CP](#scenario-cp) — Use constant, periodic and directional fixtures with independently calculated profiles; calibrator decisions must match a reference scorer, including unusable-band fallback.

<a id="file-4"></a>

## `calib.h`

### Declaration: `level_set_low(const cv::Mat& mask8, double g)`

Source: [calib.h:44](../calib.h#L44).

Callers, CLI variants, unit tests and integration tests: [`calib::level_set_low`](#function-006). This is the declaration of that same function; do not count it as separate executed coverage.

### Declaration: `clump_profile(const cv::Mat& mask8)`

Source: [calib.h:47](../calib.h#L47).

Callers, CLI variants, unit tests and integration tests: [`calib::clump_profile`](#function-007). This is the declaration of that same function; do not count it as separate executed coverage.

### Declaration: `ridge_profile(const cv::Mat& mask8)`

Source: [calib.h:57](../calib.h#L57).

Callers, CLI variants, unit tests and integration tests: [`calib::ridge_profile`](#function-009). This is the declaration of that same function; do not count it as separate executed coverage.

### Declaration: `spectrum_profile(const cv::Mat& mask8, double lowCut)`

Source: [calib.h:70](../calib.h#L70).

Callers, CLI variants, unit tests and integration tests: [`calib::spectrum_profile`](#function-010). This is the declaration of that same function; do not count it as separate executed coverage.

### Declaration: `profile_distance(const double* a, const double* ref, size_t n)`

Source: [calib.h:75](../calib.h#L75).

Callers, CLI variants, unit tests and integration tests: [`calib::profile_distance`](#function-011). This is the declaration of that same function; do not count it as separate executed coverage.

### Declaration: `reference_mask(const cv::Mat& sample, double highpass, double scale, cv::Size sz)`

Source: [calib.h:96](../calib.h#L96).

Callers, CLI variants, unit tests and integration tests: [`calib::reference_mask`](#function-012). This is the declaration of that same function; do not count it as separate executed coverage.

### Declaration: `usable(const params::Params& P, std::string_view key, double v, bool wholeNumber)`

Source: [calib.h:103](../calib.h#L103).

Callers, CLI variants, unit tests and integration tests: [`calib::usable`](#function-013). This is the declaration of that same function; do not count it as separate executed coverage.

<a id="function-014"></a>

### `calib::clump_distance` — line 77

Source: [calib.h:77](../calib.h#L77). Signature: `clump_distance(const ClumpProfile& a, const ClumpProfile& ref)`.

**Callers and how they call it**

- [`calibrate_inpaint_for`](#function-156) ([inpaintcalib.cpp:107](../inpaintcalib.cpp#L107)): direct C++ call; `clump_distance(calib::clump_profile(m), refClump)`.

- [`calibrate_markov`](#function-200) ([markovcalib.cpp:116](../markovcalib.cpp#L116)): call in enclosing function / its lambda callback; `clump_distance(clump_profile(probeMask), refProfile)`.

**CLI invocations / variants:** [CM](#scenario-cm) [CP](#scenario-cp). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Forward exactly the clump-profile length; equal and differing profiles agree with an independent log-ratio calculation.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [CM](#scenario-cm) [CP](#scenario-cp) — Run the corresponding calibrator with profiles whose expected winner is known; compare [C](#scenario-c)++ objective to independent analysis with a stated tolerance.

<a id="function-015"></a>

### `calib::ridge_distance` — line 81

Source: [calib.h:81](../calib.h#L81). Signature: `ridge_distance(const RidgeProfile& a, const RidgeProfile& ref)`.

**Callers and how they call it**

- [`calibrate_inpaint_for`](#function-156) ([inpaintcalib.cpp:107](../inpaintcalib.cpp#L107)): direct C++ call; `ridge_distance(calib::ridge_profile(m), refRidge)`.

**CLI invocations / variants:** [CP](#scenario-cp). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Forward exactly the ridge-profile length; zero and directional profiles; no out-of-bounds access.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [CP](#scenario-cp) — Run the corresponding calibrator with profiles whose expected winner is known; compare [C](#scenario-c)++ objective to independent analysis with a stated tolerance.

<a id="function-016"></a>

### `calib::spectrum_distance` — line 85

Source: [calib.h:85](../calib.h#L85). Signature: `spectrum_distance(const SpectrumProfile& a, const SpectrumProfile& ref)`.

**Callers and how they call it**

- [`calibrate_dft_for`](#function-088) ([dftcalib.cpp:95](../dftcalib.cpp#L95)): direct C++ call; `spectrum_distance(calib::spectrum_profile(m, lowCut), refSpec)`.

**CLI invocations / variants:** [CD](#scenario-cd). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Forward exactly the spectrum-profile length; invalid/empty-band behavior matches profile_distance's contract.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [CD](#scenario-cd) — Run the corresponding calibrator with profiles whose expected winner is known; compare [C](#scenario-c)++ objective to independent analysis with a stated tolerance.

<a id="file-5"></a>

## `cli.cpp`

<a id="function-017"></a>

### `write_image` — line 24

Source: [cli.cpp:24](../cli.cpp#L24). Signature: `write_image(const std::string& path, const cv::Mat& img)`.

**Callers and how they call it**

- [`write_outputs`](#function-095) ([dither.cpp:92](../dither.cpp#L92)): direct C++ call; `write_image(opt.outPath, vision::average_gray(frames, opt.useSrgb))`, `write_image((path.parent_path() / (stem + "-" + std::to_string(k) + ".png")).string(), frames[k])`.

- [`dither_main`](#function-096) ([dither.cpp:105](../dither.cpp#L105)): direct C++ call; `write_image(opt.maskOut, mask)`.

- [`run`](#function-166) ([maketexture.cpp:239](../maketexture.cpp#L239)): direct C++ call; `write_image(r.out, outImg)`, `write_image(r.paste, merged)`.

**CLI invocations / variants:** [R](#scenario-r) [O](#scenario-o) [B](#scenario-b) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [T](#scenario-t) [X](#scenario-x). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Successful write, false-return failure, and thrown codec error all produce the documented boolean/diagnostic; use an isolated filesystem fixture.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [R](#scenario-r) [O](#scenario-o) [B](#scenario-b) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [T](#scenario-t) [X](#scenario-x) — Check exit status, stdout/stderr and side effects for the named CLI route; missing/bad arguments must fail before output, accepted options must affect the intended behavior.

<a id="function-018"></a>

### `parse_pair` — line 46

Source: [cli.cpp:46](../cli.cpp#L46). Signature: `parse_pair(const std::string& v, char delim, int lo, int& a, int& b)`.

**Callers and how they call it**

- [`parse_size`](#function-019) ([cli.cpp:59](../cli.cpp#L59)): direct C++ call; `parse_pair(v, 'x', 1, w, h)`.

- [`parse_pixel`](#function-020) ([cli.cpp:65](../cli.cpp#L65)): direct C++ call; `parse_pair(v, ',', 0, row, col)`.

**CLI invocations / variants:** [H](#scenario-h) [E](#scenario-e) [R](#scenario-r) [O](#scenario-o) [X](#scenario-x). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Valid separator pair; missing/duplicate separators, trailing text, signed values, minimum, and integer overflow; no partial successful parse.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [H](#scenario-h) [E](#scenario-e) [R](#scenario-r) [O](#scenario-o) [X](#scenario-x) — Check exit status, stdout/stderr and side effects for the named CLI route; missing/bad arguments must fail before output, accepted options must affect the intended behavior.

<a id="function-019"></a>

### `parse_size` — line 59

Source: [cli.cpp:59](../cli.cpp#L59). Signature: `parse_size(const std::string& v, int& w, int& h)`.

**Callers and how they call it**

- [`parseOptions`](#function-027) ([cli.cpp:178](../cli.cpp#L178)): direct C++ call; `parse_size(need(), w, h)`.

**CLI invocations / variants:** [B](#scenario-b) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [X](#scenario-x). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** WxH accepted at lower bounds; zero, negative, fractional, overflow, missing dimension, and extra suffix rejected.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [B](#scenario-b) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [X](#scenario-x) — Check exit status, stdout/stderr and side effects for the named CLI route; missing/bad arguments must fail before output, accepted options must affect the intended behavior.

<a id="function-020"></a>

### `parse_pixel` — line 65

Source: [cli.cpp:65](../cli.cpp#L65). Signature: `parse_pixel(const std::string& v, int& row, int& col)`.

**Callers and how they call it**

- [`parseOptions`](#function-027) ([cli.cpp:178](../cli.cpp#L178)): direct C++ call; `parse_pixel(need(), opt.debug_row, opt.debug_col)`.

**CLI invocations / variants:** [O](#scenario-o) [X](#scenario-x). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Row,column order, zero coordinates, malformed separator, overflow, and negative coordinates.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [O](#scenario-o) [X](#scenario-x) — Check exit status, stdout/stderr and side effects for the named CLI route; missing/bad arguments must fail before output, accepted options must affect the intended behavior.

<a id="function-021"></a>

### `parse_budget_seconds` — line 71

Source: [cli.cpp:71](../cli.cpp#L71). Signature: `parse_budget_seconds(std::string v, double& out)`.

**Callers and how they call it**

- [`parseOptions`](#function-027) ([cli.cpp:178](../cli.cpp#L178)): direct C++ call; `parse_budget_seconds(need(), secs)`.

**CLI invocations / variants:** [V](#scenario-v) [X](#scenario-x). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Seconds and supported suffixes; fractional seconds; full-string consumption; NaN/infinity, zero/negative and overflow.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [V](#scenario-v) [X](#scenario-x) — Check exit status, stdout/stderr and side effects for the named CLI route; missing/bad arguments must fail before output, accepted options must affect the intended behavior.

<a id="function-022"></a>

### `report_reconciliation` — line 85

Source: [cli.cpp:85](../cli.cpp#L85). Signature: `report_reconciliation(const params::Reconciliation& rec)`.

**Callers and how they call it**

- [`parseOptions`](#function-027) ([cli.cpp:178](../cli.cpp#L178)): direct C++ call; `report_reconciliation(P.reconcile())`.

**CLI invocations / variants:** [E](#scenario-e) [X](#scenario-x). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Empty report is silent; changed defaults and rescued options are accurately named once; stable ordering.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [E](#scenario-e) [X](#scenario-x) — Check exit status, stdout/stderr and side effects for the named CLI route; missing/bad arguments must fail before output, accepted options must affect the intended behavior.

<a id="function-023"></a>

### `fill_options_from_params` — line 107

Source: [cli.cpp:107](../cli.cpp#L107). Signature: `fill_options_from_params(Options& opt, params::Params& P)`.

**Callers and how they call it**

- [`parseOptions`](#function-027) ([cli.cpp:178](../cli.cpp#L178)): direct C++ call; `fill_options_from_params(opt, P)`.

**CLI invocations / variants:** [H](#scenario-h) [E](#scenario-e) [R](#scenario-r) [O](#scenario-o) [X](#scenario-x). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Every cached Options field equals the resolved registry, including --set overrides and implied defaults.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [H](#scenario-h) [E](#scenario-e) [R](#scenario-r) [O](#scenario-o) [X](#scenario-x) — Check exit status, stdout/stderr and side effects for the named CLI route; missing/bad arguments must fail before output, accepted options must affect the intended behavior.

<a id="function-024"></a>

### `report_conflicts` — line 125

Source: [cli.cpp:125](../cli.cpp#L125). Signature: `report_conflicts(params::Params& P)`.

**Callers and how they call it**

- [`parseOptions`](#function-027) ([cli.cpp:178](../cli.cpp#L178)): direct C++ call; `report_conflicts(P)`.

**CLI invocations / variants:** [E](#scenario-e) [X](#scenario-x). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** No conflicts returns true; pinned incompatible settings return false with the option and required context; multiple conflicts retained.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [E](#scenario-e) [X](#scenario-x) — Check exit status, stdout/stderr and side effects for the named CLI route; missing/bad arguments must fail before output, accepted options must affect the intended behavior.

<a id="function-025"></a>

### `option_takes_value` — line 157

Source: [cli.cpp:157](../cli.cpp#L157). Signature: `option_takes_value(const params::Params& P, const std::string& flag)`.

**Callers and how they call it**

- [`dither_main`](#function-096) ([dither.cpp:105](../dither.cpp#L105)): direct C++ call; `option_takes_value(P, a)`.

**CLI invocations / variants:** [E](#scenario-e) [C](#scenario-c) [X](#scenario-x). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Generated numeric/string flags consume a token; boolean switches do not; explicit legacy flags and unknown flags; agreement with parseOptions.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [E](#scenario-e) [C](#scenario-c) [X](#scenario-x) — Check exit status, stdout/stderr and side effects for the named CLI route; missing/bad arguments must fail before output, accepted options must affect the intended behavior.

<a id="function-026"></a>

### `first_option_index` — line 173

Source: [cli.cpp:173](../cli.cpp#L173). Signature: `first_option_index(int argc, char** argv)`.

**Callers and how they call it**

- [`parseOptions`](#function-027) ([cli.cpp:178](../cli.cpp#L178)): direct C++ call; `first_option_index(argc, argv)`.

- [`dither_main`](#function-096) ([dither.cpp:105](../dither.cpp#L105)): direct C++ call; `first_option_index(argc, argv)`.

**CLI invocations / variants:** [H](#scenario-h) [E](#scenario-e) [R](#scenario-r) [O](#scenario-o) [X](#scenario-x). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** No arguments, image first, option first, and filenames beginning with a dash follow the documented grammar.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [H](#scenario-h) [E](#scenario-e) [R](#scenario-r) [O](#scenario-o) [X](#scenario-x) — Check exit status, stdout/stderr and side effects for the named CLI route; missing/bad arguments must fail before output, accepted options must affect the intended behavior.

<a id="function-027"></a>

### `parseOptions` — line 178

Source: [cli.cpp:178](../cli.cpp#L178). Signature: `parseOptions(int argc, char** argv, Options& opt, params::Params& P)`.

**Callers and how they call it**

- [`dither_main`](#function-096) ([dither.cpp:105](../dither.cpp#L105)): direct C++ call; `parseOptions(argc, argv, opt, P)`.

**CLI invocations / variants:** [H](#scenario-h) [E](#scenario-e) [R](#scenario-r) [O](#scenario-o) [X](#scenario-x). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** All explicit/generated flags and --set equivalents; precedence and repeated flags; missing values, ranges and bad types; threshold alias; reconciliation and conflicts; spatial default depends on threshold origin. Run exit-producing help/list branches in subprocesses.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [H](#scenario-h) [E](#scenario-e) [R](#scenario-r) [O](#scenario-o) [X](#scenario-x) — Check exit status, stdout/stderr and side effects for the named CLI route; missing/bad arguments must fail before output, accepted options must affect the intended behavior.

#### Anonymous function: `parseOptions::set_param@188`

Source: [cli.cpp:188](../cli.cpp#L188); capture/signature: `[&](std::string_view key, params::Value v)`.

- **Callers/how:** parseOptions and its local helper callbacks invoke set_param while consuming options.

- **CLI invocations:** [H](#scenario-h) [E](#scenario-e) [R](#scenario-r) [O](#scenario-o) [X](#scenario-x); same enclosing branch, not an independently named CLI operation.

- **Unit tests should exist:** Valid/missing/invalid values, complete token consumption, error flag propagation and range/type/precedence behavior; use subprocess for exit branches.

- **Integration tests should exist:** [H](#scenario-h) [E](#scenario-e) [R](#scenario-r) [O](#scenario-o) [X](#scenario-x); verify the enclosing function's observable assertion above with the callback branch actually reached.

#### Anonymous function: `parseOptions::set_typed@198`

Source: [cli.cpp:198](../cli.cpp#L198); capture/signature: `[&](const std::string& key, const std::string& val)`.

- **Callers/how:** parseOptions and its local helper callbacks invoke set_typed while consuming options.

- **CLI invocations:** [H](#scenario-h) [E](#scenario-e) [R](#scenario-r) [O](#scenario-o) [X](#scenario-x); same enclosing branch, not an independently named CLI operation.

- **Unit tests should exist:** Valid/missing/invalid values, complete token consumption, error flag propagation and range/type/precedence behavior; use subprocess for exit branches.

- **Integration tests should exist:** [H](#scenario-h) [E](#scenario-e) [R](#scenario-r) [O](#scenario-o) [X](#scenario-x); verify the enclosing function's observable assertion above with the callback branch actually reached.

#### Anonymous function: `parseOptions::need@209`

Source: [cli.cpp:209](../cli.cpp#L209); capture/signature: `[&]() -> std::string`.

- **Callers/how:** parseOptions and its local helper callbacks invoke need while consuming options.

- **CLI invocations:** [H](#scenario-h) [E](#scenario-e) [R](#scenario-r) [O](#scenario-o) [X](#scenario-x); same enclosing branch, not an independently named CLI operation.

- **Unit tests should exist:** Valid/missing/invalid values, complete token consumption, error flag propagation and range/type/precedence behavior; use subprocess for exit branches.

- **Integration tests should exist:** [H](#scenario-h) [E](#scenario-e) [R](#scenario-r) [O](#scenario-o) [X](#scenario-x); verify the enclosing function's observable assertion above with the callback branch actually reached.

#### Anonymous function: `parseOptions::need_int@226`

Source: [cli.cpp:226](../cli.cpp#L226); capture/signature: `[&](long long lo, long long hi) -> long long`.

- **Callers/how:** parseOptions and its local helper callbacks invoke need_int while consuming options.

- **CLI invocations:** [H](#scenario-h) [E](#scenario-e) [R](#scenario-r) [O](#scenario-o) [X](#scenario-x); same enclosing branch, not an independently named CLI operation.

- **Unit tests should exist:** Valid/missing/invalid values, complete token consumption, error flag propagation and range/type/precedence behavior; use subprocess for exit branches.

- **Integration tests should exist:** [H](#scenario-h) [E](#scenario-e) [R](#scenario-r) [O](#scenario-o) [X](#scenario-x); verify the enclosing function's observable assertion above with the callback branch actually reached.

#### Anonymous function: `parseOptions::need_num@242`

Source: [cli.cpp:242](../cli.cpp#L242); capture/signature: `[&](double lo, double hi) -> double`.

- **Callers/how:** parseOptions and its local helper callbacks invoke need_num while consuming options.

- **CLI invocations:** [H](#scenario-h) [E](#scenario-e) [R](#scenario-r) [O](#scenario-o) [X](#scenario-x); same enclosing branch, not an independently named CLI operation.

- **Unit tests should exist:** Valid/missing/invalid values, complete token consumption, error flag propagation and range/type/precedence behavior; use subprocess for exit branches.

- **Integration tests should exist:** [H](#scenario-h) [E](#scenario-e) [R](#scenario-r) [O](#scenario-o) [X](#scenario-x); verify the enclosing function's observable assertion above with the callback branch actually reached.

<a id="function-028"></a>

### `usage_brief` — line 463

Source: [cli.cpp:463](../cli.cpp#L463). Signature: `usage_brief()`.

**Callers and how they call it**

- [`dither_main`](#function-096) ([dither.cpp:105](../dither.cpp#L105)): direct C++ call; `usage_brief()`.

**CLI invocations / variants:** [H](#scenario-h) [X](#scenario-x). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Brief help identifies input/output and a usable example; no contradiction with actual defaults.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [H](#scenario-h) [X](#scenario-x) — Check exit status, stdout/stderr and side effects for the named CLI route; missing/bad arguments must fail before output, accepted options must affect the intended behavior.

<a id="function-029"></a>

### `is_shared_family` — line 516

Source: [cli.cpp:516](../cli.cpp#L516). Signature: `is_shared_family(const std::string& key)`.

**Callers and how they call it**

- [`usage_generators`](#function-031) ([cli.cpp:568](../cli.cpp#L568)): direct C++ call; `is_shared_family(key)`.

**CLI invocations / variants:** [H](#scenario-h). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Every shared namespace classified correctly; generator-specific and unknown keys do not enter the shared section.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [H](#scenario-h) — Check exit status, stdout/stderr and side effects for the named CLI route; missing/bad arguments must fail before output, accepted options must affect the intended behavior.

<a id="function-030"></a>

### `usage_ceilings` — line 525

Source: [cli.cpp:525](../cli.cpp#L525). Signature: `usage_ceilings(params::Params& P)`.

**Callers and how they call it**

- [`usage_generators`](#function-031) ([cli.cpp:568](../cli.cpp#L568)): direct C++ call; `usage_ceilings(P)`.

**CLI invocations / variants:** [H](#scenario-h). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Every generator's max_size/max_repeats appears once with the registry value; adding a synthetic generator exposes missing enumeration.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [H](#scenario-h) — Check exit status, stdout/stderr and side effects for the named CLI route; missing/bad arguments must fail before output, accepted options must affect the intended behavior.

<a id="function-031"></a>

### `usage_generators` — line 568

Source: [cli.cpp:568](../cli.cpp#L568). Signature: `usage_generators()`.

**Callers and how they call it**

- [`usage`](#function-032) ([cli.cpp:620](../cli.cpp#L620)): direct C++ call; `usage_generators()`.

**CLI invocations / variants:** [H](#scenario-h). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Names, defaults, switches and prose are consistent with the generator table and registered flags.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [H](#scenario-h) — Check exit status, stdout/stderr and side effects for the named CLI route; missing/bad arguments must fail before output, accepted options must affect the intended behavior.

<a id="function-032"></a>

### `usage` — line 620

Source: [cli.cpp:620](../cli.cpp#L620). Signature: `usage()`.

**Callers and how they call it**

- [`parseOptions`](#function-027) ([cli.cpp:178](../cli.cpp#L178)): direct C++ call; `usage()`.

- [`dither_main`](#function-096) ([dither.cpp:105](../dither.cpp#L105)): direct C++ call; `usage()`.

**CLI invocations / variants:** [H](#scenario-h). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Full help includes accepted flags and valid examples; redirect/capture stdout and assert stable semantic content rather than spacing.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [H](#scenario-h) — Check exit status, stdout/stderr and side effects for the named CLI route; missing/bad arguments must fail before output, accepted options must affect the intended behavior.

<a id="function-033"></a>

### `config_path` — line 787

Source: [cli.cpp:787](../cli.cpp#L787). Signature: `config_path()`.

**Callers and how they call it**

- [`dither_main`](#function-096) ([dither.cpp:105](../dither.cpp#L105)): direct C++ call; `config_path()`.

**CLI invocations / variants:** [H](#scenario-h) [E](#scenario-e) [R](#scenario-r) [O](#scenario-o) [X](#scenario-x). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** XDG_CONFIG_HOME precedence, HOME fallback, both unset, empty variables and paths containing spaces.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [H](#scenario-h) [E](#scenario-e) [R](#scenario-r) [O](#scenario-o) [X](#scenario-x) — Check exit status, stdout/stderr and side effects for the named CLI route; missing/bad arguments must fail before output, accepted options must affect the intended behavior.

<a id="file-6"></a>

## `cli.h`

### Declaration: `parseOptions(int argc, char** argv, Options& opt, params::Params& P)`

Source: [cli.h:11](../cli.h#L11).

Callers, CLI variants, unit tests and integration tests: [`parseOptions`](#function-027). This is the declaration of that same function; do not count it as separate executed coverage.

### Declaration: `usage()`

Source: [cli.h:13](../cli.h#L13).

Callers, CLI variants, unit tests and integration tests: [`usage`](#function-032). This is the declaration of that same function; do not count it as separate executed coverage.

### Declaration: `usage_brief()`

Source: [cli.h:14](../cli.h#L14).

Callers, CLI variants, unit tests and integration tests: [`usage_brief`](#function-028). This is the declaration of that same function; do not count it as separate executed coverage.

### Declaration: `config_path()`

Source: [cli.h:17](../cli.h#L17).

Callers, CLI variants, unit tests and integration tests: [`config_path`](#function-033). This is the declaration of that same function; do not count it as separate executed coverage.

### Declaration: `option_takes_value(const params::Params& P, const std::string& flag)`

Source: [cli.h:21](../cli.h#L21).

Callers, CLI variants, unit tests and integration tests: [`option_takes_value`](#function-025). This is the declaration of that same function; do not count it as separate executed coverage.

### Declaration: `first_option_index(int argc, char** argv)`

Source: [cli.h:22](../cli.h#L22).

Callers, CLI variants, unit tests and integration tests: [`first_option_index`](#function-026). This is the declaration of that same function; do not count it as separate executed coverage.

### Declaration: `write_image(const std::string& path, const cv::Mat& img)`

Source: [cli.h:26](../cli.h#L26).

Callers, CLI variants, unit tests and integration tests: [`write_image`](#function-017). This is the declaration of that same function; do not count it as separate executed coverage.

<a id="file-7"></a>

## `defaults.cpp`

<a id="function-034"></a>

### `params::parse_double` — line 20

Source: [defaults.cpp:20](../defaults.cpp#L20). Signature: `parse_double(const std::string& s)`.

**Callers and how they call it**

- [`parse_budget_seconds`](#function-021) ([cli.cpp:71](../cli.cpp#L71)): direct C++ call; `parse_double(v)`.

- [`parseOptions`](#function-027) ([cli.cpp:178](../cli.cpp#L178)): call in enclosing function / its lambda callback; `parse_double(val)`, `parse_double(v)`.

- [`params::Params::load_config`](#function-082) ([defaults.cpp:611](../defaults.cpp#L611)): direct C++ call; `parse_double(val)`.

- [`set_typed`](#function-164) ([maketexture.cpp:157](../maketexture.cpp#L157)): direct C++ call; `parse_double(val)`.

**CLI invocations / variants:** [E](#scenario-e) [C](#scenario-c) [V](#scenario-v) [X](#scenario-x). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Whole token consumed; decimal/exponent/sign, empty/junk, NaN/infinity, underflow/overflow and locale behavior.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [E](#scenario-e) [C](#scenario-c) [V](#scenario-v) [X](#scenario-x) — Exercise equivalent explicit flags and --set, implied/pinned conflicts, and isolated config round-trips; assert value/origin reports and persisted measurements.

<a id="function-035"></a>

### `params::parse_integer` — line 30

Source: [defaults.cpp:30](../defaults.cpp#L30). Signature: `parse_integer(const std::string& s)`.

**Callers and how they call it**

- [`parse_pair`](#function-018) ([cli.cpp:46](../cli.cpp#L46)): direct C++ call; `parse_integer(v.substr(0, at))`, `parse_integer(v.substr(at + 1))`.

- [`parseOptions`](#function-027) ([cli.cpp:178](../cli.cpp#L178)): call in enclosing function / its lambda callback; `parse_integer(val)`.

- [`checked_dim`](#function-162) ([maketexture.cpp:114](../maketexture.cpp#L114)): direct C++ call; `parse_integer(v)`.

- [`parse_rect`](#function-163) ([maketexture.cpp:126](../maketexture.cpp#L126)): direct C++ call; `parse_integer(off.substr(0, p2))`, `parse_integer(off.substr(p2 + 1))`.

- [`set_typed`](#function-164) ([maketexture.cpp:157](../maketexture.cpp#L157)): direct C++ call; `parse_integer(val)`.

- [`parse`](#function-165) ([maketexture.cpp:171](../maketexture.cpp#L171)): direct C++ call; `parse_integer(next(a))`.

**CLI invocations / variants:** [E](#scenario-e) [C](#scenario-c) [V](#scenario-v) [X](#scenario-x). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Both long-long bounds; overflow, decimal/exponent, empty, sign-only and trailing junk rejected appropriately.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [E](#scenario-e) [C](#scenario-c) [V](#scenario-v) [X](#scenario-x) — Exercise equivalent explicit flags and --set, implied/pinned conflicts, and isolated config round-trips; assert value/origin reports and persisted measurements.

<a id="function-036"></a>

### `params::origin_name` — line 40

Source: [defaults.cpp:40](../defaults.cpp#L40). Signature: `origin_name(Origin o)`.

**Callers and how they call it**

- [`params::Params::explain`](#function-080) ([defaults.cpp:538](../defaults.cpp#L538)): direct C++ call; `origin_name(s.origin)`.

**CLI invocations / variants:** [E](#scenario-e) [C](#scenario-c) [V](#scenario-v) [X](#scenario-x). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Every enum maps to the intended label; invalid enum fallback is explicitly defined.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [E](#scenario-e) [C](#scenario-c) [V](#scenario-v) [X](#scenario-x) — Exercise equivalent explicit flags and --set, implied/pinned conflicts, and isolated config round-trips; assert value/origin reports and persisted measurements.

<a id="function-037"></a>

### `params::as_number` — line 56

Source: [defaults.cpp:56](../defaults.cpp#L56). Signature: `as_number(const Value& v)`.

**Callers and how they call it**

- [`params::Params::check_range`](#function-060) ([defaults.cpp:228](../defaults.cpp#L228)): direct C++ call; `as_number(v)`.

- [`params::Params::num`](#function-064) ([defaults.cpp:304](../defaults.cpp#L304)): direct C++ call; `as_number(resolve(key))`.

- [`params::Params::integer`](#function-065) ([defaults.cpp:310](../defaults.cpp#L310)): direct C++ call; `as_number(v)`.

**CLI invocations / variants:** [E](#scenario-e) [C](#scenario-c) [V](#scenario-v) [X](#scenario-x). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Double and integer variants convert correctly; string returns no value; large integer precision is accounted for.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [E](#scenario-e) [C](#scenario-c) [V](#scenario-v) [X](#scenario-x) — Exercise equivalent explicit flags and --set, implied/pinned conflicts, and isolated config round-trips; assert value/origin reports and persisted measurements.

<a id="function-038"></a>

### `params::not_a_number` — line 63

Source: [defaults.cpp:63](../defaults.cpp#L63). Signature: `not_a_number(std::string_view key)`.

**Callers and how they call it**

- [`params::Params::num`](#function-064) ([defaults.cpp:304](../defaults.cpp#L304)): direct C++ call; `not_a_number(key)`.

- [`params::Params::integer`](#function-065) ([defaults.cpp:310](../defaults.cpp#L310)): direct C++ call; `not_a_number(key)`.

**CLI invocations / variants:** [E](#scenario-e) [C](#scenario-c) [V](#scenario-v) [X](#scenario-x). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Throw the expected exception and identify the key; no silent numeric fallback.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [E](#scenario-e) [C](#scenario-c) [V](#scenario-v) [X](#scenario-x) — Exercise equivalent explicit flags and --set, implied/pinned conflicts, and isolated config round-trips; assert value/origin reports and persisted measurements.

<a id="function-039"></a>

### `params::StackFrame::StackFrame` — line 72

Source: [defaults.cpp:72](../defaults.cpp#L72). Signature: `StackFrame(std::vector<std::string>& s, std::string key) : stack(s)`.

**Callers and how they call it**

- [`params::Params::resolve`](#function-063) ([defaults.cpp:260](../defaults.cpp#L260)): direct C++ call; receiver/signature resolved by compiler.

- [`params::Params::applicability_inputs`](#function-078) ([defaults.cpp:507](../defaults.cpp#L507)): direct C++ call; receiver/signature resolved by compiler.

**CLI invocations / variants:** [E](#scenario-e) [C](#scenario-c) [V](#scenario-v) [X](#scenario-x). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Push the key once; nested resolution records the correct stack order.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [E](#scenario-e) [C](#scenario-c) [V](#scenario-v) [X](#scenario-x) — Exercise equivalent explicit flags and --set, implied/pinned conflicts, and isolated config round-trips; assert value/origin reports and persisted measurements.

<a id="function-040"></a>

### `params::StackFrame::~StackFrame` — line 74

Source: [defaults.cpp:74](../defaults.cpp#L74). Signature: `~StackFrame()`.

**Callers and how they call it**

- [`params::Params::resolve`](#function-063) ([defaults.cpp:260](../defaults.cpp#L260)): direct C++ call; receiver/signature resolved by compiler.

- [`params::Params::applicability_inputs`](#function-078) ([defaults.cpp:507](../defaults.cpp#L507)): direct C++ call; receiver/signature resolved by compiler.

**CLI invocations / variants:** [E](#scenario-e) [C](#scenario-c) [V](#scenario-v) [X](#scenario-x). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Pop on normal exit and exception unwinding; outer stack is preserved; exercise through cyclic and failing rule resolution.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [E](#scenario-e) [C](#scenario-c) [V](#scenario-v) [X](#scenario-x) — Exercise equivalent explicit flags and --set, implied/pinned conflicts, and isolated config round-trips; assert value/origin reports and persisted measurements.

<a id="function-362"></a>

### `params::StackFrame::StackFrame(copy)` — line 76

Source: [defaults.cpp:76](../defaults.cpp#L76). Signature: `StackFrame(const StackFrame&) = delete`.

**Callers and how they call it**

- No legal runtime caller for a deleted function, or no linkable implementation for the stale declaration; see unit/API test below.

**CLI invocations / variants:** none: compile-time/API test only. IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Compile-time assertions that the guard is neither copy-constructible nor copy-assignable; no runtime invocation is legal.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** none: compile-time/API test only — Compile/link API checks rather than runtime integration; deleted guard operations must fail compilation, and the declared calibrate_markov overload needs an implementation or corrected interface.

<a id="function-363"></a>

### `params::StackFrame::operator=` — line 77

Source: [defaults.cpp:77](../defaults.cpp#L77). Signature: `StackFrame& operator=(const StackFrame&) = delete`.

**Callers and how they call it**

- No legal runtime caller for a deleted function, or no linkable implementation for the stale declaration; see unit/API test below.

**CLI invocations / variants:** none: compile-time/API test only. IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Compile-time assertions that the guard is neither copy-constructible nor copy-assignable; no runtime invocation is legal.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** none: compile-time/API test only — Compile/link API checks rather than runtime integration; deleted guard operations must fail compilation, and the declared calibrate_markov overload needs an implementation or corrected interface.

<a id="function-041"></a>

### `params::trim` — line 82

Source: [defaults.cpp:82](../defaults.cpp#L82). Signature: `trim(const std::string& t)`.

**Callers and how they call it**

- [`params::Params::load_config`](#function-082) ([defaults.cpp:611](../defaults.cpp#L611)): direct C++ call; `trim(line.substr(0, eq))`, `trim(line.substr(eq + 1))`.

**CLI invocations / variants:** [E](#scenario-e) [C](#scenario-c) [V](#scenario-v) [X](#scenario-x). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Leading/trailing spaces and tabs, all whitespace, empty strings and interior whitespace.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [E](#scenario-e) [C](#scenario-c) [V](#scenario-v) [X](#scenario-x) — Exercise equivalent explicit flags and --set, implied/pinned conflicts, and isolated config round-trips; assert value/origin reports and persisted measurements.

<a id="function-042"></a>

### `params::format` — line 91

Source: [defaults.cpp:91](../defaults.cpp#L91). Signature: `format(const Value& v)`.

**Callers and how they call it**

- [`params::Params::value_of`](#function-044) ([defaults.cpp:136](../defaults.cpp#L136)): direct C++ call; `format(resolve(key))`.

- [`params::Params::str`](#function-066) ([defaults.cpp:343](../defaults.cpp#L343)): direct C++ call; `format(v)`.

- [`params::Params::reconcile`](#function-077) ([defaults.cpp:446](../defaults.cpp#L446)): direct C++ call; `format(movable[i].options[best[i] - 1])`.

- [`params::Params::explain`](#function-080) ([defaults.cpp:538](../defaults.cpp#L538)): direct C++ call; `format(resolve(k))`, `format(s.v)`.

**CLI invocations / variants:** [E](#scenario-e) [C](#scenario-c) [V](#scenario-v) [X](#scenario-x). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Each Value alternative; readable numeric precision; special characters in strings; locale independence where required.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [E](#scenario-e) [C](#scenario-c) [V](#scenario-v) [X](#scenario-x) — Exercise equivalent explicit flags and --set, implied/pinned conflicts, and isolated config round-trips; assert value/origin reports and persisted measurements.

<a id="function-043"></a>

### `params::serialize` — line 123

Source: [defaults.cpp:123](../defaults.cpp#L123). Signature: `serialize(const Value& v)`.

**Callers and how they call it**

- [`params::Params::serialized_of`](#function-045) ([defaults.cpp:137](../defaults.cpp#L137)): direct C++ call; `serialize(resolve(key))`.

- [`params::Params::save_config`](#function-083) ([defaults.cpp:667](../defaults.cpp#L667)): direct C++ call; `serialize(it->second.v)`, `serialize(v)`.

**CLI invocations / variants:** [E](#scenario-e) [C](#scenario-c) [V](#scenario-v) [X](#scenario-x). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Double round-trip precision; distinct nearby doubles differ; explicit type tags distinguish string/integer/double; stable encoding.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [E](#scenario-e) [C](#scenario-c) [V](#scenario-v) [X](#scenario-x) — Exercise equivalent explicit flags and --set, implied/pinned conflicts, and isolated config round-trips; assert value/origin reports and persisted measurements.

<a id="function-044"></a>

### `params::Params::value_of` — line 136

Source: [defaults.cpp:136](../defaults.cpp#L136). Signature: `Params::value_of(std::string_view key)`.

**Callers and how they call it**

- [`usage_ceilings`](#function-030) ([cli.cpp:525](../cli.cpp#L525)): direct C++ call; `value_of(key)`.

- [`usage_generators`](#function-031) ([cli.cpp:568](../cli.cpp#L568)): direct C++ call; `value_of(key)`.

- [`maskcache::key_for`](#function-222) ([maskcache.cpp:116](../maskcache.cpp#L116)): direct C++ call; `value_of(key)`, `value_of(mask::keys::Method)`, `value_of(mask::keys::SizeW)`, `value_of(mask::keys::SizeH)`, `value_of(mask::keys::Highpass)`, `value_of(mask::keys::Scale)`, `value_of(mask::keys::Seed)`.

- [`case_1080`](#function-348) ([unittest.cpp:1080](../unittest.cpp#L1080)): direct C++ call; `value_of(bluenoise::keys::Sigma)`.

- [`case_1145`](#function-350) ([unittest.cpp:1145](../unittest.cpp#L1145)): call in enclosing function / its lambda callback; `value_of(key)`.

- [`case_1235`](#function-351) ([unittest.cpp:1235](../unittest.cpp#L1235)): direct C++ call; `value_of(key)`.

**CLI invocations / variants:** [E](#scenario-e) [C](#scenario-c) [V](#scenario-v) [X](#scenario-x). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Resolve lazily and render readable values; unknown key error and all Value alternatives.

**Existing direct C++ test callers:** [`case_1080`](#function-348), [`case_1145`](#function-350), [`case_1235`](#function-351). These do not establish full branch coverage.

**Integration tests should exist:** [E](#scenario-e) [C](#scenario-c) [V](#scenario-v) [X](#scenario-x) — Exercise equivalent explicit flags and --set, implied/pinned conflicts, and isolated config round-trips; assert value/origin reports and persisted measurements.

<a id="function-045"></a>

### `params::Params::serialized_of` — line 137

Source: [defaults.cpp:137](../defaults.cpp#L137). Signature: `Params::serialized_of(std::string_view key)`.

**Callers and how they call it**

- [`maskcache::key_for`](#function-222) ([maskcache.cpp:116](../maskcache.cpp#L116)): direct C++ call; `serialized_of(key)`.

- [`case_1080`](#function-348) ([unittest.cpp:1080](../unittest.cpp#L1080)): direct C++ call; `serialized_of(bluenoise::keys::Rate)`.

**CLI invocations / variants:** [E](#scenario-e) [C](#scenario-c) [V](#scenario-v) [X](#scenario-x). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Resolve and preserve exact typed identity; adjacent floating-point values and numeric-looking strings remain distinct.

**Existing direct C++ test callers:** [`case_1080`](#function-348). These do not establish full branch coverage.

**Integration tests should exist:** [E](#scenario-e) [C](#scenario-c) [V](#scenario-v) [X](#scenario-x) — Exercise equivalent explicit flags and --set, implied/pinned conflicts, and isolated config round-trips; assert value/origin reports and persisted measurements.

<a id="function-046"></a>

### `params::Params::flag_for` — line 139

Source: [defaults.cpp:139](../defaults.cpp#L139). Signature: `Params::flag_for(std::string_view key)`.

**Callers and how they call it**

- [`parseOptions`](#function-027) ([cli.cpp:178](../cli.cpp#L178)): direct C++ call; `flag_for(key)`.

- [`usage_generators`](#function-031) ([cli.cpp:568](../cli.cpp#L568)): direct C++ call; `flag_for(key)`.

- [`list_options`](#function-161) ([maketexture.cpp:102](../maketexture.cpp#L102)): direct C++ call; `flag_for(k)`.

**CLI invocations / variants:** [E](#scenario-e) [C](#scenario-c) [V](#scenario-v) [X](#scenario-x). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Namespace separator and underscores map to hyphens; supported keys round-trip with key_for_flag.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [E](#scenario-e) [C](#scenario-c) [V](#scenario-v) [X](#scenario-x) — Exercise equivalent explicit flags and --set, implied/pinned conflicts, and isolated config round-trips; assert value/origin reports and persisted measurements.

<a id="function-047"></a>

### `params::Params::key_for_flag` — line 151

Source: [defaults.cpp:151](../defaults.cpp#L151). Signature: `Params::key_for_flag(std::string_view flag)`.

**Callers and how they call it**

- [`option_takes_value`](#function-025) ([cli.cpp:157](../cli.cpp#L157)): direct C++ call; `key_for_flag(flag)`.

- [`parseOptions`](#function-027) ([cli.cpp:178](../cli.cpp#L178)): direct C++ call; `key_for_flag(k)`.

- [`parse`](#function-165) ([maketexture.cpp:171](../maketexture.cpp#L171)): direct C++ call; `key_for_flag(a)`.

**CLI invocations / variants:** [E](#scenario-e) [C](#scenario-c) [V](#scenario-v) [X](#scenario-x). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Known generated spellings, missing prefix, multiple hyphens and invalid input; round-trip all registered options.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [E](#scenario-e) [C](#scenario-c) [V](#scenario-v) [X](#scenario-x) — Exercise equivalent explicit flags and --set, implied/pinned conflicts, and isolated config round-trips; assert value/origin reports and persisted measurements.

<a id="function-048"></a>

### `params::Params::hide_option` — line 162

Source: [defaults.cpp:162](../defaults.cpp#L162). Signature: `Params::hide_option(std::string_view key)`.

**Callers and how they call it**

- [`register_bluenoise_params`](#function-005) ([bluenoise.cpp:162](../bluenoise.cpp#L162)): direct C++ call; `hide_option(bluenoise::keys::Rate)`.

- [`register_direct_params`](#function-091) ([directmask.cpp:220](../directmask.cpp#L220)): direct C++ call; `hide_option(direct::keys::Rate)`.

- [`register_inpaint_params`](#function-154) ([inpaint.cpp:480](../inpaint.cpp#L480)): direct C++ call; `hide_option(inpaint::keys::Rate)`.

- [`register_markov_params`](#function-198) ([markov.cpp:1005](../markov.cpp#L1005)): direct C++ call; `hide_option(markov::keys::Rate)`.

- [`register_mask_params`](#function-209) ([mask.cpp:160](../mask.cpp#L160)): direct C++ call; `hide_option(k)`.

- [`register_spectral_params`](#function-283) ([spectralmask.cpp:243](../spectralmask.cpp#L243)): direct C++ call; `hide_option(spectralmask::keys::Rate)`.

**CLI invocations / variants:** [E](#scenario-e) [C](#scenario-c) [V](#scenario-v) [X](#scenario-x). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Hide a registered option without changing its value or accessibility via the library; unknown key behavior.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [E](#scenario-e) [C](#scenario-c) [V](#scenario-v) [X](#scenario-x) — Exercise equivalent explicit flags and --set, implied/pinned conflicts, and isolated config round-trips; assert value/origin reports and persisted measurements.

<a id="function-049"></a>

### `params::Params::range_of` — line 168

Source: [defaults.cpp:168](../defaults.cpp#L168). Signature: `Params::range_of(std::string_view key) const`.

**Callers and how they call it**

- [`calib::usable`](#function-013) ([calib.cpp:198](../calib.cpp#L198)): direct C++ call; `range_of(key)`.

**CLI invocations / variants:** [E](#scenario-e) [C](#scenario-c) [V](#scenario-v) [X](#scenario-x). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Return declared endpoints or no range, without triggering derivation; unknown key.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [E](#scenario-e) [C](#scenario-c) [V](#scenario-v) [X](#scenario-x) — Exercise equivalent explicit flags and --set, implied/pinned conflicts, and isolated config round-trips; assert value/origin reports and persisted measurements.

<a id="function-050"></a>

### `params::Params::has_option` — line 174

Source: [defaults.cpp:174](../defaults.cpp#L174). Signature: `Params::has_option(std::string_view key) const`.

**Callers and how they call it**

- [`option_takes_value`](#function-025) ([cli.cpp:157](../cli.cpp#L157)): direct C++ call; `has_option(key)`.

- [`parseOptions`](#function-027) ([cli.cpp:178](../cli.cpp#L178)): direct C++ call; `has_option(key)`.

- [`usage_ceilings`](#function-030) ([cli.cpp:525](../cli.cpp#L525)): direct C++ call; `has_option(key)`.

- [`parse`](#function-165) ([maketexture.cpp:171](../maketexture.cpp#L171)): direct C++ call; `has_option(key)`.

**CLI invocations / variants:** [E](#scenario-e) [C](#scenario-c) [V](#scenario-v) [X](#scenario-x). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Visible, hidden, unknown and value-only keys; querying must not resolve values.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [E](#scenario-e) [C](#scenario-c) [V](#scenario-v) [X](#scenario-x) — Exercise equivalent explicit flags and --set, implied/pinned conflicts, and isolated config round-trips; assert value/origin reports and persisted measurements.

<a id="function-051"></a>

### `params::Params::doc` — line 181

Source: [defaults.cpp:181](../defaults.cpp#L181). Signature: `Params::doc(std::string_view key) const`.

**Callers and how they call it**

- [`usage_generators`](#function-031) ([cli.cpp:568](../cli.cpp#L568)): direct C++ call; `doc(key)`.

**CLI invocations / variants:** [E](#scenario-e) [C](#scenario-c) [V](#scenario-v) [X](#scenario-x). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Exact registered description, unknown key behavior, and no resolution side effects.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [E](#scenario-e) [C](#scenario-c) [V](#scenario-v) [X](#scenario-x) — Exercise equivalent explicit flags and --set, implied/pinned conflicts, and isolated config round-trips; assert value/origin reports and persisted measurements.

<a id="function-052"></a>

### `params::Params::options` — line 187

Source: [defaults.cpp:187](../defaults.cpp#L187). Signature: `Params::options() const`.

**Callers and how they call it**

- [`parseOptions`](#function-027) ([cli.cpp:178](../cli.cpp#L178)): direct C++ call; `options()`.

- [`usage_generators`](#function-031) ([cli.cpp:568](../cli.cpp#L568)): direct C++ call; `options()`.

- [`list_options`](#function-161) ([maketexture.cpp:102](../maketexture.cpp#L102)): direct C++ call; `options()`.

**CLI invocations / variants:** [E](#scenario-e) [C](#scenario-c) [V](#scenario-v) [X](#scenario-x). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** All and only exposed keys, deterministic order, no duplicates, including newly registered rules.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [E](#scenario-e) [C](#scenario-c) [V](#scenario-v) [X](#scenario-x) — Exercise equivalent explicit flags and --set, implied/pinned conflicts, and isolated config round-trips; assert value/origin reports and persisted measurements.

<a id="function-053"></a>

### `params::Params::is_switch` — line 195

Source: [defaults.cpp:195](../defaults.cpp#L195). Signature: `Params::is_switch(std::string_view key) const`.

**Callers and how they call it**

- [`option_takes_value`](#function-025) ([cli.cpp:157](../cli.cpp#L157)): direct C++ call; `is_switch(key)`.

- [`parseOptions`](#function-027) ([cli.cpp:178](../cli.cpp#L178)): direct C++ call; `is_switch(key)`.

- [`usage_generators`](#function-031) ([cli.cpp:568](../cli.cpp#L568)): direct C++ call; `is_switch(key)`.

- [`parse`](#function-165) ([maketexture.cpp:171](../maketexture.cpp#L171)): direct C++ call; `is_switch(key)`.

**CLI invocations / variants:** [E](#scenario-e) [C](#scenario-c) [V](#scenario-v) [X](#scenario-x). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Exactly the 0..1 range is a switch; wider, absent and nonnumeric ranges are not.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [E](#scenario-e) [C](#scenario-c) [V](#scenario-v) [X](#scenario-x) — Exercise equivalent explicit flags and --set, implied/pinned conflicts, and isolated config round-trips; assert value/origin reports and persisted measurements.

<a id="function-054"></a>

### `params::Params::intermediate` — line 202

Source: [defaults.cpp:202](../defaults.cpp#L202). Signature: `Params::intermediate(std::string_view key) const`.

**Callers and how they call it**

- [`maskcache::key_for`](#function-222) ([maskcache.cpp:116](../maskcache.cpp#L116)): direct C++ call; `intermediate(key)`.

- [`case_1145`](#function-350) ([unittest.cpp:1145](../unittest.cpp#L1145)): direct C++ call; `intermediate(key)`.

- [`case_1235`](#function-351) ([unittest.cpp:1235](../unittest.cpp#L1235)): direct C++ call; `intermediate(key)`.

- [`case_1292`](#function-352) ([unittest.cpp:1292](../unittest.cpp#L1292)): direct C++ call; `intermediate(key)`.

**CLI invocations / variants:** [E](#scenario-e) [C](#scenario-c) [V](#scenario-v) [X](#scenario-x). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Declared intermediate true/false; unknown/value-only keys default to false; querying has no side effects.

**Existing direct C++ test callers:** [`case_1145`](#function-350), [`case_1235`](#function-351), [`case_1292`](#function-352). These do not establish full branch coverage.

**Integration tests should exist:** [E](#scenario-e) [C](#scenario-c) [V](#scenario-v) [X](#scenario-x) — Exercise equivalent explicit flags and --set, implied/pinned conflicts, and isolated config round-trips; assert value/origin reports and persisted measurements.

<a id="function-055"></a>

### `params::Resolver::num` — line 208

Source: [defaults.cpp:208](../defaults.cpp#L208). Signature: `Resolver::num(std::string_view key) const`.

**Callers and how they call it**

- [`register_bluenoise_params`](#function-005) ([bluenoise.cpp:162](../bluenoise.cpp#L162)): call in enclosing function / its lambda callback; `num(bluenoise::keys::Eps)`.

- [`register_direct_params`](#function-091) ([directmask.cpp:220](../directmask.cpp#L220)): call in enclosing function / its lambda callback; `num(mask::keys::Highpass)`, `num(mask::keys::Scale)`.

- [`blue_work_of`](#function-118) ([generators.cpp:22](../generators.cpp#L22)): direct C++ call; `num(bluenoise::keys::Sigma)`.

- [`direct_sample_limit`](#function-128) ([generators.cpp:124](../generators.cpp#L124)): direct C++ call; `num(mask::keys::Scale)`.

- [`register_markov_params`](#function-198) ([markov.cpp:1005](../markov.cpp#L1005)): call in enclosing function / its lambda callback; `num(mask::keys::Budget)`, `num(Rate)`.

- [`mask_seconds`](#function-207) ([mask.cpp:43](../mask.cpp#L43)): direct C++ call; `num(method + ".rate")`.

- [`mask_side`](#function-208) ([mask.cpp:99](../mask.cpp#L99)): direct C++ call; `num(mask::keys::Highpass)`, `num(mask::keys::Scale)`, `num(mask::keys::Budget)`.

**CLI invocations / variants:** [E](#scenario-e) [C](#scenario-c) [V](#scenario-v) [X](#scenario-x). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Forward to its Params and record numeric rule dependencies, including nested resolution and errors.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [E](#scenario-e) [C](#scenario-c) [V](#scenario-v) [X](#scenario-x) — Exercise equivalent explicit flags and --set, implied/pinned conflicts, and isolated config round-trips; assert value/origin reports and persisted measurements.

<a id="function-056"></a>

### `params::Resolver::integer` — line 209

Source: [defaults.cpp:209](../defaults.cpp#L209). Signature: `Resolver::integer(std::string_view key) const`.

**Callers and how they call it**

- [`dft_work_of`](#function-120) ([generators.cpp:53](../generators.cpp#L53)): direct C++ call; `integer(spectralmask::keys::Iters)`.

- [`markov_work_of`](#function-122) ([generators.cpp:70](../generators.cpp#L70)): direct C++ call; `integer(markov::keys::Pairs)`, `integer(markov::keys::Iters)`.

- [`inpaint_work_of`](#function-124) ([generators.cpp:88](../generators.cpp#L88)): direct C++ call; `integer(inpaint::keys::Iters)`, `integer(inpaint::keys::Db)`, `integer(inpaint::keys::Dims)`, `integer(inpaint::keys::Scales)`, `integer(inpaint::keys::Checks)`.

- [`direct_work_of`](#function-127) ([generators.cpp:114](../generators.cpp#L114)): direct C++ call; `integer(direct::keys::Overlap)`.

- [`direct_sample_limit`](#function-128) ([generators.cpp:124](../generators.cpp#L124)): direct C++ call; `integer(direct::keys::Overlap)`.

- [`register_markov_params`](#function-198) ([markov.cpp:1005](../markov.cpp#L1005)): call in enclosing function / its lambda callback; `integer(Pairs)`, `integer(Shortlist)`, `integer(mask::keys::SizeW)`, `integer(mask::keys::SizeH)`, `integer(Iters)`, `integer(Bins)`.

- [`mask_side`](#function-208) ([mask.cpp:99](../mask.cpp#L99)): direct C++ call; `integer(mask::keys::Supersample)`, `integer(mask::keys::ImageW)`, `integer(mask::keys::ImageH)`, `integer(method + ".max_repeats")`, `integer(method + ".max_size")`, `integer(mask::keys::SampleW)`, `integer(mask::keys::SampleH)`.

**CLI invocations / variants:** [E](#scenario-e) [C](#scenario-c) [V](#scenario-v) [X](#scenario-x). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Forward integral values and preserve integer validation/dependency recording.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [E](#scenario-e) [C](#scenario-c) [V](#scenario-v) [X](#scenario-x) — Exercise equivalent explicit flags and --set, implied/pinned conflicts, and isolated config round-trips; assert value/origin reports and persisted measurements.

<a id="function-057"></a>

### `params::Resolver::str` — line 210

Source: [defaults.cpp:210](../defaults.cpp#L210). Signature: `Resolver::str(std::string_view key) const`.

**Callers and how they call it**

- [`mask::calibratable`](#function-132) ([generators.cpp:187](../generators.cpp#L187)): call in enclosing function / its lambda callback; `str(mask::keys::Method)`.

- [`mask_side`](#function-208) ([mask.cpp:99](../mask.cpp#L99)): direct C++ call; `str(mask::keys::Method)`.

- [`register_mask_params`](#function-209) ([mask.cpp:160](../mask.cpp#L160)): call in enclosing function / its lambda callback; `str(mask::keys::Sample)`.

- [`mask::building`](#function-227) ([maskparams.h:37](../maskparams.h#L37)): direct C++ call; `str(keys::Threshold)`, `str(keys::In)`.

- [`mask::thresholding`](#function-228) ([maskparams.h:47](../maskparams.h#L47)): call in enclosing function / its lambda callback; `str(keys::Threshold)`.

- [`mask::generator`](#function-229) ([maskparams.h:57](../maskparams.h#L57)): call in enclosing function / its lambda callback; `str(keys::Method)`.

- [`mask::from_sample`](#function-230) ([maskparams.h:77](../maskparams.h#L77)): call in enclosing function / its lambda callback; `str(keys::Method)`.

**CLI invocations / variants:** [E](#scenario-e) [C](#scenario-c) [V](#scenario-v) [X](#scenario-x). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Forward string values and preserve type errors and dependency recording.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [E](#scenario-e) [C](#scenario-c) [V](#scenario-v) [X](#scenario-x) — Exercise equivalent explicit flags and --set, implied/pinned conflicts, and isolated config round-trips; assert value/origin reports and persisted measurements.

<a id="function-058"></a>

### `params::add_const` — line 212

Source: [defaults.cpp:212](../defaults.cpp#L212). Signature: `add_const(Params& P, std::string key, Value v, Applicability applies, std::optional<std::pair<double, double>> range, std::string doc, bool intermediate)`.

**Callers and how they call it**

- [`register_bluenoise_params`](#function-005) ([bluenoise.cpp:162](../bluenoise.cpp#L162)): direct C++ call; `add_const(P, bluenoise::keys::Rate, 3.4e6, mine, {{1.0, 1e15}}, "measured build throughput, work units per second", true)`, `add_const(P, bluenoise::keys::Sigma, 1.8, mine, {{0.01, 1000.0}}, "repulsion kernel radius in pixels: how far dots push each other apart")`, `add_const(P, bluenoise::keys::MaxRepeats, (long long)64, mine, {{1, 1024}}, "tile repeats the grid may show before it looks regular")`, `add_const(P, bluenoise::keys::MaxSize, (long long)256, mine, {{16, 8192}}, "largest tile worth building, absent a --budget")`, `add_const(P, bluenoise::keys::Eps, 1e-4, mine, {{1e-12, 1.0}}, "sparse-kernel cutoff: lighter taps are dropped")`.

- [`register_direct_params`](#function-091) ([directmask.cpp:220](../directmask.cpp#L220)): direct C++ call; `add_const(P, direct::keys::Rate, 1.6e8, mine, {{1.0, 1e15}}, "measured build throughput, work units per second", true)`, `add_const(P, direct::keys::MaxRepeats, (long long)4, mine, {{1, 1024}}, "tile repeats the grid may show before it looks regular")`, `add_const(P, direct::keys::MaxSize, (long long)1024, mine, {{16, 8192}}, "largest tile worth building, absent a --budget")`, `add_const(P, direct::keys::Quantise, 0.9, mine, {{0.0, 16.0}}, "round the tile to this grid before ranking, in input codes")`.

- [`register_inpaint_params`](#function-154) ([inpaint.cpp:480](../inpaint.cpp#L480)): direct C++ call; `add_const(P, Radius, (long long)inpaint::fallback::Radius, mine, {{1, 8}}, "neighbourhood half-width; 4 and 5 both measure worse")`, `add_const(P, Iters, (long long)inpaint::fallback::Iters, mine, {{1, 64}}, "passes over the field, per level")`, `add_const(P, Db, (long long)inpaint::fallback::Db, mine, {{1000, 4000000}}, "sample neighbourhoods to match against; the binding constraint")`, `add_const(P, Dims, (long long)inpaint::fallback::Dims, mine, {{2, 64}}, "PCA dimensions the match is made in")`, `add_const(P, Checks, (long long)inpaint::fallback::Checks, mine, {{1, 100000}}, "kd-tree leaves visited per query")`, `add_const(P, Groups, (long long)inpaint::fallback::Groups, mine, {{1, 256}}, "in-place passes a sweep is split into; 1 is all-at-once and worse")`, `add_const(P, Scales, (long long)inpaint::fallback::Scales, mine, {{1, 5}}, "coarse-to-fine resolutions, so a small window still reaches coarse structure")`, `add_const(P, Cap, inpaint::fallback::Cap, mine, {{0.0, 100.0}}, "stop sweeping once the field is this much more directional " "than the sample; 0 never stops early")`, `add_const(P, MaxSize, (long long)256, mine, {{8, 2048}}, "largest tile this generator will build unasked")`, `add_const(P, MaxRepeats, (long long)4, mine, {{1, 1024}}, "how often its tile may repeat before that shows")`, `add_const(P, Rate, 6.0e7, mine, {{1.0, 1e15}}, "work units per second, measured from real builds", true)`.

- [`register_markov_params`](#function-198) ([markov.cpp:1005](../markov.cpp#L1005)): direct C++ call; `add_const(P, mask::keys::Calibrate, (long long)0, mask::calibratable(), {{0, 1}}, "measure this generator's settings for the sample instead of " "deriving them")`, `add_const(P, Rate, 4.8e7, mine, {{1.0, 1e15}}, "measured build throughput, work units per second", true)`, `add_const(P, Radius, (long long)5, mine, {{1, 32}}, "half-width of the neighbourhood pairs are drawn from")`, `add_const(P, Weights, std::string("flat"), mine, {}, "how each pair's vote is weighted: flat, mi or ml (flat measures best)")`, `add_const(P, Report, (long long)0, mine, {{0, 1}}, "print the fitting diagnostic")`, `add_const(P, MaxRepeats, (long long)4, mine, {{1, 1024}}, "tile repeats the grid may show before it looks regular")`, `add_const(P, MaxSize, (long long)256, mine, {{16, 8192}}, "largest tile worth building, absent a --budget")`, `add_const(P, Bins, (long long)16, mine, {{2, 32}}, "conditioning bins per neighbour; a pair's table is bins x bins cells")`, `add_const(P, Iters, (long long)2, mine, {{1, 500}}, "Gibbs sweeps over the field")`.

- [`register_mask_params`](#function-209) ([mask.cpp:160](../mask.cpp#L160)): direct C++ call; `add_const(P, mask::keys::Scale, 1.0, mask::from_sample(), {{0.05, 20.0}}, "feature size relative to the sample")`, `add_const(P, mask::keys::Highpass, 32.0, mask::from_sample(), {{0.0, 100000.0}}, "ignore sample structure coarser than this, in pixels")`, `add_const(P, mask::keys::Sample, std::string(), mask::from_sample(), {}, "the texture to imitate")`, `add_const(P, mask::keys::In, std::string(), mask::thresholding(), {}, "use this image as the mask instead of building one")`, `add_const(P, mask::keys::ImageW, (long long)512, {}, {{1, 1000000}}, "input width (nominal until an image is read)", true)`, `add_const(P, mask::keys::ImageH, (long long)512, {}, {{1, 1000000}}, "input height (nominal until an image is read)", true)`, `add_const(P, mask::keys::Budget, 0.0, {}, {{0.0, 86400.0}}, "seconds to spend building the mask; 0 uses fixed ceilings", true)`, `add_const(P, mask::keys::Supersample, (long long)1, {}, {{1, 16}}, "dither at this multiple of the output resolution", true)`, `add_const(P, mask::keys::OutSize, (long long)0, {}, {{0, 65536}}, "fit the output inside a square of this side; 0 keeps the input's size", true)`, `add_const(P, mask::keys::SampleW, (long long)0, mask::from_sample(), {{0, 1000000}}, "sample width (0 until the sample is read)", true)`, `add_const(P, mask::keys::SampleH, (long long)0, mask::from_sample(), {{0, 1000000}}, "sample height (0 until the sample is read)", true)`, `add_const(P, mask::keys::Seed, (long long)42, {}, {{0, 9000000000000000LL}}, "random seed for masks and stochastic thresholds")`.

- [`register_spectral_params`](#function-283) ([spectralmask.cpp:243](../spectralmask.cpp#L243)): direct C++ call; `add_const(P, spectralmask::keys::Rate, 1.2e8, mine, {{1.0, 1e15}}, "measured build throughput, work units per second", true)`, `add_const(P, spectralmask::keys::SmoothCells, 4.0, mine, {{0.0, 256.0}}, "frequency cells the sample's periodogram is averaged over")`, `add_const(P, spectralmask::keys::MaxRepeats, (long long)4, mine, {{1, 1024}}, "tile repeats the grid may show before it looks regular")`, `add_const(P, spectralmask::keys::MaxSize, (long long)1024, mine, {{16, 8192}}, "largest tile worth building, absent a --budget")`, `add_const(P, spectralmask::keys::Iters, (long long)20, mine, {{1, 500}}, "alternating-projection rounds; quality plateaus here")`.

**CLI invocations / variants:** [E](#scenario-e) [C](#scenario-c) [V](#scenario-v) [X](#scenario-x). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Register the literal with range, applicability, documentation and intermediate metadata; constant resolves as Fallback.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [E](#scenario-e) [C](#scenario-c) [V](#scenario-v) [X](#scenario-x) — Exercise equivalent explicit flags and --set, implied/pinned conflicts, and isolated config round-trips; assert value/origin reports and persisted measurements.

#### Anonymous function: `params::add_const::callback@217`

Source: [defaults.cpp:217](../defaults.cpp#L217); capture/signature: `[v = std::move(v)](const Resolver&)`.

- **Callers/how:** params::Params::resolve invokes Rule::derive(Resolver) lazily; params::add_const registers the callback.

- **CLI invocations:** [E](#scenario-e) [C](#scenario-c) [V](#scenario-v) [X](#scenario-x); same enclosing branch, not an independently named CLI operation.

- **Unit tests should exist:** Resolve this individual rule at each dependency boundary; compare the derived formula/default independently, validate numeric range and dependency invalidation.

- **Integration tests should exist:** [E](#scenario-e) [C](#scenario-c) [V](#scenario-v) [X](#scenario-x); verify the enclosing function's observable assertion above with the callback branch actually reached.

<a id="function-059"></a>

### `params::Params::add` — line 221

Source: [defaults.cpp:221](../defaults.cpp#L221). Signature: `Params::add(Rule r)`.

**Callers and how they call it**

- [`register_bluenoise_params`](#function-005) ([bluenoise.cpp:162](../bluenoise.cpp#L162)): direct C++ call; `add({ bluenoise::keys::Radius, [](const params::Resolver& r) -> params::Value { return std::sqrt(-2.0 * std::log(r.num(bluenoise::keys::Eps))); }, mine, {{0.0, 100.0}}, "kernel half-width in sigma ...`.

- [`params::add_const`](#function-058) ([defaults.cpp:212](../defaults.cpp#L212)): direct C++ call; `add({ std::move(key), [v = std::move(v)](const Resolver&) { return v; }, std::move(applies), range, std::move(doc), {}, intermediate })`.

- [`register_direct_params`](#function-091) ([directmask.cpp:220](../directmask.cpp#L220)): direct C++ call; `add({ direct::keys::Overlap, [](const params::Resolver& r) -> params::Value { const double sigma = maskutil::kHighpassSigmaPerPixel * r.num(mask::keys::Highpass) * r.num(mask::keys::Scale); return ...`.

- [`register_markov_params`](#function-198) ([markov.cpp:1005](../markov.cpp#L1005)): direct C++ call; `add({ Reuse, [](const params::Resolver& r) -> params::Value { const double pairs = (double)r.integer(Pairs); const double shortlist = (double)r.integer(Shortlist); return kReuseBudget * shortlist /...`, `add({ Shortlist, [](const params::Resolver& r) -> params::Value { const double pairs = (double)r.integer(Pairs); return (long long)std::ceil( (1.0 + std::sqrt(1.0 + 8.0 * kPoolFactor * pairs)) / 2....`, `add({ Pairs, [](const params::Resolver& r) -> params::Value { const double budget = r.num(mask::keys::Budget); if (budget <= 0) return (long long)64; const double w = (double)r.integer(mask::keys::...`, `add({ Temp, [](const params::Resolver& r) -> params::Value { const double bins = (double)r.integer(Bins); const double pairs = (double)r.integer(Pairs); const double iters = (double)r.integer(Iters...`.

- [`register_mask_params`](#function-209) ([mask.cpp:160](../mask.cpp#L160)): direct C++ call; `add({ mask::keys::SizeW, [](const params::Resolver& r) -> params::Value { return mask_side(r); }, mask::any_generator(), {{8, 16384}}, "mask width" })`, `add({ mask::keys::SizeH, [](const params::Resolver& r) -> params::Value { return mask_side(r); }, mask::any_generator(), {{8, 16384}}, "mask height" })`, `add({ mask::keys::Threshold, [](const params::Resolver&) -> params::Value { return std::string("round"); }, {}, {}, "how each pixel picks its level", { std::string("mask") } })`, `add({ mask::keys::Method, [](const params::Resolver& r) -> params::Value { return r.str(mask::keys::Sample).empty() ? std::string(kDefaultGenerator) : std::string(kDefaultSampleGenerator); }, mask:...`.

**CLI invocations / variants:** [E](#scenario-e) [C](#scenario-c) [V](#scenario-v) [X](#scenario-x). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Register new and duplicate keys according to policy; preserve range/candidates/visibility and invalidate stale derived state appropriately.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [E](#scenario-e) [C](#scenario-c) [V](#scenario-v) [X](#scenario-x) — Exercise equivalent explicit flags and --set, implied/pinned conflicts, and isolated config round-trips; assert value/origin reports and persisted measurements.

<a id="function-060"></a>

### `params::Params::check_range` — line 228

Source: [defaults.cpp:228](../defaults.cpp#L228). Signature: `Params::check_range(std::string_view key, const Value& v) const`.

**Callers and how they call it**

- [`params::Params::set`](#function-061) ([defaults.cpp:245](../defaults.cpp#L245)): direct C++ call; `check_range(key, v)`.

- [`params::Params::resolve`](#function-063) ([defaults.cpp:260](../defaults.cpp#L260)): direct C++ call; `check_range(key, v)`.

**CLI invocations / variants:** [E](#scenario-e) [C](#scenario-c) [V](#scenario-v) [X](#scenario-x). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Numeric inclusive boundaries; nonnumeric values for numeric rules; nonfinite and overflow cases; no range.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [E](#scenario-e) [C](#scenario-c) [V](#scenario-v) [X](#scenario-x) — Exercise equivalent explicit flags and --set, implied/pinned conflicts, and isolated config round-trips; assert value/origin reports and persisted measurements.

<a id="function-061"></a>

### `params::Params::set` — line 245

Source: [defaults.cpp:245](../defaults.cpp#L245). Signature: `Params::set(std::string_view key, Value v, Origin o)`.

**Callers and how they call it**

- [`parseOptions`](#function-027) ([cli.cpp:178](../cli.cpp#L178)): call in enclosing function / its lambda callback; `set(key, std::move(v), params::Origin::User)`, `set(mask::keys::Threshold, t, P.origin_of(mask::keys::Threshold))`.

- [`params::Params::apply_trial`](#function-076) ([defaults.cpp:437](../defaults.cpp#L437)): direct C++ call; `set(movable[i].key, movable[i].options[pick[i] - 1], Origin::Implied)`.

- [`params::Params::load_config`](#function-082) ([defaults.cpp:611](../defaults.cpp#L611)): direct C++ call; `set(key, *d, Origin::Measured)`.

- [`calibrate_dft_for`](#function-088) ([dftcalib.cpp:95](../dftcalib.cpp#L95)): direct C++ call; `set(SmoothCells, *hit, params::Origin::Measured)`, `set(SmoothCells, kLadder[best], params::Origin::Measured)`.

- [`dither_main`](#function-096) ([dither.cpp:105](../dither.cpp#L105)): direct C++ call; `set(mask::keys::ImageW, (long long)image.cols, params::Origin::Measured)`, `set(mask::keys::ImageH, (long long)image.rows, params::Origin::Measured)`, `set(mask::keys::SampleW, (long long)one.cols, params::Origin::Measured)`, `set(mask::keys::SampleH, (long long)one.rows, params::Origin::Measured)`.

- [`calibrate_inpaint_for`](#function-156) ([inpaintcalib.cpp:107](../inpaintcalib.cpp#L107)): direct C++ call; `set(inpaint::keys::Iters, v, params::Origin::Measured)`, `set(inpaint::keys::Iters, (long long)kLadder[best], params::Origin::Measured)`.

- [`set_typed`](#function-164) ([maketexture.cpp:157](../maketexture.cpp#L157)): direct C++ call; `set(key, *n, params::Origin::User)`, `set(key, *d, params::Origin::User)`, `set(key, val, params::Origin::User)`.

- [`parse`](#function-165) ([maketexture.cpp:171](../maketexture.cpp#L171)): direct C++ call; `set(mask::keys::Highpass, 0.0, params::Origin::User)`, `set(mask::keys::Threshold, std::string("mask"), params::Origin::User)`, `set(mask::keys::Method, r.method, params::Origin::User)`.

- [`apply`](#function-203) ([markovcalib.cpp:394](../markovcalib.cpp#L394)): direct C++ call; `set(markov::keys::Temp, c.temp, params::Origin::Measured)`, `set(markov::keys::Weights, c.weights, params::Origin::Measured)`, `set(markov::keys::Radius, (long long)c.radius, params::Origin::Measured)`.

- [`record_build_rate`](#function-212) ([mask.cpp:355](../mask.cpp#L355)): direct C++ call; `set(opt.maskMethod + ".rate", work / secs, params::Origin::Measured)`.

- [`case_990`](#function-346) ([unittest.cpp:990](../unittest.cpp#L990)): direct C++ call; `set(mask::keys::Threshold, std::string("mask"), params::Origin::User)`, `set(mask::keys::Method, std::string(g.name), params::Origin::User)`, `set(mask::keys::Sample, std::string("x.png"), params::Origin::User)`, `set(mask::keys::ImageW, (long long)256, params::Origin::Measured)`, `set(mask::keys::ImageH, (long long)256, params::Origin::Measured)`, `set(mask::keys::Supersample, (long long)ss, params::Origin::User)`, `set(mask::keys::SampleW, (long long)dims.first, params::Origin::Measured)`, `set(mask::keys::SampleH, (long long)dims.second, params::Origin::Measured)`.

- [`case_1044`](#function-347) ([unittest.cpp:1044](../unittest.cpp#L1044)): direct C++ call; `set(mask::keys::Supersample, 2.9, params::Origin::User)`, `set(mask::keys::Supersample, v, params::Origin::Derived)`, `set(mask::keys::Seed, big, params::Origin::User)`.

- [`case_1080`](#function-348) ([unittest.cpp:1080](../unittest.cpp#L1080)): direct C++ call; `set(mask::keys::Threshold, std::string("mask"), params::Origin::User)`, `set(mask::keys::Method, std::string("blue"), params::Origin::User)`, `set(bluenoise::keys::Sigma, base, params::Origin::User)`, `set(bluenoise::keys::Sigma, base + step, params::Origin::User)`, `set(bluenoise::keys::Rate, v, params::Origin::Measured)`.

- [`configured_registry`](#function-349) ([unittest.cpp:1134](../unittest.cpp#L1134)): direct C++ call; `set(mask::keys::Threshold, std::string("mask"), params::Origin::User)`, `set(mask::keys::Method, std::string(method), params::Origin::User)`, `set(mask::keys::Sample, sample, params::Origin::User)`.

- [`case_1145`](#function-350) ([unittest.cpp:1145](../unittest.cpp#L1145)): call in enclosing function / its lambda callback; `set(key, std::move(v), params::Origin::User)`.

- [`case_1235`](#function-351) ([unittest.cpp:1235](../unittest.cpp#L1235)): call in enclosing function / its lambda callback; `set(mask::keys::Threshold, std::string("mask"), params::Origin::User)`, `set(mask::keys::Method, std::string("blue"), params::Origin::User)`, `set(key, now, params::Origin::User)`.

- [`case_1292`](#function-352) ([unittest.cpp:1292](../unittest.cpp#L1292)): direct C++ call; `set(mask::keys::Threshold, std::string("mask"), params::Origin::User)`, `set(mask::keys::Method, std::string("markov"), params::Origin::User)`, `set(mask::keys::Sample, sample, params::Origin::User)`, `set(key, P.num(key) * 2.0, params::Origin::Measured)`.

**CLI invocations / variants:** [E](#scenario-e) [C](#scenario-c) [V](#scenario-v) [X](#scenario-x). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** User/Measured/Implied precedence, range checks and dependency invalidation; a failed write must not leave partial state.

**Existing direct C++ test callers:** [`case_990`](#function-346), [`case_1044`](#function-347), [`case_1080`](#function-348), [`case_1145`](#function-350), [`case_1235`](#function-351), [`case_1292`](#function-352). These do not establish full branch coverage.

**Integration tests should exist:** [E](#scenario-e) [C](#scenario-c) [V](#scenario-v) [X](#scenario-x) — Exercise equivalent explicit flags and --set, implied/pinned conflicts, and isolated config round-trips; assert value/origin reports and persisted measurements.

<a id="function-062"></a>

### `params::Params::known` — line 255

Source: [defaults.cpp:255](../defaults.cpp#L255). Signature: `Params::known(std::string_view key) const`.

**Callers and how they call it**

- [`parseOptions`](#function-027) ([cli.cpp:178](../cli.cpp#L178)): direct C++ call; `known(key)`.

**CLI invocations / variants:** [E](#scenario-e) [C](#scenario-c) [V](#scenario-v) [X](#scenario-x). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Rule-only, value-only, unknown and hidden keys; no implicit resolution.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [E](#scenario-e) [C](#scenario-c) [V](#scenario-v) [X](#scenario-x) — Exercise equivalent explicit flags and --set, implied/pinned conflicts, and isolated config round-trips; assert value/origin reports and persisted measurements.

<a id="function-063"></a>

### `params::Params::resolve` — line 260

Source: [defaults.cpp:260](../defaults.cpp#L260). Signature: `Params::resolve(std::string_view key)`.

**Callers and how they call it**

- [`params::Params::value_of`](#function-044) ([defaults.cpp:136](../defaults.cpp#L136)): direct C++ call; `resolve(key)`.

- [`params::Params::serialized_of`](#function-045) ([defaults.cpp:137](../defaults.cpp#L137)): direct C++ call; `resolve(key)`.

- [`params::Params::num`](#function-064) ([defaults.cpp:304](../defaults.cpp#L304)): direct C++ call; `resolve(key)`.

- [`params::Params::integer`](#function-065) ([defaults.cpp:310](../defaults.cpp#L310)): direct C++ call; `resolve(key)`.

- [`params::Params::str`](#function-066) ([defaults.cpp:343](../defaults.cpp#L343)): direct C++ call; `resolve(key)`.

- [`params::Params::origin_of`](#function-072) ([defaults.cpp:380](../defaults.cpp#L380)): direct C++ call; `resolve(key)`.

- [`params::Params::explain`](#function-080) ([defaults.cpp:538](../defaults.cpp#L538)): direct C++ call; `resolve(k)`.

**CLI invocations / variants:** [E](#scenario-e) [C](#scenario-c) [V](#scenario-v) [X](#scenario-x). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Lazy evaluation runs once; dependency chain and diamond; missing key, self-cycle and multi-key cycle; cleanup after exception; origins distinguished.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [E](#scenario-e) [C](#scenario-c) [V](#scenario-v) [X](#scenario-x) — Exercise equivalent explicit flags and --set, implied/pinned conflicts, and isolated config round-trips; assert value/origin reports and persisted measurements.

<a id="function-064"></a>

### `params::Params::num` — line 304

Source: [defaults.cpp:304](../defaults.cpp#L304). Signature: `Params::num(std::string_view key)`.

**Callers and how they call it**

- [`fill_options_from_params`](#function-023) ([cli.cpp:107](../cli.cpp#L107)): direct C++ call; `num(mask::keys::Scale)`, `num(mask::keys::Highpass)`.

- [`params::Resolver::num`](#function-055) ([defaults.cpp:208](../defaults.cpp#L208)): direct C++ call; `num(key)`.

- [`calibrate_dft_for`](#function-088) ([dftcalib.cpp:95](../dftcalib.cpp#L95)): direct C++ call; `num(SmoothCells)`, `num(mask::keys::Scale)`, `num(mask::keys::Highpass)`.

- [`dither_main`](#function-096) ([dither.cpp:105](../dither.cpp#L105)): direct C++ call; `num(mask::keys::Budget)`.

- [`blue_build`](#function-119) ([generators.cpp:27](../generators.cpp#L27)): direct C++ call; `num(bluenoise::keys::Sigma)`, `num(bluenoise::keys::Eps)`, `num(bluenoise::keys::Radius)`.

- [`dft_build`](#function-121) ([generators.cpp:58](../generators.cpp#L58)): direct C++ call; `num(mask::keys::Scale)`, `num(mask::keys::Highpass)`, `num(spectralmask::keys::SmoothCells)`.

- [`direct_build`](#function-129) ([generators.cpp:130](../generators.cpp#L130)): direct C++ call; `num(mask::keys::Scale)`, `num(mask::keys::Highpass)`, `num(direct::keys::Quantise)`.

- [`markov_params_from`](#function-130) ([generators.cpp:149](../generators.cpp#L149)): direct C++ call; `num(markov::keys::Temp)`, `num(mask::keys::Scale)`, `num(mask::keys::Highpass)`, `num(markov::keys::Reuse)`.

- [`inpaint_params_from`](#function-131) ([generators.cpp:169](../generators.cpp#L169)): direct C++ call; `num(mask::keys::Scale)`, `num(mask::keys::Highpass)`, `num(inpaint::keys::Cap)`.

- [`run`](#function-166) ([maketexture.cpp:239](../maketexture.cpp#L239)): direct C++ call; `num(mask::keys::Scale)`, `num(mask::keys::Highpass)`, `num(spectralmask::keys::SmoothCells)`.

- [`calibrate_markov_for`](#function-204) ([markovcalib.cpp:403](../markovcalib.cpp#L403)): direct C++ call; `num(markov::keys::Temp)`.

- [`case_306`](#function-329) ([unittest.cpp:306](../unittest.cpp#L306)): direct C++ call; `num(inpaint::keys::Cap)`.

- [`case_1080`](#function-348) ([unittest.cpp:1080](../unittest.cpp#L1080)): direct C++ call; `num(bluenoise::keys::Sigma)`.

- [`case_1145`](#function-350) ([unittest.cpp:1145](../unittest.cpp#L1145)): direct C++ call; `num(key)`.

- [`case_1235`](#function-351) ([unittest.cpp:1235](../unittest.cpp#L1235)): direct C++ call; `num(key)`.

- [`case_1292`](#function-352) ([unittest.cpp:1292](../unittest.cpp#L1292)): direct C++ call; `num(key)`.

**CLI invocations / variants:** [E](#scenario-e) [C](#scenario-c) [V](#scenario-v) [X](#scenario-x). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Both numeric variants; numeric-looking string rejected; unknown key and derived range errors.

**Existing direct C++ test callers:** [`case_306`](#function-329), [`case_1080`](#function-348), [`case_1145`](#function-350), [`case_1235`](#function-351), [`case_1292`](#function-352). These do not establish full branch coverage.

**Integration tests should exist:** [E](#scenario-e) [C](#scenario-c) [V](#scenario-v) [X](#scenario-x) — Exercise equivalent explicit flags and --set, implied/pinned conflicts, and isolated config round-trips; assert value/origin reports and persisted measurements.

<a id="function-065"></a>

### `params::Params::integer` — line 310

Source: [defaults.cpp:310](../defaults.cpp#L310). Signature: `Params::integer(std::string_view key)`.

**Callers and how they call it**

- [`fill_options_from_params`](#function-023) ([cli.cpp:107](../cli.cpp#L107)): direct C++ call; `integer(mask::keys::Supersample)`, `integer(mask::keys::OutSize)`, `integer(mask::keys::Calibrate)`, `integer(mask::keys::Seed)`.

- [`params::Resolver::integer`](#function-056) ([defaults.cpp:209](../defaults.cpp#L209)): direct C++ call; `integer(key)`.

- [`calibrate_dft_for`](#function-088) ([dftcalib.cpp:95](../dftcalib.cpp#L95)): direct C++ call; `integer(Iters)`.

- [`dft_build`](#function-121) ([generators.cpp:58](../generators.cpp#L58)): direct C++ call; `integer(spectralmask::keys::Iters)`.

- [`direct_build`](#function-129) ([generators.cpp:130](../generators.cpp#L130)): direct C++ call; `integer(direct::keys::Overlap)`.

- [`markov_params_from`](#function-130) ([generators.cpp:149](../generators.cpp#L149)): direct C++ call; `integer(markov::keys::Bins)`, `integer(markov::keys::Pairs)`, `integer(markov::keys::Shortlist)`, `integer(markov::keys::Radius)`, `integer(markov::keys::Iters)`, `integer(markov::keys::Report)`.

- [`inpaint_params_from`](#function-131) ([generators.cpp:169](../generators.cpp#L169)): direct C++ call; `integer(inpaint::keys::Radius)`, `integer(inpaint::keys::Iters)`, `integer(inpaint::keys::Db)`, `integer(inpaint::keys::Dims)`, `integer(inpaint::keys::Groups)`, `integer(inpaint::keys::Checks)`, `integer(inpaint::keys::Scales)`.

- [`calibrate_inpaint_for`](#function-156) ([inpaintcalib.cpp:107](../inpaintcalib.cpp#L107)): direct C++ call; `integer(inpaint::keys::Iters)`.

- [`run`](#function-166) ([maketexture.cpp:239](../maketexture.cpp#L239)): direct C++ call; `integer(spectralmask::keys::Iters)`.

- [`obtain_mask`](#function-213) ([mask.cpp:377](../mask.cpp#L377)): direct C++ call; `integer(mask::keys::SizeW)`, `integer(mask::keys::SizeH)`.

- [`case_306`](#function-329) ([unittest.cpp:306](../unittest.cpp#L306)): direct C++ call; `integer(w.key)`.

- [`case_990`](#function-346) ([unittest.cpp:990](../unittest.cpp#L990)): direct C++ call; `integer(mask::keys::SizeW)`, `integer(mask::keys::SizeH)`.

- [`case_1044`](#function-347) ([unittest.cpp:1044](../unittest.cpp#L1044)): direct C++ call; `integer(mask::keys::Supersample)`, `integer(mask::keys::Seed)`.

**CLI invocations / variants:** [E](#scenario-e) [C](#scenario-c) [V](#scenario-v) [X](#scenario-x). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Exact integer double accepted; fractional user/measured value rejected; tiny derived rounding residue accepted; representable bounds and nonfinite values.

**Existing direct C++ test callers:** [`case_306`](#function-329), [`case_990`](#function-346), [`case_1044`](#function-347). These do not establish full branch coverage.

**Integration tests should exist:** [E](#scenario-e) [C](#scenario-c) [V](#scenario-v) [X](#scenario-x) — Exercise equivalent explicit flags and --set, implied/pinned conflicts, and isolated config round-trips; assert value/origin reports and persisted measurements.

<a id="function-066"></a>

### `params::Params::str` — line 343

Source: [defaults.cpp:343](../defaults.cpp#L343). Signature: `Params::str(std::string_view key)`.

**Callers and how they call it**

- [`fill_options_from_params`](#function-023) ([cli.cpp:107](../cli.cpp#L107)): direct C++ call; `str(mask::keys::Threshold)`, `str(mask::keys::Method)`, `str(mask::keys::In)`, `str(mask::keys::Sample)`.

- [`parseOptions`](#function-027) ([cli.cpp:178](../cli.cpp#L178)): direct C++ call; `str(mask::keys::Threshold)`.

- [`params::Resolver::str`](#function-057) ([defaults.cpp:210](../defaults.cpp#L210)): direct C++ call; `str(key)`.

- [`dither_main`](#function-096) ([dither.cpp:105](../dither.cpp#L105)): direct C++ call; `str(mask::keys::Method)`.

- [`markov_validate`](#function-126) ([generators.cpp:104](../generators.cpp#L104)): direct C++ call; `str(markov::keys::Weights)`.

- [`markov_params_from`](#function-130) ([generators.cpp:149](../generators.cpp#L149)): direct C++ call; `str(markov::keys::Weights)`.

**CLI invocations / variants:** [E](#scenario-e) [C](#scenario-c) [V](#scenario-v) [X](#scenario-x). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** String round-trip; numeric variant rejected; missing key error includes key.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [E](#scenario-e) [C](#scenario-c) [V](#scenario-v) [X](#scenario-x) — Exercise equivalent explicit flags and --set, implied/pinned conflicts, and isolated config round-trips; assert value/origin reports and persisted measurements.

<a id="function-067"></a>

### `params::Params::applies` — line 350

Source: [defaults.cpp:350](../defaults.cpp#L350). Signature: `Params::applies(std::string_view key)`.

**Callers and how they call it**

- [`params::Params::explain`](#function-080) ([defaults.cpp:538](../defaults.cpp#L538)): direct C++ call; `applies(k)`.

- [`parse`](#function-165) ([maketexture.cpp:171](../maketexture.cpp#L171)): direct C++ call; `applies(k)`.

- [`maskcache::key_for`](#function-222) ([maskcache.cpp:116](../maskcache.cpp#L116)): direct C++ call; `applies(key)`.

- [`case_1145`](#function-350) ([unittest.cpp:1145](../unittest.cpp#L1145)): direct C++ call; `applies(key)`.

- [`case_1235`](#function-351) ([unittest.cpp:1235](../unittest.cpp#L1235)): direct C++ call; `applies(key)`.

- [`case_1292`](#function-352) ([unittest.cpp:1292](../unittest.cpp#L1292)): direct C++ call; `applies(key)`.

**CLI invocations / variants:** [E](#scenario-e) [C](#scenario-c) [V](#scenario-v) [X](#scenario-x). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Missing predicate means applicable; true/false predicate; predicate dependencies recorded without altering user values.

**Existing direct C++ test callers:** [`case_1145`](#function-350), [`case_1235`](#function-351), [`case_1292`](#function-352). These do not establish full branch coverage.

**Integration tests should exist:** [E](#scenario-e) [C](#scenario-c) [V](#scenario-v) [X](#scenario-x) — Exercise equivalent explicit flags and --set, implied/pinned conflicts, and isolated config round-trips; assert value/origin reports and persisted measurements.

<a id="function-068"></a>

### `params::Params::cached` — line 357

Source: [defaults.cpp:357](../defaults.cpp#L357). Signature: `Params::cached(std::string_view key) const`.

**Callers and how they call it**

- [`calibrate_dft_for`](#function-088) ([dftcalib.cpp:95](../dftcalib.cpp#L95)): direct C++ call; `cached(key)`.

- [`calibrate_inpaint_for`](#function-156) ([inpaintcalib.cpp:107](../inpaintcalib.cpp#L107)): direct C++ call; `cached(key)`.

- [`cached_calibration`](#function-202) ([markovcalib.cpp:355](../markovcalib.cpp#L355)): direct C++ call; `cached(key)`, `cached(wkey)`, `cached(rkey)`.

**CLI invocations / variants:** [CD](#scenario-cd) [CM](#scenario-cm) [CP](#scenario-cp). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Missing and present measurement, zero and boundary values; reading does not derive a rule.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [CD](#scenario-cd) [CM](#scenario-cm) [CP](#scenario-cp) — Exercise equivalent explicit flags and --set, implied/pinned conflicts, and isolated config round-trips; assert value/origin reports and persisted measurements.

<a id="function-069"></a>

### `params::Params::remember` — line 363

Source: [defaults.cpp:363](../defaults.cpp#L363). Signature: `Params::remember(std::string_view key, double v)`.

**Callers and how they call it**

- [`calibrate_dft_for`](#function-088) ([dftcalib.cpp:95](../dftcalib.cpp#L95)): direct C++ call; `remember(key, kLadder[best])`.

- [`calibrate_inpaint_for`](#function-156) ([inpaintcalib.cpp:107](../inpaintcalib.cpp#L107)): direct C++ call; `remember(key, (double)kLadder[best])`.

- [`calibrate_markov_for`](#function-204) ([markovcalib.cpp:403](../markovcalib.cpp#L403)): direct C++ call; `remember(key, c->temp)`, `remember(wkey, (double)i)`, `remember(rkey, (double)c->radius)`.

**CLI invocations / variants:** [CD](#scenario-cd) [CM](#scenario-cm) [CP](#scenario-cp). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Insert/replace measurement; round-trip through config; preserve unrelated measurements.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [CD](#scenario-cd) [CM](#scenario-cm) [CP](#scenario-cp) — Exercise equivalent explicit flags and --set, implied/pinned conflicts, and isolated config round-trips; assert value/origin reports and persisted measurements.

<a id="function-070"></a>

### `params::Params::measured` — line 368

Source: [defaults.cpp:368](../defaults.cpp#L368). Signature: `Params::measured(std::string_view key) const`.

**Callers and how they call it**

- [`dither_main`](#function-096) ([dither.cpp:105](../dither.cpp#L105)): direct C++ call; `measured(P.str(mask::keys::Method) + ".rate")`.

**CLI invocations / variants:** [E](#scenario-e) [C](#scenario-c) [V](#scenario-v) [X](#scenario-x). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Only Measured origin returns true; unknown key and all other origins; no eager derivation.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [E](#scenario-e) [C](#scenario-c) [V](#scenario-v) [X](#scenario-x) — Exercise equivalent explicit flags and --set, implied/pinned conflicts, and isolated config round-trips; assert value/origin reports and persisted measurements.

<a id="function-071"></a>

### `params::Params::user_set` — line 374

Source: [defaults.cpp:374](../defaults.cpp#L374). Signature: `Params::user_set(std::string_view key) const`.

**Callers and how they call it**

- [`calibrate_dft_for`](#function-088) ([dftcalib.cpp:95](../dftcalib.cpp#L95)): direct C++ call; `user_set(SmoothCells)`.

- [`calibrate_inpaint_for`](#function-156) ([inpaintcalib.cpp:107](../inpaintcalib.cpp#L107)): direct C++ call; `user_set(inpaint::keys::Iters)`.

- [`calibrate_markov_for`](#function-204) ([markovcalib.cpp:403](../markovcalib.cpp#L403)): direct C++ call; `user_set(markov::keys::Temp)`, `user_set(markov::keys::Weights)`, `user_set(markov::keys::Radius)`.

**CLI invocations / variants:** [E](#scenario-e) [C](#scenario-c) [V](#scenario-v) [X](#scenario-x). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Only User origin returns true; query does not run a rule or change state.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [E](#scenario-e) [C](#scenario-c) [V](#scenario-v) [X](#scenario-x) — Exercise equivalent explicit flags and --set, implied/pinned conflicts, and isolated config round-trips; assert value/origin reports and persisted measurements.

<a id="function-072"></a>

### `params::Params::origin_of` — line 380

Source: [defaults.cpp:380](../defaults.cpp#L380). Signature: `Params::origin_of(std::string_view key)`.

**Callers and how they call it**

- [`parseOptions`](#function-027) ([cli.cpp:178](../cli.cpp#L178)): direct C++ call; `origin_of(mask::keys::Threshold)`.

- [`params::Params::integer`](#function-065) ([defaults.cpp:310](../defaults.cpp#L310)): direct C++ call; `origin_of(key)`.

**CLI invocations / variants:** [E](#scenario-e) [C](#scenario-c) [V](#scenario-v) [X](#scenario-x). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Resolve first when needed and return User/Measured/Implied/Derived/Fallback accurately.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [E](#scenario-e) [C](#scenario-c) [V](#scenario-v) [X](#scenario-x) — Exercise equivalent explicit flags and --set, implied/pinned conflicts, and isolated config round-trips; assert value/origin reports and persisted measurements.

<a id="function-073"></a>

### `params::Params::stranded` — line 388

Source: [defaults.cpp:388](../defaults.cpp#L388). Signature: `Params::stranded()`.

**Callers and how they call it**

- [`params::Params::apply_trial`](#function-076) ([defaults.cpp:437](../defaults.cpp#L437)): direct C++ call; `stranded()`.

- [`params::Params::reconcile`](#function-077) ([defaults.cpp:446](../defaults.cpp#L446)): direct C++ call; `stranded()`.

- [`params::Params::conflicts`](#function-079) ([defaults.cpp:522](../defaults.cpp#L522)): direct C++ call; `stranded()`.

**CLI invocations / variants:** [E](#scenario-e) [C](#scenario-c) [V](#scenario-v) [X](#scenario-x). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** List only supplied inapplicable parameters; dependencies and exceptions; deterministic order.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [E](#scenario-e) [C](#scenario-c) [V](#scenario-v) [X](#scenario-x) — Exercise equivalent explicit flags and --set, implied/pinned conflicts, and isolated config round-trips; assert value/origin reports and persisted measurements.

<a id="function-074"></a>

### `params::Params::forget_unpinned` — line 410

Source: [defaults.cpp:410](../defaults.cpp#L410). Signature: `Params::forget_unpinned()`.

**Callers and how they call it**

- [`params::Params::apply_trial`](#function-076) ([defaults.cpp:437](../defaults.cpp#L437)): direct C++ call; `forget_unpinned()`.

- [`params::Params::reconcile`](#function-077) ([defaults.cpp:446](../defaults.cpp#L446)): direct C++ call; `forget_unpinned()`.

**CLI invocations / variants:** [E](#scenario-e) [C](#scenario-c) [V](#scenario-v) [X](#scenario-x). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Preserve User/Measured values while clearing recomputable defaults and their dependency edges.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [E](#scenario-e) [C](#scenario-c) [V](#scenario-v) [X](#scenario-x) — Exercise equivalent explicit flags and --set, implied/pinned conflicts, and isolated config round-trips; assert value/origin reports and persisted measurements.

<a id="function-075"></a>

### `params::Params::movable_defaults` — line 422

Source: [defaults.cpp:422](../defaults.cpp#L422). Signature: `Params::movable_defaults() const`.

**Callers and how they call it**

- [`params::Params::reconcile`](#function-077) ([defaults.cpp:446](../defaults.cpp#L446)): direct C++ call; `movable_defaults()`.

**CLI invocations / variants:** [E](#scenario-e) [C](#scenario-c) [V](#scenario-v) [X](#scenario-x). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Only unpinned rules with candidates are movable; candidates retained in deterministic order.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [E](#scenario-e) [C](#scenario-c) [V](#scenario-v) [X](#scenario-x) — Exercise equivalent explicit flags and --set, implied/pinned conflicts, and isolated config round-trips; assert value/origin reports and persisted measurements.

<a id="function-076"></a>

### `params::Params::apply_trial` — line 437

Source: [defaults.cpp:437](../defaults.cpp#L437). Signature: `Params::apply_trial(const std::vector<Movable>& movable, const std::vector<int>& pick)`.

**Callers and how they call it**

- [`params::Params::reconcile`](#function-077) ([defaults.cpp:446](../defaults.cpp#L446)): direct C++ call; `apply_trial(movable, pick)`, `apply_trial(movable, best)`.

**CLI invocations / variants:** [E](#scenario-e) [C](#scenario-c) [V](#scenario-v) [X](#scenario-x). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Apply a candidate assignment, preserve pinned values and count moves correctly; state reset between trials.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [E](#scenario-e) [C](#scenario-c) [V](#scenario-v) [X](#scenario-x) — Exercise equivalent explicit flags and --set, implied/pinned conflicts, and isolated config round-trips; assert value/origin reports and persisted measurements.

<a id="function-077"></a>

### `params::Params::reconcile` — line 446

Source: [defaults.cpp:446](../defaults.cpp#L446). Signature: `Params::reconcile()`.

**Callers and how they call it**

- [`parseOptions`](#function-027) ([cli.cpp:178](../cli.cpp#L178)): direct C++ call; `reconcile()`.

**CLI invocations / variants:** [E](#scenario-e) [C](#scenario-c) [V](#scenario-v) [X](#scenario-x). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Already-valid no-op; rescue one/multiple supplied options with minimal changes; impossible/ambiguous alternatives; pinned defaults never moved; failed trials do not leak.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [E](#scenario-e) [C](#scenario-c) [V](#scenario-v) [X](#scenario-x) — Exercise equivalent explicit flags and --set, implied/pinned conflicts, and isolated config round-trips; assert value/origin reports and persisted measurements.

<a id="function-078"></a>

### `params::Params::applicability_inputs` — line 507

Source: [defaults.cpp:507](../defaults.cpp#L507). Signature: `Params::applicability_inputs(const Rule& r)`.

**Callers and how they call it**

- [`params::Params::conflicts`](#function-079) ([defaults.cpp:522](../defaults.cpp#L522)): direct C++ call; `applicability_inputs(rule)`.

**CLI invocations / variants:** [E](#scenario-e) [C](#scenario-c) [V](#scenario-v) [X](#scenario-x). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Collect keys read by predicate, including nested reads; restore tracking state on success and exception.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [E](#scenario-e) [C](#scenario-c) [V](#scenario-v) [X](#scenario-x) — Exercise equivalent explicit flags and --set, implied/pinned conflicts, and isolated config round-trips; assert value/origin reports and persisted measurements.

<a id="function-079"></a>

### `params::Params::conflicts` — line 522

Source: [defaults.cpp:522](../defaults.cpp#L522). Signature: `Params::conflicts()`.

**Callers and how they call it**

- [`report_conflicts`](#function-024) ([cli.cpp:125](../cli.cpp#L125)): direct C++ call; `conflicts()`.

**CLI invocations / variants:** [E](#scenario-e) [C](#scenario-c) [V](#scenario-v) [X](#scenario-x). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Name each stranded user key and controlling inputs; no false positives for inactive derived values.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [E](#scenario-e) [C](#scenario-c) [V](#scenario-v) [X](#scenario-x) — Exercise equivalent explicit flags and --set, implied/pinned conflicts, and isolated config round-trips; assert value/origin reports and persisted measurements.

<a id="function-080"></a>

### `params::Params::explain` — line 538

Source: [defaults.cpp:538](../defaults.cpp#L538). Signature: `Params::explain(std::ostream& os)`.

**Callers and how they call it**

- [`dither_main`](#function-096) ([dither.cpp:105](../dither.cpp#L105)): direct C++ call; `explain(std::cout)`.

**CLI invocations / variants:** [E](#scenario-e). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Values, origins, dependencies, scope and generated flag names; unused parameters and resolution failures; output stable enough for users to compare.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [E](#scenario-e) — Exercise equivalent explicit flags and --set, implied/pinned conflicts, and isolated config round-trips; assert value/origin reports and persisted measurements.

#### Anonymous function: `params::Params::explain::pad@558`

Source: [defaults.cpp:558](../defaults.cpp#L558); capture/signature: `[&os](const std::string& s, size_t w)`.

- **Callers/how:** params::Params::explain invokes local formatting helper.

- **CLI invocations:** [E](#scenario-e); same enclosing branch, not an independently named CLI operation.

- **Unit tests should exist:** Empty/long labels and width boundary; formatting does not alter registry state.

- **Integration tests should exist:** [E](#scenario-e); verify the enclosing function's observable assertion above with the callback branch actually reached.

<a id="function-081"></a>

### `params::Params::keys_ending` — line 601

Source: [defaults.cpp:601](../defaults.cpp#L601). Signature: `Params::keys_ending(std::string_view suffix) const`.

**Callers and how they call it**

- [`calibrate_dft_for`](#function-088) ([dftcalib.cpp:95](../dftcalib.cpp#L95)): direct C++ call; `keys_ending(".rate")`.

- [`calibrate_inpaint_for`](#function-156) ([inpaintcalib.cpp:107](../inpaintcalib.cpp#L107)): direct C++ call; `keys_ending(".rate")`.

- [`calibrate_markov_for`](#function-204) ([markovcalib.cpp:403](../markovcalib.cpp#L403)): direct C++ call; `keys_ending(".rate")`.

- [`record_build_rate`](#function-212) ([mask.cpp:355](../mask.cpp#L355)): direct C++ call; `keys_ending(".rate")`.

- [`maskcache::key_for`](#function-222) ([maskcache.cpp:116](../maskcache.cpp#L116)): direct C++ call; `keys_ending("")`.

- [`case_1145`](#function-350) ([unittest.cpp:1145](../unittest.cpp#L1145)): direct C++ call; `keys_ending("")`.

- [`case_1235`](#function-351) ([unittest.cpp:1235](../unittest.cpp#L1235)): direct C++ call; `keys_ending("")`.

- [`case_1292`](#function-352) ([unittest.cpp:1292](../unittest.cpp#L1292)): direct C++ call; `keys_ending(".rate")`.

**CLI invocations / variants:** [E](#scenario-e) [C](#scenario-c) [V](#scenario-v) [X](#scenario-x). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Exact suffix, empty suffix, no matches and all registered generator rates; stable ordering.

**Existing direct C++ test callers:** [`case_1145`](#function-350), [`case_1235`](#function-351), [`case_1292`](#function-352). These do not establish full branch coverage.

**Integration tests should exist:** [E](#scenario-e) [C](#scenario-c) [V](#scenario-v) [X](#scenario-x) — Exercise equivalent explicit flags and --set, implied/pinned conflicts, and isolated config round-trips; assert value/origin reports and persisted measurements.

<a id="function-082"></a>

### `params::Params::load_config` — line 611

Source: [defaults.cpp:611](../defaults.cpp#L611). Signature: `Params::load_config(const std::string& path)`.

**Callers and how they call it**

- [`dither_main`](#function-096) ([dither.cpp:105](../dither.cpp#L105)): direct C++ call; `load_config(cfg)`.

**CLI invocations / variants:** [C](#scenario-c) [CD](#scenario-cd) [CM](#scenario-cm) [CP](#scenario-cp) [V](#scenario-v) [X](#scenario-x). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Missing file, comments/whitespace, malformed rows, invalid/nonfinite/out-of-range numbers, unknown keys and calibrations; user settings remain authoritative.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [C](#scenario-c) [CD](#scenario-cd) [CM](#scenario-cm) [CP](#scenario-cp) [V](#scenario-v) [X](#scenario-x) — Exercise equivalent explicit flags and --set, implied/pinned conflicts, and isolated config round-trips; assert value/origin reports and persisted measurements.

<a id="function-083"></a>

### `params::Params::save_config` — line 667

Source: [defaults.cpp:667](../defaults.cpp#L667). Signature: `Params::save_config(const std::string& path, const std::vector<std::string>& keys)`.

**Callers and how they call it**

- [`calibrate_dft_for`](#function-088) ([dftcalib.cpp:95](../dftcalib.cpp#L95)): direct C++ call; `save_config(cfg, P.keys_ending(".rate"))`.

- [`calibrate_inpaint_for`](#function-156) ([inpaintcalib.cpp:107](../inpaintcalib.cpp#L107)): direct C++ call; `save_config(cfg, P.keys_ending(".rate"))`.

- [`calibrate_markov_for`](#function-204) ([markovcalib.cpp:403](../markovcalib.cpp#L403)): direct C++ call; `save_config(cfg, P.keys_ending(".rate"))`.

- [`record_build_rate`](#function-212) ([mask.cpp:355](../mask.cpp#L355)): direct C++ call; `save_config(cfg, P.keys_ending(".rate"))`.

**CLI invocations / variants:** [C](#scenario-c) [CD](#scenario-cd) [CM](#scenario-cm) [CP](#scenario-cp) [V](#scenario-v). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Save selected rates plus cached measurements, preserve sufficient precision; missing parent and unwritable destination; round-trip; path metacharacters remain literal.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [C](#scenario-c) [CD](#scenario-cd) [CM](#scenario-cm) [CP](#scenario-cp) [V](#scenario-v) — Exercise equivalent explicit flags and --set, implied/pinned conflicts, and isolated config round-trips; assert value/origin reports and persisted measurements.

<a id="file-8"></a>

## `defaults.h`

### Declaration: `parse_double(const std::string& s)`

Source: [defaults.h:36](../defaults.h#L36).

Callers, CLI variants, unit tests and integration tests: [`params::parse_double`](#function-034). This is the declaration of that same function; do not count it as separate executed coverage.

### Declaration: `parse_integer(const std::string& s)`

Source: [defaults.h:37](../defaults.h#L37).

Callers, CLI variants, unit tests and integration tests: [`params::parse_integer`](#function-035). This is the declaration of that same function; do not count it as separate executed coverage.

### Declaration: `origin_name(Origin)`

Source: [defaults.h:47](../defaults.h#L47).

Callers, CLI variants, unit tests and integration tests: [`params::origin_name`](#function-036). This is the declaration of that same function; do not count it as separate executed coverage.

### Declaration: `num(std::string_view key) const`

Source: [defaults.h:56](../defaults.h#L56).

Callers, CLI variants, unit tests and integration tests: [`params::Resolver::num`](#function-055). This is the declaration of that same function; do not count it as separate executed coverage.

### Declaration: `integer(std::string_view key) const`

Source: [defaults.h:57](../defaults.h#L57).

Callers, CLI variants, unit tests and integration tests: [`params::Resolver::integer`](#function-056). This is the declaration of that same function; do not count it as separate executed coverage.

### Declaration: `str(std::string_view key) const`

Source: [defaults.h:58](../defaults.h#L58).

Callers, CLI variants, unit tests and integration tests: [`params::Resolver::str`](#function-057). This is the declaration of that same function; do not count it as separate executed coverage.

### Declaration: `add_const(Params& P, std::string key, Value v, Applicability applies, std::optional<std::pair<double, double>> range, std::string doc, bool intermediate = false)`

Source: [defaults.h:128](../defaults.h#L128).

Callers, CLI variants, unit tests and integration tests: [`params::add_const`](#function-058). This is the declaration of that same function; do not count it as separate executed coverage.

### Declaration: `add(Rule r)`

Source: [defaults.h:143](../defaults.h#L143).

Callers, CLI variants, unit tests and integration tests: [`params::Params::add`](#function-059). This is the declaration of that same function; do not count it as separate executed coverage.

### Declaration: `set(std::string_view key, Value v, Origin o)`

Source: [defaults.h:144](../defaults.h#L144).

Callers, CLI variants, unit tests and integration tests: [`params::Params::set`](#function-061). This is the declaration of that same function; do not count it as separate executed coverage.

### Declaration: `known(std::string_view key) const`

Source: [defaults.h:145](../defaults.h#L145).

Callers, CLI variants, unit tests and integration tests: [`params::Params::known`](#function-062). This is the declaration of that same function; do not count it as separate executed coverage.

### Declaration: `num(std::string_view key)`

Source: [defaults.h:149](../defaults.h#L149).

Callers, CLI variants, unit tests and integration tests: [`params::Params::num`](#function-064). This is the declaration of that same function; do not count it as separate executed coverage.

### Declaration: `integer(std::string_view key)`

Source: [defaults.h:150](../defaults.h#L150).

Callers, CLI variants, unit tests and integration tests: [`params::Params::integer`](#function-065). This is the declaration of that same function; do not count it as separate executed coverage.

### Declaration: `str(std::string_view key)`

Source: [defaults.h:151](../defaults.h#L151).

Callers, CLI variants, unit tests and integration tests: [`params::Params::str`](#function-066). This is the declaration of that same function; do not count it as separate executed coverage.

### Declaration: `value_of(std::string_view key)`

Source: [defaults.h:156](../defaults.h#L156).

Callers, CLI variants, unit tests and integration tests: [`params::Params::value_of`](#function-044). This is the declaration of that same function; do not count it as separate executed coverage.

### Declaration: `serialized_of(std::string_view key)`

Source: [defaults.h:162](../defaults.h#L162).

Callers, CLI variants, unit tests and integration tests: [`params::Params::serialized_of`](#function-045). This is the declaration of that same function; do not count it as separate executed coverage.

### Declaration: `flag_for(std::string_view key)`

Source: [defaults.h:171](../defaults.h#L171).

Callers, CLI variants, unit tests and integration tests: [`params::Params::flag_for`](#function-046). This is the declaration of that same function; do not count it as separate executed coverage.

### Declaration: `key_for_flag(std::string_view flag)`

Source: [defaults.h:172](../defaults.h#L172).

Callers, CLI variants, unit tests and integration tests: [`params::Params::key_for_flag`](#function-047). This is the declaration of that same function; do not count it as separate executed coverage.

### Declaration: `hide_option(std::string_view key)`

Source: [defaults.h:175](../defaults.h#L175).

Callers, CLI variants, unit tests and integration tests: [`params::Params::hide_option`](#function-048). This is the declaration of that same function; do not count it as separate executed coverage.

### Declaration: `has_option(std::string_view key) const`

Source: [defaults.h:176](../defaults.h#L176).

Callers, CLI variants, unit tests and integration tests: [`params::Params::has_option`](#function-050). This is the declaration of that same function; do not count it as separate executed coverage.

### Declaration: `doc(std::string_view key) const`

Source: [defaults.h:185](../defaults.h#L185).

Callers, CLI variants, unit tests and integration tests: [`params::Params::doc`](#function-051). This is the declaration of that same function; do not count it as separate executed coverage.

### Declaration: `options() const`

Source: [defaults.h:188](../defaults.h#L188).

Callers, CLI variants, unit tests and integration tests: [`params::Params::options`](#function-052). This is the declaration of that same function; do not count it as separate executed coverage.

### Declaration: `is_switch(std::string_view key) const`

Source: [defaults.h:193](../defaults.h#L193).

Callers, CLI variants, unit tests and integration tests: [`params::Params::is_switch`](#function-053). This is the declaration of that same function; do not count it as separate executed coverage.

### Declaration: `intermediate(std::string_view key) const`

Source: [defaults.h:197](../defaults.h#L197).

Callers, CLI variants, unit tests and integration tests: [`params::Params::intermediate`](#function-054). This is the declaration of that same function; do not count it as separate executed coverage.

### Declaration: `applies(std::string_view key)`

Source: [defaults.h:199](../defaults.h#L199).

Callers, CLI variants, unit tests and integration tests: [`params::Params::applies`](#function-067). This is the declaration of that same function; do not count it as separate executed coverage.

### Declaration: `measured(std::string_view key) const`

Source: [defaults.h:200](../defaults.h#L200).

Callers, CLI variants, unit tests and integration tests: [`params::Params::measured`](#function-070). This is the declaration of that same function; do not count it as separate executed coverage.

### Declaration: `user_set(std::string_view key) const`

Source: [defaults.h:206](../defaults.h#L206).

Callers, CLI variants, unit tests and integration tests: [`params::Params::user_set`](#function-071). This is the declaration of that same function; do not count it as separate executed coverage.

### Declaration: `origin_of(std::string_view key)`

Source: [defaults.h:208](../defaults.h#L208).

Callers, CLI variants, unit tests and integration tests: [`params::Params::origin_of`](#function-072). This is the declaration of that same function; do not count it as separate executed coverage.

### Declaration: `reconcile()`

Source: [defaults.h:226](../defaults.h#L226).

Callers, CLI variants, unit tests and integration tests: [`params::Params::reconcile`](#function-077). This is the declaration of that same function; do not count it as separate executed coverage.

### Declaration: `conflicts()`

Source: [defaults.h:242](../defaults.h#L242).

Callers, CLI variants, unit tests and integration tests: [`params::Params::conflicts`](#function-079). This is the declaration of that same function; do not count it as separate executed coverage.

### Declaration: `explain(std::ostream& os)`

Source: [defaults.h:244](../defaults.h#L244).

Callers, CLI variants, unit tests and integration tests: [`params::Params::explain`](#function-080). This is the declaration of that same function; do not count it as separate executed coverage.

### Declaration: `keys_ending(std::string_view suffix) const`

Source: [defaults.h:250](../defaults.h#L250).

Callers, CLI variants, unit tests and integration tests: [`params::Params::keys_ending`](#function-081). This is the declaration of that same function; do not count it as separate executed coverage.

### Declaration: `load_config(const std::string& path)`

Source: [defaults.h:257](../defaults.h#L257).

Callers, CLI variants, unit tests and integration tests: [`params::Params::load_config`](#function-082). This is the declaration of that same function; do not count it as separate executed coverage.

### Declaration: `save_config(const std::string& path, const std::vector<std::string>& keys)`

Source: [defaults.h:258](../defaults.h#L258).

Callers, CLI variants, unit tests and integration tests: [`params::Params::save_config`](#function-083). This is the declaration of that same function; do not count it as separate executed coverage.

### Declaration: `cached(std::string_view key) const`

Source: [defaults.h:263](../defaults.h#L263).

Callers, CLI variants, unit tests and integration tests: [`params::Params::cached`](#function-068). This is the declaration of that same function; do not count it as separate executed coverage.

### Declaration: `remember(std::string_view key, double v)`

Source: [defaults.h:264](../defaults.h#L264).

Callers, CLI variants, unit tests and integration tests: [`params::Params::remember`](#function-069). This is the declaration of that same function; do not count it as separate executed coverage.

### Declaration: `resolve(std::string_view key)`

Source: [defaults.h:268](../defaults.h#L268).

Callers, CLI variants, unit tests and integration tests: [`params::Params::resolve`](#function-063). This is the declaration of that same function; do not count it as separate executed coverage.

### Declaration: `check_range(std::string_view key, const Value& v) const`

Source: [defaults.h:269](../defaults.h#L269).

Callers, CLI variants, unit tests and integration tests: [`params::Params::check_range`](#function-060). This is the declaration of that same function; do not count it as separate executed coverage.

### Declaration: `stranded()`

Source: [defaults.h:273](../defaults.h#L273).

Callers, CLI variants, unit tests and integration tests: [`params::Params::stranded`](#function-073). This is the declaration of that same function; do not count it as separate executed coverage.

### Declaration: `forget_unpinned()`

Source: [defaults.h:274](../defaults.h#L274).

Callers, CLI variants, unit tests and integration tests: [`params::Params::forget_unpinned`](#function-074). This is the declaration of that same function; do not count it as separate executed coverage.

### Declaration: `movable_defaults() const`

Source: [defaults.h:280](../defaults.h#L280).

Callers, CLI variants, unit tests and integration tests: [`params::Params::movable_defaults`](#function-075). This is the declaration of that same function; do not count it as separate executed coverage.

### Declaration: `apply_trial(const std::vector<Movable>& movable, const std::vector<int>& pick)`

Source: [defaults.h:281](../defaults.h#L281).

Callers, CLI variants, unit tests and integration tests: [`params::Params::apply_trial`](#function-076). This is the declaration of that same function; do not count it as separate executed coverage.

### Declaration: `applicability_inputs(const Rule& r)`

Source: [defaults.h:286](../defaults.h#L286).

Callers, CLI variants, unit tests and integration tests: [`params::Params::applicability_inputs`](#function-078). This is the declaration of that same function; do not count it as separate executed coverage.

<a id="function-084"></a>

### `params::Resolver::Resolver` — line 55

Source: [defaults.h:55](../defaults.h#L55). Signature: `Resolver(Params& p) : p_(p)`.

**Callers and how they call it**

- [`params::Params::resolve`](#function-063) ([defaults.cpp:260](../defaults.cpp#L260)): direct C++ call; `Resolver(*this)`.

- [`params::Params::applies`](#function-067) ([defaults.cpp:350](../defaults.cpp#L350)): direct C++ call; `Resolver(*this)`.

- [`params::Params::stranded`](#function-073) ([defaults.cpp:388](../defaults.cpp#L388)): direct C++ call; `Resolver(*this)`.

- [`params::Params::applicability_inputs`](#function-078) ([defaults.cpp:507](../defaults.cpp#L507)): direct C++ call; `Resolver(*this)`.

- [`record_build_rate`](#function-212) ([mask.cpp:355](../mask.cpp#L355)): direct C++ call; see linked caller for arguments (may be template/constructor lowering).

- [`case_990`](#function-346) ([unittest.cpp:990](../unittest.cpp#L990)): direct C++ call; see linked caller for arguments (may be template/constructor lowering).

**CLI invocations / variants:** [E](#scenario-e) [C](#scenario-c) [X](#scenario-x). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Bind the intended registry; two resolvers for separate registries never cross-read state.

**Existing direct C++ test callers:** [`case_990`](#function-346). These do not establish full branch coverage.

**Integration tests should exist:** [E](#scenario-e) [C](#scenario-c) [X](#scenario-x) — Run explain/reconciliation and config cases, checking origins and dependency reports; a small linked API client covers construction and metadata predicates.

<a id="function-085"></a>

### `params::Applicability::operator bool` — line 76

Source: [defaults.h:76](../defaults.h#L76). Signature: `operator bool() const`.

**Callers and how they call it**

- [`params::Params::applies`](#function-067) ([defaults.cpp:350](../defaults.cpp#L350)): direct C++ call; receiver/signature resolved by compiler.

- [`params::Params::stranded`](#function-073) ([defaults.cpp:388](../defaults.cpp#L388)): direct C++ call; receiver/signature resolved by compiler.

**CLI invocations / variants:** [E](#scenario-e) [C](#scenario-c) [X](#scenario-x). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Empty versus nonempty callback; conversion must not execute the predicate.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [E](#scenario-e) [C](#scenario-c) [X](#scenario-x) — Run explain/reconciliation and config cases, checking origins and dependency reports; a small linked API client covers construction and metadata predicates.

<a id="function-086"></a>

### `params::Reconciliation::empty` — line 138

Source: [defaults.h:138](../defaults.h#L138). Signature: `empty() const`.

**Callers and how they call it**

- [`report_reconciliation`](#function-022) ([cli.cpp:85](../cli.cpp#L85)): direct C++ call; receiver/signature resolved by compiler.

**CLI invocations / variants:** [E](#scenario-e) [C](#scenario-c) [X](#scenario-x). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Empty changed list is empty even if rescued differs; nonempty changed list returns false.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [E](#scenario-e) [C](#scenario-c) [X](#scenario-x) — Run explain/reconciliation and config cases, checking origins and dependency reports; a small linked API client covers construction and metadata predicates.

<a id="file-9"></a>

## `dftcalib.cpp`

<a id="function-087"></a>

### `calibration_key` — line 62

Source: [dftcalib.cpp:62](../dftcalib.cpp#L62). Signature: `calibration_key(const samples::Set& set, double scale, double highpass, int iters, uint64_t seed)`.

**Callers and how they call it**

- [`calibrate_dft_for`](#function-088) ([dftcalib.cpp:95](../dftcalib.cpp#L95)): direct C++ call; `calibration_key(set, scale, highpass, iters, seed)`.

**CLI invocations / variants:** [CD](#scenario-cd). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Stable digest for identical data/seed/settings; modify each hashed field, scan pixels, order, dimensions/type and pin state where present; noncontinuous versus cloned scans give identical keys. The tuned quantity and requested output size should not invalidate calibration unless deliberately hashed; version changes must invalidate old results.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [CD](#scenario-cd) — Fresh and repeated calibration, pinned smoothing, small/flat samples, corrupted cache and unwritable config; verify only accepted measurements persist and the final build uses the selected setting.

#### Anonymous function: `calibration_key::mix@66`

Source: [dftcalib.cpp:66](../dftcalib.cpp#L66); capture/signature: `[&h](const void* data, size_t n)`.

- **Callers/how:** calibration_key invokes the local hash helpers; mix_i/mix_d forward typed bytes to mix.

- **CLI invocations:** [CD](#scenario-cd); same enclosing branch, not an independently named CLI operation.

- **Unit tests should exist:** Known bytes, empty update and split-update equivalence; typed values change the digest as specified; captured hash state is updated exactly once.

- **Integration tests should exist:** [CD](#scenario-cd); verify the enclosing function's observable assertion above with the callback branch actually reached.

#### Anonymous function: `calibration_key::mix_i@70`

Source: [dftcalib.cpp:70](../dftcalib.cpp#L70); capture/signature: `[&mix](long long v)`.

- **Callers/how:** calibration_key invokes the local hash helpers; mix_i/mix_d forward typed bytes to mix.

- **CLI invocations:** [CD](#scenario-cd); same enclosing branch, not an independently named CLI operation.

- **Unit tests should exist:** Known bytes, empty update and split-update equivalence; typed values change the digest as specified; captured hash state is updated exactly once.

- **Integration tests should exist:** [CD](#scenario-cd); verify the enclosing function's observable assertion above with the callback branch actually reached.

#### Anonymous function: `calibration_key::mix_d@71`

Source: [dftcalib.cpp:71](../dftcalib.cpp#L71); capture/signature: `[&mix](double v)`.

- **Callers/how:** calibration_key invokes the local hash helpers; mix_i/mix_d forward typed bytes to mix.

- **CLI invocations:** [CD](#scenario-cd); same enclosing branch, not an independently named CLI operation.

- **Unit tests should exist:** Known bytes, empty update and split-update equivalence; typed values change the digest as specified; captured hash state is updated exactly once.

- **Integration tests should exist:** [CD](#scenario-cd); verify the enclosing function's observable assertion above with the callback branch actually reached.

<a id="function-088"></a>

### `calibrate_dft_for` — line 95

Source: [dftcalib.cpp:95](../dftcalib.cpp#L95). Signature: `calibrate_dft_for(params::Params& P, const std::string& sample_path, uint64_t seed, const std::string& cfg, std::ostream& log)`.

**Callers and how they call it**

- [`maybe_calibrate`](#function-214) ([mask.cpp:435](../mask.cpp#L435)): indirect: Generator::calibrate(P, sample path, seed, config, log), only when calibration is requested.

**CLI invocations / variants:** [CD](#scenario-cd). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Explicit smoothing skips before reading sample; cached valid ladder value reused and invalid value rejected; 256/128/64 probe fallback; no usable band/failed probes; nearest-default baseline and 10% margin; finite scores, measured origin and save failure.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [CD](#scenario-cd) — Fresh and repeated calibration, pinned smoothing, small/flat samples, corrupted cache and unwritable config; verify only accepted measurements persist and the final build uses the selected setting.

#### Anonymous function: `calibrate_dft_for::trace@184`

Source: [dftcalib.cpp:184](../dftcalib.cpp#L184); capture/signature: `[&](std::ostream& o)`.

- **Callers/how:** calibrate_dft_for calls trace(log) on accepted and fallback reporting paths.

- **CLI invocations:** [CD](#scenario-cd); same enclosing branch, not an independently named CLI operation.

- **Unit tests should exist:** Only valid candidates printed with correct score/ladder value; no mutation of scores or winner.

- **Integration tests should exist:** [CD](#scenario-cd); verify the enclosing function's observable assertion above with the callback branch actually reached.

<a id="file-10"></a>

## `dftcalib.h`

### Declaration: `calibrate_dft_for(params::Params& P, const std::string& sample_path, uint64_t seed, const std::string& cfg, std::ostream& log)`

Source: [dftcalib.h:13](../dftcalib.h#L13).

Callers, CLI variants, unit tests and integration tests: [`calibrate_dft_for`](#function-088). This is the declaration of that same function; do not count it as separate executed coverage.

<a id="file-11"></a>

## `directmask.cpp`

<a id="function-089"></a>

### `build_direct_mask` — line 69

Source: [directmask.cpp:69](../directmask.cpp#L69). Signature: `build_direct_mask(cv::Size sz, const cv::Mat& sample, double scale, double highpass, int overlap, double quantStep, uint64_t seed, std::ostream* log)`.

**Callers and how they call it**

- [`direct_build`](#function-129) ([generators.cpp:130](../generators.cpp#L130)): direct C++ call; `build_direct_mask(q.size, q.samples.primary(), P.num(mask::keys::Scale), P.num(mask::keys::Highpass), (int)P.integer(direct::keys::Overlap), P.num(direct::keys::Quantise), q.seed, q.log)`.

- [`case_1473`](#function-358) ([unittest.cpp:1473](../unittest.cpp#L1473)): direct C++ call; `build_direct_mask({64, 64}, flat, 1.0, 32.0, 8, 0.9, 1, nullptr)`, `build_direct_mask({64, 64}, ramp, 1.0, 32.0, 8, 0.9, 1, nullptr)`, `build_direct_mask({64, 64}, noise, 1.0, 32.0, 8, 0.9, 1, nullptr)`, `build_direct_mask({64, 64}, flat, 1.0, 32.0, 8, 0.0, 1, nullptr)`, `build_direct_mask({64, 64}, noise, 1.0, 32.0, 8, 0.0, 1, nullptr)`.

**CLI invocations / variants:** [Q](#scenario-q). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Validate sample size/overlap, highpass/scale and quantization; source-only seam quilting without averaging; uniform ranks with seeded tie-breaking; reject excessive tie share; null log and deterministic seed.

**Existing direct C++ test callers:** [`case_1473`](#function-358). These do not establish full branch coverage.

**Integration tests should exist:** [Q](#scenario-q) — Read direct masks from textured and flat/tiny samples; check seam continuity without averaging, deterministic ties and clear refusal when detail is inadequate.

<a id="function-090"></a>

### `direct_work` — line 204

Source: [directmask.cpp:204](../directmask.cpp#L204). Signature: `direct_work(int w, int h, int overlap)`.

**Callers and how they call it**

- [`direct_work_of`](#function-127) ([generators.cpp:114](../generators.cpp#L114)): direct C++ call; `direct_work(w, h, (int)r.integer(direct::keys::Overlap))`.

**CLI invocations / variants:** [Q](#scenario-q). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Positive finite estimate; dimensions/overlap follow the stated cost formula; minimum and typical dimensions.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [Q](#scenario-q) — Read direct masks from textured and flat/tiny samples; check seam continuity without averaging, deterministic ties and clear refusal when detail is inadequate.

<a id="function-091"></a>

### `register_direct_params` — line 220

Source: [directmask.cpp:220](../directmask.cpp#L220). Signature: `register_direct_params(params::Params& P)`.

**Callers and how they call it**

- [`usage_generators`](#function-031) ([cli.cpp:568](../cli.cpp#L568)): indirect: generators() row .register_params(P), once for every generator.

- [`dither_main`](#function-096) ([dither.cpp:105](../dither.cpp#L105)): indirect: generators() row .register_params(P), once for every generator.

- [`list_options`](#function-161) ([maketexture.cpp:102](../maketexture.cpp#L102)): indirect: generators() row .register_params(P), once for every generator.

- [`run`](#function-166) ([maketexture.cpp:239](../maketexture.cpp#L239)): indirect: generators() row .register_params(P), once for every generator.

- [`cache_registry`](#function-345) ([unittest.cpp:981](../unittest.cpp#L981)): indirect: generators() row .register_params(P), once for every generator.

**CLI invocations / variants:** [H](#scenario-h) [E](#scenario-e) [R](#scenario-r) [T](#scenario-t) [U](#scenario-u) (registration); selected-generator scenarios exercise lazy derived rules. IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Register every direct rule once; assert defaults, range endpoints, option visibility and applicability. Resolve derived rules at dependency boundaries and after overrides, check origins/dependency edges, and verify parameter-struct defaults agree. Test each lambda below through the registry.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [H](#scenario-h) [E](#scenario-e) [R](#scenario-r) [T](#scenario-t) [U](#scenario-u) (registration); selected-generator scenarios exercise lazy derived rules — Read direct masks from textured and flat/tiny samples; check seam continuity without averaging, deterministic ties and clear refusal when detail is inadequate.

#### Anonymous function: `register_direct_params::callback@287`

Source: [directmask.cpp:287](../directmask.cpp#L287); capture/signature: `[](const params::Resolver& r) -> params::Value`.

- **Callers/how:** params::Params::resolve invokes Rule::derive(Resolver) lazily; register_direct_params registers the callback.

- **CLI invocations:** [H](#scenario-h) [E](#scenario-e) [R](#scenario-r) [T](#scenario-t) [U](#scenario-u) (registration); selected-generator scenarios exercise lazy derived rules; same enclosing branch, not an independently named CLI operation.

- **Unit tests should exist:** Resolve this individual rule at each dependency boundary; compare the derived formula/default independently, validate numeric range and dependency invalidation. Vary each read dependency independently: `mask::keys::Highpass`, `mask::keys::Scale`.

- **Integration tests should exist:** [H](#scenario-h) [E](#scenario-e) [R](#scenario-r) [T](#scenario-t) [U](#scenario-u) (registration); selected-generator scenarios exercise lazy derived rules; verify the enclosing function's observable assertion above with the callback branch actually reached.

<a id="file-12"></a>

## `directmask.h`

### Declaration: `build_direct_mask(cv::Size sz, const cv::Mat& sample, double scale, double highpass, int overlap, double quantStep, uint64_t seed, std::ostream* log)`

Source: [directmask.h:26](../directmask.h#L26).

Callers, CLI variants, unit tests and integration tests: [`build_direct_mask`](#function-089). This is the declaration of that same function; do not count it as separate executed coverage.

### Declaration: `register_direct_params(params::Params&)`

Source: [directmask.h:30](../directmask.h#L30).

Callers, CLI variants, unit tests and integration tests: [`register_direct_params`](#function-091). This is the declaration of that same function; do not count it as separate executed coverage.

### Declaration: `direct_work(int w, int h, int overlap)`

Source: [directmask.h:33](../directmask.h#L33).

Callers, CLI variants, unit tests and integration tests: [`direct_work`](#function-090). This is the declaration of that same function; do not count it as separate executed coverage.

<a id="file-13"></a>

## `dither.cpp`

<a id="function-092"></a>

### `prepare_target` — line 26

Source: [dither.cpp:26](../dither.cpp#L26). Signature: `prepare_target(const cv::Mat& image, const vision::LevelSet& ladder, const Options& opt)`.

**Callers and how they call it**

- [`dither_main`](#function-096) ([dither.cpp:105](../dither.cpp#L105)): direct C++ call; `prepare_target(image, ladder, opt)`.

**CLI invocations / variants:** [R](#scenario-r) [F](#scenario-f) [O](#scenario-o). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** 8/16-bit decode and all ladders; supersample 1 identity size and >1 cubic enlargement in display units; rectangular images.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [R](#scenario-r) [F](#scenario-f) [O](#scenario-o) — Read average and saved frames, verify dimensions/palette/linear-light mean, exact output paths and correct failure status; compare no-resize and single-resize branches.

<a id="function-093"></a>

### `report_debug_pixel` — line 42

Source: [dither.cpp:42](../dither.cpp#L42). Signature: `report_debug_pixel(const cv::Mat& target, const Options& opt)`.

**Callers and how they call it**

- [`dither_main`](#function-096) ([dither.cpp:105](../dither.cpp#L105)): direct C++ call; `report_debug_pixel(target, opt)`.

**CLI invocations / variants:** [O](#scenario-o). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Disabled coordinate, valid corners and exactly-outside row/column; coordinates interpreted after supersampling.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [O](#scenario-o) — Read average and saved frames, verify dimensions/palette/linear-light mean, exact output paths and correct failure status; compare no-resize and single-resize branches.

<a id="function-094"></a>

### `encode_and_resize` — line 73

Source: [dither.cpp:73](../dither.cpp#L73). Signature: `encode_and_resize(std::vector<cv::Mat>& frames, const vision::LevelSet& ladder, const Options& opt)`.

**Callers and how they call it**

- [`dither_main`](#function-096) ([dither.cpp:105](../dither.cpp#L105)): direct C++ call; `encode_and_resize(frames, ladder, opt)`.

**CLI invocations / variants:** [F](#scenario-f) [O](#scenario-o). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Encode each frame; no resize, supersample reduction, explicit out-size, aspect ratio and minimum one pixel; assert single-resample linear-light result.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [F](#scenario-f) [O](#scenario-o) — Read average and saved frames, verify dimensions/palette/linear-light mean, exact output paths and correct failure status; compare no-resize and single-resize branches.

<a id="function-095"></a>

### `write_outputs` — line 92

Source: [dither.cpp:92](../dither.cpp#L92). Signature: `write_outputs(const std::vector<cv::Mat>& frames, const Options& opt)`.

**Callers and how they call it**

- [`dither_main`](#function-096) ([dither.cpp:105](../dither.cpp#L105)): direct C++ call; `write_outputs(frames, opt)`.

**CLI invocations / variants:** [R](#scenario-r) [N](#scenario-n) [F](#scenario-f) [O](#scenario-o). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Average-only and saved frames; correct stem/directory and numbering; stop on average/frame write failure; linear-light mean.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [R](#scenario-r) [N](#scenario-n) [F](#scenario-f) [O](#scenario-o) — Read average and saved frames, verify dimensions/palette/linear-light mean, exact output paths and correct failure status; compare no-resize and single-resize branches.

<a id="function-096"></a>

### `dither_main` — line 105

Source: [dither.cpp:105](../dither.cpp#L105). Signature: `dither_main(int argc, char** argv)`.

**Callers and how they call it**

- [`main`](#function-116) ([experiments/inpaint-20260927/thread_control.cpp:7](../experiments/inpaint-20260927/thread_control.cpp#L7)): direct: after setting OpenCV threads, forwards argc-1 and argv+1.

- [`main`](#function-157) ([main.cpp:9](../main.cpp#L9)): direct C++ call; `dither_main(argc, argv)`.

**CLI invocations / variants:** [R](#scenario-r) [N](#scenario-n) [F](#scenario-f) [O](#scenario-o) [B](#scenario-b) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [L](#scenario-l) [C](#scenario-c) [CD](#scenario-cd) [CM](#scenario-cm) [CP](#scenario-cp) [X](#scenario-x). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Help/no arguments, explanation before build, config pre-scan, input failure, generator failure and output failure; deterministic seed; complete success pipeline.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [R](#scenario-r) [N](#scenario-n) [F](#scenario-f) [O](#scenario-o) [B](#scenario-b) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [L](#scenario-l) [C](#scenario-c) [CD](#scenario-cd) [CM](#scenario-cm) [CP](#scenario-cp) [X](#scenario-x) — Read average and saved frames, verify dimensions/palette/linear-light mean, exact output paths and correct failure status; compare no-resize and single-resize branches.

<a id="file-14"></a>

## `engine.cpp`

<a id="function-097"></a>

### `spatial_none` — line 13

Source: [engine.cpp:13](../engine.cpp#L13). Signature: `spatial_none(int /*row*/, int /*col*/, int /*dir*/, cv::Mat& /*spatial_err*/, float /*value*/)`.

**Callers and how they call it**

- [`quantize_scan`](#function-106) ([engine.cpp:177](../engine.cpp#L177)): indirect: SpatialFn(row, col, dir, ledger, residual), selected by spatial==0 versus >0.

**CLI invocations / variants:** [F](#scenario-f) [N](#scenario-n) [L](#scenario-l) [R](#scenario-r) (explicit --spatial 0). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Nonzero input error leaves every ledger cell unchanged, including border calls.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [F](#scenario-f) [N](#scenario-n) [L](#scenario-l) [R](#scenario-r) (explicit --spatial 0) — Compare raw no-diffusion temporal budgets and encoded output averages across thresholds/ladders; separately assert whole-image light conservation with diffusion and valid debug traces.

<a id="function-098"></a>

### `spatial_floydsteinberg` — line 18

Source: [engine.cpp:18](../engine.cpp#L18). Signature: `spatial_floydsteinberg(int row, int col, int dir, cv::Mat& spatial_err, float value)`.

**Callers and how they call it**

- [`quantize_scan`](#function-106) ([engine.cpp:177](../engine.cpp#L177)): indirect: SpatialFn(row, col, dir, ledger, residual), selected by spatial==0 versus >0.

**CLI invocations / variants:** [R](#scenario-r) [O](#scenario-o) (spatial > 0). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Independent stencil for both directions; edge renormalization, intentional last-row loss, 1x1 and one-column images; no out-of-bounds write.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [R](#scenario-r) [O](#scenario-o) (spatial > 0) — Compare raw no-diffusion temporal budgets and encoded output averages across thresholds/ladders; separately assert whole-image light conservation with diffusion and valid debug traces.

<a id="function-099"></a>

### `for_serpentine` — line 51

Source: [engine.cpp:51](../engine.cpp#L51). Signature: `for_serpentine(cv::Size size, Body&& body)`.

**Callers and how they call it**

- [`quantize_scan`](#function-106) ([engine.cpp:177](../engine.cpp#L177)): call in enclosing function / its lambda callback; `for_serpentine(size, [&](int row, int col, int dir) { // Two values, and the distinction is what keeps the tone honest. // // decide is what the threshold rule sees: the target plus only // spat...`.

**CLI invocations / variants:** [R](#scenario-r) [N](#scenario-n) [F](#scenario-f) [O](#scenario-o) [B](#scenario-b) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [L](#scenario-l). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Record visitation: every cell once, alternating direction, correct dir argument; 1xN, Nx1 and empty grid.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [R](#scenario-r) [N](#scenario-n) [F](#scenario-f) [O](#scenario-o) [B](#scenario-b) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [L](#scenario-l) — Compare raw no-diffusion temporal budgets and encoded output averages across thresholds/ladders; separately assert whole-image light conservation with diffusion and valid debug traces.

<a id="function-100"></a>

### `Space::of` — line 92

Source: [engine.cpp:92](../engine.cpp#L92). Signature: `of(const vision::LevelSet& l)`.

**Callers and how they call it**

- [`ditherPass`](#function-107) ([engine.cpp:249](../engine.cpp#L249)): direct C++ call; receiver/signature resolved by compiler.

**CLI invocations / variants:** [R](#scenario-r) [N](#scenario-n) [F](#scenario-f) [O](#scenario-o) [B](#scenario-b) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [L](#scenario-l). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Uniform or singleton ladder uses identity; nonuniform multi-level ladder stores the ladder.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [R](#scenario-r) [N](#scenario-n) [F](#scenario-f) [O](#scenario-o) [B](#scenario-b) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [L](#scenario-l) — Compare raw no-diffusion temporal budgets and encoded output averages across thresholds/ladders; separately assert whole-image light conservation with diffusion and valid debug traces.

<a id="function-101"></a>

### `Space::interval_of_display` — line 109

Source: [engine.cpp:109](../engine.cpp#L109). Signature: `interval_of_display(double display) const`.

**Callers and how they call it**

- [`Space::to_conserved`](#function-103) ([engine.cpp:121](../engine.cpp#L121)): direct C++ call; `interval_of_display(display)`.

**CLI invocations / variants:** [R](#scenario-r) [N](#scenario-n) [F](#scenario-f) [O](#scenario-o) [B](#scenario-b) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [L](#scenario-l). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Below/above endpoints, exact integer and fractional rung; clamp to a valid interval with successor.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [R](#scenario-r) [N](#scenario-n) [F](#scenario-f) [O](#scenario-o) [B](#scenario-b) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [L](#scenario-l) — Compare raw no-diffusion temporal budgets and encoded output averages across thresholds/ladders; separately assert whole-image light conservation with diffusion and valid debug traces.

<a id="function-102"></a>

### `Space::interval_of_linear` — line 113

Source: [engine.cpp:113](../engine.cpp#L113). Signature: `interval_of_linear(double conserved) const`.

**Callers and how they call it**

- [`Space::to_display`](#function-104) ([engine.cpp:141](../engine.cpp#L141)): direct C++ call; `interval_of_linear(conserved)`.

**CLI invocations / variants:** [R](#scenario-r) [N](#scenario-n) [F](#scenario-f) [O](#scenario-o) [B](#scenario-b) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [L](#scenario-l). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Each rung boundary and out-of-range light values; correct upper_bound interval on uneven ladders.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [R](#scenario-r) [N](#scenario-n) [F](#scenario-f) [O](#scenario-o) [B](#scenario-b) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [L](#scenario-l) — Compare raw no-diffusion temporal budgets and encoded output averages across thresholds/ladders; separately assert whole-image light conservation with diffusion and valid debug traces.

<a id="function-103"></a>

### `Space::to_conserved` — line 121

Source: [engine.cpp:121](../engine.cpp#L121). Signature: `to_conserved(double display) const`.

**Callers and how they call it**

- [`ditherPass`](#function-107) ([engine.cpp:249](../engine.cpp#L249)): call in enclosing function / its lambda callback; `to_conserved(g / (double)frames)`.

**CLI invocations / variants:** [R](#scenario-r) [N](#scenario-n) [F](#scenario-f) [O](#scenario-o) [B](#scenario-b) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [L](#scenario-l). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Identity mode and independent interpolation/extrapolation; exact rungs; inverse with to_display within tolerance.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [R](#scenario-r) [N](#scenario-n) [F](#scenario-f) [O](#scenario-o) [B](#scenario-b) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [L](#scenario-l) — Compare raw no-diffusion temporal budgets and encoded output averages across thresholds/ladders; separately assert whole-image light conservation with diffusion and valid debug traces.

<a id="function-104"></a>

### `Space::to_display` — line 141

Source: [engine.cpp:141](../engine.cpp#L141). Signature: `to_display(double conserved) const`.

**Callers and how they call it**

- [`quantize_scan`](#function-106) ([engine.cpp:177](../engine.cpp#L177)): call in enclosing function / its lambda callback; receiver/signature resolved by compiler.

**CLI invocations / variants:** [R](#scenario-r) [N](#scenario-n) [F](#scenario-f) [O](#scenario-o) [B](#scenario-b) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [L](#scenario-l). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Identity and uneven spans; exact rungs and just inside/outside kSnap; extrapolation; degenerate span behavior.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [R](#scenario-r) [N](#scenario-n) [F](#scenario-f) [O](#scenario-o) [B](#scenario-b) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [L](#scenario-l) — Compare raw no-diffusion temporal budgets and encoded output averages across thresholds/ladders; separately assert whole-image light conservation with diffusion and valid debug traces.

<a id="function-105"></a>

### `Space::of_level` — line 153

Source: [engine.cpp:153](../engine.cpp#L153). Signature: `of_level(int level) const`.

**Callers and how they call it**

- [`quantize_scan`](#function-106) ([engine.cpp:177](../engine.cpp#L177)): call in enclosing function / its lambda callback; `of_level(level)`.

- [`ditherPass`](#function-107) ([engine.cpp:249](../engine.cpp#L249)): call in enclosing function / its lambda callback; `of_level(display)`.

**CLI invocations / variants:** [R](#scenario-r) [N](#scenario-n) [F](#scenario-f) [O](#scenario-o) [B](#scenario-b) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [L](#scenario-l). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Identity and lookup mode; clamp out-of-range integer levels where a ladder is active.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [R](#scenario-r) [N](#scenario-n) [F](#scenario-f) [O](#scenario-o) [B](#scenario-b) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [L](#scenario-l) — Compare raw no-diffusion temporal budgets and encoded output averages across thresholds/ladders; separately assert whole-image light conservation with diffusion and valid debug traces.

<a id="function-106"></a>

### `quantize_scan` — line 177

Source: [engine.cpp:177](../engine.cpp#L177). Signature: `quantize_scan(cv::Size size, DitherFn& dither_fn, SpatialFn spatial_fn, float spatial, int max_level, const Space& space, ValueFn value_fn, Sink sink)`.

**Callers and how they call it**

- [`for_serpentine`](#function-099) ([engine.cpp:51](../engine.cpp#L51)): call in enclosing function / its lambda callback; see linked caller for arguments (may be template/constructor lowering).

- [`ditherPass`](#function-107) ([engine.cpp:249](../engine.cpp#L249)): direct C++ call; `quantize_scan(goals.size(), dither_fn, spatial_fn, 1.0f, frames * (levels - 1), Space{}, [&](int row, int col) { return goals.at<double>(row, col); }, [&](int row, int col, int goal, float) { goals...`, `quantize_scan(target.size(), dither_fn, spatial_fn, spatial, levels - 1, space, [&](int row, int col) { return (goals.at<double>(row, col) - displayed.at<double>(row, col)) / (double)(frames - f); ...`.

**CLI invocations / variants:** [R](#scenario-r) [N](#scenario-n) [F](#scenario-f) [O](#scenario-o) [B](#scenario-b) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [L](#scenario-l). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Spy threshold and sink verify target/decision/account units, clamping, residual deposition and call order; spatial 0, 0.5 and 1; uniform/nonuniform Space.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [R](#scenario-r) [N](#scenario-n) [F](#scenario-f) [O](#scenario-o) [B](#scenario-b) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [L](#scenario-l) — Compare raw no-diffusion temporal budgets and encoded output averages across thresholds/ladders; separately assert whole-image light conservation with diffusion and valid debug traces.

#### Anonymous function: `quantize_scan::callback@182`

Source: [engine.cpp:182](../engine.cpp#L182); capture/signature: `[&](int row, int col, int dir)`.

- **Callers/how:** for_serpentine invokes body(row, col, dir) once per pixel.

- **CLI invocations:** [R](#scenario-r) [N](#scenario-n) [F](#scenario-f) [O](#scenario-o) [B](#scenario-b) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [L](#scenario-l); same enclosing branch, not an independently named CLI operation.

- **Unit tests should exist:** Visit order, decision/account distinction, clamping and full residual deposit; sink receives correct display value.

- **Integration tests should exist:** [R](#scenario-r) [N](#scenario-n) [F](#scenario-f) [O](#scenario-o) [B](#scenario-b) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [L](#scenario-l); verify the enclosing function's observable assertion above with the callback branch actually reached.

<a id="function-107"></a>

### `ditherPass` — line 249

Source: [engine.cpp:249](../engine.cpp#L249). Signature: `ditherPass(const cv::Mat& target, const vision::LevelSet& ladder, DitherFn& dither_fn, SpatialFn spatial_fn, float spatial, int levels, int frames, int debug_row, int debug_col)`.

**Callers and how they call it**

- [`dither_main`](#function-096) ([dither.cpp:105](../dither.cpp#L105)): direct C++ call; `ditherPass(target, ladder, *dither_fn, spatial_fn, opt.spatial, opt.levels, opt.frames, opt.debug_row, opt.debug_col)`.

- [`quantize_scan`](#function-106) ([engine.cpp:177](../engine.cpp#L177)): call in enclosing function / its lambda callback; see linked caller for arguments (may be template/constructor lowering).

- [`case_253`](#function-328) ([unittest.cpp:253](../unittest.cpp#L253)): direct C++ call; `ditherPass(target, ladder, fn, spatial_floydsteinberg, 1.0f, 256, 1, -1, -1)`.

- [`budget_holds`](#function-360) ([unittest.cpp:1545](../unittest.cpp#L1545)): direct C++ call; `ditherPass(target, ladder, *fn, spatial_none, 0.0f, c.levels, c.frames, -1, -1)`.

**CLI invocations / variants:** [R](#scenario-r) [N](#scenario-n) [F](#scenario-f) [O](#scenario-o) [B](#scenario-b) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [L](#scenario-l). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** One/many frames, exact no-diffusion per-pixel budget for all rules and ladders; diffusion light conservation separately; new_frame timing, RNG reproducibility and debug output.

**Existing direct C++ test callers:** [`case_253`](#function-328). These do not establish full branch coverage.

**Integration tests should exist:** [R](#scenario-r) [N](#scenario-n) [F](#scenario-f) [O](#scenario-o) [B](#scenario-b) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [L](#scenario-l) — Compare raw no-diffusion temporal budgets and encoded output averages across thresholds/ladders; separately assert whole-image light conservation with diffusion and valid debug traces.

#### Anonymous function: `ditherPass::callback@272`

Source: [engine.cpp:272](../engine.cpp#L272); capture/signature: `[&](int row, int col)`.

- **Callers/how:** quantize_scan invokes the value/sink callback per pixel, or cv::Mat::forEach invokes the conserved-unit conversion callback.

- **CLI invocations:** [R](#scenario-r) [N](#scenario-n) [F](#scenario-f) [O](#scenario-o) [B](#scenario-b) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [L](#scenario-l); same enclosing branch, not an independently named CLI operation.

- **Unit tests should exist:** Goal-pass integer storage, frame remaining-budget division or conserved-unit conversion as applicable; last frame denominator is one; exact coordinates and captured state updates.

- **Integration tests should exist:** [R](#scenario-r) [N](#scenario-n) [F](#scenario-f) [O](#scenario-o) [B](#scenario-b) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [L](#scenario-l); verify the enclosing function's observable assertion above with the callback branch actually reached.

#### Anonymous function: `ditherPass::callback@273`

Source: [engine.cpp:273](../engine.cpp#L273); capture/signature: `[&](int row, int col, int goal, float)`.

- **Callers/how:** quantize_scan invokes the value/sink callback per pixel, or cv::Mat::forEach invokes the conserved-unit conversion callback.

- **CLI invocations:** [R](#scenario-r) [N](#scenario-n) [F](#scenario-f) [O](#scenario-o) [B](#scenario-b) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [L](#scenario-l); same enclosing branch, not an independently named CLI operation.

- **Unit tests should exist:** Goal-pass integer storage, frame remaining-budget division or conserved-unit conversion as applicable; last frame denominator is one; exact coordinates and captured state updates.

- **Integration tests should exist:** [R](#scenario-r) [N](#scenario-n) [F](#scenario-f) [O](#scenario-o) [B](#scenario-b) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [L](#scenario-l); verify the enclosing function's observable assertion above with the callback branch actually reached.

#### Anonymous function: `ditherPass::callback@279`

Source: [engine.cpp:279](../engine.cpp#L279); capture/signature: `[&](double& g, const int*)`.

- **Callers/how:** quantize_scan invokes the value/sink callback per pixel, or cv::Mat::forEach invokes the conserved-unit conversion callback.

- **CLI invocations:** [R](#scenario-r) [N](#scenario-n) [F](#scenario-f) [O](#scenario-o) [B](#scenario-b) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [L](#scenario-l); same enclosing branch, not an independently named CLI operation.

- **Unit tests should exist:** Goal-pass integer storage, frame remaining-budget division or conserved-unit conversion as applicable; last frame denominator is one; exact coordinates and captured state updates.

- **Integration tests should exist:** [R](#scenario-r) [N](#scenario-n) [F](#scenario-f) [O](#scenario-o) [B](#scenario-b) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [L](#scenario-l); verify the enclosing function's observable assertion above with the callback branch actually reached.

#### Anonymous function: `ditherPass::callback@291`

Source: [engine.cpp:291](../engine.cpp#L291); capture/signature: `[&](int row, int col)`.

- **Callers/how:** quantize_scan invokes the value/sink callback per pixel, or cv::Mat::forEach invokes the conserved-unit conversion callback.

- **CLI invocations:** [R](#scenario-r) [N](#scenario-n) [F](#scenario-f) [O](#scenario-o) [B](#scenario-b) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [L](#scenario-l); same enclosing branch, not an independently named CLI operation.

- **Unit tests should exist:** Goal-pass integer storage, frame remaining-budget division or conserved-unit conversion as applicable; last frame denominator is one; exact coordinates and captured state updates.

- **Integration tests should exist:** [R](#scenario-r) [N](#scenario-n) [F](#scenario-f) [O](#scenario-o) [B](#scenario-b) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [L](#scenario-l); verify the enclosing function's observable assertion above with the callback branch actually reached.

#### Anonymous function: `ditherPass::callback@295`

Source: [engine.cpp:295](../engine.cpp#L295); capture/signature: `[&](int row, int col, int display, float ideal)`.

- **Callers/how:** quantize_scan invokes the value/sink callback per pixel, or cv::Mat::forEach invokes the conserved-unit conversion callback.

- **CLI invocations:** [R](#scenario-r) [N](#scenario-n) [F](#scenario-f) [O](#scenario-o) [B](#scenario-b) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [L](#scenario-l); same enclosing branch, not an independently named CLI operation.

- **Unit tests should exist:** Goal-pass integer storage, frame remaining-budget division or conserved-unit conversion as applicable; last frame denominator is one; exact coordinates and captured state updates.

- **Integration tests should exist:** [R](#scenario-r) [N](#scenario-n) [F](#scenario-f) [O](#scenario-o) [B](#scenario-b) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [L](#scenario-l); verify the enclosing function's observable assertion above with the callback branch actually reached.

<a id="file-15"></a>

## `engine.h`

### Declaration: `spatial_none(int row, int col, int dir, cv::Mat& spatial_err, float value)`

Source: [engine.h:107](../engine.h#L107).

Callers, CLI variants, unit tests and integration tests: [`spatial_none`](#function-097). This is the declaration of that same function; do not count it as separate executed coverage.

### Declaration: `spatial_floydsteinberg(int row, int col, int dir, cv::Mat& spatial_err, float value)`

Source: [engine.h:110](../engine.h#L110).

Callers, CLI variants, unit tests and integration tests: [`spatial_floydsteinberg`](#function-098). This is the declaration of that same function; do not count it as separate executed coverage.

### Declaration: `ditherPass(const cv::Mat& target, const vision::LevelSet& ladder, DitherFn& dither_fn, SpatialFn spatial_fn, float spatial, int levels, int frames, int debug_row, int debug_col)`

Source: [engine.h:114](../engine.h#L114).

Callers, CLI variants, unit tests and integration tests: [`ditherPass`](#function-107). This is the declaration of that same function; do not count it as separate executed coverage.

<a id="function-114"></a>

### `DitherFn::~DitherFn` — line 32

Source: [engine.h:32](../engine.h#L32). Signature: `virtual ~DitherFn() = default;`.

**Callers and how they call it**

- [`dither_main`](#function-096) ([dither.cpp:105](../dither.cpp#L105)): RAII: unique_ptr<DitherFn> destruction dispatches virtually to concrete rule.

- [`MaskDither::MaskDither`](#function-111) ([engine.h:60](../engine.h#L60)): direct C++ call; receiver/signature resolved by compiler.

- [`budget_holds`](#function-360) ([unittest.cpp:1545](../unittest.cpp#L1545)): RAII: unique_ptr<DitherFn> destruction dispatches virtually to concrete rule.

**CLI invocations / variants:** [R](#scenario-r) [N](#scenario-n) [F](#scenario-f) [B](#scenario-b) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [L](#scenario-l). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Delete a test-derived object through unique_ptr<DitherFn> and verify its destructor runs exactly once.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [R](#scenario-r) [N](#scenario-n) [F](#scenario-f) [B](#scenario-b) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [L](#scenario-l) — Run threshold/frame variants, check deterministic seeded replay and no-diffusion budgets; source-coordinate and threshold-equality assertions remain small unit tests.

<a id="function-115"></a>

### `DitherFn::operator()` — line 33

Source: [engine.h:33](../engine.h#L33). Signature: `virtual int operator()(float v, int row, int col) = 0;`.

**Callers and how they call it**

- [`quantize_scan`](#function-106) ([engine.cpp:177](../engine.cpp#L177)): virtual: dither_fn(shown, row, col) once per pixel; concrete rule selected by threshold.

**CLI invocations / variants:** [R](#scenario-r) [N](#scenario-n) [F](#scenario-f) [B](#scenario-b) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [L](#scenario-l). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Abstract dispatch contract: a spy implementation receives float value and exact coordinates; quantize_scan owns output clamping.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [R](#scenario-r) [N](#scenario-n) [F](#scenario-f) [B](#scenario-b) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [L](#scenario-l) — Run threshold/frame variants, check deterministic seeded replay and no-diffusion budgets; source-coordinate and threshold-equality assertions remain small unit tests.

<a id="function-108"></a>

### `DitherFn::new_frame` — line 34

Source: [engine.h:34](../engine.h#L34). Signature: `new_frame()`.

**Callers and how they call it**

- [`ditherPass`](#function-107) ([engine.cpp:249](../engine.cpp#L249)): virtual: dither_fn.new_frame() once at the start of each output frame, after the goal pass.

**CLI invocations / variants:** [R](#scenario-r) [N](#scenario-n) [F](#scenario-f). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Base hook leaves round/random behavior unchanged; invoke through a base reference.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [R](#scenario-r) [N](#scenario-n) [F](#scenario-f) — Run threshold/frame variants, check deterministic seeded replay and no-diffusion budgets; source-coordinate and threshold-equality assertions remain small unit tests.

<a id="function-109"></a>

### `RoundDither::operator()` — line 39

Source: [engine.h:39](../engine.h#L39). Signature: `operator()(float v, int /*row*/, int /*col*/) override`.

**Callers and how they call it**

- [`quantize_scan`](#function-106) ([engine.cpp:177](../engine.cpp#L177)): virtual: dither_fn(shown, row, col) once per pixel; concrete rule selected by threshold.

**CLI invocations / variants:** [R](#scenario-r) [F](#scenario-f) [O](#scenario-o). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Negative, integer and half-way values follow lround; no coordinate dependence.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [R](#scenario-r) [F](#scenario-f) [O](#scenario-o) — Run threshold/frame variants, check deterministic seeded replay and no-diffusion budgets; source-coordinate and threshold-equality assertions remain small unit tests.

<a id="function-110"></a>

### `RandomDither::operator()` — line 46

Source: [engine.h:46](../engine.h#L46). Signature: `operator()(float v, int /*row*/, int /*col*/) override`.

**Callers and how they call it**

- [`quantize_scan`](#function-106) ([engine.cpp:177](../engine.cpp#L177)): virtual: dither_fn(shown, row, col) once per pixel; concrete rule selected by threshold.

**CLI invocations / variants:** [N](#scenario-n). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Integer inputs exact; fractional/negative values choose floor or ceiling; seeded replay; statistical firing rate with a justified confidence interval.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [N](#scenario-n) — Run threshold/frame variants, check deterministic seeded replay and no-diffusion budgets; source-coordinate and threshold-equality assertions remain small unit tests.

<a id="function-111"></a>

### `MaskDither::MaskDither` — line 60

Source: [engine.h:60](../engine.h#L60). Signature: `MaskDither(cv::Mat m) : mask(std::move(m))`.

**Callers and how they call it**

- [`dither_main`](#function-096) ([dither.cpp:105](../dither.cpp#L105)): construction: cv::Mat supplied to MaskDither, directly or through std::make_unique.

- [`budget_holds`](#function-360) ([unittest.cpp:1545](../unittest.cpp#L1545)): construction: cv::Mat supplied to MaskDither, directly or through std::make_unique.

**CLI invocations / variants:** [B](#scenario-b) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [L](#scenario-l). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Stored image remains valid after caller's cv::Mat goes out of scope; rectangular mask; define empty/type preconditions.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [B](#scenario-b) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [L](#scenario-l) — Run threshold/frame variants, check deterministic seeded replay and no-diffusion budgets; source-coordinate and threshold-equality assertions remain small unit tests.

<a id="function-112"></a>

### `MaskDither::operator()` — line 61

Source: [engine.h:61](../engine.h#L61). Signature: `operator()(float v, int row, int col) override`.

**Callers and how they call it**

- [`quantize_scan`](#function-106) ([engine.cpp:177](../engine.cpp#L177)): virtual: dither_fn(shown, row, col) once per pixel; concrete rule selected by threshold.

**CLI invocations / variants:** [B](#scenario-b) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [L](#scenario-l). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Negative input uses floor; strict threshold equality; scale 254 and endpoint masks; row/column wrap, integer inputs and shifted frames.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [B](#scenario-b) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [L](#scenario-l) — Run threshold/frame variants, check deterministic seeded replay and no-diffusion budgets; source-coordinate and threshold-equality assertions remain small unit tests.

<a id="function-113"></a>

### `MaskDither::new_frame` — line 95

Source: [engine.h:95](../engine.h#L95). Signature: `new_frame() override`.

**Callers and how they call it**

- [`ditherPass`](#function-107) ([engine.cpp:249](../engine.cpp#L249)): virtual: dither_fn.new_frame() once at the start of each output frame, after the goal pass.

**CLI invocations / variants:** [B](#scenario-b) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [L](#scenario-l). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Offsets stay in each dimension, seeded replay matches RNG sequence; 1x1 and rectangular masks; test behavior through operator().

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [B](#scenario-b) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [L](#scenario-l) — Run threshold/frame variants, check deterministic seeded replay and no-diffusion budgets; source-coordinate and threshold-equality assertions remain small unit tests.

<a id="file-16"></a>

## `experiments/inpaint-20260927/thread_control.cpp`

<a id="function-116"></a>

### `main` — line 7

Source: [experiments/inpaint-20260927/thread_control.cpp:7](../experiments/inpaint-20260927/thread_control.cpp#L7). Signature: `main(int argc, char** argv)`.

**Callers and how they call it**

- Process runtime invokes this executable entry point with argc/argv; it is not called by another project function.

**CLI invocations / variants:** [EI](#scenario-ei). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Probe path, argc validation, stoi failure, thread count application and exact argv shifting; exception translated to failure.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [EI](#scenario-ei) — Build the archived adapter separately and compare forwarded CLI output at 1/2 threads; probe reports configured thread count; malformed argument fails.

<a id="file-17"></a>

## `experiments/markov-20260927/draw.cpp`

<a id="function-117"></a>

### `main` — line 8

Source: [experiments/markov-20260927/draw.cpp:8](../experiments/markov-20260927/draw.cpp#L8). Signature: `main(int argc, char** argv)`.

**Callers and how they call it**

- Process runtime invokes this executable entry point with argc/argv; it is not called by another project function.

**CLI invocations / variants:** [EM](#scenario-em). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** First add compile/link test against current API. Then fit-key reuse/invalidation (including every fit-affecting parameter), stream-open errors, malformed rows and output failures.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [EM](#scenario-em) — After adapting the stale API call, feed two same-key then changed-key jobs and verify reuse/refit timings, pixels, CSV and failure statuses.

<a id="file-18"></a>

## `generators.cpp`

<a id="function-118"></a>

### `blue_work_of` — line 22

Source: [generators.cpp:22](../generators.cpp#L22). Signature: `blue_work_of(const params::Resolver& r, int w, int h, int)`.

**Callers and how they call it**

- [`mask_seconds`](#function-207) ([mask.cpp:43](../mask.cpp#L43)): indirect: selected Generator::work; estimate uses reference pairs, measurement passes 0 to resolve actual pairs.

- [`record_build_rate`](#function-212) ([mask.cpp:355](../mask.cpp#L355)): indirect: selected Generator::work; estimate uses reference pairs, measurement passes 0 to resolve actual pairs.

**CLI invocations / variants:** [B](#scenario-b) [V](#scenario-v) [E](#scenario-e). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Forward resolved settings and width/height to the corresponding work formula; changing only a relevant parameter changes the estimate. For markov, pairs=0 resolves the registry while explicit pairs bypass that read.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [B](#scenario-b) [V](#scenario-v) [E](#scenario-e) — Run each selected row through CLI and through a linked adapter test; verify correct sample policy, resolved parameters, diagnostics and failure propagation.

<a id="function-119"></a>

### `blue_build` — line 27

Source: [generators.cpp:27](../generators.cpp#L27). Signature: `blue_build(params::Params& P, const MaskRequest& q)`.

**Callers and how they call it**

- [`build_with_method`](#function-211) ([mask.cpp:296](../mask.cpp#L296)): indirect: selected Generator::build(P, q), with size, sample set, seed and log.

- [`case_1235`](#function-351) ([unittest.cpp:1235](../unittest.cpp#L1235)): indirect: blue->build(before/after, q), comparing a changed intermediate.

**CLI invocations / variants:** [B](#scenario-b). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Forward size, seed, correct sample representation and every resolved generator setting; null/log stream behavior; propagate empty result. Compare this adapter with a direct blue builder call using identical settings.

**Existing direct C++ test callers:** [`case_1235`](#function-351). These do not establish full branch coverage.

**Integration tests should exist:** [B](#scenario-b) — Run each selected row through CLI and through a linked adapter test; verify correct sample policy, resolved parameters, diagnostics and failure propagation.

<a id="function-120"></a>

### `dft_work_of` — line 53

Source: [generators.cpp:53](../generators.cpp#L53). Signature: `dft_work_of(const params::Resolver& r, int w, int h, int)`.

**Callers and how they call it**

- [`mask_seconds`](#function-207) ([mask.cpp:43](../mask.cpp#L43)): indirect: selected Generator::work; estimate uses reference pairs, measurement passes 0 to resolve actual pairs.

- [`record_build_rate`](#function-212) ([mask.cpp:355](../mask.cpp#L355)): indirect: selected Generator::work; estimate uses reference pairs, measurement passes 0 to resolve actual pairs.

**CLI invocations / variants:** [D](#scenario-d) [V](#scenario-v) [E](#scenario-e). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Forward resolved settings and width/height to the corresponding work formula; changing only a relevant parameter changes the estimate. For markov, pairs=0 resolves the registry while explicit pairs bypass that read.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [D](#scenario-d) [V](#scenario-v) [E](#scenario-e) — Run each selected row through CLI and through a linked adapter test; verify correct sample policy, resolved parameters, diagnostics and failure propagation.

<a id="function-121"></a>

### `dft_build` — line 58

Source: [generators.cpp:58](../generators.cpp#L58). Signature: `dft_build(params::Params& P, const MaskRequest& q)`.

**Callers and how they call it**

- [`build_with_method`](#function-211) ([mask.cpp:296](../mask.cpp#L296)): indirect: selected Generator::build(P, q), with size, sample set, seed and log.

**CLI invocations / variants:** [D](#scenario-d). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Forward size, seed, correct sample representation and every resolved generator setting; null/log stream behavior; propagate empty result. Compare this adapter with a direct dft builder call using identical settings.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [D](#scenario-d) — Run each selected row through CLI and through a linked adapter test; verify correct sample policy, resolved parameters, diagnostics and failure propagation.

<a id="function-122"></a>

### `markov_work_of` — line 70

Source: [generators.cpp:70](../generators.cpp#L70). Signature: `markov_work_of(const params::Resolver& r, int w, int h, int pairs)`.

**Callers and how they call it**

- [`mask_seconds`](#function-207) ([mask.cpp:43](../mask.cpp#L43)): indirect: selected Generator::work; estimate uses reference pairs, measurement passes 0 to resolve actual pairs.

- [`record_build_rate`](#function-212) ([mask.cpp:355](../mask.cpp#L355)): indirect: selected Generator::work; estimate uses reference pairs, measurement passes 0 to resolve actual pairs.

**CLI invocations / variants:** [M](#scenario-m) [V](#scenario-v) [E](#scenario-e). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Forward resolved settings and width/height to the corresponding work formula; changing only a relevant parameter changes the estimate. For markov, pairs=0 resolves the registry while explicit pairs bypass that read.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [M](#scenario-m) [V](#scenario-v) [E](#scenario-e) — Run each selected row through CLI and through a linked adapter test; verify correct sample policy, resolved parameters, diagnostics and failure propagation.

<a id="function-123"></a>

### `markov_build` — line 79

Source: [generators.cpp:79](../generators.cpp#L79). Signature: `markov_build(params::Params& P, const MaskRequest& q)`.

**Callers and how they call it**

- [`build_with_method`](#function-211) ([mask.cpp:296](../mask.cpp#L296)): indirect: selected Generator::build(P, q), with size, sample set, seed and log.

**CLI invocations / variants:** [M](#scenario-m). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Forward size, seed, correct sample representation and every resolved generator setting; null/log stream behavior; propagate empty result. Compare this adapter with a direct markov builder call using identical settings.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [M](#scenario-m) — Run each selected row through CLI and through a linked adapter test; verify correct sample policy, resolved parameters, diagnostics and failure propagation.

<a id="function-124"></a>

### `inpaint_work_of` — line 88

Source: [generators.cpp:88](../generators.cpp#L88). Signature: `inpaint_work_of(const params::Resolver& r, int w, int h, int)`.

**Callers and how they call it**

- [`mask_seconds`](#function-207) ([mask.cpp:43](../mask.cpp#L43)): indirect: selected Generator::work; estimate uses reference pairs, measurement passes 0 to resolve actual pairs.

- [`record_build_rate`](#function-212) ([mask.cpp:355](../mask.cpp#L355)): indirect: selected Generator::work; estimate uses reference pairs, measurement passes 0 to resolve actual pairs.

**CLI invocations / variants:** [P](#scenario-p) [V](#scenario-v) [E](#scenario-e). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Forward resolved settings and width/height to the corresponding work formula; changing only a relevant parameter changes the estimate. For markov, pairs=0 resolves the registry while explicit pairs bypass that read.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [P](#scenario-p) [V](#scenario-v) [E](#scenario-e) — Run each selected row through CLI and through a linked adapter test; verify correct sample policy, resolved parameters, diagnostics and failure propagation.

<a id="function-125"></a>

### `inpaint_build` — line 98

Source: [generators.cpp:98](../generators.cpp#L98). Signature: `inpaint_build(params::Params& P, const MaskRequest& q)`.

**Callers and how they call it**

- [`build_with_method`](#function-211) ([mask.cpp:296](../mask.cpp#L296)): indirect: selected Generator::build(P, q), with size, sample set, seed and log.

**CLI invocations / variants:** [P](#scenario-p). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Forward size, seed, correct sample representation and every resolved generator setting; null/log stream behavior; propagate empty result. Compare this adapter with a direct inpaint builder call using identical settings.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [P](#scenario-p) — Run each selected row through CLI and through a linked adapter test; verify correct sample policy, resolved parameters, diagnostics and failure propagation.

<a id="function-126"></a>

### `markov_validate` — line 104

Source: [generators.cpp:104](../generators.cpp#L104). Signature: `markov_validate(params::Params& P)`.

**Callers and how they call it**

- [`parseOptions`](#function-027) ([cli.cpp:178](../cli.cpp#L178)): indirect: selected Generator::validate(P).

- [`parse`](#function-165) ([maketexture.cpp:171](../maketexture.cpp#L171)): indirect: selected Generator::validate(P).

**CLI invocations / variants:** [M](#scenario-m). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Accept flat/mi/ml; reject empty, unknown and case variants with diagnostic.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [M](#scenario-m) — Run each selected row through CLI and through a linked adapter test; verify correct sample policy, resolved parameters, diagnostics and failure propagation.

<a id="function-127"></a>

### `direct_work_of` — line 114

Source: [generators.cpp:114](../generators.cpp#L114). Signature: `direct_work_of(const params::Resolver& r, int w, int h, int)`.

**Callers and how they call it**

- [`mask_seconds`](#function-207) ([mask.cpp:43](../mask.cpp#L43)): indirect: selected Generator::work; estimate uses reference pairs, measurement passes 0 to resolve actual pairs.

- [`record_build_rate`](#function-212) ([mask.cpp:355](../mask.cpp#L355)): indirect: selected Generator::work; estimate uses reference pairs, measurement passes 0 to resolve actual pairs.

**CLI invocations / variants:** [Q](#scenario-q) [V](#scenario-v) [E](#scenario-e). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Forward resolved settings and width/height to the corresponding work formula; changing only a relevant parameter changes the estimate. For markov, pairs=0 resolves the registry while explicit pairs bypass that read.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [Q](#scenario-q) [V](#scenario-v) [E](#scenario-e) — Run each selected row through CLI and through a linked adapter test; verify correct sample policy, resolved parameters, diagnostics and failure propagation.

<a id="function-128"></a>

### `direct_sample_limit` — line 124

Source: [generators.cpp:124](../generators.cpp#L124). Signature: `direct_sample_limit(const params::Resolver& r, long long sampleSide)`.

**Callers and how they call it**

- [`mask_side`](#function-208) ([mask.cpp:99](../mask.cpp#L99)): indirect: selected Generator::sample_limit(r, sample width/height).

**CLI invocations / variants:** [Q](#scenario-q) [V](#scenario-v) [E](#scenario-e). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Scale first, truncate consistently then subtract overlap; tiny samples and fractional scale; match builder's actual size constraints.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [Q](#scenario-q) [V](#scenario-v) [E](#scenario-e) — Run each selected row through CLI and through a linked adapter test; verify correct sample policy, resolved parameters, diagnostics and failure propagation.

<a id="function-129"></a>

### `direct_build` — line 130

Source: [generators.cpp:130](../generators.cpp#L130). Signature: `direct_build(params::Params& P, const MaskRequest& q)`.

**Callers and how they call it**

- [`build_with_method`](#function-211) ([mask.cpp:296](../mask.cpp#L296)): indirect: selected Generator::build(P, q), with size, sample set, seed and log.

**CLI invocations / variants:** [Q](#scenario-q). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Forward size, seed, correct sample representation and every resolved generator setting; null/log stream behavior; propagate empty result. Compare this adapter with a direct direct builder call using identical settings.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [Q](#scenario-q) — Run each selected row through CLI and through a linked adapter test; verify correct sample policy, resolved parameters, diagnostics and failure propagation.

<a id="function-130"></a>

### `markov_params_from` — line 149

Source: [generators.cpp:149](../generators.cpp#L149). Signature: `markov_params_from(params::Params& P)`.

**Callers and how they call it**

- [`markov_build`](#function-123) ([generators.cpp:79](../generators.cpp#L79)): direct C++ call; `markov_params_from(P)`.

- [`run`](#function-166) ([maketexture.cpp:239](../maketexture.cpp#L239)): direct C++ call; `markov_params_from(P)`.

- [`calibrate_markov_for`](#function-204) ([markovcalib.cpp:403](../markovcalib.cpp#L403)): direct C++ call; `markov_params_from(P)`.

**CLI invocations / variants:** [M](#scenario-m) [CM](#scenario-cm). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Every struct field equals its registry key, including scale/highpass and overrides; no missing fields when schema grows.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [M](#scenario-m) [CM](#scenario-cm) — Run each selected row through CLI and through a linked adapter test; verify correct sample policy, resolved parameters, diagnostics and failure propagation.

<a id="function-131"></a>

### `inpaint_params_from` — line 169

Source: [generators.cpp:169](../generators.cpp#L169). Signature: `inpaint_params_from(params::Params& P)`.

**Callers and how they call it**

- [`inpaint_build`](#function-125) ([generators.cpp:98](../generators.cpp#L98)): direct C++ call; `inpaint_params_from(P)`.

- [`calibrate_inpaint_for`](#function-156) ([inpaintcalib.cpp:107](../inpaintcalib.cpp#L107)): direct C++ call; `inpaint_params_from(P)`.

- [`run`](#function-166) ([maketexture.cpp:239](../maketexture.cpp#L239)): direct C++ call; `inpaint_params_from(P)`.

**CLI invocations / variants:** [P](#scenario-p) [CP](#scenario-cp). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Every struct field equals its registry key, including cap/groups/scales; defaults and overrides agree.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [P](#scenario-p) [CP](#scenario-cp) — Run each selected row through CLI and through a linked adapter test; verify correct sample policy, resolved parameters, diagnostics and failure propagation.

<a id="function-132"></a>

### `mask::calibratable` — line 187

Source: [generators.cpp:187](../generators.cpp#L187). Signature: `mask::calibratable()`.

**Callers and how they call it**

- [`register_markov_params`](#function-198) ([markov.cpp:1005](../markov.cpp#L1005)): direct C++ call; `calibratable()`.

**CLI invocations / variants:** [E](#scenario-e) [B](#scenario-b) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [CD](#scenario-cd) [CM](#scenario-cm) [CP](#scenario-cp) [T](#scenario-t). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** False when not building; true only for selected table rows with calibrate hooks; loaded-mask path false.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [E](#scenario-e) [B](#scenario-b) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [CD](#scenario-cd) [CM](#scenario-cm) [CP](#scenario-cp) [T](#scenario-t) — Run each selected row through CLI and through a linked adapter test; verify correct sample policy, resolved parameters, diagnostics and failure propagation.

#### Anonymous function: `mask::calibratable::callback@189`

Source: [generators.cpp:189](../generators.cpp#L189); capture/signature: `[](const params::Resolver& r)`.

- **Callers/how:** params::Params::applies, stranded and applicability_inputs invoke Applicability::test(Resolver); mask::calibratable constructs the callback.

- **CLI invocations:** [E](#scenario-e) [B](#scenario-b) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [CD](#scenario-cd) [CM](#scenario-cm) [CP](#scenario-cp) [T](#scenario-t); same enclosing branch, not an independently named CLI operation.

- **Unit tests should exist:** Predicate true/false boundaries, every key it reads and exact scope; no modification of User values. Vary each read dependency independently: `mask::keys::Method`.

- **Integration tests should exist:** [E](#scenario-e) [B](#scenario-b) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [CD](#scenario-cd) [CM](#scenario-cm) [CP](#scenario-cp) [T](#scenario-t); verify the enclosing function's observable assertion above with the callback branch actually reached.

<a id="function-133"></a>

### `generators` — line 197

Source: [generators.cpp:197](../generators.cpp#L197). Signature: `generators()`.

**Callers and how they call it**

- [`usage_ceilings`](#function-030) ([cli.cpp:525](../cli.cpp#L525)): direct C++ call; `generators()`.

- [`usage_generators`](#function-031) ([cli.cpp:568](../cli.cpp#L568)): direct C++ call; `generators()`.

- [`dither_main`](#function-096) ([dither.cpp:105](../dither.cpp#L105)): direct C++ call; `generators()`.

- [`generator_list`](#function-134) ([generators.cpp:218](../generators.cpp#L218)): direct C++ call; `generators()`.

- [`reconcile_candidates`](#function-135) ([generators.cpp:229](../generators.cpp#L229)): direct C++ call; `generators()`.

- [`find_generator`](#function-136) ([generators.cpp:237](../generators.cpp#L237)): direct C++ call; `generators()`.

- [`accepted`](#function-160) ([maketexture.cpp:86](../maketexture.cpp#L86)): direct C++ call; `generators()`.

- [`list_options`](#function-161) ([maketexture.cpp:102](../maketexture.cpp#L102)): direct C++ call; `generators()`.

- [`run`](#function-166) ([maketexture.cpp:239](../maketexture.cpp#L239)): direct C++ call; `generators()`.

- [`cache_registry`](#function-345) ([unittest.cpp:981](../unittest.cpp#L981)): direct C++ call; `generators()`.

- [`case_990`](#function-346) ([unittest.cpp:990](../unittest.cpp#L990)): direct C++ call; `generators()`.

- [`case_1145`](#function-350) ([unittest.cpp:1145](../unittest.cpp#L1145)): direct C++ call; `generators()`.

**CLI invocations / variants:** [E](#scenario-e) [B](#scenario-b) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [CD](#scenario-cd) [CM](#scenario-cm) [CP](#scenario-cp) [T](#scenario-t). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Exactly five unique names and correct callback/null combinations; sample and combines policy; callbacks accept null log where promised.

**Existing direct C++ test callers:** [`case_990`](#function-346), [`case_1145`](#function-350). These do not establish full branch coverage.

**Integration tests should exist:** [E](#scenario-e) [B](#scenario-b) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [CD](#scenario-cd) [CM](#scenario-cm) [CP](#scenario-cp) [T](#scenario-t) — Run each selected row through CLI and through a linked adapter test; verify correct sample policy, resolved parameters, diagnostics and failure propagation.

<a id="function-134"></a>

### `generator_list` — line 218

Source: [generators.cpp:218](../generators.cpp#L218). Signature: `generator_list(bool (*pick)(const Generator&))`.

**Callers and how they call it**

- [`parseOptions`](#function-027) ([cli.cpp:178](../cli.cpp#L178)): direct C++ call; `generator_list(any_gen)`, `generator_list(imitating_gen)`.

**CLI invocations / variants:** [E](#scenario-e) [B](#scenario-b) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [CD](#scenario-cd) [CM](#scenario-cm) [CP](#scenario-cp) [T](#scenario-t). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Zero/one/two/many selections, punctuation and stable order; predicate invoked once for each row.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [E](#scenario-e) [B](#scenario-b) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [CD](#scenario-cd) [CM](#scenario-cm) [CP](#scenario-cp) [T](#scenario-t) — Run each selected row through CLI and through a linked adapter test; verify correct sample policy, resolved parameters, diagnostics and failure propagation.

<a id="function-135"></a>

### `reconcile_candidates` — line 229

Source: [generators.cpp:229](../generators.cpp#L229). Signature: `reconcile_candidates()`.

**Callers and how they call it**

- [`register_mask_params`](#function-209) ([mask.cpp:160](../mask.cpp#L160)): direct C++ call; `reconcile_candidates()`.

**CLI invocations / variants:** [E](#scenario-e) [B](#scenario-b) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [CD](#scenario-cd) [CM](#scenario-cm) [CP](#scenario-cp) [T](#scenario-t). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Only offer_in_reconcile rows included, no duplicates, deterministic order.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [E](#scenario-e) [B](#scenario-b) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [CD](#scenario-cd) [CM](#scenario-cm) [CP](#scenario-cp) [T](#scenario-t) — Run each selected row through CLI and through a linked adapter test; verify correct sample policy, resolved parameters, diagnostics and failure propagation.

<a id="function-136"></a>

### `find_generator` — line 237

Source: [generators.cpp:237](../generators.cpp#L237). Signature: `find_generator(const std::string& name)`.

**Callers and how they call it**

- [`parseOptions`](#function-027) ([cli.cpp:178](../cli.cpp#L178)): direct C++ call; `find_generator(opt.maskMethod)`.

- [`mask::calibratable`](#function-132) ([generators.cpp:187](../generators.cpp#L187)): call in enclosing function / its lambda callback; `find_generator(r.str(mask::keys::Method))`.

- [`parse`](#function-165) ([maketexture.cpp:171](../maketexture.cpp#L171)): direct C++ call; `find_generator(r.method)`.

- [`mask_seconds`](#function-207) ([mask.cpp:43](../mask.cpp#L43)): direct C++ call; `find_generator(method)`.

- [`mask_side`](#function-208) ([mask.cpp:99](../mask.cpp#L99)): direct C++ call; `find_generator(method)`.

- [`build_with_method`](#function-211) ([mask.cpp:296](../mask.cpp#L296)): direct C++ call; `find_generator(opt.maskMethod)`.

- [`record_build_rate`](#function-212) ([mask.cpp:355](../mask.cpp#L355)): direct C++ call; `find_generator(opt.maskMethod)`.

- [`maybe_calibrate`](#function-214) ([mask.cpp:435](../mask.cpp#L435)): direct C++ call; `find_generator(opt.maskMethod)`.

- [`configured_registry`](#function-349) ([unittest.cpp:1134](../unittest.cpp#L1134)): direct C++ call; `find_generator(method)`.

- [`case_1235`](#function-351) ([unittest.cpp:1235](../unittest.cpp#L1235)): direct C++ call; `find_generator("blue")`.

**CLI invocations / variants:** [E](#scenario-e) [B](#scenario-b) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [CD](#scenario-cd) [CM](#scenario-cm) [CP](#scenario-cp) [T](#scenario-t). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Each registered name returns that row; empty/unknown/case-changed names return null.

**Existing direct C++ test callers:** [`case_1235`](#function-351). These do not establish full branch coverage.

**Integration tests should exist:** [E](#scenario-e) [B](#scenario-b) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [CD](#scenario-cd) [CM](#scenario-cm) [CP](#scenario-cp) [T](#scenario-t) — Run each selected row through CLI and through a linked adapter test; verify correct sample policy, resolved parameters, diagnostics and failure propagation.

<a id="file-19"></a>

## `generators.h`

### Declaration: `markov_params_from(params::Params& P)`

Source: [generators.h:121](../generators.h#L121).

Callers, CLI variants, unit tests and integration tests: [`markov_params_from`](#function-130). This is the declaration of that same function; do not count it as separate executed coverage.

### Declaration: `inpaint_params_from(params::Params& P)`

Source: [generators.h:125](../generators.h#L125).

Callers, CLI variants, unit tests and integration tests: [`inpaint_params_from`](#function-131). This is the declaration of that same function; do not count it as separate executed coverage.

### Declaration: `generators()`

Source: [generators.h:128](../generators.h#L128).

Callers, CLI variants, unit tests and integration tests: [`generators`](#function-133). This is the declaration of that same function; do not count it as separate executed coverage.

### Declaration: `reconcile_candidates()`

Source: [generators.h:138](../generators.h#L138).

Callers, CLI variants, unit tests and integration tests: [`reconcile_candidates`](#function-135). This is the declaration of that same function; do not count it as separate executed coverage.

### Declaration: `find_generator(const std::string& name)`

Source: [generators.h:142](../generators.h#L142).

Callers, CLI variants, unit tests and integration tests: [`find_generator`](#function-136). This is the declaration of that same function; do not count it as separate executed coverage.

<a id="function-137"></a>

### `any_gen` — line 134

Source: [generators.h:134](../generators.h#L134). Signature: `any_gen(const Generator&)`.

**Callers and how they call it**

- [`generator_list`](#function-134) ([generators.cpp:218](../generators.cpp#L218)): indirect: pick(g) predicate for each generator row.

**CLI invocations / variants:** [E](#scenario-e) [H](#scenario-h) [X](#scenario-x). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Always true for all table rows and a synthetic row.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [E](#scenario-e) [H](#scenario-h) [X](#scenario-x) — Help/error lists and reconciliation must enumerate the same supported rows as actual builds; add a linked table-consistency test.

<a id="function-138"></a>

### `imitating_gen` — line 135

Source: [generators.h:135](../generators.h#L135). Signature: `imitating_gen(const Generator& g)`.

**Callers and how they call it**

- [`generator_list`](#function-134) ([generators.cpp:218](../generators.cpp#L218)): indirect: pick(g) predicate for each generator row.

**CLI invocations / variants:** [E](#scenario-e) [H](#scenario-h) [X](#scenario-x). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Exactly reflects needs_sample, independent of name and combines.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [E](#scenario-e) [H](#scenario-h) [X](#scenario-x) — Help/error lists and reconciliation must enumerate the same supported rows as actual builds; add a linked table-consistency test.

<a id="file-20"></a>

## `inpaint.cpp`

<a id="function-139"></a>

### `downsample` — line 58

Source: [inpaint.cpp:58](../inpaint.cpp#L58). Signature: `downsample(const cv::Mat& src, int f)`.

**Callers and how they call it**

- [`downsample_set`](#function-140) ([inpaint.cpp:79](../inpaint.cpp#L79)): direct C++ call; `downsample(s, f)`.

**CLI invocations / variants:** [P](#scenario-p) [CP](#scenario-cp) [T](#scenario-t). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Exact block means, nondivisible dimensions, f=1, type and dimensions; independent hand-calculated small array.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [P](#scenario-p) [CP](#scenario-cp) [T](#scenario-t) — Compare one/multiple scans, scales/cap variants and thread counts; check no joins between input scans, mask rank histogram and reproducibility independent of earlier builds.

<a id="function-140"></a>

### `downsample_set` — line 79

Source: [inpaint.cpp:79](../inpaint.cpp#L79). Signature: `downsample_set(const samples::Set& set, int f)`.

**Callers and how they call it**

- [`synthesise_inpaint_field`](#function-152) ([inpaint.cpp:363](../inpaint.cpp#L363)): direct C++ call; `downsample_set(prepared, f)`.

**CLI invocations / variants:** [P](#scenario-p) [CP](#scenario-cp) [T](#scenario-t). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Preserve scan separation and order, skip unusable coarse scans as implemented; unequal image sizes.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [P](#scenario-p) [CP](#scenario-cp) [T](#scenario-t) — Compare one/multiple scans, scales/cap variants and thread counts; check no joins between input scans, mask rank histogram and reproducibility independent of earlier builds.

<a id="function-141"></a>

### `set_coherence` — line 92

Source: [inpaint.cpp:92](../inpaint.cpp#L92). Signature: `set_coherence(const samples::Set& set)`.

**Callers and how they call it**

- [`synthesise_inpaint_field`](#function-152) ([inpaint.cpp:363](../inpaint.cpp#L363)): direct C++ call; `set_coherence(src)`.

**CLI invocations / variants:** [P](#scenario-p) [CP](#scenario-cp) [T](#scenario-t). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Sum tensors before taking ratio; disagreeing orientations cancel appropriately; empty/constant and one-scan cases.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [P](#scenario-p) [CP](#scenario-cp) [T](#scenario-t) — Compare one/multiple scans, scales/cap variants and thread counts; check no joins between input scans, mask rank histogram and reproducibility independent of earlier builds.

<a id="function-142"></a>

### `PatchDatabase::PatchDatabase` — line 113

Source: [inpaint.cpp:113](../inpaint.cpp#L113). Signature: `PatchDatabase(const samples::Set& set, const std::vector<cv::Point>& ring, int r, int want, int dims, std::mt19937_64& rng)`.

**Callers and how they call it**

- [`synthesise_inpaint_field`](#function-152) ([inpaint.cpp:363](../inpaint.cpp#L363)): direct C++ call; see linked caller for arguments (may be template/constructor lowering).

**CLI invocations / variants:** [P](#scenario-p) [CP](#scenario-cp) [T](#scenario-t). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Patch quotas proportional to usable centers; remainder allocation and unusable scans; no crossing scan boundaries; seed repeatability and PCA dimension constraints.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [P](#scenario-p) [CP](#scenario-cp) [T](#scenario-t) — Compare one/multiple scans, scales/cap variants and thread counts; check no joins between input scans, mask rank histogram and reproducibility independent of earlier builds.

<a id="function-143"></a>

### `PatchDatabase::size` — line 170

Source: [inpaint.cpp:170](../inpaint.cpp#L170). Signature: `size() const`.

**Callers and how they call it**

- [`synthesise_inpaint_field`](#function-152) ([inpaint.cpp:363](../inpaint.cpp#L363)): direct C++ call; receiver/signature resolved by compiler.

**CLI invocations / variants:** [P](#scenario-p) [CP](#scenario-cp) [T](#scenario-t). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Return number actually sampled rather than requested, including capped and minimal databases.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [P](#scenario-p) [CP](#scenario-cp) [T](#scenario-t) — Compare one/multiple scans, scales/cap variants and thread counts; check no joins between input scans, mask rank histogram and reproducibility independent of earlier builds.

<a id="function-144"></a>

### `PatchDatabase::centre` — line 171

Source: [inpaint.cpp:171](../inpaint.cpp#L171). Signature: `centre(int i) const`.

**Callers and how they call it**

- [`sweep_level`](#function-149) ([inpaint.cpp:291](../inpaint.cpp#L291)): direct C++ call; receiver/signature resolved by compiler.

**CLI invocations / variants:** [P](#scenario-p) [CP](#scenario-cp) [T](#scenario-t). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Every valid index retrieves the stored source center; verify against deterministic sampled patches.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [P](#scenario-p) [CP](#scenario-cp) [T](#scenario-t) — Compare one/multiple scans, scales/cap variants and thread counts; check no joins between input scans, mask rank histogram and reproducibility independent of earlier builds.

<a id="function-145"></a>

### `PatchDatabase::nearest` — line 215

Source: [inpaint.cpp:215](../inpaint.cpp#L215). Signature: `nearest(const cv::Mat& query, int checks, cv::Mat& nn)`.

**Callers and how they call it**

- [`sweep_level`](#function-149) ([inpaint.cpp:291](../inpaint.cpp#L291)): direct C++ call; receiver/signature resolved by compiler.

**CLI invocations / variants:** [P](#scenario-p) [CP](#scenario-cp) [T](#scenario-t). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Compare returned neighbor to brute force in projected space with exhaustive checks; batches, single row and thread counts give identical indexes.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [P](#scenario-p) [CP](#scenario-cp) [T](#scenario-t) — Compare one/multiple scans, scales/cap variants and thread counts; check no joins between input scans, mask rank histogram and reproducibility independent of earlier builds.

#### Anonymous function: `PatchDatabase::nearest::callback@225`

Source: [inpaint.cpp:225](../inpaint.cpp#L225); capture/signature: `[&](const cv::Range& part)`.

- **Callers/how:** cv::parallel_for_ invokes callback for each query-row range.

- **CLI invocations:** [P](#scenario-p) [CP](#scenario-cp) [T](#scenario-t); same enclosing branch, not an independently named CLI operation.

- **Unit tests should exist:** Nonoverlapping output rows, complete coverage, nearest-neighbor equality at 1/2/many threads and stripe cap.

- **Integration tests should exist:** [P](#scenario-p) [CP](#scenario-cp) [T](#scenario-t); verify the enclosing function's observable assertion above with the callback branch actually reached.

<a id="function-146"></a>

### `ring_offsets` — line 242

Source: [inpaint.cpp:242](../inpaint.cpp#L242). Signature: `ring_offsets(int radius)`.

**Callers and how they call it**

- [`synthesise_inpaint_field`](#function-152) ([inpaint.cpp:363](../inpaint.cpp#L363)): direct C++ call; `ring_offsets(p.radius)`.

**CLI invocations / variants:** [P](#scenario-p) [CP](#scenario-cp) [T](#scenario-t). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Exactly (2r+1)^2-1 unique offsets; center absent; boundaries and ordering; smallest supported radius.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [P](#scenario-p) [CP](#scenario-cp) [T](#scenario-t) — Compare one/multiple scans, scales/cap variants and thread counts; check no joins between input scans, mask rank histogram and reproducibility independent of earlier builds.

<a id="function-147"></a>

### `gather_queries` — line 252

Source: [inpaint.cpp:252](../inpaint.cpp#L252). Signature: `gather_queries(const cv::Mat& field, const std::vector<int>& pixels, const std::vector<cv::Point>& ring, cv::Mat& query)`.

**Callers and how they call it**

- [`sweep_level`](#function-149) ([inpaint.cpp:291](../inpaint.cpp#L291)): direct C++ call; `gather_queries(field, group, ring, query)`.

**CLI invocations / variants:** [P](#scenario-p) [CP](#scenario-cp) [T](#scenario-t). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Each query row contains wrapped neighbors in ring order; corners, pixel permutation and rectangular fields.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [P](#scenario-p) [CP](#scenario-cp) [T](#scenario-t) — Compare one/multiple scans, scales/cap variants and thread counts; check no joins between input scans, mask rank histogram and reproducibility independent of earlier builds.

<a id="function-148"></a>

### `start_field` — line 270

Source: [inpaint.cpp:270](../inpaint.cpp#L270). Signature: `start_field(const cv::Mat& previous, int W, int H, std::mt19937_64& rng)`.

**Callers and how they call it**

- [`synthesise_inpaint_field`](#function-152) ([inpaint.cpp:363](../inpaint.cpp#L363)): direct C++ call; `start_field(field, W, H, rng)`.

**CLI invocations / variants:** [P](#scenario-p) [CP](#scenario-cp) [T](#scenario-t). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** First scale seeded random field versus upsampled prior; exact dimensions/range and repeatability.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [P](#scenario-p) [CP](#scenario-cp) [T](#scenario-t) — Compare one/multiple scans, scales/cap variants and thread counts; check no joins between input scans, mask rank histogram and reproducibility independent of earlier builds.

<a id="function-149"></a>

### `sweep_level` — line 291

Source: [inpaint.cpp:291](../inpaint.cpp#L291). Signature: `sweep_level(cv::Mat& field, PatchDatabase& db, const std::vector<cv::Point>& ring, const InpaintParams& p, std::mt19937_64& rng, std::vector<int>& order, double refCoherence)`.

**Callers and how they call it**

- [`synthesise_inpaint_field`](#function-152) ([inpaint.cpp:363](../inpaint.cpp#L363)): direct C++ call; `sweep_level(field, db, ring, p, rng, order, refCoherence)`.

**CLI invocations / variants:** [P](#scenario-p) [CP](#scenario-cp) [T](#scenario-t). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Correct group assignment, nearest-center replacement and ranking; groups 1/many; seam wrap and iteration count; no missing or duplicate pixels.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [P](#scenario-p) [CP](#scenario-cp) [T](#scenario-t) — Compare one/multiple scans, scales/cap variants and thread counts; check no joins between input scans, mask rank histogram and reproducibility independent of earlier builds.

<a id="function-150"></a>

### `inpaint_work` — line 333

Source: [inpaint.cpp:333](../inpaint.cpp#L333). Signature: `inpaint_work(int w, int h, int iters, int db, int dims, int scales, int checks)`.

**Callers and how they call it**

- [`inpaint_work_of`](#function-124) ([generators.cpp:88](../generators.cpp#L88)): direct C++ call; `inpaint_work(w, h, (int)r.integer(inpaint::keys::Iters), (int)r.integer(inpaint::keys::Db), (int)r.integer(inpaint::keys::Dims), (int)r.integer(inpaint::keys::Scales), (int)r.integer(inpaint::keys:...`.

**CLI invocations / variants:** [P](#scenario-p) [CP](#scenario-cp) [T](#scenario-t). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Finite positive estimate; area/iterations/database/checks/scales dependencies and valid boundary values.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [P](#scenario-p) [CP](#scenario-cp) [T](#scenario-t) — Compare one/multiple scans, scales/cap variants and thread counts; check no joins between input scans, mask rank histogram and reproducibility independent of earlier builds.

<a id="function-151"></a>

### `synthesise_inpaint_field` — line 356

Source: [inpaint.cpp:356](../inpaint.cpp#L356). Signature: `synthesise_inpaint_field(cv::Size sz, const cv::Mat& input, const InpaintParams& p, uint64_t seed, std::ostream* log)`.

**Callers and how they call it**

- [`run`](#function-166) ([maketexture.cpp:239](../maketexture.cpp#L239)): direct C++ call; `synthesise_inpaint_field(want, prepared, inpaint_params_from(P), r.seed, &std::cerr)`.

**CLI invocations / variants:** [T](#scenario-t) (inpaint adapter). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Single-Mat and Set overload equivalence for one scan; multiscale dimensions, cap early-stop, repeatability and too-small sample failure; output field type/range.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [T](#scenario-t) (inpaint adapter) — Compare one/multiple scans, scales/cap variants and thread counts; check no joins between input scans, mask rank histogram and reproducibility independent of earlier builds.

<a id="function-152"></a>

### `synthesise_inpaint_field` — line 363

Source: [inpaint.cpp:363](../inpaint.cpp#L363). Signature: `synthesise_inpaint_field(cv::Size sz, const samples::Set& prepared_in, const InpaintParams& p, uint64_t seed, std::ostream* log)`.

**Callers and how they call it**

- [`synthesise_inpaint_field`](#function-151) ([inpaint.cpp:356](../inpaint.cpp#L356)): direct C++ call; `synthesise_inpaint_field(sz, samples::Set::of(input), p, seed, log)`.

- [`build_inpaint_mask`](#function-153) ([inpaint.cpp:467](../inpaint.cpp#L467)): direct C++ call; `synthesise_inpaint_field( sz, set.prepared(p.highpass, p.scale), p, seed, log)`.

**CLI invocations / variants:** [P](#scenario-p) [CP](#scenario-cp) [T](#scenario-t). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Single-Mat and Set overload equivalence for one scan; multiscale dimensions, cap early-stop, repeatability and too-small sample failure; output field type/range.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [P](#scenario-p) [CP](#scenario-cp) [T](#scenario-t) — Compare one/multiple scans, scales/cap variants and thread counts; check no joins between input scans, mask rank histogram and reproducibility independent of earlier builds.

<a id="function-153"></a>

### `build_inpaint_mask` — line 467

Source: [inpaint.cpp:467](../inpaint.cpp#L467). Signature: `build_inpaint_mask(cv::Size sz, const samples::Set& set, const InpaintParams& p, uint64_t seed, std::ostream* log)`.

**Callers and how they call it**

- [`inpaint_build`](#function-125) ([generators.cpp:98](../generators.cpp#L98)): direct C++ call; `build_inpaint_mask(q.size, q.samples, inpaint_params_from(P), q.seed, q.log)`.

- [`calibrate_inpaint_for`](#function-156) ([inpaintcalib.cpp:107](../inpaintcalib.cpp#L107)): direct C++ call; `build_inpaint_mask(probe, set, probe_p, seed, nullptr)`.

**CLI invocations / variants:** [P](#scenario-p) [CP](#scenario-cp) [T](#scenario-t). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Valid field becomes uniformly ranked 0..254 mask; propagate empty field; deterministic seed; null versus supplied log does not change pixels.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [P](#scenario-p) [CP](#scenario-cp) [T](#scenario-t) — Compare one/multiple scans, scales/cap variants and thread counts; check no joins between input scans, mask rank histogram and reproducibility independent of earlier builds.

<a id="function-154"></a>

### `register_inpaint_params` — line 480

Source: [inpaint.cpp:480](../inpaint.cpp#L480). Signature: `register_inpaint_params(params::Params& P)`.

**Callers and how they call it**

- [`usage_generators`](#function-031) ([cli.cpp:568](../cli.cpp#L568)): indirect: generators() row .register_params(P), once for every generator.

- [`dither_main`](#function-096) ([dither.cpp:105](../dither.cpp#L105)): indirect: generators() row .register_params(P), once for every generator.

- [`list_options`](#function-161) ([maketexture.cpp:102](../maketexture.cpp#L102)): indirect: generators() row .register_params(P), once for every generator.

- [`run`](#function-166) ([maketexture.cpp:239](../maketexture.cpp#L239)): indirect: generators() row .register_params(P), once for every generator.

- [`case_306`](#function-329) ([unittest.cpp:306](../unittest.cpp#L306)): direct C++ call; `register_inpaint_params(P)`.

- [`cache_registry`](#function-345) ([unittest.cpp:981](../unittest.cpp#L981)): indirect: generators() row .register_params(P), once for every generator.

**CLI invocations / variants:** [H](#scenario-h) [E](#scenario-e) [R](#scenario-r) [T](#scenario-t) [U](#scenario-u) (registration); selected-generator scenarios exercise lazy derived rules. IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Register every inpaint rule once; assert defaults, range endpoints, option visibility and applicability. Resolve derived rules at dependency boundaries and after overrides, check origins/dependency edges, and verify parameter-struct defaults agree. Test each lambda below through the registry.

**Existing direct C++ test callers:** [`case_306`](#function-329). These do not establish full branch coverage.

**Integration tests should exist:** [H](#scenario-h) [E](#scenario-e) [R](#scenario-r) [T](#scenario-t) [U](#scenario-u) (registration); selected-generator scenarios exercise lazy derived rules — Compare one/multiple scans, scales/cap variants and thread counts; check no joins between input scans, mask rank histogram and reproducibility independent of earlier builds.

<a id="file-21"></a>

## `inpaint.h`

### Declaration: `build_inpaint_mask(cv::Size sz, const samples::Set& set, const InpaintParams& p, uint64_t seed, std::ostream* log)`

Source: [inpaint.h:64](../inpaint.h#L64).

Callers, CLI variants, unit tests and integration tests: [`build_inpaint_mask`](#function-153). This is the declaration of that same function; do not count it as separate executed coverage.

### Declaration: `synthesise_inpaint_field(cv::Size sz, const cv::Mat& input, const InpaintParams& p, uint64_t seed, std::ostream* log)`

Source: [inpaint.h:77](../inpaint.h#L77).

Callers, CLI variants, unit tests and integration tests: [`synthesise_inpaint_field`](#function-151). This is the declaration of that same function; do not count it as separate executed coverage.

### Declaration: `synthesise_inpaint_field(cv::Size sz, const samples::Set& prepared, const InpaintParams& p, uint64_t seed, std::ostream* log)`

Source: [inpaint.h:82](../inpaint.h#L82).

Callers, CLI variants, unit tests and integration tests: [`synthesise_inpaint_field`](#function-152). This is the declaration of that same function; do not count it as separate executed coverage.

### Declaration: `register_inpaint_params(params::Params&)`

Source: [inpaint.h:86](../inpaint.h#L86).

Callers, CLI variants, unit tests and integration tests: [`register_inpaint_params`](#function-154). This is the declaration of that same function; do not count it as separate executed coverage.

### Declaration: `inpaint_work(int w, int h, int iters, int db, int dims, int scales, int checks)`

Source: [inpaint.h:91](../inpaint.h#L91).

Callers, CLI variants, unit tests and integration tests: [`inpaint_work`](#function-150). This is the declaration of that same function; do not count it as separate executed coverage.

<a id="file-22"></a>

## `inpaintcalib.cpp`

<a id="function-155"></a>

### `calibration_key` — line 69

Source: [inpaintcalib.cpp:69](../inpaintcalib.cpp#L69). Signature: `calibration_key(const samples::Set& set, const InpaintParams& p, uint64_t seed)`.

**Callers and how they call it**

- [`calibrate_inpaint_for`](#function-156) ([inpaintcalib.cpp:107](../inpaintcalib.cpp#L107)): direct C++ call; `calibration_key(set, ip, seed)`.

**CLI invocations / variants:** [CP](#scenario-cp). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Stable digest for identical data/seed/settings; modify each hashed field, scan pixels, order, dimensions/type and pin state where present; noncontinuous versus cloned scans give identical keys. The tuned quantity and requested output size should not invalidate calibration unless deliberately hashed; version changes must invalidate old results.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [CP](#scenario-cp) — Fresh/reused/pinned calibration, invalid integral cache entries, no decisive improvement and save failure; check iteration origin, search trace and final mask settings.

#### Anonymous function: `calibration_key::mix@73`

Source: [inpaintcalib.cpp:73](../inpaintcalib.cpp#L73); capture/signature: `[&h](const void* data, size_t n)`.

- **Callers/how:** calibration_key invokes the local hash helpers; mix_i/mix_d forward typed bytes to mix.

- **CLI invocations:** [CP](#scenario-cp); same enclosing branch, not an independently named CLI operation.

- **Unit tests should exist:** Known bytes, empty update and split-update equivalence; typed values change the digest as specified; captured hash state is updated exactly once.

- **Integration tests should exist:** [CP](#scenario-cp); verify the enclosing function's observable assertion above with the callback branch actually reached.

#### Anonymous function: `calibration_key::mix_i@77`

Source: [inpaintcalib.cpp:77](../inpaintcalib.cpp#L77); capture/signature: `[&mix](long long v)`.

- **Callers/how:** calibration_key invokes the local hash helpers; mix_i/mix_d forward typed bytes to mix.

- **CLI invocations:** [CP](#scenario-cp); same enclosing branch, not an independently named CLI operation.

- **Unit tests should exist:** Known bytes, empty update and split-update equivalence; typed values change the digest as specified; captured hash state is updated exactly once.

- **Integration tests should exist:** [CP](#scenario-cp); verify the enclosing function's observable assertion above with the callback branch actually reached.

#### Anonymous function: `calibration_key::mix_d@78`

Source: [inpaintcalib.cpp:78](../inpaintcalib.cpp#L78); capture/signature: `[&mix](double v)`.

- **Callers/how:** calibration_key invokes the local hash helpers; mix_i/mix_d forward typed bytes to mix.

- **CLI invocations:** [CP](#scenario-cp); same enclosing branch, not an independently named CLI operation.

- **Unit tests should exist:** Known bytes, empty update and split-update equivalence; typed values change the digest as specified; captured hash state is updated exactly once.

- **Integration tests should exist:** [CP](#scenario-cp); verify the enclosing function's observable assertion above with the callback branch actually reached.

<a id="function-156"></a>

### `calibrate_inpaint_for` — line 107

Source: [inpaintcalib.cpp:107](../inpaintcalib.cpp#L107). Signature: `calibrate_inpaint_for(params::Params& P, const std::string& sample_path, uint64_t seed, const std::string& cfg, std::ostream& log)`.

**Callers and how they call it**

- [`maybe_calibrate`](#function-214) ([mask.cpp:435](../mask.cpp#L435)): indirect: Generator::calibrate(P, sample path, seed, config, log), only when calibration is requested.

**CLI invocations / variants:** [CP](#scenario-cp). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Explicit iterations skip; cached value must be integral, in registry range and in {2,3,4,6}; too-small reference and failed/nonfinite probes; normalized clump/ridge objective, zero means, exact 10% margin, no decisive winner means no new cache entry; save failure.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [CP](#scenario-cp) — Fresh/reused/pinned calibration, invalid integral cache entries, no decisive improvement and save failure; check iteration origin, search trace and final mask settings.

#### Anonymous function: `calibrate_inpaint_for::trace@211`

Source: [inpaintcalib.cpp:211](../inpaintcalib.cpp#L211); capture/signature: `[&](std::ostream& o)`.

- **Callers/how:** calibrate_inpaint_for calls trace(log) on accepted and fallback reporting paths.

- **CLI invocations:** [CP](#scenario-cp); same enclosing branch, not an independently named CLI operation.

- **Unit tests should exist:** Only valid candidates printed with correct score/ladder value; no mutation of scores or winner.

- **Integration tests should exist:** [CP](#scenario-cp); verify the enclosing function's observable assertion above with the callback branch actually reached.

<a id="file-23"></a>

## `inpaintcalib.h`

### Declaration: `calibrate_inpaint_for(params::Params& P, const std::string& sample_path, uint64_t seed, const std::string& cfg, std::ostream& log)`

Source: [inpaintcalib.h:20](../inpaintcalib.h#L20).

Callers, CLI variants, unit tests and integration tests: [`calibrate_inpaint_for`](#function-156). This is the declaration of that same function; do not count it as separate executed coverage.

<a id="file-24"></a>

## `main.cpp`

<a id="function-157"></a>

### `main` — line 9

Source: [main.cpp:9](../main.cpp#L9). Signature: `main(int argc, char** argv)`.

**Callers and how they call it**

- Process runtime invokes this executable entry point with argc/argv; it is not called by another project function.

**CLI invocations / variants:** [H](#scenario-h) [E](#scenario-e) [R](#scenario-r) [X](#scenario-x). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Verify forwarding and exit status; thrown std::exception becomes a readable error and nonzero exit. Exercise process behavior in a subprocess.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [H](#scenario-h) [E](#scenario-e) [R](#scenario-r) [X](#scenario-x) — Invoke the binary as a process for help, valid image, malformed input and write failure; exit status and exception diagnostics must be correct.

<a id="file-25"></a>

## `maketexture.cpp`

<a id="function-158"></a>

### `print_usage` — line 41

Source: [maketexture.cpp:41](../maketexture.cpp#L41). Signature: `print_usage()`.

**Callers and how they call it**

- [`parse`](#function-165) ([maketexture.cpp:171](../maketexture.cpp#L171)): direct C++ call; `print_usage()`.

- [`main`](#function-167) ([maketexture.cpp:345](../maketexture.cpp#L345)): direct C++ call; `print_usage()`.

**CLI invocations / variants:** [TH](#scenario-th). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Documented defaults match parser and synthesizer; list supported methods and paste size constraint.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [TH](#scenario-th) — Read texture/paste outputs for each method, verify only selected ROI changed and histogram/size are correct; malformed/ignored flags and write failures must return nonzero.

<a id="function-159"></a>

### `fail` — line 79

Source: [maketexture.cpp:79](../maketexture.cpp#L79). Signature: `fail(const std::string& why)`.

**Callers and how they call it**

- [`checked_dim`](#function-162) ([maketexture.cpp:114](../maketexture.cpp#L114)): direct C++ call; `fail(std::string(what) + " must be a whole number in 1.." + std::to_string(kMaxSide) + ", not '" + v + "'")`.

- [`parse_rect`](#function-163) ([maketexture.cpp:126](../maketexture.cpp#L126)): direct C++ call; `fail(std::string(what) + ": expected WxH in '" + s + "'")`, `fail(std::string(what) + ": expected WxH+X+Y in '" + s + "'")`, `fail(std::string(what) + ": offsets must be whole and non-negative")`, `fail(std::string(what) + ": " + std::to_string(w) + "x" + std::to_string(h) + " is more than " + std::to_string(kMaxArea) + " pixels")`.

- [`parse`](#function-165) ([maketexture.cpp:171](../maketexture.cpp#L171)): call in enclosing function / its lambda callback; `fail(what + " needs a value")`, `fail("--seed must be a whole number, zero or more")`, `fail("Unknown option: " + a)`, `fail(a + " is a dither option; maketexture synthesises a texture " "from a patch and has no use for it")`, `fail("Unexpected argument: " + a)`, `fail("--mask-method must be inpaint, markov or dft (blue imitates nothing, " "and direct is a crop of the sample rather than a model of it)")`, `fail(k + " does not apply to --mask-method " + r.method)`, `fail("invalid settings for " + r.method)`.

- [`run`](#function-166) ([maketexture.cpp:239](../maketexture.cpp#L239)): direct C++ call; `fail("Could not read " + r.in)`, `fail("--sample " + std::to_string(sample.width) + "x" + std::to_string(sample.height) + "+" + std::to_string(sample.x) + "+" + std::to_string(sample.y) + " is not inside the " + std::to_string(imag...`, `fail("--paste needs --size to equal the sample's size (" + std::to_string(sample.width) + "x" + std::to_string(sample.height) + "), because the restored tone belongs to the region being replaced")`, `fail("the sample is " + std::to_string(prepared.cols) + "x" + std::to_string(prepared.rows) + " after --mask-scale, too small to model")`.

**CLI invocations / variants:** [T](#scenario-t) [TH](#scenario-th). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Throws runtime_error with original message; top-level handler emits nonzero status without abort.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [T](#scenario-t) [TH](#scenario-th) — Read texture/paste outputs for each method, verify only selected ROI changed and histogram/size are correct; malformed/ignored flags and write failures must return nonzero.

<a id="function-160"></a>

### `accepted` — line 86

Source: [maketexture.cpp:86](../maketexture.cpp#L86). Signature: `accepted(const std::string& key)`.

**Callers and how they call it**

- [`list_options`](#function-161) ([maketexture.cpp:102](../maketexture.cpp#L102)): direct C++ call; `accepted(k)`.

- [`parse`](#function-165) ([maketexture.cpp:171](../maketexture.cpp#L171)): direct C++ call; `accepted(key)`.

**CLI invocations / variants:** [T](#scenario-t) [TH](#scenario-th). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Accept selected synthesis namespaces and mask scale/highpass; refuse blue/direct, rate, max_size/max_repeats and workflow-only settings.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [T](#scenario-t) [TH](#scenario-th) — Read texture/paste outputs for each method, verify only selected ROI changed and histogram/size are correct; malformed/ignored flags and write failures must return nonzero.

<a id="function-161"></a>

### `list_options` — line 102

Source: [maketexture.cpp:102](../maketexture.cpp#L102). Signature: `list_options()`.

**Callers and how they call it**

- [`main`](#function-167) ([maketexture.cpp:345](../maketexture.cpp#L345)): direct C++ call; `list_options()`.

**CLI invocations / variants:** [TH](#scenario-th). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Every printed flag is accepted and every supported generator parameter is present; no workflow-only flags.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [TH](#scenario-th) — Read texture/paste outputs for each method, verify only selected ROI changed and histogram/size are correct; malformed/ignored flags and write failures must return nonzero.

<a id="function-162"></a>

### `checked_dim` — line 114

Source: [maketexture.cpp:114](../maketexture.cpp#L114). Signature: `checked_dim(const std::string& v, const char* what)`.

**Callers and how they call it**

- [`parse_rect`](#function-163) ([maketexture.cpp:126](../maketexture.cpp#L126)): direct C++ call; `checked_dim(s.substr(0, x), what)`, `checked_dim(rest.substr(0, p1), what)`, `checked_dim(rest, what)`.

**CLI invocations / variants:** [T](#scenario-t) [TH](#scenario-th). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** 1 and kMaxSide accepted; zero, negative, fraction, overflow and suffix rejected.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [T](#scenario-t) [TH](#scenario-th) — Read texture/paste outputs for each method, verify only selected ROI changed and histogram/size are correct; malformed/ignored flags and write failures must return nonzero.

<a id="function-163"></a>

### `parse_rect` — line 126

Source: [maketexture.cpp:126](../maketexture.cpp#L126). Signature: `parse_rect(const std::string& s, bool withOffset, const char* what)`.

**Callers and how they call it**

- [`parse`](#function-165) ([maketexture.cpp:171](../maketexture.cpp#L171)): direct C++ call; `parse_rect(next(a), true, "--sample")`, `parse_rect(next(a), false, "--size")`.

**CLI invocations / variants:** [T](#scenario-t) [TH](#scenario-th). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** WxH and WxH+X+Y, zero offsets and bounds; malformed tokens, area overflow, trailing junk and missing offset.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [T](#scenario-t) [TH](#scenario-th) — Read texture/paste outputs for each method, verify only selected ROI changed and histogram/size are correct; malformed/ignored flags and write failures must return nonzero.

<a id="function-164"></a>

### `set_typed` — line 157

Source: [maketexture.cpp:157](../maketexture.cpp#L157). Signature: `set_typed(params::Params& P, const std::string& key, const std::string& val)`.

**Callers and how they call it**

- [`parse`](#function-165) ([maketexture.cpp:171](../maketexture.cpp#L171)): direct C++ call; `set_typed(P, k, v)`.

**CLI invocations / variants:** [T](#scenario-t) [TH](#scenario-th). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Integer/double/exponent/string detection matches dither parser; range errors and numeric-looking invalid strings.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [T](#scenario-t) [TH](#scenario-th) — Read texture/paste outputs for each method, verify only selected ROI changed and histogram/size are correct; malformed/ignored flags and write failures must return nonzero.

<a id="function-165"></a>

### `parse` — line 171

Source: [maketexture.cpp:171](../maketexture.cpp#L171). Signature: `parse(int argc, char** argv, params::Params& P)`.

**Callers and how they call it**

- [`run`](#function-166) ([maketexture.cpp:239](../maketexture.cpp#L239)): direct C++ call; receiver/signature resolved by compiler.

**CLI invocations / variants:** [T](#scenario-t) [TH](#scenario-th). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Default highpass zero; aliases and last-write wins; explicit patch/size/seed; cross-generator and ignored workflow flags rejected.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [T](#scenario-t) [TH](#scenario-th) — Read texture/paste outputs for each method, verify only selected ROI changed and histogram/size are correct; malformed/ignored flags and write failures must return nonzero.

#### Anonymous function: `parse::next@185`

Source: [maketexture.cpp:185](../maketexture.cpp#L185); capture/signature: `[&](const std::string& what)`.

- **Callers/how:** parse invokes next(option) for flags requiring an operand.

- **CLI invocations:** [T](#scenario-t) [TH](#scenario-th); same enclosing branch, not an independently named CLI operation.

- **Unit tests should exist:** Consume exactly one following token; missing value names the flag and throws; values resembling flags stay values.

- **Integration tests should exist:** [T](#scenario-t) [TH](#scenario-th); verify the enclosing function's observable assertion above with the callback branch actually reached.

<a id="function-166"></a>

### `run` — line 239

Source: [maketexture.cpp:239](../maketexture.cpp#L239). Signature: `run(int argc, char** argv)`.

**Callers and how they call it**

- [`main`](#function-167) ([maketexture.cpp:345](../maketexture.cpp#L345)): direct C++ call; receiver/signature resolved by compiler.

**CLI invocations / variants:** [T](#scenario-t) [TH](#scenario-th). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Default centered crop, explicit patch, all three methods, original histogram mapping, highpass detail recombination and paste; bounds and output errors.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [T](#scenario-t) [TH](#scenario-th) — Read texture/paste outputs for each method, verify only selected ROI changed and histogram/size are correct; malformed/ignored flags and write failures must return nonzero.

<a id="function-167"></a>

### `main` — line 345

Source: [maketexture.cpp:345](../maketexture.cpp#L345). Signature: `main(int argc, char** argv)`.

**Callers and how they call it**

- Process runtime invokes this executable entry point with argc/argv; it is not called by another project function.

**CLI invocations / variants:** [T](#scenario-t) [TH](#scenario-th). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** No arguments/help/list-options status, exception translation and run status propagation; use subprocess tests because parsing may exit.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [T](#scenario-t) [TH](#scenario-th) — Read texture/paste outputs for each method, verify only selected ROI changed and histogram/size are correct; malformed/ignored flags and write failures must return nonzero.

<a id="file-26"></a>

## `markov.cpp`

<a id="function-168"></a>

### `quantise_set` — line 103

Source: [markov.cpp:103](../markov.cpp#L103). Signature: `quantise_set(const samples::Set& fields, int Qc, int Qv)`.

**Callers and how they call it**

- [`fit_markov_model`](#function-192) ([markov.cpp:851](../markov.cpp#L851)): direct C++ call; `quantise_set(fields, Qc, Qv)`.

**CLI invocations / variants:** [M](#scenario-m) [CM](#scenario-cm) [T](#scenario-t). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Pooled ranks across unequal scans; exact Qc/Qv bin populations and shared intensity order; constant ties and boundary bin counts.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [M](#scenario-m) [CM](#scenario-cm) [T](#scenario-t) — Exercise flat/mi/ml, temperature, radius/pair counts and report toggle; masks must be seeded reproducible and histogram-flat; fit/report must not cross scan boundaries or mutate results.

<a id="function-169"></a>

### `column_span` — line 126

Source: [markov.cpp:126](../markov.cpp#L126). Signature: `column_span(const cv::Mat& m, Columns part, int radius, int& lo, int& hi)`.

**Callers and how they call it**

- [`tally`](#function-171) ([markov.cpp:176](../markov.cpp#L176)): direct C++ call; `column_span(qn, part, radius, lo, hi)`.

- [`pooled_logloss`](#function-181) ([markov.cpp:385](../markov.cpp#L385)): direct C++ call; `column_span(qn, part, radius, lo, hi)`.

**CLI invocations / variants:** [M](#scenario-m) [CM](#scenario-cm) [T](#scenario-t). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** All/Left/Right ranges for odd/even widths and radius gaps; empty valid interior never permits invalid indexing.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [M](#scenario-m) [CM](#scenario-cm) [T](#scenario-t) — Exercise flat/mi/ml, temperature, radius/pair counts and report toggle; masks must be seeded reproducible and histogram-flat; fit/report must not cross scan boundaries or mutate results.

<a id="function-170"></a>

### `mutual_information` — line 141

Source: [markov.cpp:141](../markov.cpp#L141). Signature: `mutual_information(const std::vector<float>& n, int nb, int nv)`.

**Callers and how they call it**

- [`select_pairs`](#function-184) ([markov.cpp:505](../markov.cpp#L505)): direct C++ call; `mutual_information(tbl, Qc, Qv)`, `mutual_information(tbl, cells, Qv)`.

- [`fit_expert`](#function-191) ([markov.cpp:824](../markov.cpp#L824)): direct C++ call; `mutual_information(tbl, cells, Qv)`.

**CLI invocations / variants:** [M](#scenario-m) [CM](#scenario-cm) [T](#scenario-t). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Independent uniform counts give zero; perfectly coupled counts match analytic entropy; zero counts handled without NaN.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [M](#scenario-m) [CM](#scenario-cm) [T](#scenario-t) — Exercise flat/mi/ml, temperature, radius/pair counts and report toggle; masks must be seeded reproducible and histogram-flat; fit/report must not cross scan boundaries or mutate results.

<a id="function-171"></a>

### `tally` — line 176

Source: [markov.cpp:176](../markov.cpp#L176). Signature: `tally(const Quantised& q, int Qv, int radius, int step, Offset d1, const Offset* d2, int Qc, std::vector<float>& n, Columns part = Columns::All)`.

**Callers and how they call it**

- [`select_pairs`](#function-184) ([markov.cpp:505](../markov.cpp#L505)): direct C++ call; `tally(q, Qv, radius, step, nb.off[i], nullptr, Qc, tbl)`, `tally(q, Qv, radius, step, nb.off[shortlist[a]], &nb.off[shortlist[b]], Qc, tbl)`.

- [`report_holdout`](#function-186) ([markov.cpp:618](../markov.cpp#L618)): direct C++ call; `tally(q, Qv, radius, step, experts[k].d1, &experts[k].d2, Qc, t2, Columns::Left)`.

- [`fit_markov_model`](#function-192) ([markov.cpp:851](../markov.cpp#L851)): direct C++ call; `tally(q, Qv, radius, step, d1, &d2, Qc, tbl)`.

**CLI invocations / variants:** [M](#scenario-m) [CM](#scenario-cm) [T](#scenario-t). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Compare pair counts to a slow enumerator; scan-local borders, held-out halves, thinning step and multiple scans; counts sum correctly.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [M](#scenario-m) [CM](#scenario-cm) [T](#scenario-t) — Exercise flat/mi/ml, temperature, radius/pair counts and report toggle; masks must be seeded reproducible and histogram-flat; fit/report must not cross scan boundaries or mutate results.

<a id="function-172"></a>

### `blur_axis` — line 199

Source: [markov.cpp:199](../markov.cpp#L199). Signature: `blur_axis(std::vector<float>& t, int outer, int len, int inner)`.

**Callers and how they call it**

- [`to_logp`](#function-180) ([markov.cpp:364](../markov.cpp#L364)): direct C++ call; `blur_axis(tbl, 1, Qc, Qc * Qv)`, `blur_axis(tbl, Qc, Qc, Qv)`, `blur_axis(tbl, cells, Qv, 1)`.

**CLI invocations / variants:** [M](#scenario-m) [CM](#scenario-cm) [T](#scenario-t). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Independent separable blur for each outer/inner layout; edge handling and normalization; zero/constant tables.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [M](#scenario-m) [CM](#scenario-cm) [T](#scenario-t) — Exercise flat/mi/ml, temperature, radius/pair counts and report toggle; masks must be seeded reproducible and histogram-flat; fit/report must not cross scan boundaries or mutate results.

<a id="function-173"></a>

### `symmetry` — line 233

Source: [markov.cpp:233](../markov.cpp#L233). Signature: `symmetry(Offset d, int k)`.

**Callers and how they call it**

- [`shortlist_offsets`](#function-176) ([markov.cpp:266](../markov.cpp#L266)): call in enclosing function / its lambda callback; `symmetry(nb.off[i], k)`.

- [`add_symmetry_orbit`](#function-178) ([markov.cpp:323](../markov.cpp#L323)): direct C++ call; `symmetry(nb.off[shortlist[cands[seed_cand].a]], k)`, `symmetry(nb.off[shortlist[cands[seed_cand].b]], k)`.

**CLI invocations / variants:** [M](#scenario-m) [CM](#scenario-cm) [T](#scenario-t). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** All eight square symmetries preserve radius and produce the expected orbit, including axial/diagonal duplicates.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [M](#scenario-m) [CM](#scenario-cm) [T](#scenario-t) — Exercise flat/mi/ml, temperature, radius/pair counts and report toggle; masks must be seeded reproducible and histogram-flat; fit/report must not cross scan boundaries or mutate results.

<a id="function-174"></a>

### `Neighbourhood::Neighbourhood` — line 247

Source: [markov.cpp:247](../markov.cpp#L247). Signature: `Neighbourhood(int r) : radius(r), side(2 * r + 1), at(side * side, -1)`.

**Callers and how they call it**

- [`fit_markov_model`](#function-192) ([markov.cpp:851](../markov.cpp#L851)): direct C++ call; see linked caller for arguments (may be template/constructor lowering).

**CLI invocations / variants:** [M](#scenario-m) [CM](#scenario-cm) [T](#scenario-t). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Populate exactly the noncenter offsets within radius; index/order consistent for smallest and larger radius.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [M](#scenario-m) [CM](#scenario-cm) [T](#scenario-t) — Exercise flat/mi/ml, temperature, radius/pair counts and report toggle; masks must be seeded reproducible and histogram-flat; fit/report must not cross scan boundaries or mutate results.

<a id="function-175"></a>

### `Neighbourhood::index` — line 255

Source: [markov.cpp:255](../markov.cpp#L255). Signature: `index(Offset d) const`.

**Callers and how they call it**

- [`shortlist_offsets`](#function-176) ([markov.cpp:266](../markov.cpp#L266)): call in enclosing function / its lambda callback; receiver/signature resolved by compiler.

- [`add_symmetry_orbit`](#function-178) ([markov.cpp:323](../markov.cpp#L323)): direct C++ call; receiver/signature resolved by compiler.

**CLI invocations / variants:** [M](#scenario-m) [CM](#scenario-cm) [T](#scenario-t). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Map every valid offset to its stored index; origin/outside contract; no collisions.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [M](#scenario-m) [CM](#scenario-cm) [T](#scenario-t) — Exercise flat/mi/ml, temperature, radius/pair counts and report toggle; masks must be seeded reproducible and histogram-flat; fit/report must not cross scan boundaries or mutate results.

<a id="function-176"></a>

### `shortlist_offsets` — line 266

Source: [markov.cpp:266](../markov.cpp#L266). Signature: `shortlist_offsets(const Neighbourhood& nb, const std::vector<double>& mi, int want)`.

**Callers and how they call it**

- [`select_pairs`](#function-184) ([markov.cpp:505](../markov.cpp#L505)): direct C++ call; `shortlist_offsets(nb, miSingle, shortlistSize)`.

**CLI invocations / variants:** [M](#scenario-m) [CM](#scenario-cm) [T](#scenario-t). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Highest-information selection with band/orbit constraints; requested count greater than available, ties and empty candidates.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [M](#scenario-m) [CM](#scenario-cm) [T](#scenario-t) — Exercise flat/mi/ml, temperature, radius/pair counts and report toggle; masks must be seeded reproducible and histogram-flat; fit/report must not cross scan boundaries or mutate results.

#### Anonymous function: `shortlist_offsets::callback@272`

Source: [markov.cpp:272](../markov.cpp#L272); capture/signature: `[&](int a, int b)`.

- **Callers/how:** std::sort/stable_sort invokes the comparator supplied by shortlist_offsets.

- **CLI invocations:** [M](#scenario-m) [CM](#scenario-cm) [T](#scenario-t); same enclosing branch, not an independently named CLI operation.

- **Unit tests should exist:** Strict weak ordering, equal values, stable/deterministic ties as required; sorted output is a permutation.

- **Integration tests should exist:** [M](#scenario-m) [CM](#scenario-cm) [T](#scenario-t); verify the enclosing function's observable assertion above with the callback branch actually reached.

#### Anonymous function: `shortlist_offsets::add_orbit@276`

Source: [markov.cpp:276](../markov.cpp#L276); capture/signature: `[&](int i)`.

- **Callers/how:** shortlist_offsets invokes this local callback directly or through the algorithm receiving it; inspect linked source for the call site.

- **CLI invocations:** [M](#scenario-m) [CM](#scenario-cm) [T](#scenario-t); same enclosing branch, not an independently named CLI operation.

- **Unit tests should exist:** Exercise each branch of this callback through the enclosing function tests; verify captured-state updates and boundary values against the enclosing function oracle.

- **Integration tests should exist:** [M](#scenario-m) [CM](#scenario-cm) [T](#scenario-t); verify the enclosing function's observable assertion above with the callback branch actually reached.

<a id="function-177"></a>

### `best_unused_candidate` — line 301

Source: [markov.cpp:301](../markov.cpp#L301). Signature: `best_unused_candidate(const std::vector<Cand>& cands, const Neighbourhood& nb, const std::vector<int>& shortlist, const std::vector<int>& uses, const std::vector<char>& used, int nearBand, bool farOnly, double reuse)`.

**Callers and how they call it**

- [`pick_pairs`](#function-179) ([markov.cpp:345](../markov.cpp#L345)): direct C++ call; `best_unused_candidate(cands, nb, shortlist, uses, used, nearBand, farOnly, reuse)`.

**CLI invocations / variants:** [M](#scenario-m) [CM](#scenario-cm) [T](#scenario-t). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Exclude selected pairs; reuse penalty can change winner; ties deterministic and exhausted candidates handled.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [M](#scenario-m) [CM](#scenario-cm) [T](#scenario-t) — Exercise flat/mi/ml, temperature, radius/pair counts and report toggle; masks must be seeded reproducible and histogram-flat; fit/report must not cross scan boundaries or mutate results.

<a id="function-178"></a>

### `add_symmetry_orbit` — line 323

Source: [markov.cpp:323](../markov.cpp#L323). Signature: `add_symmetry_orbit(int seed_cand, const std::vector<Cand>& cands, const std::vector<int>& cellOf, const Neighbourhood& nb, const std::vector<int>& shortlist, const std::vector<int>& place, int want, std::vector<int>& uses, std::vector<char>& used, std::vector<int>& out)`.

**Callers and how they call it**

- [`pick_pairs`](#function-179) ([markov.cpp:345](../markov.cpp#L345)): direct C++ call; `add_symmetry_orbit(best, cands, cellOf, nb, shortlist, place, want, uses, used, out)`.

**CLI invocations / variants:** [M](#scenario-m) [CM](#scenario-cm) [T](#scenario-t). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Add only valid unused orbit members; duplicate symmetries and capacity limit; usage counts increment exactly once.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [M](#scenario-m) [CM](#scenario-cm) [T](#scenario-t) — Exercise flat/mi/ml, temperature, radius/pair counts and report toggle; masks must be seeded reproducible and histogram-flat; fit/report must not cross scan boundaries or mutate results.

<a id="function-179"></a>

### `pick_pairs` — line 345

Source: [markov.cpp:345](../markov.cpp#L345). Signature: `pick_pairs(const std::vector<Cand>& cands, const std::vector<int>& cellOf, const Neighbourhood& nb, const std::vector<int>& shortlist, const std::vector<int>& place, int nearBand, bool farOnly, int want, double reuse, std::vector<int>& uses, std::vector<char>& used, std::vector<int>& out)`.

**Callers and how they call it**

- [`select_pairs`](#function-184) ([markov.cpp:505](../markov.cpp#L505)): direct C++ call; `pick_pairs(cands, cellOf, nb, shortlist, place, 2, true, want / 2, reuse, uses, used, chosen)`, `pick_pairs(cands, cellOf, nb, shortlist, place, 2, false, want, reuse, uses, used, chosen)`.

**CLI invocations / variants:** [M](#scenario-m) [CM](#scenario-cm) [T](#scenario-t). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Requested capacity respected, pairs unique, deterministic tie-breaking and reuse balancing; short candidate pool.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [M](#scenario-m) [CM](#scenario-cm) [T](#scenario-t) — Exercise flat/mi/ml, temperature, radius/pair counts and report toggle; masks must be seeded reproducible and histogram-flat; fit/report must not cross scan boundaries or mutate results.

<a id="function-180"></a>

### `to_logp` — line 364

Source: [markov.cpp:364](../markov.cpp#L364). Signature: `to_logp(std::vector<float>& tbl, int cells, int Qc, int Qv, std::vector<float>& logp)`.

**Callers and how they call it**

- [`report_holdout`](#function-186) ([markov.cpp:618](../markov.cpp#L618)): direct C++ call; `to_logp(t2, cells, Qc, Qv, half[k])`.

- [`fit_expert`](#function-191) ([markov.cpp:824](../markov.cpp#L824)): direct C++ call; `to_logp(tbl, cells, Qc, Qv, e.logp)`.

**CLI invocations / variants:** [M](#scenario-m) [CM](#scenario-cm) [T](#scenario-t). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Per-cell probabilities normalize before logging; unobserved cells use the marginal prior; finite logs with zero counts.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [M](#scenario-m) [CM](#scenario-cm) [T](#scenario-t) — Exercise flat/mi/ml, temperature, radius/pair counts and report toggle; masks must be seeded reproducible and histogram-flat; fit/report must not cross scan boundaries or mutate results.

<a id="function-181"></a>

### `pooled_logloss` — line 385

Source: [markov.cpp:385](../markov.cpp#L385). Signature: `pooled_logloss(const Quantised& q, int Qv, int Qc, int radius, int step, const std::vector<Expert>& experts, const std::vector<double>& weights, const std::vector<std::vector<float>>& logp, Columns part)`.

**Callers and how they call it**

- [`report_holdout`](#function-186) ([markov.cpp:618](../markov.cpp#L618)): direct C++ call; `pooled_logloss(q, Qv, Qc, radius, step, experts, weights, half, Columns::Left)`, `pooled_logloss(q, Qv, Qc, radius, step, experts, weights, half, Columns::Right)`.

- [`report_weights`](#function-187) ([markov.cpp:676](../markov.cpp#L676)): direct C++ call; `pooled_logloss(q, Qv, Qc, radius, step, experts, flat, full, Columns::All)`, `pooled_logloss(q, Qv, Qc, radius, step, experts, wml, full, Columns::All)`.

**CLI invocations / variants:** [M](#scenario-m) [CM](#scenario-cm) [T](#scenario-t). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Compare to independent tiny table scorer; scan-local held-out regions, flat/mi/ml weights and zero observations.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [M](#scenario-m) [CM](#scenario-cm) [T](#scenario-t) — Exercise flat/mi/ml, temperature, radius/pair counts and report toggle; masks must be seeded reproducible and histogram-flat; fit/report must not cross scan boundaries or mutate results.

<a id="function-182"></a>

### `fit_weights_ml` — line 423

Source: [markov.cpp:423](../markov.cpp#L423). Signature: `fit_weights_ml(const Quantised& quant, int Qv, int Qc, int radius, int step, double total, const std::vector<Expert>& experts, const std::vector<std::vector<float>>& logp, int iters = 30)`.

**Callers and how they call it**

- [`report_weights`](#function-187) ([markov.cpp:676](../markov.cpp#L676)): direct C++ call; `fit_weights_ml(q, Qv, Qc, radius, step, total, experts, full)`.

- [`sample_markov_order`](#function-193) ([markov.cpp:927](../markov.cpp#L927)): direct C++ call; `fit_weights_ml(q, Qv, Qc, radius, step, invTemp, experts, lp)`.

**CLI invocations / variants:** [M](#scenario-m) [CM](#scenario-cm) [T](#scenario-t). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Finite nonnegative weights and improvement/no-regression of training objective; degenerate experts and deterministic convergence.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [M](#scenario-m) [CM](#scenario-cm) [T](#scenario-t) — Exercise flat/mi/ml, temperature, radius/pair counts and report toggle; masks must be seeded reproducible and histogram-flat; fit/report must not cross scan boundaries or mutate results.

<a id="function-183"></a>

### `highpass_field` — line 484

Source: [markov.cpp:484](../markov.cpp#L484). Signature: `highpass_field(std::vector<float>& field, int H, int W, double px)`.

**Callers and how they call it**

- [`gibbs_field`](#function-189) ([markov.cpp:731](../markov.cpp#L731)): direct C++ call; `highpass_field(field, H, W, p.highpass * p.scale)`.

**CLI invocations / variants:** [M](#scenario-m) [CM](#scenario-cm) [T](#scenario-t). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Vector-to-Mat dimensions and toroidal filtering match maskutil; disabled filter leaves values unchanged.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [M](#scenario-m) [CM](#scenario-cm) [T](#scenario-t) — Exercise flat/mi/ml, temperature, radius/pair counts and report toggle; masks must be seeded reproducible and histogram-flat; fit/report must not cross scan boundaries or mutate results.

<a id="function-184"></a>

### `select_pairs` — line 505

Source: [markov.cpp:505](../markov.cpp#L505). Signature: `select_pairs(const Quantised& q, int Qv, int Qc, int cells, int radius, int step, const Neighbourhood& nb, int wantPairs, int shortlistSize, double reuse)`.

**Callers and how they call it**

- [`fit_markov_model`](#function-192) ([markov.cpp:851](../markov.cpp#L851)): direct C++ call; `select_pairs(q, Qv, Qc, cells, radius, step, nb, p.pairs, std::min<int>(p.shortlist, (int)nb.off.size()), p.reuse)`.

**CLI invocations / variants:** [M](#scenario-m) [CM](#scenario-cm) [T](#scenario-t). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Screening/shortlist/pair limits and quantization resolution; deterministic selected offsets; too-small usable region and sparse counts.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [M](#scenario-m) [CM](#scenario-cm) [T](#scenario-t) — Exercise flat/mi/ml, temperature, radius/pair counts and report toggle; masks must be seeded reproducible and histogram-flat; fit/report must not cross scan boundaries or mutate results.

<a id="function-185"></a>

### `report_occupancy` — line 566

Source: [markov.cpp:566](../markov.cpp#L566). Signature: `report_occupancy(int Qv, int Qc, int cells, int step, const std::vector<double>& occupancy)`.

**Callers and how they call it**

- [`report_fit`](#function-188) ([markov.cpp:717](../markov.cpp#L717)): direct C++ call; `report_occupancy(Qv, Qc, cells, step, occupancy)`.

**CLI invocations / variants:** [M](#scenario-m) (--markov-report). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Known occupancy table yields expected empty/sparse counts and quantiles; no mutation of model data.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [M](#scenario-m) (--markov-report) — Exercise flat/mi/ml, temperature, radius/pair counts and report toggle; masks must be seeded reproducible and histogram-flat; fit/report must not cross scan boundaries or mutate results.

#### Anonymous function: `report_occupancy::pct@574`

Source: [markov.cpp:574](../markov.cpp#L574); capture/signature: `[&](double q)`.

- **Callers/how:** std::sort/stable_sort invokes the comparator supplied by report_occupancy.

- **CLI invocations:** [M](#scenario-m) (--markov-report); same enclosing branch, not an independently named CLI operation.

- **Unit tests should exist:** Strict weak ordering, equal values, stable/deterministic ties as required; sorted output is a permutation.

- **Integration tests should exist:** [M](#scenario-m) (--markov-report); verify the enclosing function's observable assertion above with the callback branch actually reached.

#### Anonymous function: `report_occupancy::callback@590`

Source: [markov.cpp:590](../markov.cpp#L590); capture/signature: `[](double c)`.

- **Callers/how:** report_occupancy invokes this local callback directly or through the algorithm receiving it; inspect linked source for the call site.

- **CLI invocations:** [M](#scenario-m) (--markov-report); same enclosing branch, not an independently named CLI operation.

- **Unit tests should exist:** Exercise each branch of this callback through the enclosing function tests; verify captured-state updates and boundary values against the enclosing function oracle.

- **Integration tests should exist:** [M](#scenario-m) (--markov-report); verify the enclosing function's observable assertion above with the callback branch actually reached.

<a id="function-186"></a>

### `report_holdout` — line 618

Source: [markov.cpp:618](../markov.cpp#L618). Signature: `report_holdout(const Quantised& q, int Qv, int Qc, int cells, int radius, int step, const std::vector<Expert>& experts, const std::vector<double>& weights)`.

**Callers and how they call it**

- [`report_fit`](#function-188) ([markov.cpp:717](../markov.cpp#L717)): direct C++ call; `report_holdout(q, Qv, Qc, cells, radius, step, experts, weights)`.

**CLI invocations / variants:** [M](#scenario-m) (--markov-report). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Disjoint training/test columns with radius gap in every scan; finite metrics and empty holdout diagnostic.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [M](#scenario-m) (--markov-report) — Exercise flat/mi/ml, temperature, radius/pair counts and report toggle; masks must be seeded reproducible and histogram-flat; fit/report must not cross scan boundaries or mutate results.

<a id="function-187"></a>

### `report_weights` — line 676

Source: [markov.cpp:676](../markov.cpp#L676). Signature: `report_weights(const Quantised& q, int Qv, int Qc, int radius, int step, const std::vector<Expert>& experts, double temp)`.

**Callers and how they call it**

- [`report_fit`](#function-188) ([markov.cpp:717](../markov.cpp#L717)): direct C++ call; `report_weights(q, Qv, Qc, radius, step, experts, temp)`.

**CLI invocations / variants:** [M](#scenario-m) (--markov-report). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Report flat/mi/ml comparison on the same data; no model/RNG mutation; degenerate experts.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [M](#scenario-m) (--markov-report) — Exercise flat/mi/ml, temperature, radius/pair counts and report toggle; masks must be seeded reproducible and histogram-flat; fit/report must not cross scan boundaries or mutate results.

#### Anonymous function: `report_weights::callback@698`

Source: [markov.cpp:698](../markov.cpp#L698); capture/signature: `[&](size_t a, size_t b)`.

- **Callers/how:** std::sort/stable_sort invokes the comparator supplied by report_weights.

- **CLI invocations:** [M](#scenario-m) (--markov-report); same enclosing branch, not an independently named CLI operation.

- **Unit tests should exist:** Strict weak ordering, equal values, stable/deterministic ties as required; sorted output is a permutation.

- **Integration tests should exist:** [M](#scenario-m) (--markov-report); verify the enclosing function's observable assertion above with the callback branch actually reached.

<a id="function-188"></a>

### `report_fit` — line 717

Source: [markov.cpp:717](../markov.cpp#L717). Signature: `report_fit(const Quantised& q, int Qv, int Qc, int cells, int radius, int step, const std::vector<Expert>& experts, const std::vector<double>& weights, const std::vector<double>& occupancy, double temp)`.

**Callers and how they call it**

- [`sample_markov_order`](#function-193) ([markov.cpp:927](../markov.cpp#L927)): direct C++ call; `report_fit(q, Qv, Qc, cells, radius, step, experts, w, occupancy, p.temp)`.

**CLI invocations / variants:** [M](#scenario-m) (--markov-report). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** All diagnostic sections present when requested; model and generated output unchanged by reporting.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [M](#scenario-m) (--markov-report) — Exercise flat/mi/ml, temperature, radius/pair counts and report toggle; masks must be seeded reproducible and histogram-flat; fit/report must not cross scan boundaries or mutate results.

<a id="function-189"></a>

### `gibbs_field` — line 731

Source: [markov.cpp:731](../markov.cpp#L731). Signature: `gibbs_field(int H, int W, int Qv, int Qc, const std::vector<Expert>& experts, const std::vector<double>& weights, const MarkovParams& p, uint64_t seed)`.

**Callers and how they call it**

- [`sample_markov_order`](#function-193) ([markov.cpp:927](../markov.cpp#L927)): direct C++ call; `gibbs_field(H, W, Qv, Qc, experts, w, p, seed)`.

**CLI invocations / variants:** [M](#scenario-m) [CM](#scenario-cm) [T](#scenario-t). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Seed repeatability, valid bins and toroidal neighborhood lookup; zero/one/many sweeps as supported, temperature and weighting variants; exact rank population.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [M](#scenario-m) [CM](#scenario-cm) [T](#scenario-t) — Exercise flat/mi/ml, temperature, radius/pair counts and report toggle; masks must be seeded reproducible and histogram-flat; fit/report must not cross scan boundaries or mutate results.

#### Anonymous function: `gibbs_field::callback@795`

Source: [markov.cpp:795](../markov.cpp#L795); capture/signature: `[&](int a, int b)`.

- **Callers/how:** std::sort/stable_sort invokes the comparator supplied by gibbs_field.

- **CLI invocations:** [M](#scenario-m) [CM](#scenario-cm) [T](#scenario-t); same enclosing branch, not an independently named CLI operation.

- **Unit tests should exist:** Strict weak ordering, equal values, stable/deterministic ties as required; sorted output is a permutation.

- **Integration tests should exist:** [M](#scenario-m) [CM](#scenario-cm) [T](#scenario-t); verify the enclosing function's observable assertion above with the callback branch actually reached.

<a id="function-190"></a>

### `append_occupancy` — line 808

Source: [markov.cpp:808](../markov.cpp#L808). Signature: `append_occupancy(const std::vector<float>& tbl, int cells, int Qv, std::vector<double>& occupancy)`.

**Callers and how they call it**

- [`fit_markov_model`](#function-192) ([markov.cpp:851](../markov.cpp#L851)): direct C++ call; `append_occupancy(tbl, cells, Qv, occupancy)`.

**CLI invocations / variants:** [M](#scenario-m) [CM](#scenario-cm) [T](#scenario-t). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Append each conditioning-cell count once; preserve existing output; table dimensions and zero counts.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [M](#scenario-m) [CM](#scenario-cm) [T](#scenario-t) — Exercise flat/mi/ml, temperature, radius/pair counts and report toggle; masks must be seeded reproducible and histogram-flat; fit/report must not cross scan boundaries or mutate results.

<a id="function-191"></a>

### `fit_expert` — line 824

Source: [markov.cpp:824](../markov.cpp#L824). Signature: `fit_expert(Offset d1, Offset d2, std::vector<float>& tbl, int cells, int Qc, int Qv)`.

**Callers and how they call it**

- [`fit_markov_model`](#function-192) ([markov.cpp:851](../markov.cpp#L851)): direct C++ call; `fit_expert(d1, d2, tbl, cells, Qc, Qv)`.

**CLI invocations / variants:** [M](#scenario-m) [CM](#scenario-cm) [T](#scenario-t). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Offsets, conditional log tables, mutual information and occupancy agree with independent tiny counts; zero-cell prior.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [M](#scenario-m) [CM](#scenario-cm) [T](#scenario-t) — Exercise flat/mi/ml, temperature, radius/pair counts and report toggle; masks must be seeded reproducible and histogram-flat; fit/report must not cross scan boundaries or mutate results.

<a id="function-192"></a>

### `fit_markov_model` — line 851

Source: [markov.cpp:851](../markov.cpp#L851). Signature: `fit_markov_model(const samples::Set& set, const MarkovParams& p, cv::Size sz)`.

**Callers and how they call it**

- [`main`](#function-117) ([experiments/markov-20260927/draw.cpp:8](../experiments/markov-20260927/draw.cpp#L8)): archived source: intended direct call; driver currently fails to compile because fit_markov_model receives cv::Mat.

- [`synthesise_markov_order`](#function-195) ([markov.cpp:982](../markov.cpp#L982)): direct C++ call; `fit_markov_model(samples::Set::of(sample), p, sz)`.

- [`build_markov_mask`](#function-196) ([markov.cpp:990](../markov.cpp#L990)): direct C++ call; `fit_markov_model(set, p, sz)`.

- [`calibrate_markov`](#function-200) ([markovcalib.cpp:116](../markovcalib.cpp#L116)): direct C++ call; `fit_markov_model(set, fp, probe)`.

**CLI invocations / variants:** [M](#scenario-m) [CM](#scenario-cm) [T](#scenario-t). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** One/multiple scans, too-small sample returns null, requested versus actual pair counts; parameter bounds; model independent of unrelated global RNG.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [M](#scenario-m) [CM](#scenario-cm) [T](#scenario-t) — Exercise flat/mi/ml, temperature, radius/pair counts and report toggle; masks must be seeded reproducible and histogram-flat; fit/report must not cross scan boundaries or mutate results.

<a id="function-193"></a>

### `sample_markov_order` — line 927

Source: [markov.cpp:927](../markov.cpp#L927). Signature: `sample_markov_order(cv::Size sz, const MarkovModel& m, const MarkovParams& p, uint64_t seed, std::ostream* log)`.

**Callers and how they call it**

- [`sample_markov_mask`](#function-194) ([markov.cpp:976](../markov.cpp#L976)): direct C++ call; `sample_markov_order(sz, m, p, seed, log)`.

- [`synthesise_markov_order`](#function-195) ([markov.cpp:982](../markov.cpp#L982)): direct C++ call; `sample_markov_order(sz, *m, p, seed, log)`.

- [`build_markov_mask`](#function-196) ([markov.cpp:990](../markov.cpp#L990)): direct C++ call; `sample_markov_order(sz, *m, p, seed, log)`.

**CLI invocations / variants:** [M](#scenario-m) [CM](#scenario-cm) [T](#scenario-t). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Permutation and seed repeatability; flat/mi/ml and temperatures; immutable model across repeated/concurrent draws; reporting toggle leaves order unchanged.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [M](#scenario-m) [CM](#scenario-cm) [T](#scenario-t) — Exercise flat/mi/ml, temperature, radius/pair counts and report toggle; masks must be seeded reproducible and histogram-flat; fit/report must not cross scan boundaries or mutate results.

<a id="function-194"></a>

### `sample_markov_mask` — line 976

Source: [markov.cpp:976](../markov.cpp#L976). Signature: `sample_markov_mask(cv::Size sz, const MarkovModel& m, const MarkovParams& p, uint64_t seed, std::ostream* log)`.

**Callers and how they call it**

- [`main`](#function-117) ([experiments/markov-20260927/draw.cpp:8](../experiments/markov-20260927/draw.cpp#L8)): archived source: intended direct call; driver currently fails to compile because fit_markov_model receives cv::Mat.

- [`calibrate_markov`](#function-200) ([markovcalib.cpp:116](../markovcalib.cpp#L116)): call in enclosing function / its lambda callback; `sample_markov_mask(probe, *model, pp, seed + r, nullptr)`.

**CLI invocations / variants:** [M](#scenario-m) [CM](#scenario-cm) [T](#scenario-t). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Exactly threshold_mask(sample_markov_order(...)); size/range/histogram and same seed equality.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [M](#scenario-m) [CM](#scenario-cm) [T](#scenario-t) — Exercise flat/mi/ml, temperature, radius/pair counts and report toggle; masks must be seeded reproducible and histogram-flat; fit/report must not cross scan boundaries or mutate results.

<a id="function-195"></a>

### `synthesise_markov_order` — line 982

Source: [markov.cpp:982](../markov.cpp#L982). Signature: `synthesise_markov_order(cv::Size sz, const cv::Mat& sample, const MarkovParams& p, uint64_t seed, std::ostream* log)`.

**Callers and how they call it**

- [`run`](#function-166) ([maketexture.cpp:239](../maketexture.cpp#L239)): direct C++ call; `synthesise_markov_order(want, patch, markov_params_from(P), r.seed, &std::cerr)`.

**CLI invocations / variants:** [T](#scenario-t) (markov). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** One-Mat adapter agrees with fit+sample using Set::of; failure propagation and seeded replay.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [T](#scenario-t) (markov) — Exercise flat/mi/ml, temperature, radius/pair counts and report toggle; masks must be seeded reproducible and histogram-flat; fit/report must not cross scan boundaries or mutate results.

<a id="function-196"></a>

### `build_markov_mask` — line 990

Source: [markov.cpp:990](../markov.cpp#L990). Signature: `build_markov_mask(cv::Size sz, const samples::Set& set, const MarkovParams& p, uint64_t seed, std::ostream* log)`.

**Callers and how they call it**

- [`markov_build`](#function-123) ([generators.cpp:79](../generators.cpp#L79)): direct C++ call; `build_markov_mask(q.size, q.samples, markov_params_from(P), q.seed, q.log)`.

**CLI invocations / variants:** [M](#scenario-m) [CM](#scenario-cm) [T](#scenario-t). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Fit failure returns empty; success matches fit+sample path; multiple samples, null logging and deterministic seed.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [M](#scenario-m) [CM](#scenario-cm) [T](#scenario-t) — Exercise flat/mi/ml, temperature, radius/pair counts and report toggle; masks must be seeded reproducible and histogram-flat; fit/report must not cross scan boundaries or mutate results.

<a id="function-197"></a>

### `markov_work` — line 1000

Source: [markov.cpp:1000](../markov.cpp#L1000). Signature: `markov_work(int w, int h, int iters, int pairs)`.

**Callers and how they call it**

- [`markov_work_of`](#function-122) ([generators.cpp:70](../generators.cpp#L70)): direct C++ call; `markov_work(w, h, (int)r.integer(markov::keys::Iters), pairs)`.

- [`register_markov_params`](#function-198) ([markov.cpp:1005](../markov.cpp#L1005)): call in enclosing function / its lambda callback; `markov_work((int)w, (int)h, (int)iters, (int)p)`.

**CLI invocations / variants:** [M](#scenario-m) [CM](#scenario-cm) [T](#scenario-t). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Area, iteration and pair scaling; finite estimate at valid extremes; consistency with wrapper's resolved pair count.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [M](#scenario-m) [CM](#scenario-cm) [T](#scenario-t) — Exercise flat/mi/ml, temperature, radius/pair counts and report toggle; masks must be seeded reproducible and histogram-flat; fit/report must not cross scan boundaries or mutate results.

<a id="function-198"></a>

### `register_markov_params` — line 1005

Source: [markov.cpp:1005](../markov.cpp#L1005). Signature: `register_markov_params(params::Params& P)`.

**Callers and how they call it**

- [`usage_generators`](#function-031) ([cli.cpp:568](../cli.cpp#L568)): indirect: generators() row .register_params(P), once for every generator.

- [`dither_main`](#function-096) ([dither.cpp:105](../dither.cpp#L105)): indirect: generators() row .register_params(P), once for every generator.

- [`list_options`](#function-161) ([maketexture.cpp:102](../maketexture.cpp#L102)): indirect: generators() row .register_params(P), once for every generator.

- [`run`](#function-166) ([maketexture.cpp:239](../maketexture.cpp#L239)): indirect: generators() row .register_params(P), once for every generator.

- [`case_306`](#function-329) ([unittest.cpp:306](../unittest.cpp#L306)): direct C++ call; `register_markov_params(P)`.

- [`cache_registry`](#function-345) ([unittest.cpp:981](../unittest.cpp#L981)): indirect: generators() row .register_params(P), once for every generator.

**CLI invocations / variants:** [H](#scenario-h) [E](#scenario-e) [R](#scenario-r) [T](#scenario-t) [U](#scenario-u) (registration); selected-generator scenarios exercise lazy derived rules. IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Register every markov rule once; assert defaults, range endpoints, option visibility and applicability. Resolve derived rules at dependency boundaries and after overrides, check origins/dependency edges, and verify parameter-struct defaults agree. Test each lambda below through the registry.

**Existing direct C++ test callers:** [`case_306`](#function-329). These do not establish full branch coverage.

**Integration tests should exist:** [H](#scenario-h) [E](#scenario-e) [R](#scenario-r) [T](#scenario-t) [U](#scenario-u) (registration); selected-generator scenarios exercise lazy derived rules — Exercise flat/mi/ml, temperature, radius/pair counts and report toggle; masks must be seeded reproducible and histogram-flat; fit/report must not cross scan boundaries or mutate results.

#### Anonymous function: `register_markov_params::callback@1090`

Source: [markov.cpp:1090](../markov.cpp#L1090); capture/signature: `[](const params::Resolver& r) -> params::Value`.

- **Callers/how:** params::Params::resolve invokes Rule::derive(Resolver) lazily; register_markov_params registers the callback.

- **CLI invocations:** [H](#scenario-h) [E](#scenario-e) [R](#scenario-r) [T](#scenario-t) [U](#scenario-u) (registration); selected-generator scenarios exercise lazy derived rules; same enclosing branch, not an independently named CLI operation.

- **Unit tests should exist:** Resolve this individual rule at each dependency boundary; compare the derived formula/default independently, validate numeric range and dependency invalidation. Vary each read dependency independently: `Pairs`, `Shortlist`.

- **Integration tests should exist:** [H](#scenario-h) [E](#scenario-e) [R](#scenario-r) [T](#scenario-t) [U](#scenario-u) (registration); selected-generator scenarios exercise lazy derived rules; verify the enclosing function's observable assertion above with the callback branch actually reached.

#### Anonymous function: `register_markov_params::callback@1151`

Source: [markov.cpp:1151](../markov.cpp#L1151); capture/signature: `[](const params::Resolver& r) -> params::Value`.

- **Callers/how:** params::Params::resolve invokes Rule::derive(Resolver) lazily; register_markov_params registers the callback.

- **CLI invocations:** [H](#scenario-h) [E](#scenario-e) [R](#scenario-r) [T](#scenario-t) [U](#scenario-u) (registration); selected-generator scenarios exercise lazy derived rules; same enclosing branch, not an independently named CLI operation.

- **Unit tests should exist:** Resolve this individual rule at each dependency boundary; compare the derived formula/default independently, validate numeric range and dependency invalidation. Vary each read dependency independently: `Pairs`.

- **Integration tests should exist:** [H](#scenario-h) [E](#scenario-e) [R](#scenario-r) [T](#scenario-t) [U](#scenario-u) (registration); selected-generator scenarios exercise lazy derived rules; verify the enclosing function's observable assertion above with the callback branch actually reached.

#### Anonymous function: `register_markov_params::callback@1166`

Source: [markov.cpp:1166](../markov.cpp#L1166); capture/signature: `[](const params::Resolver& r) -> params::Value`.

- **Callers/how:** params::Params::resolve invokes Rule::derive(Resolver) lazily; register_markov_params registers the callback.

- **CLI invocations:** [H](#scenario-h) [E](#scenario-e) [R](#scenario-r) [T](#scenario-t) [U](#scenario-u) (registration); selected-generator scenarios exercise lazy derived rules; same enclosing branch, not an independently named CLI operation.

- **Unit tests should exist:** Resolve this individual rule at each dependency boundary; compare the derived formula/default independently, validate numeric range and dependency invalidation. Vary each read dependency independently: `mask::keys::Budget`, `mask::keys::SizeW`, `mask::keys::SizeH`, `Iters`, `Rate`.

- **Integration tests should exist:** [H](#scenario-h) [E](#scenario-e) [R](#scenario-r) [T](#scenario-t) [U](#scenario-u) (registration); selected-generator scenarios exercise lazy derived rules; verify the enclosing function's observable assertion above with the callback branch actually reached.

#### Anonymous function: `register_markov_params::callback@1210`

Source: [markov.cpp:1210](../markov.cpp#L1210); capture/signature: `[](const params::Resolver& r) -> params::Value`.

- **Callers/how:** params::Params::resolve invokes Rule::derive(Resolver) lazily; register_markov_params registers the callback.

- **CLI invocations:** [H](#scenario-h) [E](#scenario-e) [R](#scenario-r) [T](#scenario-t) [U](#scenario-u) (registration); selected-generator scenarios exercise lazy derived rules; same enclosing branch, not an independently named CLI operation.

- **Unit tests should exist:** Resolve this individual rule at each dependency boundary; compare the derived formula/default independently, validate numeric range and dependency invalidation. Vary each read dependency independently: `Bins`, `Pairs`, `Iters`.

- **Integration tests should exist:** [H](#scenario-h) [E](#scenario-e) [R](#scenario-r) [T](#scenario-t) [U](#scenario-u) (registration); selected-generator scenarios exercise lazy derived rules; verify the enclosing function's observable assertion above with the callback branch actually reached.

<a id="file-27"></a>

## `markov.h`

### Declaration: `register_markov_params(params::Params&)`

Source: [markov.h:34](../markov.h#L34).

Callers, CLI variants, unit tests and integration tests: [`register_markov_params`](#function-198). This is the declaration of that same function; do not count it as separate executed coverage.

### Declaration: `markov_work(int w, int h, int iters, int pairs)`

Source: [markov.h:39](../markov.h#L39).

Callers, CLI variants, unit tests and integration tests: [`markov_work`](#function-197). This is the declaration of that same function; do not count it as separate executed coverage.

### Declaration: `build_markov_mask(cv::Size sz, const samples::Set& set, const MarkovParams& p, uint64_t seed, std::ostream* log)`

Source: [markov.h:109](../markov.h#L109).

Callers, CLI variants, unit tests and integration tests: [`build_markov_mask`](#function-196). This is the declaration of that same function; do not count it as separate executed coverage.

### Declaration: `synthesise_markov_order(cv::Size sz, const cv::Mat& sample, const MarkovParams& p, uint64_t seed, std::ostream* log)`

Source: [markov.h:115](../markov.h#L115).

Callers, CLI variants, unit tests and integration tests: [`synthesise_markov_order`](#function-195). This is the declaration of that same function; do not count it as separate executed coverage.

<a id="function-364"></a>

### `calibrate_markov(cv::Mat declaration)` — line 90

Source: [markov.h:90](../markov.h#L90). Signature: `calibrate_markov(const cv::Mat& sample, const MarkovParams& p, bool weights_pinned, bool radius_pinned, uint64_t seed, std::ostream& log)`.

**Callers and how they call it**

- No legal runtime caller for a deleted function, or no linkable implementation for the stale declaration; see unit/API test below.

**CLI invocations / variants:** none: compile-time/API test only. IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Add a separate client translation unit including only markov.h and call this exact signature, then link dither_lib. It currently has no matching definition; reconcile declaration and implementation before claiming coverage.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** none: compile-time/API test only — Compile/link API checks rather than runtime integration; deleted guard operations must fail compilation, and the declared calibrate_markov overload needs an implementation or corrected interface.

<a id="file-28"></a>

## `markovcalib.cpp`

<a id="function-199"></a>

### `reference_mask` — line 65

Source: [markovcalib.cpp:65](../markovcalib.cpp#L65). Signature: `reference_mask(const samples::Set& set, const MarkovParams& p, cv::Size sz)`.

**Callers and how they call it**

- [`calibrate_markov`](#function-200) ([markovcalib.cpp:116](../markovcalib.cpp#L116)): direct C++ call; `reference_mask(set, p, probe)`.

**CLI invocations / variants:** [CM](#scenario-cm). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Use primary scan, highpass/scale and requested probe size exactly; one/multiple scans and too-small reference; compare to calib::reference_mask.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [CM](#scenario-cm) — Exercise full/restricted/pinned search and corrupt/missing cache entries; verify failure/no optimum does not persist a fake answer, selected params reach the build, and read-only config remains nonfatal.

<a id="function-200"></a>

### `calibrate_markov` — line 116

Source: [markovcalib.cpp:116](../markovcalib.cpp#L116). Signature: `calibrate_markov(const samples::Set& set, const MarkovParams& p, bool weights_pinned, bool radius_pinned, uint64_t seed, std::ostream& log)`.

**Callers and how they call it**

- [`calibrate_markov_for`](#function-204) ([markovcalib.cpp:403](../markovcalib.cpp#L403)): direct C++ call; `calibrate_markov(set, mp, weightsPinned, radiusPinned, seed, log)`.

**CLI invocations / variants:** [CM](#scenario-cm). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Pinned/unpinned radius and scheme grids; too-small reference or fit/draw failures; no winners; endpoint best rejected; quadratic interpolation only with valid evenly spaced neighbors; deterministic averaging of seeded draws; model not mutated by concurrent sampling.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [CM](#scenario-cm) — Exercise full/restricted/pinned search and corrupt/missing cache entries; verify failure/no optimum does not persist a fake answer, selected params reach the build, and read-only config remains nonfatal.

#### Anonymous function: `calibrate_markov::temp_at@183`

Source: [markovcalib.cpp:183](../markovcalib.cpp#L183); capture/signature: `[&sc](int i)`.

- **Callers/how:** calibrate_markov and its parallel task callback invoke temp_at(i).

- **CLI invocations:** [CM](#scenario-cm); same enclosing branch, not an independently named CLI operation.

- **Unit tests should exist:** First/last probe equal scheme endpoints; evenly spaced interiors and correct denominator.

- **Integration tests should exist:** [CM](#scenario-cm); verify the enclosing function's observable assertion above with the callback branch actually reached.

#### Anonymous function: `calibrate_markov::callback@190`

Source: [markovcalib.cpp:190](../markovcalib.cpp#L190); capture/signature: `[&](const cv::Range& range)`.

- **Callers/how:** cv::parallel_for_ executes probe/realisation tasks.

- **CLI invocations:** [CM](#scenario-cm); same enclosing branch, not an independently named CLI operation.

- **Unit tests should exist:** Each task writes its own score slot, seed+realisation assignment, failed draw sentinel and immutable shared model; sequential/parallel score equality.

- **Integration tests should exist:** [CM](#scenario-cm); verify the enclosing function's observable assertion above with the callback branch actually reached.

<a id="function-201"></a>

### `calibration_key` — line 300

Source: [markovcalib.cpp:300](../markovcalib.cpp#L300). Signature: `calibration_key(const samples::Set& set, const MarkovParams& p, uint64_t seed, bool weightsPinned, bool radiusPinned)`.

**Callers and how they call it**

- [`calibrate_markov_for`](#function-204) ([markovcalib.cpp:403](../markovcalib.cpp#L403)): direct C++ call; `calibration_key(set, mp, seed, weightsPinned, radiusPinned)`.

**CLI invocations / variants:** [CM](#scenario-cm). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Stable digest for identical data/seed/settings; modify each hashed field, scan pixels, order, dimensions/type and pin state where present; noncontinuous versus cloned scans give identical keys. The tuned quantity and requested output size should not invalidate calibration unless deliberately hashed; version changes must invalidate old results.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [CM](#scenario-cm) — Exercise full/restricted/pinned search and corrupt/missing cache entries; verify failure/no optimum does not persist a fake answer, selected params reach the build, and read-only config remains nonfatal.

#### Anonymous function: `calibration_key::mix@304`

Source: [markovcalib.cpp:304](../markovcalib.cpp#L304); capture/signature: `[&h](const void* data, size_t n)`.

- **Callers/how:** calibration_key invokes the local hash helpers; mix_i/mix_d forward typed bytes to mix.

- **CLI invocations:** [CM](#scenario-cm); same enclosing branch, not an independently named CLI operation.

- **Unit tests should exist:** Known bytes, empty update and split-update equivalence; typed values change the digest as specified; captured hash state is updated exactly once.

- **Integration tests should exist:** [CM](#scenario-cm); verify the enclosing function's observable assertion above with the callback branch actually reached.

#### Anonymous function: `calibration_key::mix_i@308`

Source: [markovcalib.cpp:308](../markovcalib.cpp#L308); capture/signature: `[&mix](long long v)`.

- **Callers/how:** calibration_key invokes the local hash helpers; mix_i/mix_d forward typed bytes to mix.

- **CLI invocations:** [CM](#scenario-cm); same enclosing branch, not an independently named CLI operation.

- **Unit tests should exist:** Known bytes, empty update and split-update equivalence; typed values change the digest as specified; captured hash state is updated exactly once.

- **Integration tests should exist:** [CM](#scenario-cm); verify the enclosing function's observable assertion above with the callback branch actually reached.

#### Anonymous function: `calibration_key::mix_d@309`

Source: [markovcalib.cpp:309](../markovcalib.cpp#L309); capture/signature: `[&mix](double v)`.

- **Callers/how:** calibration_key invokes the local hash helpers; mix_i/mix_d forward typed bytes to mix.

- **CLI invocations:** [CM](#scenario-cm); same enclosing branch, not an independently named CLI operation.

- **Unit tests should exist:** Known bytes, empty update and split-update equivalence; typed values change the digest as specified; captured hash state is updated exactly once.

- **Integration tests should exist:** [CM](#scenario-cm); verify the enclosing function's observable assertion above with the callback branch actually reached.

<a id="function-202"></a>

### `cached_calibration` — line 355

Source: [markovcalib.cpp:355](../markovcalib.cpp#L355). Signature: `cached_calibration( params::Params& P, const std::string& key, const std::string& wkey, const std::string& rkey, const MarkovParams& mp, bool weightsPinned, bool radiusPinned)`.

**Callers and how they call it**

- [`calibrate_markov_for`](#function-204) ([markovcalib.cpp:403](../markovcalib.cpp#L403)): direct C++ call; `cached_calibration(P, key, wkey, rkey, mp, weightsPinned, radiusPinned)`.

**CLI invocations / variants:** [CM](#scenario-cm). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Missing temperature/weight/radius entries, NaN/infinity, fractions, out-of-range and off-ladder values all miss safely; pinned fields may omit corresponding cache entries and remain unchanged.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [CM](#scenario-cm) — Exercise full/restricted/pinned search and corrupt/missing cache entries; verify failure/no optimum does not persist a fake answer, selected params reach the build, and read-only config remains nonfatal.

<a id="function-203"></a>

### `apply` — line 394

Source: [markovcalib.cpp:394](../markovcalib.cpp#L394). Signature: `apply(params::Params& P, const Calibration& c, bool weightsPinned, bool radiusPinned)`.

**Callers and how they call it**

- [`calibrate_markov_for`](#function-204) ([markovcalib.cpp:403](../markovcalib.cpp#L403)): direct C++ call; `apply(P, *c, weightsPinned, radiusPinned)`.

**CLI invocations / variants:** [CM](#scenario-cm). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Set measured temperature and only unpinned weighting/radius; preserve User origins and invalidate dependent defaults correctly.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [CM](#scenario-cm) — Exercise full/restricted/pinned search and corrupt/missing cache entries; verify failure/no optimum does not persist a fake answer, selected params reach the build, and read-only config remains nonfatal.

<a id="function-204"></a>

### `calibrate_markov_for` — line 403

Source: [markovcalib.cpp:403](../markovcalib.cpp#L403). Signature: `calibrate_markov_for(params::Params& P, const std::string& sample_path, uint64_t seed, const std::string& cfg, std::ostream& log)`.

**Callers and how they call it**

- [`maybe_calibrate`](#function-214) ([mask.cpp:435](../mask.cpp#L435)): indirect: Generator::calibrate(P, sample path, seed, config, log), only when calibration is requested.

**CLI invocations / variants:** [CM](#scenario-cm). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Explicit temp skips entire search before sample read; unreadable sample fails; pinned weighting/radius preserved; valid complete cache hit versus missing/corrupt values; null calibration retains defaults without caching; apply and save correct selected settings.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [CM](#scenario-cm) — Exercise full/restricted/pinned search and corrupt/missing cache entries; verify failure/no optimum does not persist a fake answer, selected params reach the build, and read-only config remains nonfatal.

<a id="file-29"></a>

## `markovcalib.h`

### Declaration: `calibrate_markov_for(params::Params& P, const std::string& sample_path, uint64_t seed, const std::string& cfg, std::ostream& log)`

Source: [markovcalib.h:16](../markovcalib.h#L16).

Callers, CLI variants, unit tests and integration tests: [`calibrate_markov_for`](#function-204). This is the declaration of that same function; do not count it as separate executed coverage.

<a id="file-30"></a>

## `markovmodel.h`

### Declaration: `fit_markov_model(const samples::Set& set, const MarkovParams& p, cv::Size sz)`

Source: [markovmodel.h:62](../markovmodel.h#L62).

Callers, CLI variants, unit tests and integration tests: [`fit_markov_model`](#function-192). This is the declaration of that same function; do not count it as separate executed coverage.

### Declaration: `sample_markov_order(cv::Size sz, const MarkovModel& m, const MarkovParams& p, uint64_t seed, std::ostream* log)`

Source: [markovmodel.h:70](../markovmodel.h#L70).

Callers, CLI variants, unit tests and integration tests: [`sample_markov_order`](#function-193). This is the declaration of that same function; do not count it as separate executed coverage.

### Declaration: `sample_markov_mask(cv::Size sz, const MarkovModel& m, const MarkovParams& p, uint64_t seed, std::ostream* log)`

Source: [markovmodel.h:74](../markovmodel.h#L74).

Callers, CLI variants, unit tests and integration tests: [`sample_markov_mask`](#function-194). This is the declaration of that same function; do not count it as separate executed coverage.

<a id="function-205"></a>

### `Offset::band` — line 22

Source: [markovmodel.h:22](../markovmodel.h#L22). Signature: `band() const`.

**Callers and how they call it**

- [`shortlist_offsets`](#function-176) ([markov.cpp:266](../markov.cpp#L266)): direct C++ call; receiver/signature resolved by compiler.

- [`best_unused_candidate`](#function-177) ([markov.cpp:301](../markov.cpp#L301)): direct C++ call; receiver/signature resolved by compiler.

**CLI invocations / variants:** [M](#scenario-m) [CM](#scenario-cm) [T](#scenario-t). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Chebyshev distance for axes, diagonals, signs and origin.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [M](#scenario-m) [CM](#scenario-cm) [T](#scenario-t) — Fit then draw repeatedly in a linked client; compare masks and confirm immutable fitted state, valid experts and per-scan quantization.

<a id="function-206"></a>

### `Quantised::size` — line 44

Source: [markovmodel.h:44](../markovmodel.h#L44). Signature: `size() const`.

**Callers and how they call it**

- [`tally`](#function-171) ([markov.cpp:176](../markov.cpp#L176)): direct C++ call; receiver/signature resolved by compiler.

- [`pooled_logloss`](#function-181) ([markov.cpp:385](../markov.cpp#L385)): direct C++ call; receiver/signature resolved by compiler.

- [`fit_weights_ml`](#function-182) ([markov.cpp:423](../markov.cpp#L423)): direct C++ call; receiver/signature resolved by compiler.

**CLI invocations / variants:** [M](#scenario-m) [CM](#scenario-cm) [T](#scenario-t). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Return qn scan count for empty and populated structures; assert construction keeps qn/qv paired.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [M](#scenario-m) [CM](#scenario-cm) [T](#scenario-t) — Fit then draw repeatedly in a linked client; compare masks and confirm immutable fitted state, valid experts and per-scan quantization.

<a id="file-31"></a>

## `mask.cpp`

<a id="function-207"></a>

### `mask_seconds` — line 43

Source: [mask.cpp:43](../mask.cpp#L43). Signature: `mask_seconds(const params::Resolver& r, const std::string& method, double w, double h, int pairs)`.

**Callers and how they call it**

- [`mask_side`](#function-208) ([mask.cpp:99](../mask.cpp#L99)): direct C++ call; `mask_seconds(r, method, side, side, kBudgetRefPairs)`.

**CLI invocations / variants:** [E](#scenario-e) [B](#scenario-b) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [L](#scenario-l) [C](#scenario-c) [V](#scenario-v) [CD](#scenario-cd) [CM](#scenario-cm) [CP](#scenario-cp). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Selected generator work divided by rate; each table row, reference pair count and realistic small/large geometry.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [E](#scenario-e) [B](#scenario-b) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [L](#scenario-l) [C](#scenario-c) [V](#scenario-v) [CD](#scenario-cd) [CM](#scenario-cm) [CP](#scenario-cp) — Miss/hit/load paths and each generator; verify size limits, sample errors, build rates and measured calibration routing; a hit must not masquerade as a fresh build.

<a id="function-208"></a>

### `mask_side` — line 99

Source: [mask.cpp:99](../mask.cpp#L99). Signature: `mask_side(const params::Resolver& r)`.

**Callers and how they call it**

- [`register_mask_params`](#function-209) ([mask.cpp:160](../mask.cpp#L160)): call in enclosing function / its lambda callback; `mask_side(r)`.

**CLI invocations / variants:** [E](#scenario-e) [B](#scenario-b) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [L](#scenario-l) [C](#scenario-c) [V](#scenario-v) [CD](#scenario-cd) [CM](#scenario-cm) [CP](#scenario-cp). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Repeat ceiling, generator max size, sample limit, supersampling, budget and explicit dimensions; each constraint wins independently; minimum bounds and non-square source.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [E](#scenario-e) [B](#scenario-b) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [L](#scenario-l) [C](#scenario-c) [V](#scenario-v) [CD](#scenario-cd) [CM](#scenario-cm) [CP](#scenario-cp) — Miss/hit/load paths and each generator; verify size limits, sample errors, build rates and measured calibration routing; a hit must not masquerade as a fresh build.

<a id="function-209"></a>

### `register_mask_params` — line 160

Source: [mask.cpp:160](../mask.cpp#L160). Signature: `register_mask_params(params::Params& P)`.

**Callers and how they call it**

- [`usage_generators`](#function-031) ([cli.cpp:568](../cli.cpp#L568)): direct C++ call; `register_mask_params(P)`.

- [`dither_main`](#function-096) ([dither.cpp:105](../dither.cpp#L105)): direct C++ call; `register_mask_params(P)`.

- [`list_options`](#function-161) ([maketexture.cpp:102](../maketexture.cpp#L102)): direct C++ call; `register_mask_params(P)`.

- [`run`](#function-166) ([maketexture.cpp:239](../maketexture.cpp#L239)): direct C++ call; `register_mask_params(P)`.

- [`case_306`](#function-329) ([unittest.cpp:306](../unittest.cpp#L306)): direct C++ call; `register_mask_params(P)`.

- [`cache_registry`](#function-345) ([unittest.cpp:981](../unittest.cpp#L981)): direct C++ call; `register_mask_params(P)`.

**CLI invocations / variants:** [H](#scenario-h) [E](#scenario-e) [R](#scenario-r) [T](#scenario-t) [U](#scenario-u) (registration); selected-generator scenarios exercise lazy derived rules. IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Register every mask rule once; assert defaults, range endpoints, option visibility and applicability. Resolve derived rules at dependency boundaries and after overrides, check origins/dependency edges, and verify parameter-struct defaults agree. Test each lambda below through the registry.

**Existing direct C++ test callers:** [`case_306`](#function-329). These do not establish full branch coverage.

**Integration tests should exist:** [H](#scenario-h) [E](#scenario-e) [R](#scenario-r) [T](#scenario-t) [U](#scenario-u) (registration); selected-generator scenarios exercise lazy derived rules — Miss/hit/load paths and each generator; verify size limits, sample errors, build rates and measured calibration routing; a hit must not masquerade as a fresh build.

#### Anonymous function: `register_mask_params::callback@163`

Source: [mask.cpp:163](../mask.cpp#L163); capture/signature: `[](const params::Resolver& r) -> params::Value`.

- **Callers/how:** params::Params::resolve invokes Rule::derive(Resolver) lazily; register_mask_params registers the callback.

- **CLI invocations:** [H](#scenario-h) [E](#scenario-e) [R](#scenario-r) [T](#scenario-t) [U](#scenario-u) (registration); selected-generator scenarios exercise lazy derived rules; same enclosing branch, not an independently named CLI operation.

- **Unit tests should exist:** Resolve this individual rule at each dependency boundary; compare the derived formula/default independently, validate numeric range and dependency invalidation.

- **Integration tests should exist:** [H](#scenario-h) [E](#scenario-e) [R](#scenario-r) [T](#scenario-t) [U](#scenario-u) (registration); selected-generator scenarios exercise lazy derived rules; verify the enclosing function's observable assertion above with the callback branch actually reached.

#### Anonymous function: `register_mask_params::callback@166`

Source: [mask.cpp:166](../mask.cpp#L166); capture/signature: `[](const params::Resolver& r) -> params::Value`.

- **Callers/how:** params::Params::resolve invokes Rule::derive(Resolver) lazily; register_mask_params registers the callback.

- **CLI invocations:** [H](#scenario-h) [E](#scenario-e) [R](#scenario-r) [T](#scenario-t) [U](#scenario-u) (registration); selected-generator scenarios exercise lazy derived rules; same enclosing branch, not an independently named CLI operation.

- **Unit tests should exist:** Resolve this individual rule at each dependency boundary; compare the derived formula/default independently, validate numeric range and dependency invalidation.

- **Integration tests should exist:** [H](#scenario-h) [E](#scenario-e) [R](#scenario-r) [T](#scenario-t) [U](#scenario-u) (registration); selected-generator scenarios exercise lazy derived rules; verify the enclosing function's observable assertion above with the callback branch actually reached.

#### Anonymous function: `register_mask_params::callback@183`

Source: [mask.cpp:183](../mask.cpp#L183); capture/signature: `[](const params::Resolver&) -> params::Value`.

- **Callers/how:** params::Params::resolve invokes Rule::derive(Resolver) lazily; register_mask_params registers the callback.

- **CLI invocations:** [H](#scenario-h) [E](#scenario-e) [R](#scenario-r) [T](#scenario-t) [U](#scenario-u) (registration); selected-generator scenarios exercise lazy derived rules; same enclosing branch, not an independently named CLI operation.

- **Unit tests should exist:** Resolve this individual rule at each dependency boundary; compare the derived formula/default independently, validate numeric range and dependency invalidation.

- **Integration tests should exist:** [H](#scenario-h) [E](#scenario-e) [R](#scenario-r) [T](#scenario-t) [U](#scenario-u) (registration); selected-generator scenarios exercise lazy derived rules; verify the enclosing function's observable assertion above with the callback branch actually reached.

#### Anonymous function: `register_mask_params::callback@200`

Source: [mask.cpp:200](../mask.cpp#L200); capture/signature: `[](const params::Resolver& r) -> params::Value`.

- **Callers/how:** params::Params::resolve invokes Rule::derive(Resolver) lazily; register_mask_params registers the callback.

- **CLI invocations:** [H](#scenario-h) [E](#scenario-e) [R](#scenario-r) [T](#scenario-t) [U](#scenario-u) (registration); selected-generator scenarios exercise lazy derived rules; same enclosing branch, not an independently named CLI operation.

- **Unit tests should exist:** Resolve this individual rule at each dependency boundary; compare the derived formula/default independently, validate numeric range and dependency invalidation. Vary each read dependency independently: `mask::keys::Sample`.

- **Integration tests should exist:** [H](#scenario-h) [E](#scenario-e) [R](#scenario-r) [T](#scenario-t) [U](#scenario-u) (registration); selected-generator scenarios exercise lazy derived rules; verify the enclosing function's observable assertion above with the callback branch actually reached.

<a id="function-210"></a>

### `describe` — line 287

Source: [mask.cpp:287](../mask.cpp#L287). Signature: `describe(const samples::Set& set, bool combines)`.

**Callers and how they call it**

- [`build_with_method`](#function-211) ([mask.cpp:296](../mask.cpp#L296)): direct C++ call; `describe(set, gen->combines)`.

**CLI invocations / variants:** [E](#scenario-e) [B](#scenario-b) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [L](#scenario-l) [C](#scenario-c) [V](#scenario-v) [CD](#scenario-cd) [CM](#scenario-cm) [CP](#scenario-cp). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** One scan dimensions; combined count/megapixels for multiple; direct reports primary scan only; caller's nonempty precondition.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [E](#scenario-e) [B](#scenario-b) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [L](#scenario-l) [C](#scenario-c) [V](#scenario-v) [CD](#scenario-cd) [CM](#scenario-cm) [CP](#scenario-cp) — Miss/hit/load paths and each generator; verify size limits, sample errors, build rates and measured calibration routing; a hit must not masquerade as a fresh build.

<a id="function-211"></a>

### `build_with_method` — line 296

Source: [mask.cpp:296](../mask.cpp#L296). Signature: `build_with_method(cv::Size maskSz, const samples::Set& set, const Options& opt, params::Params& P)`.

**Callers and how they call it**

- [`obtain_mask`](#function-213) ([mask.cpp:377](../mask.cpp#L377)): direct C++ call; `build_with_method(maskSz, set, opt, P)`.

**CLI invocations / variants:** [E](#scenario-e) [B](#scenario-b) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [L](#scenario-l) [C](#scenario-c) [V](#scenario-v) [CD](#scenario-cd) [CM](#scenario-cm) [CP](#scenario-cp). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Each table row dispatches correct MaskRequest; missing sample error, library suggestions, multi-scan direct warning; empty result propagation.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [E](#scenario-e) [B](#scenario-b) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [L](#scenario-l) [C](#scenario-c) [V](#scenario-v) [CD](#scenario-cd) [CM](#scenario-cm) [CP](#scenario-cp) — Miss/hit/load paths and each generator; verify size limits, sample errors, build rates and measured calibration routing; a hit must not masquerade as a fresh build.

<a id="function-212"></a>

### `record_build_rate` — line 355

Source: [mask.cpp:355](../mask.cpp#L355). Signature: `record_build_rate(const Options& opt, params::Params& P, const cv::Mat& mask, double secs, const std::string& cfg)`.

**Callers and how they call it**

- [`obtain_mask`](#function-213) ([mask.cpp:377](../mask.cpp#L377)): direct C++ call; `record_build_rate(opt, P, mask, std::chrono::duration<double>( std::chrono::steady_clock::now() - started).count(), cfg)`.

**CLI invocations / variants:** [E](#scenario-e) [B](#scenario-b) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [L](#scenario-l) [C](#scenario-c) [V](#scenario-v) [CD](#scenario-cd) [CM](#scenario-cm) [CP](#scenario-cp). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** At/below 0.25 seconds does nothing; above it stores work/seconds; no rate from a cache hit; save failure diagnostic without failed image run.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [E](#scenario-e) [B](#scenario-b) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [L](#scenario-l) [C](#scenario-c) [V](#scenario-v) [CD](#scenario-cd) [CM](#scenario-cm) [CP](#scenario-cp) — Miss/hit/load paths and each generator; verify size limits, sample errors, build rates and measured calibration routing; a hit must not masquerade as a fresh build.

<a id="function-213"></a>

### `obtain_mask` — line 377

Source: [mask.cpp:377](../mask.cpp#L377). Signature: `obtain_mask(const Options& opt, const samples::Set& set, params::Params& P, const std::string& cfg)`.

**Callers and how they call it**

- [`dither_main`](#function-096) ([dither.cpp:105](../dither.cpp#L105)): direct C++ call; `obtain_mask(opt, set, P, cfg)`.

**CLI invocations / variants:** [E](#scenario-e) [B](#scenario-b) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [L](#scenario-l) [C](#scenario-c) [V](#scenario-v) [CD](#scenario-cd) [CM](#scenario-cm) [CP](#scenario-cp). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Explicit mask, valid cache hit, wrong-sized/corrupt cache and miss; build failure; config disabled avoids key hashing; write/read round-trip.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [E](#scenario-e) [B](#scenario-b) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [L](#scenario-l) [C](#scenario-c) [V](#scenario-v) [CD](#scenario-cd) [CM](#scenario-cm) [CP](#scenario-cp) — Miss/hit/load paths and each generator; verify size limits, sample errors, build rates and measured calibration routing; a hit must not masquerade as a fresh build.

<a id="function-214"></a>

### `maybe_calibrate` — line 435

Source: [mask.cpp:435](../mask.cpp#L435). Signature: `maybe_calibrate(const Options& opt, params::Params& P, const std::string& cfg)`.

**Callers and how they call it**

- [`dither_main`](#function-096) ([dither.cpp:105](../dither.cpp#L105)): direct C++ call; `maybe_calibrate(opt, P, cfg)`.

**CLI invocations / variants:** [E](#scenario-e) [B](#scenario-b) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [L](#scenario-l) [C](#scenario-c) [V](#scenario-v) [CD](#scenario-cd) [CM](#scenario-cm) [CP](#scenario-cp). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Disabled is no-op; selected hook called once with sample/seed/config; unsupported hook no-op at this layer; false propagates.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [E](#scenario-e) [B](#scenario-b) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [L](#scenario-l) [C](#scenario-c) [V](#scenario-v) [CD](#scenario-cd) [CM](#scenario-cm) [CP](#scenario-cp) — Miss/hit/load paths and each generator; verify size limits, sample errors, build rates and measured calibration routing; a hit must not masquerade as a fresh build.

<a id="file-32"></a>

## `mask.h`

### Declaration: `register_mask_params(params::Params& P)`

Source: [mask.h:15](../mask.h#L15).

Callers, CLI variants, unit tests and integration tests: [`register_mask_params`](#function-209). This is the declaration of that same function; do not count it as separate executed coverage.

### Declaration: `obtain_mask(const Options& opt, const samples::Set& set, params::Params& P, const std::string& cfg)`

Source: [mask.h:23](../mask.h#L23).

Callers, CLI variants, unit tests and integration tests: [`obtain_mask`](#function-213). This is the declaration of that same function; do not count it as separate executed coverage.

### Declaration: `maybe_calibrate(const Options& opt, params::Params& P, const std::string& cfg)`

Source: [mask.h:29](../mask.h#L29).

Callers, CLI variants, unit tests and integration tests: [`maybe_calibrate`](#function-214). This is the declaration of that same function; do not count it as separate executed coverage.

<a id="file-33"></a>

## `maskcache.cpp`

<a id="function-215"></a>

### `maskcache::Fnv::add` — line 34

Source: [maskcache.cpp:34](../maskcache.cpp#L34). Signature: `add(const void* p, size_t n)`.

**Callers and how they call it**

- [`maskcache::Fnv::add`](#function-216) ([maskcache.cpp:39](../maskcache.cpp#L39)): direct C++ call; receiver/signature resolved by compiler.

- [`maskcache::hash_file`](#function-219) ([maskcache.cpp:83](../maskcache.cpp#L83)): direct C++ call; receiver/signature resolved by compiler.

- [`maskcache::key_for`](#function-222) ([maskcache.cpp:116](../maskcache.cpp#L116)): direct C++ call; receiver/signature resolved by compiler.

**CLI invocations / variants:** [C](#scenario-c). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Known byte-sequence hash vectors; split updates equal concatenated update; string overload hashes exactly its bytes (it adds no delimiter).

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [C](#scenario-c) — Two-process repeated builds hit the cache; changed relevant inputs miss; corrupt/wrong-size entries rebuild; failed cache writes preserve successful output. Add concurrent-writer and eviction fixtures.

<a id="function-216"></a>

### `maskcache::Fnv::add` — line 39

Source: [maskcache.cpp:39](../maskcache.cpp#L39). Signature: `add(const std::string& s)`.

**Callers and how they call it**

- [`maskcache::key_for`](#function-222) ([maskcache.cpp:116](../maskcache.cpp#L116)): direct C++ call; receiver/signature resolved by compiler.

**CLI invocations / variants:** [C](#scenario-c). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Known byte-sequence hash vectors; split updates equal concatenated update; string overload hashes exactly its bytes (it adds no delimiter).

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [C](#scenario-c) — Two-process repeated builds hit the cache; changed relevant inputs miss; corrupt/wrong-size entries rebuild; failed cache writes preserve successful output. Add concurrent-writer and eviction fixtures.

<a id="function-217"></a>

### `maskcache::build_identity` — line 51

Source: [maskcache.cpp:51](../maskcache.cpp#L51). Signature: `build_identity()`.

**Callers and how they call it**

- [`maskcache::key_for`](#function-222) ([maskcache.cpp:116](../maskcache.cpp#L116)): direct C++ call; `build_identity()`.

**CLI invocations / variants:** [C](#scenario-c). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Stable for unchanged executable and changes with executable identity; failure-to-stat path disables unsafe reuse as implemented.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [C](#scenario-c) — Two-process repeated builds hit the cache; changed relevant inputs miss; corrupt/wrong-size entries rebuild; failed cache writes preserve successful output. Add concurrent-writer and eviction fixtures.

<a id="function-218"></a>

### `maskcache::tame` — line 70

Source: [maskcache.cpp:70](../maskcache.cpp#L70). Signature: `tame(const std::string& s, size_t cap)`.

**Callers and how they call it**

- [`maskcache::key_for`](#function-222) ([maskcache.cpp:116](../maskcache.cpp#L116)): direct C++ call; `tame(P.value_of(mask::keys::Method), 16)`, `tame(P.value_of(mask::keys::Highpass), 8)`, `tame(P.value_of(mask::keys::Scale), 8)`, `tame(P.value_of(mask::keys::Seed), 12)`.

**CLI invocations / variants:** [C](#scenario-c). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Unsafe path characters sanitized, maximum length, empty string and Unicode bytes; cannot introduce directory traversal.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [C](#scenario-c) — Two-process repeated builds hit the cache; changed relevant inputs miss; corrupt/wrong-size entries rebuild; failed cache writes preserve successful output. Add concurrent-writer and eviction fixtures.

<a id="function-219"></a>

### `maskcache::hash_file` — line 83

Source: [maskcache.cpp:83](../maskcache.cpp#L83). Signature: `hash_file(const std::string& path, Fnv& f)`.

**Callers and how they call it**

- [`maskcache::key_for`](#function-222) ([maskcache.cpp:116](../maskcache.cpp#L116)): direct C++ call; `hash_file(p, sf)`.

**CLI invocations / variants:** [C](#scenario-c). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Known file bytes; changed content changes hash; unreadable/missing file fails; empty/large files and block boundaries.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [C](#scenario-c) — Two-process repeated builds hit the cache; changed relevant inputs miss; corrupt/wrong-size entries rebuild; failed cache writes preserve successful output. Add concurrent-writer and eviction fixtures.

<a id="function-220"></a>

### `maskcache::hex` — line 101

Source: [maskcache.cpp:101](../maskcache.cpp#L101). Signature: `hex(uint64_t v)`.

**Callers and how they call it**

- [`maskcache::key_for`](#function-222) ([maskcache.cpp:116](../maskcache.cpp#L116)): direct C++ call; `hex(sf.h)`, `hex(f.h)`.

**CLI invocations / variants:** [C](#scenario-c). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Zero, leading zeros and UINT64_MAX; fixed-width lowercase representation.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [C](#scenario-c) — Two-process repeated builds hit the cache; changed relevant inputs miss; corrupt/wrong-size entries rebuild; failed cache writes preserve successful output. Add concurrent-writer and eviction fixtures.

<a id="function-221"></a>

### `maskcache::dir_for` — line 110

Source: [maskcache.cpp:110](../maskcache.cpp#L110). Signature: `dir_for(const std::string& configPath)`.

**Callers and how they call it**

- [`obtain_mask`](#function-213) ([mask.cpp:377](../mask.cpp#L377)): direct C++ call; `dir_for(cfg)`.

**CLI invocations / variants:** [C](#scenario-c). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Empty config disables cache; ordinary/relative/path-with-spaces config derives expected sibling directory.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [C](#scenario-c) — Two-process repeated builds hit the cache; changed relevant inputs miss; corrupt/wrong-size entries rebuild; failed cache writes preserve successful output. Add concurrent-writer and eviction fixtures.

<a id="function-222"></a>

### `maskcache::key_for` — line 116

Source: [maskcache.cpp:116](../maskcache.cpp#L116). Signature: `key_for(params::Params& P, const std::string& samplePath)`.

**Callers and how they call it**

- [`obtain_mask`](#function-213) ([mask.cpp:377](../mask.cpp#L377)): direct C++ call; `key_for(P, opt.maskSample)`.

- [`case_1080`](#function-348) ([unittest.cpp:1080](../unittest.cpp#L1080)): direct C++ call; `key_for(a, "")`, `key_for(b, "")`.

- [`case_1145`](#function-350) ([unittest.cpp:1145](../unittest.cpp#L1145)): direct C++ call; `key_for(before, g.needs_sample ? sample : "")`, `key_for(after, g.needs_sample ? sample : "")`.

- [`case_1235`](#function-351) ([unittest.cpp:1235](../unittest.cpp#L1235)): direct C++ call; `key_for(before, "")`, `key_for(after, "")`.

- [`case_1292`](#function-352) ([unittest.cpp:1292](../unittest.cpp#L1292)): direct C++ call; `key_for(P, sample)`, `key_for(P, "examples/Kodak-4X-2.png")`, `key_for(P, "examples/does-not-exist.png")`.

**CLI invocations / variants:** [C](#scenario-c). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** All applicable nonintermediate parameters and sample contents affect key; nearby doubles distinguished; rates/inactive/intermediate values excluded; seed/build identity and multi-scan order.

**Existing direct C++ test callers:** [`case_1080`](#function-348), [`case_1145`](#function-350), [`case_1235`](#function-351), [`case_1292`](#function-352). These do not establish full branch coverage.

**Integration tests should exist:** [C](#scenario-c) — Two-process repeated builds hit the cache; changed relevant inputs miss; corrupt/wrong-size entries rebuild; failed cache writes preserve successful output. Add concurrent-writer and eviction fixtures.

<a id="function-223"></a>

### `maskcache::load` — line 203

Source: [maskcache.cpp:203](../maskcache.cpp#L203). Signature: `load(const std::string& dir, const Key& k, std::ostream* log)`.

**Callers and how they call it**

- [`obtain_mask`](#function-213) ([mask.cpp:377](../mask.cpp#L377)): direct C++ call; receiver/signature resolved by compiler.

**CLI invocations / variants:** [C](#scenario-c). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Missing, truncated, malformed metadata, wrong key/type and valid entry; no exception escapes for unusable cache; log behavior.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [C](#scenario-c) — Two-process repeated builds hit the cache; changed relevant inputs miss; corrupt/wrong-size entries rebuild; failed cache writes preserve successful output. Add concurrent-writer and eviction fixtures.

<a id="function-224"></a>

### `maskcache::write_entry` — line 226

Source: [maskcache.cpp:226](../maskcache.cpp#L226). Signature: `write_entry(const fs::path& dir, const Key& k, const cv::Mat& mask)`.

**Callers and how they call it**

- [`maskcache::store`](#function-226) ([maskcache.cpp:303](../maskcache.cpp#L303)): direct C++ call; `write_entry(d, k, mask)`.

**CLI invocations / variants:** [C](#scenario-c). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Successful mask/metadata pair; partial and unwritable writes; metadata faithfully preserves key; no valid-looking incomplete entry.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [C](#scenario-c) — Two-process repeated builds hit the cache; changed relevant inputs miss; corrupt/wrong-size entries rebuild; failed cache writes preserve successful output. Add concurrent-writer and eviction fixtures.

<a id="function-225"></a>

### `maskcache::trim_cache` — line 258

Source: [maskcache.cpp:258](../maskcache.cpp#L258). Signature: `trim_cache(const fs::path& dir, const fs::path& just_written)`.

**Callers and how they call it**

- [`maskcache::store`](#function-226) ([maskcache.cpp:303](../maskcache.cpp#L303)): direct C++ call; `trim_cache(d, d / (k.name + ".png"))`.

**CLI invocations / variants:** [C](#scenario-c). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Byte-cap boundary (below/at/above); oldest entries evicted, just-written entry retained; orphan/foreign files, ties and filesystem errors.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [C](#scenario-c) — Two-process repeated builds hit the cache; changed relevant inputs miss; corrupt/wrong-size entries rebuild; failed cache writes preserve successful output. Add concurrent-writer and eviction fixtures.

<a id="function-226"></a>

### `maskcache::store` — line 303

Source: [maskcache.cpp:303](../maskcache.cpp#L303). Signature: `store(const std::string& dir, const Key& k, const cv::Mat& mask)`.

**Callers and how they call it**

- [`obtain_mask`](#function-213) ([mask.cpp:377](../mask.cpp#L377)): direct C++ call; receiver/signature resolved by compiler.

**CLI invocations / variants:** [C](#scenario-c). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Disabled/empty key/mask no-op; directory creation, failed writes and successful trim; concurrent writers must not expose corrupt hits.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [C](#scenario-c) — Two-process repeated builds hit the cache; changed relevant inputs miss; corrupt/wrong-size entries rebuild; failed cache writes preserve successful output. Add concurrent-writer and eviction fixtures.

<a id="file-34"></a>

## `maskcache.h`

### Declaration: `dir_for(const std::string& configPath)`

Source: [maskcache.h:65](../maskcache.h#L65).

Callers, CLI variants, unit tests and integration tests: [`maskcache::dir_for`](#function-221). This is the declaration of that same function; do not count it as separate executed coverage.

### Declaration: `key_for(params::Params& P, const std::string& samplePath)`

Source: [maskcache.h:71](../maskcache.h#L71).

Callers, CLI variants, unit tests and integration tests: [`maskcache::key_for`](#function-222). This is the declaration of that same function; do not count it as separate executed coverage.

### Declaration: `load(const std::string& dir, const Key& k, std::ostream* log)`

Source: [maskcache.h:74](../maskcache.h#L74).

Callers, CLI variants, unit tests and integration tests: [`maskcache::load`](#function-223). This is the declaration of that same function; do not count it as separate executed coverage.

### Declaration: `store(const std::string& dir, const Key& k, const cv::Mat& mask)`

Source: [maskcache.h:80](../maskcache.h#L80).

Callers, CLI variants, unit tests and integration tests: [`maskcache::store`](#function-226). This is the declaration of that same function; do not count it as separate executed coverage.

<a id="file-35"></a>

## `maskparams.h`

### Declaration: `calibratable()`

Source: [maskparams.h:74](../maskparams.h#L74).

Callers, CLI variants, unit tests and integration tests: [`mask::calibratable`](#function-132). This is the declaration of that same function; do not count it as separate executed coverage.

<a id="function-227"></a>

### `mask::building` — line 37

Source: [maskparams.h:37](../maskparams.h#L37). Signature: `building(const params::Resolver& r)`.

**Callers and how they call it**

- [`mask::calibratable`](#function-132) ([generators.cpp:187](../generators.cpp#L187)): call in enclosing function / its lambda callback; `building(r)`.

- [`mask::generator`](#function-229) ([maskparams.h:57](../maskparams.h#L57)): call in enclosing function / its lambda callback; `building(r)`.

- [`mask::from_sample`](#function-230) ([maskparams.h:77](../maskparams.h#L77)): call in enclosing function / its lambda callback; `building(r)`.

- [`mask::any_generator`](#function-231) ([maskparams.h:86](../maskparams.h#L86)): call in enclosing function / its lambda callback; `building(r)`.

**CLI invocations / variants:** [E](#scenario-e) [X](#scenario-x) [B](#scenario-b) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [L](#scenario-l) [CD](#scenario-cd) [CM](#scenario-cm) [CP](#scenario-cp). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Mask threshold with empty mask-in is true; round/random or loaded mask false.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [E](#scenario-e) [X](#scenario-x) [B](#scenario-b) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [L](#scenario-l) [CD](#scenario-cd) [CM](#scenario-cm) [CP](#scenario-cp) — Compare explain and conflicts for building/loading and each method; inactive supplied options must not be silently ignored.

<a id="function-228"></a>

### `mask::thresholding` — line 47

Source: [maskparams.h:47](../maskparams.h#L47). Signature: `thresholding()`.

**Callers and how they call it**

- [`register_mask_params`](#function-209) ([mask.cpp:160](../mask.cpp#L160)): direct C++ call; `thresholding()`.

**CLI invocations / variants:** [E](#scenario-e) [X](#scenario-x) [B](#scenario-b) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [L](#scenario-l) [CD](#scenario-cd) [CM](#scenario-cm) [CP](#scenario-cp). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Returned predicate true only under mask threshold; scope text matches behavior.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [E](#scenario-e) [X](#scenario-x) [B](#scenario-b) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [L](#scenario-l) [CD](#scenario-cd) [CM](#scenario-cm) [CP](#scenario-cp) — Compare explain and conflicts for building/loading and each method; inactive supplied options must not be silently ignored.

#### Anonymous function: `mask::thresholding::callback@49`

Source: [maskparams.h:49](../maskparams.h#L49); capture/signature: `[](const params::Resolver& r)`.

- **Callers/how:** params::Params::applies, stranded and applicability_inputs invoke Applicability::test(Resolver); mask::thresholding constructs the callback.

- **CLI invocations:** [E](#scenario-e) [X](#scenario-x) [B](#scenario-b) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [L](#scenario-l) [CD](#scenario-cd) [CM](#scenario-cm) [CP](#scenario-cp); same enclosing branch, not an independently named CLI operation.

- **Unit tests should exist:** Predicate true/false boundaries, every key it reads and exact scope; no modification of User values. Vary each read dependency independently: `keys::Threshold`.

- **Integration tests should exist:** [E](#scenario-e) [X](#scenario-x) [B](#scenario-b) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [L](#scenario-l) [CD](#scenario-cd) [CM](#scenario-cm) [CP](#scenario-cp); verify the enclosing function's observable assertion above with the callback branch actually reached.

<a id="function-229"></a>

### `mask::generator` — line 57

Source: [maskparams.h:57](../maskparams.h#L57). Signature: `generator(const char* method)`.

**Callers and how they call it**

- [`register_bluenoise_params`](#function-005) ([bluenoise.cpp:162](../bluenoise.cpp#L162)): direct C++ call; `generator("blue")`.

- [`register_direct_params`](#function-091) ([directmask.cpp:220](../directmask.cpp#L220)): direct C++ call; `generator("direct")`.

- [`register_inpaint_params`](#function-154) ([inpaint.cpp:480](../inpaint.cpp#L480)): direct C++ call; `generator("inpaint")`.

- [`register_markov_params`](#function-198) ([markov.cpp:1005](../markov.cpp#L1005)): direct C++ call; `generator("markov")`.

- [`register_spectral_params`](#function-283) ([spectralmask.cpp:243](../spectralmask.cpp#L243)): direct C++ call; `generator("dft")`.

**CLI invocations / variants:** [E](#scenario-e) [X](#scenario-x) [B](#scenario-b) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [L](#scenario-l) [CD](#scenario-cd) [CM](#scenario-cm) [CP](#scenario-cp). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Returned predicate combines build state and exact selected generator; captured method lifetime and scope text.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [E](#scenario-e) [X](#scenario-x) [B](#scenario-b) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [L](#scenario-l) [CD](#scenario-cd) [CM](#scenario-cm) [CP](#scenario-cp) — Compare explain and conflicts for building/loading and each method; inactive supplied options must not be silently ignored.

#### Anonymous function: `mask::generator::callback@60`

Source: [maskparams.h:60](../maskparams.h#L60); capture/signature: `[m](const params::Resolver& r)`.

- **Callers/how:** params::Params::applies, stranded and applicability_inputs invoke Applicability::test(Resolver); mask::generator constructs the callback.

- **CLI invocations:** [E](#scenario-e) [X](#scenario-x) [B](#scenario-b) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [L](#scenario-l) [CD](#scenario-cd) [CM](#scenario-cm) [CP](#scenario-cp); same enclosing branch, not an independently named CLI operation.

- **Unit tests should exist:** Predicate true/false boundaries, every key it reads and exact scope; no modification of User values. Vary each read dependency independently: `keys::Method`.

- **Integration tests should exist:** [E](#scenario-e) [X](#scenario-x) [B](#scenario-b) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [L](#scenario-l) [CD](#scenario-cd) [CM](#scenario-cm) [CP](#scenario-cp); verify the enclosing function's observable assertion above with the callback branch actually reached.

<a id="function-230"></a>

### `mask::from_sample` — line 77

Source: [maskparams.h:77](../maskparams.h#L77). Signature: `from_sample()`.

**Callers and how they call it**

- [`register_mask_params`](#function-209) ([mask.cpp:160](../mask.cpp#L160)): direct C++ call; `from_sample()`.

**CLI invocations / variants:** [E](#scenario-e) [X](#scenario-x) [B](#scenario-b) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [L](#scenario-l) [CD](#scenario-cd) [CM](#scenario-cm) [CP](#scenario-cp). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Sample-related applicability across build/load and method selection; predicate dependencies correctly recorded.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [E](#scenario-e) [X](#scenario-x) [B](#scenario-b) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [L](#scenario-l) [CD](#scenario-cd) [CM](#scenario-cm) [CP](#scenario-cp) — Compare explain and conflicts for building/loading and each method; inactive supplied options must not be silently ignored.

#### Anonymous function: `mask::from_sample::callback@79`

Source: [maskparams.h:79](../maskparams.h#L79); capture/signature: `[](const params::Resolver& r)`.

- **Callers/how:** params::Params::applies, stranded and applicability_inputs invoke Applicability::test(Resolver); mask::from_sample constructs the callback.

- **CLI invocations:** [E](#scenario-e) [X](#scenario-x) [B](#scenario-b) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [L](#scenario-l) [CD](#scenario-cd) [CM](#scenario-cm) [CP](#scenario-cp); same enclosing branch, not an independently named CLI operation.

- **Unit tests should exist:** Predicate true/false boundaries, every key it reads and exact scope; no modification of User values. Vary each read dependency independently: `keys::Method`.

- **Integration tests should exist:** [E](#scenario-e) [X](#scenario-x) [B](#scenario-b) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [L](#scenario-l) [CD](#scenario-cd) [CM](#scenario-cm) [CP](#scenario-cp); verify the enclosing function's observable assertion above with the callback branch actually reached.

<a id="function-231"></a>

### `mask::any_generator` — line 86

Source: [maskparams.h:86](../maskparams.h#L86). Signature: `any_generator()`.

**Callers and how they call it**

- [`register_mask_params`](#function-209) ([mask.cpp:160](../mask.cpp#L160)): direct C++ call; `any_generator()`.

**CLI invocations / variants:** [E](#scenario-e) [X](#scenario-x) [B](#scenario-b) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [L](#scenario-l) [CD](#scenario-cd) [CM](#scenario-cm) [CP](#scenario-cp). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Only building state gates applicability; all five generators accepted, loaded mask excluded.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [E](#scenario-e) [X](#scenario-x) [B](#scenario-b) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [L](#scenario-l) [CD](#scenario-cd) [CM](#scenario-cm) [CP](#scenario-cp) — Compare explain and conflicts for building/loading and each method; inactive supplied options must not be silently ignored.

#### Anonymous function: `mask::any_generator::callback@88`

Source: [maskparams.h:88](../maskparams.h#L88); capture/signature: `[](const params::Resolver& r)`.

- **Callers/how:** params::Params::applies, stranded and applicability_inputs invoke Applicability::test(Resolver); mask::any_generator constructs the callback.

- **CLI invocations:** [E](#scenario-e) [X](#scenario-x) [B](#scenario-b) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [L](#scenario-l) [CD](#scenario-cd) [CM](#scenario-cm) [CP](#scenario-cp); same enclosing branch, not an independently named CLI operation.

- **Unit tests should exist:** Predicate true/false boundaries, every key it reads and exact scope; no modification of User values.

- **Integration tests should exist:** [E](#scenario-e) [X](#scenario-x) [B](#scenario-b) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [L](#scenario-l) [CD](#scenario-cd) [CM](#scenario-cm) [CP](#scenario-cp); verify the enclosing function's observable assertion above with the callback branch actually reached.

<a id="file-36"></a>

## `maskutil.cpp`

<a id="function-232"></a>

### `maskutil::rank_order` — line 13

Source: [maskutil.cpp:13](../maskutil.cpp#L13). Signature: `rank_order(const cv::Mat& f)`.

**Callers and how they call it**

- [`calib::reference_mask`](#function-012) ([calib.cpp:188](../calib.cpp#L188)): direct C++ call; `rank_order(crop)`.

- [`build_inpaint_mask`](#function-153) ([inpaint.cpp:467](../inpaint.cpp#L467)): direct C++ call; `rank_order(field)`.

- [`run`](#function-166) ([maketexture.cpp:239](../maketexture.cpp#L239)): direct C++ call; `rank_order(field)`.

- [`maskutil::tie_share`](#function-234) ([maskutil.cpp:37](../maskutil.cpp#L37)): direct C++ call; `rank_order(f)`.

- [`maskutil::rank_uniform_unit`](#function-236) ([maskutil.cpp:80](../maskutil.cpp#L80)): direct C++ call; `rank_order(f)`.

- [`maskutil::rank_uniform_signed`](#function-237) ([maskutil.cpp:88](../maskutil.cpp#L88)): direct C++ call; `rank_order(f)`.

- [`case_369`](#function-331) ([unittest.cpp:369](../unittest.cpp#L369)): direct C++ call; `rank_order(f)`.

- [`case_1411`](#function-356) ([unittest.cpp:1411](../unittest.cpp#L1411)): direct C++ call; `rank_order(q)`.

- [`flat_mask`](#function-359) ([unittest.cpp:1524](../unittest.cpp#L1524)): direct C++ call; `rank_order(f)`.

**CLI invocations / variants:** [B](#scenario-b) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [CD](#scenario-cd) [CM](#scenario-cm) [CP](#scenario-cp) [T](#scenario-t). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Permutation, ascending values, stable ties; noncontinuous Mat and tiny input contract.

**Existing direct C++ test callers:** [`case_369`](#function-331), [`case_1411`](#function-356). These do not establish full branch coverage.

**Integration tests should exist:** [B](#scenario-b) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [CD](#scenario-cd) [CM](#scenario-cm) [CP](#scenario-cp) [T](#scenario-t) — Read masks/texture outputs and check the relevant rank, wrap, seam or frequency invariant against independent analysis; use generated fixtures instead of visual judgment.

#### Anonymous function: `maskutil::rank_order::callback@20`

Source: [maskutil.cpp:20](../maskutil.cpp#L20); capture/signature: `[v](int a, int b)`.

- **Callers/how:** std::sort/stable_sort invokes the comparator supplied by maskutil::rank_order.

- **CLI invocations:** [B](#scenario-b) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [CD](#scenario-cd) [CM](#scenario-cm) [CP](#scenario-cp) [T](#scenario-t); same enclosing branch, not an independently named CLI operation.

- **Unit tests should exist:** Strict weak ordering, equal values, stable/deterministic ties as required; sorted output is a permutation.

- **Integration tests should exist:** [B](#scenario-b) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [CD](#scenario-cd) [CM](#scenario-cm) [CP](#scenario-cp) [T](#scenario-t); verify the enclosing function's observable assertion above with the callback branch actually reached.

<a id="function-233"></a>

### `maskutil::quantise` — line 24

Source: [maskutil.cpp:24](../maskutil.cpp#L24). Signature: `quantise(cv::Mat& f, double step)`.

**Callers and how they call it**

- [`build_direct_mask`](#function-089) ([directmask.cpp:69](../directmask.cpp#L69)): direct C++ call; `quantise(field, quantStep)`.

- [`case_1360`](#function-354) ([unittest.cpp:1360](../unittest.cpp#L1360)): direct C++ call; `quantise(q, 0.9)`, `quantise(none, 0.0)`.

- [`case_1389`](#function-355) ([unittest.cpp:1389](../unittest.cpp#L1389)): direct C++ call; `quantise(q, 0.9)`.

- [`case_1411`](#function-356) ([unittest.cpp:1411](../unittest.cpp#L1411)): direct C++ call; `quantise(q, 0.9)`.

- [`case_1461`](#function-357) ([unittest.cpp:1461](../unittest.cpp#L1461)): direct C++ call; `quantise(q, 0.9)`.

**CLI invocations / variants:** [Q](#scenario-q). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Disabled/nonpositive step behavior; positive step rounds to intended lattice, including negative half values and constant fields.

**Existing direct C++ test callers:** [`case_1360`](#function-354), [`case_1389`](#function-355), [`case_1411`](#function-356), [`case_1461`](#function-357). These do not establish full branch coverage.

**Integration tests should exist:** [Q](#scenario-q) — Read masks/texture outputs and check the relevant rank, wrap, seam or frequency invariant against independent analysis; use generated fixtures instead of visual judgment.

<a id="function-234"></a>

### `maskutil::tie_share` — line 37

Source: [maskutil.cpp:37](../maskutil.cpp#L37). Signature: `tie_share(const cv::Mat& f)`.

**Callers and how they call it**

- [`build_direct_mask`](#function-089) ([directmask.cpp:69](../directmask.cpp#L69)): direct C++ call; `tie_share(field)`.

- [`case_1389`](#function-355) ([unittest.cpp:1389](../unittest.cpp#L1389)): direct C++ call; `tie_share(allSame)`, `tie_share(allDiff)`, `tie_share(q)`.

**CLI invocations / variants:** [Q](#scenario-q). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** All equal gives 1, all distinct gives 1/N; mixed populations equal the sum of squared group fractions (collision probability); empty input contract.

**Existing direct C++ test callers:** [`case_1389`](#function-355). These do not establish full branch coverage.

**Integration tests should exist:** [Q](#scenario-q) — Read masks/texture outputs and check the relevant rank, wrap, seam or frequency invariant against independent analysis; use generated fixtures instead of visual judgment.

<a id="function-235"></a>

### `maskutil::rank_order_random` — line 56

Source: [maskutil.cpp:56](../maskutil.cpp#L56). Signature: `rank_order_random(const cv::Mat& f, uint64_t seed)`.

**Callers and how they call it**

- [`build_direct_mask`](#function-089) ([directmask.cpp:69](../directmask.cpp#L69)): direct C++ call; `rank_order_random(field, seed)`.

- [`case_1411`](#function-356) ([unittest.cpp:1411](../unittest.cpp#L1411)): direct C++ call; `rank_order_random(q, 7)`.

- [`case_1461`](#function-357) ([unittest.cpp:1461](../unittest.cpp#L1461)): direct C++ call; `rank_order_random(q, 7)`, `rank_order_random(q, 8)`.

**CLI invocations / variants:** [Q](#scenario-q). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Sorted between distinct values, uniform seeded tie shuffle within equal groups; fixed seed and independence from global engine RNG.

**Existing direct C++ test callers:** [`case_1411`](#function-356), [`case_1461`](#function-357). These do not establish full branch coverage.

**Integration tests should exist:** [Q](#scenario-q) — Read masks/texture outputs and check the relevant rank, wrap, seam or frequency invariant against independent analysis; use generated fixtures instead of visual judgment.

#### Anonymous function: `maskutil::rank_order_random::callback@71`

Source: [maskutil.cpp:71](../maskutil.cpp#L71); capture/signature: `[v, &key](int a, int b)`.

- **Callers/how:** std::sort/stable_sort invokes the comparator supplied by maskutil::rank_order_random.

- **CLI invocations:** [Q](#scenario-q); same enclosing branch, not an independently named CLI operation.

- **Unit tests should exist:** Strict weak ordering, equal values, stable/deterministic ties as required; sorted output is a permutation.

- **Integration tests should exist:** [Q](#scenario-q); verify the enclosing function's observable assertion above with the callback branch actually reached.

<a id="function-236"></a>

### `maskutil::rank_uniform_unit` — line 80

Source: [maskutil.cpp:80](../maskutil.cpp#L80). Signature: `rank_uniform_unit(cv::Mat& f, std::vector<int>& order)`.

**Callers and how they call it**

- [`downsample`](#function-139) ([inpaint.cpp:58](../inpaint.cpp#L58)): direct C++ call; `rank_uniform_unit(out, order)`.

- [`sweep_level`](#function-149) ([inpaint.cpp:291](../inpaint.cpp#L291)): direct C++ call; `rank_uniform_unit(field, order)`.

- [`synthesise_inpaint_field`](#function-152) ([inpaint.cpp:363](../inpaint.cpp#L363)): direct C++ call; `rank_uniform_unit(field, order)`.

- [`case_603`](#function-339) ([unittest.cpp:603](../unittest.cpp#L603)): direct C++ call; `rank_uniform_unit(g, order)`.

**CLI invocations / variants:** [B](#scenario-b) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [CD](#scenario-cd) [CM](#scenario-cm) [CP](#scenario-cp) [T](#scenario-t). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Order preserved, exact [0,1) rank mapping and output permutation; ties and one element.

**Existing direct C++ test callers:** [`case_603`](#function-339). These do not establish full branch coverage.

**Integration tests should exist:** [B](#scenario-b) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [CD](#scenario-cd) [CM](#scenario-cm) [CP](#scenario-cp) [T](#scenario-t) — Read masks/texture outputs and check the relevant rank, wrap, seam or frequency invariant against independent analysis; use generated fixtures instead of visual judgment.

<a id="function-237"></a>

### `maskutil::rank_uniform_signed` — line 88

Source: [maskutil.cpp:88](../maskutil.cpp#L88). Signature: `rank_uniform_signed(cv::Mat& f, std::vector<int>& order)`.

**Callers and how they call it**

- [`order_from_amplitude`](#function-278) ([spectralmask.cpp:172](../spectralmask.cpp#L172)): direct C++ call; `rank_uniform_signed(f, order)`.

- [`case_603`](#function-339) ([unittest.cpp:603](../unittest.cpp#L603)): direct C++ call; `rank_uniform_signed(g, order)`.

**CLI invocations / variants:** [B](#scenario-b) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [CD](#scenario-cd) [CM](#scenario-cm) [CP](#scenario-cp) [T](#scenario-t). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Exact [-1,1] convention from implementation, endpoints and one element; stable tied ranks.

**Existing direct C++ test callers:** [`case_603`](#function-339). These do not establish full branch coverage.

**Integration tests should exist:** [B](#scenario-b) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [CD](#scenario-cd) [CM](#scenario-cm) [CP](#scenario-cp) [T](#scenario-t) — Read masks/texture outputs and check the relevant rank, wrap, seam or frequency invariant against independent analysis; use generated fixtures instead of visual judgment.

<a id="function-238"></a>

### `maskutil::threshold_mask` — line 97

Source: [maskutil.cpp:97](../maskutil.cpp#L97). Signature: `threshold_mask(const std::vector<int>& order, cv::Size sz)`.

**Callers and how they call it**

- [`build_blue_noise_mask`](#function-003) ([bluenoise.cpp:77](../bluenoise.cpp#L77)): direct C++ call; `threshold_mask(order, sz)`.

- [`calib::reference_mask`](#function-012) ([calib.cpp:188](../calib.cpp#L188)): direct C++ call; `threshold_mask(maskutil::rank_order(crop), crop.size())`.

- [`build_direct_mask`](#function-089) ([directmask.cpp:69](../directmask.cpp#L69)): direct C++ call; `threshold_mask(order, sz)`.

- [`build_inpaint_mask`](#function-153) ([inpaint.cpp:467](../inpaint.cpp#L467)): direct C++ call; `threshold_mask(maskutil::rank_order(field), sz)`.

- [`sample_markov_mask`](#function-194) ([markov.cpp:976](../markov.cpp#L976)): direct C++ call; `threshold_mask(sample_markov_order(sz, m, p, seed, log), sz)`.

- [`build_markov_mask`](#function-196) ([markov.cpp:990](../markov.cpp#L990)): direct C++ call; `threshold_mask(order, sz)`.

- [`build_spectral_mask`](#function-281) ([spectralmask.cpp:224](../spectralmask.cpp#L224)): direct C++ call; `threshold_mask( order_from_amplitude(sz, amplitude_for(set, sz, scale, highpass, smooth), iters, seed), sz)`.

- [`case_655`](#function-340) ([unittest.cpp:655](../unittest.cpp#L655)): direct C++ call; `threshold_mask(order, sz)`.

- [`flat_mask`](#function-359) ([unittest.cpp:1524](../unittest.cpp#L1524)): direct C++ call; `threshold_mask(maskutil::rank_order(f), sz)`.

**CLI invocations / variants:** [B](#scenario-b) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [CD](#scenario-cd) [CM](#scenario-cm) [CP](#scenario-cp) [T](#scenario-t). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Dimensions/type, every threshold byte equals rank formula, balanced histogram; permutation and size preconditions explicit.

**Existing direct C++ test callers:** [`case_655`](#function-340). These do not establish full branch coverage.

**Integration tests should exist:** [B](#scenario-b) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [CD](#scenario-cd) [CM](#scenario-cm) [CP](#scenario-cp) [T](#scenario-t) — Read masks/texture outputs and check the relevant rank, wrap, seam or frequency invariant against independent analysis; use generated fixtures instead of visual judgment.

<a id="function-239"></a>

### `maskutil::boundary_jump` — line 114

Source: [maskutil.cpp:114](../maskutil.cpp#L114). Signature: `boundary_jump(const cv::Mat& u)`.

**Callers and how they call it**

- [`maskutil::periodic_component`](#function-240) ([maskutil.cpp:132](../maskutil.cpp#L132)): direct C++ call; `boundary_jump(u)`.

**CLI invocations / variants:** [B](#scenario-b) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [CD](#scenario-cd) [CM](#scenario-cm) [CP](#scenario-cp) [T](#scenario-t). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Independent boundary discrepancies; constant/periodic field gives zero; corners accumulate both axis contributions.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [B](#scenario-b) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [CD](#scenario-cd) [CM](#scenario-cm) [CP](#scenario-cp) [T](#scenario-t) — Read masks/texture outputs and check the relevant rank, wrap, seam or frequency invariant against independent analysis; use generated fixtures instead of visual judgment.

<a id="function-240"></a>

### `maskutil::periodic_component` — line 132

Source: [maskutil.cpp:132](../maskutil.cpp#L132). Signature: `periodic_component(const cv::Mat& u)`.

**Callers and how they call it**

- [`case_451`](#function-335) ([unittest.cpp:451](../unittest.cpp#L451)): direct C++ call; `periodic_component(u)`.

- [`case_483`](#function-336) ([unittest.cpp:483](../unittest.cpp#L483)): direct C++ call; `periodic_component(u)`.

- [`case_522`](#function-337) ([unittest.cpp:522](../unittest.cpp#L522)): direct C++ call; `periodic_component(u)`.

**CLI invocations / variants:** [B](#scenario-b) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [CD](#scenario-cd) [CM](#scenario-cm) [CP](#scenario-cp) [T](#scenario-t). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Independent Poisson solution on small image; mean preserved, smooth correction harmonic internally, seams removed; rectangular/constant fields.

**Existing direct C++ test callers:** [`case_451`](#function-335), [`case_483`](#function-336), [`case_522`](#function-337). These do not establish full branch coverage.

**Integration tests should exist:** [B](#scenario-b) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [CD](#scenario-cd) [CM](#scenario-cm) [CP](#scenario-cp) [T](#scenario-t) — Read masks/texture outputs and check the relevant rank, wrap, seam or frequency invariant against independent analysis; use generated fixtures instead of visual judgment.

<a id="function-241"></a>

### `maskutil::mincut_path` — line 159

Source: [maskutil.cpp:159](../maskutil.cpp#L159). Signature: `mincut_path(const cv::Mat& cost)`.

**Callers and how they call it**

- [`maskutil::quilt_axis`](#function-242) ([maskutil.cpp:213](../maskutil.cpp#L213)): direct C++ call; `mincut_path(cost)`.

- [`case_857`](#function-343) ([unittest.cpp:857](../unittest.cpp#L857)): direct C++ call; `mincut_path(cost)`, `mincut_path(rnd)`, `mincut_path(thin)`.

**CLI invocations / variants:** [Q](#scenario-q). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Brute-force minimum on tiny grids; connected path and wrap closure; unique optimum, ties, one-column/row and infinity costs.

**Existing direct C++ test callers:** [`case_857`](#function-343). These do not establish full branch coverage.

**Integration tests should exist:** [Q](#scenario-q) — Read masks/texture outputs and check the relevant rank, wrap, seam or frequency invariant against independent analysis; use generated fixtures instead of visual judgment.

<a id="function-242"></a>

### `maskutil::quilt_axis` — line 213

Source: [maskutil.cpp:213](../maskutil.cpp#L213). Signature: `quilt_axis(const cv::Mat& c, int keep, int overlap, int axis)`.

**Callers and how they call it**

- [`build_direct_mask`](#function-089) ([directmask.cpp:69](../directmask.cpp#L69)): direct C++ call; `quilt_axis(crop, sz.width, ov, 1)`, `quilt_axis(field, sz.height, ov, 0)`.

- [`case_902`](#function-344) ([unittest.cpp:902](../unittest.cpp#L902)): direct C++ call; `quilt_axis(wide, keep, ov, 1)`, `quilt_axis(wide.t(), keep, ov, 0)`, `quilt_axis(wide(cv::Rect(0, 0, keep, L)).clone(), keep, 0, 1)`.

**CLI invocations / variants:** [Q](#scenario-q). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Output dimensions; every pixel copied from a source candidate without averaging; both axes, closed seam and overlap bounds.

**Existing direct C++ test callers:** [`case_902`](#function-344). These do not establish full branch coverage.

**Integration tests should exist:** [Q](#scenario-q) — Read masks/texture outputs and check the relevant rank, wrap, seam or frequency invariant against independent analysis; use generated fixtures instead of visual judgment.

<a id="function-243"></a>

### `maskutil::structure_tensor` — line 248

Source: [maskutil.cpp:248](../maskutil.cpp#L248). Signature: `structure_tensor(const cv::Mat& f)`.

**Callers and how they call it**

- [`set_coherence`](#function-141) ([inpaint.cpp:92](../inpaint.cpp#L92)): direct C++ call; `structure_tensor(f)`.

- [`maskutil::field_coherence`](#function-251) ([maskutil.h:200](../maskutil.h#L200)): direct C++ call; `structure_tensor(f)`.

**CLI invocations / variants:** [B](#scenario-b) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [CD](#scenario-cd) [CM](#scenario-cm) [CP](#scenario-cp) [T](#scenario-t). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Analytic constant/linear/sinusoidal fields; periodic central differences and rotation; sum tensor components against slow reference.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [B](#scenario-b) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [CD](#scenario-cd) [CM](#scenario-cm) [CP](#scenario-cp) [T](#scenario-t) — Read masks/texture outputs and check the relevant rank, wrap, seam or frequency invariant against independent analysis; use generated fixtures instead of visual judgment.

<a id="function-244"></a>

### `maskutil::coherence_of` — line 268

Source: [maskutil.cpp:268](../maskutil.cpp#L268). Signature: `coherence_of(const Tensor& t)`.

**Callers and how they call it**

- [`set_coherence`](#function-141) ([inpaint.cpp:92](../inpaint.cpp#L92)): direct C++ call; `coherence_of(t)`.

- [`maskutil::field_coherence`](#function-251) ([maskutil.h:200](../maskutil.h#L200)): direct C++ call; `coherence_of(structure_tensor(f))`.

**CLI invocations / variants:** [B](#scenario-b) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [CD](#scenario-cd) [CM](#scenario-cm) [CP](#scenario-cp) [T](#scenario-t). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Zero tensor returns defined value; isotropic gives zero, rank-one gives one; scaling invariance and roundoff bounds.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [B](#scenario-b) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [CD](#scenario-cd) [CM](#scenario-cm) [CP](#scenario-cp) [T](#scenario-t) — Read masks/texture outputs and check the relevant rank, wrap, seam or frequency invariant against independent analysis; use generated fixtures instead of visual judgment.

<a id="function-245"></a>

### `maskutil::highpass_toroidal` — line 274

Source: [maskutil.cpp:274](../maskutil.cpp#L274). Signature: `highpass_toroidal(cv::Mat& field, double px)`.

**Callers and how they call it**

- [`highpass_field`](#function-183) ([markov.cpp:484](../markov.cpp#L484)): direct C++ call; `highpass_toroidal(f, px)`.

- [`case_715`](#function-341) ([unittest.cpp:715](../unittest.cpp#L715)): direct C++ call; `highpass_toroidal(wrapped, 32.0)`.

- [`case_792`](#function-342) ([unittest.cpp:792](../unittest.cpp#L792)): direct C++ call; `highpass_toroidal(got, px)`, `highpass_toroidal(untouched, 1.0)`.

- [`manufactured_detail_field`](#function-353) ([unittest.cpp:1345](../unittest.cpp#L1345)): direct C++ call; `highpass_toroidal(two, 32.0)`.

**CLI invocations / variants:** [B](#scenario-b) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [CD](#scenario-cd) [CM](#scenario-cm) [CP](#scenario-cp) [T](#scenario-t). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Compare explicit wrapped convolution; no-op cutoff, constant field, tiny/rectangular image and broad kernel.

**Existing direct C++ test callers:** [`case_715`](#function-341), [`case_792`](#function-342). These do not establish full branch coverage.

**Integration tests should exist:** [B](#scenario-b) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [CD](#scenario-cd) [CM](#scenario-cm) [CP](#scenario-cp) [T](#scenario-t) — Read masks/texture outputs and check the relevant rank, wrap, seam or frequency invariant against independent analysis; use generated fixtures instead of visual judgment.

<a id="function-246"></a>

### `maskutil::prepare_texture_sample` — line 294

Source: [maskutil.cpp:294](../maskutil.cpp#L294). Signature: `prepare_texture_sample(const cv::Mat& sample, double highpass, double scale)`.

**Callers and how they call it**

- [`calib::reference_mask`](#function-012) ([calib.cpp:188](../calib.cpp#L188)): direct C++ call; `prepare_texture_sample(sample, highpass, scale)`.

- [`build_direct_mask`](#function-089) ([directmask.cpp:69](../directmask.cpp#L69)): direct C++ call; `prepare_texture_sample(sample, highpass, scale)`.

- [`run`](#function-166) ([maketexture.cpp:239](../maketexture.cpp#L239)): direct C++ call; `prepare_texture_sample(patch, highpass, scale)`.

- [`samples::Set::prepared`](#function-261) ([samples.cpp:149](../samples.cpp#L149)): direct C++ call; `prepare_texture_sample(f, highpass, scale)`.

- [`case_715`](#function-341) ([unittest.cpp:715](../unittest.cpp#L715)): direct C++ call; `prepare_texture_sample(ramp, 32.0, 1.0)`, `prepare_texture_sample(ramp, 0.0, 1.0)`, `prepare_texture_sample(ramp, 0.0, 0.5)`.

**CLI invocations / variants:** [B](#scenario-b) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [CD](#scenario-cd) [CM](#scenario-cm) [CP](#scenario-cp) [T](#scenario-t). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Type/mean behavior, highpass off/on, reflective borders and scale up/down; no seam introduced by wrapping an ordinary photograph.

**Existing direct C++ test callers:** [`case_715`](#function-341). These do not establish full branch coverage.

**Integration tests should exist:** [B](#scenario-b) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [CD](#scenario-cd) [CM](#scenario-cm) [CP](#scenario-cp) [T](#scenario-t) — Read masks/texture outputs and check the relevant rank, wrap, seam or frequency invariant against independent analysis; use generated fixtures instead of visual judgment.

<a id="file-37"></a>

## `maskutil.h`

### Declaration: `rank_order(const cv::Mat& f)`

Source: [maskutil.h:63](../maskutil.h#L63).

Callers, CLI variants, unit tests and integration tests: [`maskutil::rank_order`](#function-232). This is the declaration of that same function; do not count it as separate executed coverage.

### Declaration: `quantise(cv::Mat& f, double step)`

Source: [maskutil.h:73](../maskutil.h#L73).

Callers, CLI variants, unit tests and integration tests: [`maskutil::quantise`](#function-233). This is the declaration of that same function; do not count it as separate executed coverage.

### Declaration: `tie_share(const cv::Mat& f)`

Source: [maskutil.h:85](../maskutil.h#L85).

Callers, CLI variants, unit tests and integration tests: [`maskutil::tie_share`](#function-234). This is the declaration of that same function; do not count it as separate executed coverage.

### Declaration: `rank_order_random(const cv::Mat& f, uint64_t seed)`

Source: [maskutil.h:95](../maskutil.h#L95).

Callers, CLI variants, unit tests and integration tests: [`maskutil::rank_order_random`](#function-235). This is the declaration of that same function; do not count it as separate executed coverage.

### Declaration: `rank_uniform_unit(cv::Mat& f, std::vector<int>& order)`

Source: [maskutil.h:99](../maskutil.h#L99).

Callers, CLI variants, unit tests and integration tests: [`maskutil::rank_uniform_unit`](#function-236). This is the declaration of that same function; do not count it as separate executed coverage.

### Declaration: `rank_uniform_signed(cv::Mat& f, std::vector<int>& order)`

Source: [maskutil.h:102](../maskutil.h#L102).

Callers, CLI variants, unit tests and integration tests: [`maskutil::rank_uniform_signed`](#function-237). This is the declaration of that same function; do not count it as separate executed coverage.

### Declaration: `threshold_mask(const std::vector<int>& order, cv::Size sz)`

Source: [maskutil.h:105](../maskutil.h#L105).

Callers, CLI variants, unit tests and integration tests: [`maskutil::threshold_mask`](#function-238). This is the declaration of that same function; do not count it as separate executed coverage.

### Declaration: `prepare_texture_sample(const cv::Mat& sample, double highpass, double scale)`

Source: [maskutil.h:112](../maskutil.h#L112).

Callers, CLI variants, unit tests and integration tests: [`maskutil::prepare_texture_sample`](#function-246). This is the declaration of that same function; do not count it as separate executed coverage.

### Declaration: `periodic_component(const cv::Mat& u)`

Source: [maskutil.h:129](../maskutil.h#L129).

Callers, CLI variants, unit tests and integration tests: [`maskutil::periodic_component`](#function-240). This is the declaration of that same function; do not count it as separate executed coverage.

### Declaration: `mincut_path(const cv::Mat& cost)`

Source: [maskutil.h:145](../maskutil.h#L145).

Callers, CLI variants, unit tests and integration tests: [`maskutil::mincut_path`](#function-241). This is the declaration of that same function; do not count it as separate executed coverage.

### Declaration: `quilt_axis(const cv::Mat& c, int keep, int overlap, int axis)`

Source: [maskutil.h:167](../maskutil.h#L167).

Callers, CLI variants, unit tests and integration tests: [`maskutil::quilt_axis`](#function-242). This is the declaration of that same function; do not count it as separate executed coverage.

### Declaration: `highpass_toroidal(cv::Mat& field, double px)`

Source: [maskutil.h:180](../maskutil.h#L180).

Callers, CLI variants, unit tests and integration tests: [`maskutil::highpass_toroidal`](#function-245). This is the declaration of that same function; do not count it as separate executed coverage.

### Declaration: `structure_tensor(const cv::Mat& f)`

Source: [maskutil.h:198](../maskutil.h#L198).

Callers, CLI variants, unit tests and integration tests: [`maskutil::structure_tensor`](#function-243). This is the declaration of that same function; do not count it as separate executed coverage.

### Declaration: `coherence_of(const Tensor& t)`

Source: [maskutil.h:199](../maskutil.h#L199).

Callers, CLI variants, unit tests and integration tests: [`maskutil::coherence_of`](#function-244). This is the declaration of that same function; do not count it as separate executed coverage.

<a id="function-247"></a>

### `maskutil::bin_freq` — line 34

Source: [maskutil.h:34](../maskutil.h#L34). Signature: `bin_freq(int i, int n)`.

**Callers and how they call it**

- [`calib::level_set_low`](#function-006) ([calib.cpp:24](../calib.cpp#L24)): direct C++ call; `bin_freq(y, H)`, `bin_freq(x, W)`.

- [`calib::spectrum_profile`](#function-010) ([calib.cpp:130](../calib.cpp#L130)): direct C++ call; `bin_freq(y, H)`, `bin_freq(x, W)`.

- [`amplitude_grid`](#function-276) ([spectralmask.cpp:115](../spectralmask.cpp#L115)): direct C++ call; `bin_freq(y, sz.height)`, `bin_freq(x, sz.width)`.

- [`case_568`](#function-338) ([unittest.cpp:568](../unittest.cpp#L568)): direct C++ call; `bin_freq(0, n)`, `bin_freq(n / 2, n)`, `bin_freq(i, n)`, `bin_freq(n - i, n)`.

- [`case_792`](#function-342) ([unittest.cpp:792](../unittest.cpp#L792)): direct C++ call; `bin_freq(y, n)`, `bin_freq(x, n)`.

**CLI invocations / variants:** [B](#scenario-b) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [CD](#scenario-cd) [CM](#scenario-cm) [CP](#scenario-cp) [T](#scenario-t). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** DC, Nyquist, negative half, odd/even lengths; frequency endpoints match DFT indexing.

**Existing direct C++ test callers:** [`case_568`](#function-338), [`case_792`](#function-342). These do not establish full branch coverage.

**Integration tests should exist:** [B](#scenario-b) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [CD](#scenario-cd) [CM](#scenario-cm) [CP](#scenario-cp) [T](#scenario-t) — Use rectangular mask builds and synthetic periodic fields; verify rank probabilities, seam wrapping and expected frequency-bin positions.

<a id="function-248"></a>

### `maskutil::wrap_near` — line 42

Source: [maskutil.h:42](../maskutil.h#L42). Signature: `wrap_near(int v, int n)`.

**Callers and how they call it**

- [`gather_queries`](#function-147) ([inpaint.cpp:252](../inpaint.cpp#L252)): direct C++ call; `wrap_near(y + ring[j].y, H)`, `wrap_near(x + ring[j].x, W)`.

- [`gibbs_field`](#function-189) ([markov.cpp:731](../markov.cpp#L731)): direct C++ call; `wrap_near(y + e.d1.dy, H)`, `wrap_near(x + e.d1.dx, W)`, `wrap_near(y + e.d2.dy, H)`, `wrap_near(x + e.d2.dx, W)`.

- [`case_396`](#function-332) ([unittest.cpp:396](../unittest.cpp#L396)): direct C++ call; `wrap_near(v, n)`.

**CLI invocations / variants:** [B](#scenario-b) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [CD](#scenario-cd) [CM](#scenario-cm) [CP](#scenario-cp) [T](#scenario-t). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** One-period domain including -1, 0, n-1, n and supported extremes; agree with modulo only within declared domain.

**Existing direct C++ test callers:** [`case_396`](#function-332). These do not establish full branch coverage.

**Integration tests should exist:** [B](#scenario-b) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [CD](#scenario-cd) [CM](#scenario-cm) [CP](#scenario-cp) [T](#scenario-t) — Use rectangular mask builds and synthetic periodic fields; verify rank probabilities, seam wrapping and expected frequency-bin positions.

<a id="function-249"></a>

### `maskutil::wrap_any` — line 46

Source: [maskutil.h:46](../maskutil.h#L46). Signature: `wrap_any(int v, int n)`.

**Callers and how they call it**

- [`addKernelWrapped`](#function-001) ([bluenoise.cpp:44](../bluenoise.cpp#L44)): direct C++ call; `wrap_any(y + t.dy, H)`, `wrap_any(x + t.dx, W)`.

- [`build_blue_noise_mask`](#function-003) ([bluenoise.cpp:77](../bluenoise.cpp#L77)): direct C++ call; `wrap_any(by + t.dy, H)`, `wrap_any(bx + t.dx, W)`.

- [`wrapped`](#function-274) ([spectralmask.cpp:95](../spectralmask.cpp#L95)): direct C++ call; `wrap_any(y, m.rows)`, `wrap_any(x, m.cols)`.

- [`case_396`](#function-332) ([unittest.cpp:396](../unittest.cpp#L396)): direct C++ call; `wrap_any(v, n)`.

**CLI invocations / variants:** [B](#scenario-b) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [CD](#scenario-cd) [CM](#scenario-cm) [CP](#scenario-cp) [T](#scenario-t). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Large positive/negative coordinates, exact multiples and n=1; require positive modulus.

**Existing direct C++ test callers:** [`case_396`](#function-332). These do not establish full branch coverage.

**Integration tests should exist:** [B](#scenario-b) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [CD](#scenario-cd) [CM](#scenario-cm) [CP](#scenario-cp) [T](#scenario-t) — Use rectangular mask builds and synthetic periodic fields; verify rank probabilities, seam wrapping and expected frequency-bin positions.

<a id="function-250"></a>

### `maskutil::threshold_byte` — line 50

Source: [maskutil.h:50](../maskutil.h#L50). Signature: `threshold_byte(long long r, long long n)`.

**Callers and how they call it**

- [`maskutil::threshold_mask`](#function-238) ([maskutil.cpp:97](../maskutil.cpp#L97)): direct C++ call; `threshold_byte(r, n)`.

- [`case_344`](#function-330) ([unittest.cpp:344](../unittest.cpp#L344)): direct C++ call; `threshold_byte(0, n)`, `threshold_byte(n - 1, n)`, `threshold_byte(r, n)`, `threshold_byte(r - 1, n)`.

**CLI invocations / variants:** [B](#scenario-b) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [CD](#scenario-cd) [CM](#scenario-cm) [CP](#scenario-cp) [T](#scenario-t). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Rank endpoints 0 and n-1; n=1 behavior; large n avoids multiplication overflow; rank probability invariant.

**Existing direct C++ test callers:** [`case_344`](#function-330). These do not establish full branch coverage.

**Integration tests should exist:** [B](#scenario-b) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [CD](#scenario-cd) [CM](#scenario-cm) [CP](#scenario-cp) [T](#scenario-t) — Use rectangular mask builds and synthetic periodic fields; verify rank probabilities, seam wrapping and expected frequency-bin positions.

<a id="function-251"></a>

### `maskutil::field_coherence` — line 200

Source: [maskutil.h:200](../maskutil.h#L200). Signature: `field_coherence(const cv::Mat& f)`.

**Callers and how they call it**

- [`sweep_level`](#function-149) ([inpaint.cpp:291](../inpaint.cpp#L291)): direct C++ call; `field_coherence(field)`.

**CLI invocations / variants:** [B](#scenario-b) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [CD](#scenario-cd) [CM](#scenario-cm) [CP](#scenario-cp) [T](#scenario-t). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Exactly coherence_of(structure_tensor(field)); constant, stripes and isotropic fields.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [B](#scenario-b) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [CD](#scenario-cd) [CM](#scenario-cm) [CP](#scenario-cp) [T](#scenario-t) — Use rectangular mask builds and synthetic periodic fields; verify rank probabilities, seam wrapping and expected frequency-bin positions.

<a id="file-38"></a>

## `options.h`

This file declares data/types/constants only; no authored functions to exercise independently. Test its values through the consumers listed elsewhere.

<a id="file-39"></a>

## `samples.cpp`

<a id="function-252"></a>

### `samples::is_image` — line 23

Source: [samples.cpp:23](../samples.cpp#L23). Signature: `is_image(const fs::path& p)`.

**Callers and how they call it**

- [`samples::images_in`](#function-253) ([samples.cpp:37](../samples.cpp#L37)): direct C++ call; `is_image(it->path())`.

- [`samples::library_names`](#function-256) ([samples.cpp:85](../samples.cpp#L85)): direct C++ call; `is_image(it->path())`.

**CLI invocations / variants:** [S](#scenario-s) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [CD](#scenario-cd) [CM](#scenario-cm) [CP](#scenario-cp) [T](#scenario-t). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Every allowed extension, case variants, unsupported suffix, directory and misleading filename.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [S](#scenario-s) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [CD](#scenario-cd) [CM](#scenario-cm) [CP](#scenario-cp) [T](#scenario-t) — Use isolated library with differently sized/exposed scans, ambiguous literal names and unreadable entries; verify stable ordering, primary choice and no scan joins.

<a id="function-253"></a>

### `samples::images_in` — line 37

Source: [samples.cpp:37](../samples.cpp#L37). Signature: `images_in(const fs::path& dir)`.

**Callers and how they call it**

- [`samples::resolve`](#function-255) ([samples.cpp:62](../samples.cpp#L62)): call in enclosing function / its lambda callback; `images_in(p)`.

- [`samples::library_names`](#function-256) ([samples.cpp:85](../samples.cpp#L85)): direct C++ call; `images_in(it->path())`.

**CLI invocations / variants:** [S](#scenario-s) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [CD](#scenario-cd) [CM](#scenario-cm) [CP](#scenario-cp) [T](#scenario-t). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Sorted image paths, skip nonimages/subdirectories, empty/missing/unreadable directory and literal special characters.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [S](#scenario-s) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [CD](#scenario-cd) [CM](#scenario-cm) [CP](#scenario-cp) [T](#scenario-t) — Use isolated library with differently sized/exposed scans, ambiguous literal names and unreadable entries; verify stable ordering, primary choice and no scan joins.

<a id="function-254"></a>

### `samples::library_root` — line 53

Source: [samples.cpp:53](../samples.cpp#L53). Signature: `library_root()`.

**Callers and how they call it**

- [`build_with_method`](#function-211) ([mask.cpp:296](../mask.cpp#L296)): direct C++ call; `library_root()`.

- [`samples::resolve`](#function-255) ([samples.cpp:62](../samples.cpp#L62)): direct C++ call; `library_root()`.

- [`samples::library_names`](#function-256) ([samples.cpp:85](../samples.cpp#L85)): direct C++ call; `library_root()`.

**CLI invocations / variants:** [H](#scenario-h) [S](#scenario-s) [X](#scenario-x). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** XDG_CONFIG_HOME then HOME fallback; neither/empty set; path separators and spaces.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [H](#scenario-h) [S](#scenario-s) [X](#scenario-x) — Use isolated library with differently sized/exposed scans, ambiguous literal names and unreadable entries; verify stable ordering, primary choice and no scan joins.

<a id="function-255"></a>

### `samples::resolve` — line 62

Source: [samples.cpp:62](../samples.cpp#L62). Signature: `resolve(const std::string& spec)`.

**Callers and how they call it**

- [`calibrate_dft_for`](#function-088) ([dftcalib.cpp:95](../dftcalib.cpp#L95)): direct C++ call; receiver/signature resolved by compiler.

- [`dither_main`](#function-096) ([dither.cpp:105](../dither.cpp#L105)): direct C++ call; receiver/signature resolved by compiler.

- [`calibrate_inpaint_for`](#function-156) ([inpaintcalib.cpp:107](../inpaintcalib.cpp#L107)): direct C++ call; receiver/signature resolved by compiler.

- [`calibrate_markov_for`](#function-204) ([markovcalib.cpp:403](../markovcalib.cpp#L403)): direct C++ call; receiver/signature resolved by compiler.

- [`maskcache::key_for`](#function-222) ([maskcache.cpp:116](../maskcache.cpp#L116)): direct C++ call; receiver/signature resolved by compiler.

**CLI invocations / variants:** [S](#scenario-s) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [CD](#scenario-cd) [CM](#scenario-cm) [CP](#scenario-cp). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Literal file/directory wins over library name; sorted multi-scan results; missing name, empty directory and special-character paths.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [S](#scenario-s) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [CD](#scenario-cd) [CM](#scenario-cm) [CP](#scenario-cp) — Use isolated library with differently sized/exposed scans, ambiguous literal names and unreadable entries; verify stable ordering, primary choice and no scan joins.

#### Anonymous function: `samples::resolve::expand@67`

Source: [samples.cpp:67](../samples.cpp#L67); capture/signature: `[&](const fs::path& p) -> std::vector<std::string>`.

- **Callers/how:** samples::resolve invokes this local callback directly or through the algorithm receiving it; inspect linked source for the call site.

- **CLI invocations:** [S](#scenario-s) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [CD](#scenario-cd) [CM](#scenario-cm) [CP](#scenario-cp); same enclosing branch, not an independently named CLI operation.

- **Unit tests should exist:** Exercise each branch of this callback through the enclosing function tests; verify captured-state updates and boundary values against the enclosing function oracle.

- **Integration tests should exist:** [S](#scenario-s) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [CD](#scenario-cd) [CM](#scenario-cm) [CP](#scenario-cp); verify the enclosing function's observable assertion above with the callback branch actually reached.

<a id="function-256"></a>

### `samples::library_names` — line 85

Source: [samples.cpp:85](../samples.cpp#L85). Signature: `library_names()`.

**Callers and how they call it**

- [`parseOptions`](#function-027) ([cli.cpp:178](../cli.cpp#L178)): direct C++ call; `library_names()`.

- [`build_with_method`](#function-211) ([mask.cpp:296](../mask.cpp#L296)): direct C++ call; `library_names()`.

**CLI invocations / variants:** [H](#scenario-h) [S](#scenario-s) [X](#scenario-x). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Only valid offered library entries, deterministic names and absent/unreadable root.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [H](#scenario-h) [S](#scenario-s) [X](#scenario-x) — Use isolated library with differently sized/exposed scans, ambiguous literal names and unreadable entries; verify stable ordering, primary choice and no scan joins.

<a id="function-257"></a>

### `samples::Set::read` — line 104

Source: [samples.cpp:104](../samples.cpp#L104). Signature: `Set::read(const std::vector<std::string>& paths, std::ostream* log)`.

**Callers and how they call it**

- [`calibrate_dft_for`](#function-088) ([dftcalib.cpp:95](../dftcalib.cpp#L95)): direct C++ call; receiver/signature resolved by compiler.

- [`dither_main`](#function-096) ([dither.cpp:105](../dither.cpp#L105)): direct C++ call; receiver/signature resolved by compiler.

- [`calibrate_inpaint_for`](#function-156) ([inpaintcalib.cpp:107](../inpaintcalib.cpp#L107)): direct C++ call; receiver/signature resolved by compiler.

- [`calibrate_markov_for`](#function-204) ([markovcalib.cpp:403](../markovcalib.cpp#L403)): direct C++ call; receiver/signature resolved by compiler.

**CLI invocations / variants:** [S](#scenario-s) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [CD](#scenario-cd) [CM](#scenario-cm) [CP](#scenario-cp) [T](#scenario-t). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** 8/16-bit and color files, mixed readable/unreadable paths, empty list; log versus nullptr; preserve scan order.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [S](#scenario-s) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [CD](#scenario-cd) [CM](#scenario-cm) [CP](#scenario-cp) [T](#scenario-t) — Use isolated library with differently sized/exposed scans, ambiguous literal names and unreadable entries; verify stable ordering, primary choice and no scan joins.

<a id="function-258"></a>

### `samples::Set::of` — line 124

Source: [samples.cpp:124](../samples.cpp#L124). Signature: `Set::of(const cv::Mat& one)`.

**Callers and how they call it**

- [`synthesise_inpaint_field`](#function-151) ([inpaint.cpp:356](../inpaint.cpp#L356)): direct C++ call; receiver/signature resolved by compiler.

- [`synthesise_markov_order`](#function-195) ([markov.cpp:982](../markov.cpp#L982)): direct C++ call; receiver/signature resolved by compiler.

**CLI invocations / variants:** [S](#scenario-s) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [CD](#scenario-cd) [CM](#scenario-cm) [CP](#scenario-cp) [T](#scenario-t). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Mat and vector overloads: empty input behavior, order/type, ownership and aliasing contract; equivalence for one scan.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [S](#scenario-s) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [CD](#scenario-cd) [CM](#scenario-cm) [CP](#scenario-cp) [T](#scenario-t) — Use isolated library with differently sized/exposed scans, ambiguous literal names and unreadable entries; verify stable ordering, primary choice and no scan joins.

<a id="function-259"></a>

### `samples::Set::of` — line 131

Source: [samples.cpp:131](../samples.cpp#L131). Signature: `Set::of(std::vector<cv::Mat> scans)`.

**Callers and how they call it**

- [`downsample_set`](#function-140) ([inpaint.cpp:79](../inpaint.cpp#L79)): direct C++ call; receiver/signature resolved by compiler.

**CLI invocations / variants:** [S](#scenario-s) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [CD](#scenario-cd) [CM](#scenario-cm) [CP](#scenario-cp) [T](#scenario-t). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Mat and vector overloads: empty input behavior, order/type, ownership and aliasing contract; equivalence for one scan.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [S](#scenario-s) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [CD](#scenario-cd) [CM](#scenario-cm) [CP](#scenario-cp) [T](#scenario-t) — Use isolated library with differently sized/exposed scans, ambiguous literal names and unreadable entries; verify stable ordering, primary choice and no scan joins.

<a id="function-260"></a>

### `samples::Set::primary` — line 138

Source: [samples.cpp:138](../samples.cpp#L138). Signature: `Set::primary() const`.

**Callers and how they call it**

- [`calibrate_dft_for`](#function-088) ([dftcalib.cpp:95](../dftcalib.cpp#L95)): direct C++ call; `primary()`.

- [`dither_main`](#function-096) ([dither.cpp:105](../dither.cpp#L105)): direct C++ call; `primary()`.

- [`direct_build`](#function-129) ([generators.cpp:130](../generators.cpp#L130)): direct C++ call; `primary()`.

- [`calibrate_inpaint_for`](#function-156) ([inpaintcalib.cpp:107](../inpaintcalib.cpp#L107)): direct C++ call; `primary()`.

- [`reference_mask`](#function-199) ([markovcalib.cpp:65](../markovcalib.cpp#L65)): direct C++ call; `primary()`.

- [`describe`](#function-210) ([mask.cpp:287](../mask.cpp#L287)): direct C++ call; `primary()`.

**CLI invocations / variants:** [S](#scenario-s) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [CD](#scenario-cd) [CM](#scenario-cm) [CP](#scenario-cp) [T](#scenario-t). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Largest area, stable first-on-tie, empty set precondition; unequal aspect ratios.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [S](#scenario-s) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [CD](#scenario-cd) [CM](#scenario-cm) [CP](#scenario-cp) [T](#scenario-t) — Use isolated library with differently sized/exposed scans, ambiguous literal names and unreadable entries; verify stable ordering, primary choice and no scan joins.

<a id="function-261"></a>

### `samples::Set::prepared` — line 149

Source: [samples.cpp:149](../samples.cpp#L149). Signature: `Set::prepared(double highpass, double scale) const`.

**Callers and how they call it**

- [`build_inpaint_mask`](#function-153) ([inpaint.cpp:467](../inpaint.cpp#L467)): direct C++ call; `prepared(p.highpass, p.scale)`.

- [`fit_markov_model`](#function-192) ([markov.cpp:851](../markov.cpp#L851)): direct C++ call; `prepared(p.highpass, p.scale)`.

**CLI invocations / variants:** [S](#scenario-s) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [CD](#scenario-cd) [CM](#scenario-cm) [CP](#scenario-cp) [T](#scenario-t). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Separate scans retained; shared-mean normalization with highpass off, contrast retained; highpass and scale applied to each; input not mutated.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [S](#scenario-s) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [CD](#scenario-cd) [CM](#scenario-cm) [CP](#scenario-cp) [T](#scenario-t) — Use isolated library with differently sized/exposed scans, ambiguous literal names and unreadable entries; verify stable ordering, primary choice and no scan joins.

<a id="function-262"></a>

### `samples::Set::Ordering::scan_of` — line 172

Source: [samples.cpp:172](../samples.cpp#L172). Signature: `Set::Ordering::scan_of(std::size_t flat) const`.

**Callers and how they call it**

- [`quantise_set`](#function-168) ([markov.cpp:103](../markov.cpp#L103)): direct C++ call; `scan_of(g)`.

- [`samples::Set::ranked_unit`](#function-264) ([samples.cpp:204](../samples.cpp#L204)): direct C++ call; `scan_of(g)`.

**CLI invocations / variants:** [S](#scenario-s) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [CD](#scenario-cd) [CM](#scenario-cm) [CP](#scenario-cp) [T](#scenario-t). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Every base boundary and last valid flat index; unequal/empty scan sizes; upper_bound behavior.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [S](#scenario-s) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [CD](#scenario-cd) [CM](#scenario-cm) [CP](#scenario-cp) [T](#scenario-t) — Use isolated library with differently sized/exposed scans, ambiguous literal names and unreadable entries; verify stable ordering, primary choice and no scan joins.

<a id="function-263"></a>

### `samples::Set::ordering` — line 177

Source: [samples.cpp:177](../samples.cpp#L177). Signature: `Set::ordering() const`.

**Callers and how they call it**

- [`quantise_set`](#function-168) ([markov.cpp:103](../markov.cpp#L103)): direct C++ call; `ordering()`.

- [`samples::Set::ranked_unit`](#function-264) ([samples.cpp:204](../samples.cpp#L204)): direct C++ call; `ordering()`.

**CLI invocations / variants:** [S](#scenario-s) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [CD](#scenario-cd) [CM](#scenario-cm) [CP](#scenario-cp) [T](#scenario-t). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Flat permutation across all scans, stable ties, correct base offsets; independent per-scan ranking must not substitute for pooled ranking.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [S](#scenario-s) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [CD](#scenario-cd) [CM](#scenario-cm) [CP](#scenario-cp) [T](#scenario-t) — Use isolated library with differently sized/exposed scans, ambiguous literal names and unreadable entries; verify stable ordering, primary choice and no scan joins.

#### Anonymous function: `samples::Set::ordering::callback@200`

Source: [samples.cpp:200](../samples.cpp#L200); capture/signature: `[v](int a, int b)`.

- **Callers/how:** std::sort/stable_sort invokes the comparator supplied by samples::Set::ordering.

- **CLI invocations:** [S](#scenario-s) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [CD](#scenario-cd) [CM](#scenario-cm) [CP](#scenario-cp) [T](#scenario-t); same enclosing branch, not an independently named CLI operation.

- **Unit tests should exist:** Strict weak ordering, equal values, stable/deterministic ties as required; sorted output is a permutation.

- **Integration tests should exist:** [S](#scenario-s) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [CD](#scenario-cd) [CM](#scenario-cm) [CP](#scenario-cp) [T](#scenario-t); verify the enclosing function's observable assertion above with the callback branch actually reached.

<a id="function-264"></a>

### `samples::Set::ranked_unit` — line 204

Source: [samples.cpp:204](../samples.cpp#L204). Signature: `Set::ranked_unit() const`.

**Callers and how they call it**

- [`synthesise_inpaint_field`](#function-152) ([inpaint.cpp:363](../inpaint.cpp#L363)): direct C++ call; `ranked_unit()`.

**CLI invocations / variants:** [S](#scenario-s) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [CD](#scenario-cd) [CM](#scenario-cm) [CP](#scenario-cp) [T](#scenario-t). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Pooled [0,1) ranks mapped back to each scan; dimensions preserved and contrast ordering retained.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [S](#scenario-s) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [CD](#scenario-cd) [CM](#scenario-cm) [CP](#scenario-cp) [T](#scenario-t) — Use isolated library with differently sized/exposed scans, ambiguous literal names and unreadable entries; verify stable ordering, primary choice and no scan joins.

<a id="function-265"></a>

### `samples::Set::pixels` — line 223

Source: [samples.cpp:223](../samples.cpp#L223). Signature: `Set::pixels() const`.

**Callers and how they call it**

- [`describe`](#function-210) ([mask.cpp:287](../mask.cpp#L287)): direct C++ call; `pixels()`.

- [`samples::Set::read`](#function-257) ([samples.cpp:104](../samples.cpp#L104)): direct C++ call; `pixels()`.

- [`samples::Set::ordering`](#function-263) ([samples.cpp:177](../samples.cpp#L177)): direct C++ call; `pixels()`.

**CLI invocations / variants:** [S](#scenario-s) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [CD](#scenario-cd) [CM](#scenario-cm) [CP](#scenario-cp) [T](#scenario-t). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Sum areas in wide arithmetic; empty and differently sized scans.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [S](#scenario-s) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [CD](#scenario-cd) [CM](#scenario-cm) [CP](#scenario-cp) [T](#scenario-t) — Use isolated library with differently sized/exposed scans, ambiguous literal names and unreadable entries; verify stable ordering, primary choice and no scan joins.

<a id="function-266"></a>

### `samples::Set::smallest` — line 230

Source: [samples.cpp:230](../samples.cpp#L230). Signature: `Set::smallest() const`.

**Callers and how they call it**

- [`synthesise_inpaint_field`](#function-152) ([inpaint.cpp:363](../inpaint.cpp#L363)): direct C++ call; `smallest()`.

- [`fit_markov_model`](#function-192) ([markov.cpp:851](../markov.cpp#L851)): direct C++ call; `smallest()`.

**CLI invocations / variants:** [S](#scenario-s) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [CD](#scenario-cd) [CM](#scenario-cm) [CP](#scenario-cp) [T](#scenario-t). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Independent minimum width and height, potentially from different scans; empty behavior.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [S](#scenario-s) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [CD](#scenario-cd) [CM](#scenario-cm) [CP](#scenario-cp) [T](#scenario-t) — Use isolated library with differently sized/exposed scans, ambiguous literal names and unreadable entries; verify stable ordering, primary choice and no scan joins.

<a id="file-40"></a>

## `samples.h`

### Declaration: `library_root()`

Source: [samples.h:26](../samples.h#L26).

Callers, CLI variants, unit tests and integration tests: [`samples::library_root`](#function-254). This is the declaration of that same function; do not count it as separate executed coverage.

### Declaration: `resolve(const std::string& spec)`

Source: [samples.h:41](../samples.h#L41).

Callers, CLI variants, unit tests and integration tests: [`samples::resolve`](#function-255). This is the declaration of that same function; do not count it as separate executed coverage.

### Declaration: `library_names()`

Source: [samples.h:45](../samples.h#L45).

Callers, CLI variants, unit tests and integration tests: [`samples::library_names`](#function-256). This is the declaration of that same function; do not count it as separate executed coverage.

### Declaration: `read(const std::vector<std::string>& paths, std::ostream* log)`

Source: [samples.h:65](../samples.h#L65).

Callers, CLI variants, unit tests and integration tests: [`samples::Set::read`](#function-257). This is the declaration of that same function; do not count it as separate executed coverage.

### Declaration: `of(const cv::Mat& one)`

Source: [samples.h:69](../samples.h#L69).

Callers, CLI variants, unit tests and integration tests: [`samples::Set::of`](#function-258). This is the declaration of that same function; do not count it as separate executed coverage.

### Declaration: `of(std::vector<cv::Mat> scans)`

Source: [samples.h:75](../samples.h#L75).

Callers, CLI variants, unit tests and integration tests: [`samples::Set::of`](#function-259). This is the declaration of that same function; do not count it as separate executed coverage.

### Declaration: `primary() const`

Source: [samples.h:87](../samples.h#L87).

Callers, CLI variants, unit tests and integration tests: [`samples::Set::primary`](#function-260). This is the declaration of that same function; do not count it as separate executed coverage.

### Declaration: `prepared(double highpass, double scale) const`

Source: [samples.h:101](../samples.h#L101).

Callers, CLI variants, unit tests and integration tests: [`samples::Set::prepared`](#function-261). This is the declaration of that same function; do not count it as separate executed coverage.

### Declaration: `ranked_unit() const`

Source: [samples.h:108](../samples.h#L108).

Callers, CLI variants, unit tests and integration tests: [`samples::Set::ranked_unit`](#function-264). This is the declaration of that same function; do not count it as separate executed coverage.

### Declaration: `scan_of(std::size_t flat) const`

Source: [samples.h:117](../samples.h#L117).

Callers, CLI variants, unit tests and integration tests: [`samples::Set::Ordering::scan_of`](#function-262). This is the declaration of that same function; do not count it as separate executed coverage.

### Declaration: `ordering() const`

Source: [samples.h:119](../samples.h#L119).

Callers, CLI variants, unit tests and integration tests: [`samples::Set::ordering`](#function-263). This is the declaration of that same function; do not count it as separate executed coverage.

### Declaration: `pixels() const`

Source: [samples.h:121](../samples.h#L121).

Callers, CLI variants, unit tests and integration tests: [`samples::Set::pixels`](#function-265). This is the declaration of that same function; do not count it as separate executed coverage.

### Declaration: `smallest() const`

Source: [samples.h:122](../samples.h#L122).

Callers, CLI variants, unit tests and integration tests: [`samples::Set::smallest`](#function-266). This is the declaration of that same function; do not count it as separate executed coverage.

<a id="function-267"></a>

### `samples::Set::empty` — line 77

Source: [samples.h:77](../samples.h#L77). Signature: `empty() const`.

**Callers and how they call it**

- [`calibrate_dft_for`](#function-088) ([dftcalib.cpp:95](../dftcalib.cpp#L95)): direct C++ call; receiver/signature resolved by compiler.

- [`dither_main`](#function-096) ([dither.cpp:105](../dither.cpp#L105)): direct C++ call; receiver/signature resolved by compiler.

- [`calibrate_inpaint_for`](#function-156) ([inpaintcalib.cpp:107](../inpaintcalib.cpp#L107)): direct C++ call; receiver/signature resolved by compiler.

- [`calibrate_markov_for`](#function-204) ([markovcalib.cpp:403](../markovcalib.cpp#L403)): direct C++ call; receiver/signature resolved by compiler.

- [`build_with_method`](#function-211) ([mask.cpp:296](../mask.cpp#L296)): direct C++ call; receiver/signature resolved by compiler.

**CLI invocations / variants:** [S](#scenario-s) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [CD](#scenario-cd) [CM](#scenario-cm) [CP](#scenario-cp) [T](#scenario-t). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Empty and populated state agree with size; moved/transformed sets as applicable.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [S](#scenario-s) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [CD](#scenario-cd) [CM](#scenario-cm) [CP](#scenario-cp) [T](#scenario-t) — Use multi-scan generator runs and a linked Set client; verify complete scan retention, deterministic primary and stable iteration.

<a id="function-268"></a>

### `samples::Set::size` — line 78

Source: [samples.h:78](../samples.h#L78). Signature: `size() const`.

**Callers and how they call it**

- [`calibration_key`](#function-087) ([dftcalib.cpp:62](../dftcalib.cpp#L62)): direct C++ call; receiver/signature resolved by compiler.

- [`downsample_set`](#function-140) ([inpaint.cpp:79](../inpaint.cpp#L79)): direct C++ call; receiver/signature resolved by compiler.

- [`PatchDatabase::PatchDatabase`](#function-142) ([inpaint.cpp:113](../inpaint.cpp#L113)): direct C++ call; receiver/signature resolved by compiler.

- [`calibration_key`](#function-155) ([inpaintcalib.cpp:69](../inpaintcalib.cpp#L69)): direct C++ call; receiver/signature resolved by compiler.

- [`calibration_key`](#function-201) ([markovcalib.cpp:300](../markovcalib.cpp#L300)): direct C++ call; receiver/signature resolved by compiler.

- [`describe`](#function-210) ([mask.cpp:287](../mask.cpp#L287)): direct C++ call; receiver/signature resolved by compiler.

- [`build_with_method`](#function-211) ([mask.cpp:296](../mask.cpp#L296)): direct C++ call; receiver/signature resolved by compiler.

- [`amplitude_for`](#function-280) ([spectralmask.cpp:208](../spectralmask.cpp#L208)): direct C++ call; receiver/signature resolved by compiler.

**CLI invocations / variants:** [S](#scenario-s) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [CD](#scenario-cd) [CM](#scenario-cm) [CP](#scenario-cp) [T](#scenario-t). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Number of scans rather than pixel count; empty/one/many.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [S](#scenario-s) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [CD](#scenario-cd) [CM](#scenario-cm) [CP](#scenario-cp) [T](#scenario-t) — Use multi-scan generator runs and a linked Set client; verify complete scan retention, deterministic primary and stable iteration.

<a id="function-269"></a>

### `samples::Set::operator[]` — line 79

Source: [samples.h:79](../samples.h#L79). Signature: `operator[](std::size_t i) const`.

**Callers and how they call it**

- [`PatchDatabase::PatchDatabase`](#function-142) ([inpaint.cpp:113](../inpaint.cpp#L113)): direct C++ call; receiver/signature resolved by compiler.

- [`describe`](#function-210) ([mask.cpp:287](../mask.cpp#L287)): direct C++ call; receiver/signature resolved by compiler.

- [`amplitude_for`](#function-280) ([spectralmask.cpp:208](../spectralmask.cpp#L208)): direct C++ call; receiver/signature resolved by compiler.

**CLI invocations / variants:** [S](#scenario-s) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [CD](#scenario-cd) [CM](#scenario-cm) [CP](#scenario-cp) [T](#scenario-t). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Valid indices return corresponding scan without copying; const access; out-of-range precondition documented.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [S](#scenario-s) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [CD](#scenario-cd) [CM](#scenario-cm) [CP](#scenario-cp) [T](#scenario-t) — Use multi-scan generator runs and a linked Set client; verify complete scan retention, deterministic primary and stable iteration.

<a id="function-270"></a>

### `samples::Set::begin` — line 80

Source: [samples.h:80](../samples.h#L80). Signature: `begin() const`.

**Callers and how they call it**

- [`calibration_key`](#function-087) ([dftcalib.cpp:62](../dftcalib.cpp#L62)): direct C++ call; receiver/signature resolved by compiler.

- [`downsample_set`](#function-140) ([inpaint.cpp:79](../inpaint.cpp#L79)): direct C++ call; receiver/signature resolved by compiler.

- [`set_coherence`](#function-141) ([inpaint.cpp:92](../inpaint.cpp#L92)): direct C++ call; receiver/signature resolved by compiler.

- [`PatchDatabase::PatchDatabase`](#function-142) ([inpaint.cpp:113](../inpaint.cpp#L113)): direct C++ call; receiver/signature resolved by compiler.

- [`calibration_key`](#function-155) ([inpaintcalib.cpp:69](../inpaintcalib.cpp#L69)): direct C++ call; receiver/signature resolved by compiler.

- [`quantise_set`](#function-168) ([markov.cpp:103](../markov.cpp#L103)): direct C++ call; receiver/signature resolved by compiler.

- [`fit_markov_model`](#function-192) ([markov.cpp:851](../markov.cpp#L851)): direct C++ call; receiver/signature resolved by compiler.

- [`calibration_key`](#function-201) ([markovcalib.cpp:300](../markovcalib.cpp#L300)): direct C++ call; receiver/signature resolved by compiler.

**CLI invocations / variants:** [S](#scenario-s) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [CD](#scenario-cd) [CM](#scenario-cm) [CP](#scenario-cp) [T](#scenario-t). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Range iteration starts at first scan, const access and empty begin==end.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [S](#scenario-s) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [CD](#scenario-cd) [CM](#scenario-cm) [CP](#scenario-cp) [T](#scenario-t) — Use multi-scan generator runs and a linked Set client; verify complete scan retention, deterministic primary and stable iteration.

<a id="function-271"></a>

### `samples::Set::end` — line 81

Source: [samples.h:81](../samples.h#L81). Signature: `end() const`.

**Callers and how they call it**

- [`calibration_key`](#function-087) ([dftcalib.cpp:62](../dftcalib.cpp#L62)): direct C++ call; receiver/signature resolved by compiler.

- [`downsample_set`](#function-140) ([inpaint.cpp:79](../inpaint.cpp#L79)): direct C++ call; receiver/signature resolved by compiler.

- [`set_coherence`](#function-141) ([inpaint.cpp:92](../inpaint.cpp#L92)): direct C++ call; receiver/signature resolved by compiler.

- [`PatchDatabase::PatchDatabase`](#function-142) ([inpaint.cpp:113](../inpaint.cpp#L113)): direct C++ call; receiver/signature resolved by compiler.

- [`calibration_key`](#function-155) ([inpaintcalib.cpp:69](../inpaintcalib.cpp#L69)): direct C++ call; receiver/signature resolved by compiler.

- [`quantise_set`](#function-168) ([markov.cpp:103](../markov.cpp#L103)): direct C++ call; receiver/signature resolved by compiler.

- [`fit_markov_model`](#function-192) ([markov.cpp:851](../markov.cpp#L851)): direct C++ call; receiver/signature resolved by compiler.

- [`calibration_key`](#function-201) ([markovcalib.cpp:300](../markovcalib.cpp#L300)): direct C++ call; receiver/signature resolved by compiler.

**CLI invocations / variants:** [S](#scenario-s) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [CD](#scenario-cd) [CM](#scenario-cm) [CP](#scenario-cp) [T](#scenario-t). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Range iteration terminates after size scans; empty and populated containers.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [S](#scenario-s) [D](#scenario-d) [M](#scenario-m) [P](#scenario-p) [Q](#scenario-q) [CD](#scenario-cd) [CM](#scenario-cm) [CP](#scenario-cp) [T](#scenario-t) — Use multi-scan generator runs and a linked Set client; verify complete scan retention, deterministic primary and stable iteration.

<a id="file-41"></a>

## `spectralmask.cpp`

<a id="function-272"></a>

### `smooth_power` — line 38

Source: [spectralmask.cpp:38](../spectralmask.cpp#L38). Signature: `smooth_power(cv::Mat& power, cv::Size sz, double cells)`.

**Callers and how they call it**

- [`sample_amplitude`](#function-273) ([spectralmask.cpp:71](../spectralmask.cpp#L71)): direct C++ call; `smooth_power(power, sz, smooth)`.

**CLI invocations / variants:** [D](#scenario-d) [CD](#scenario-cd) [T](#scenario-t). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** No smoothing at disabled width; periodic smoothing, Hermitian symmetry and nonnegative power; independent small-grid reference.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [D](#scenario-d) [CD](#scenario-cd) [T](#scenario-t) — Sinusoidal sample plus scale 1/2/4: expected peaks/cutoff and low out-of-band energy; multi-scan power pooling and deterministic rank masks.

<a id="function-273"></a>

### `sample_amplitude` — line 71

Source: [spectralmask.cpp:71](../spectralmask.cpp#L71). Signature: `sample_amplitude(const cv::Mat& sample, double highpass, cv::Size sz, double smooth)`.

**Callers and how they call it**

- [`synthesise_spectral_order`](#function-279) ([spectralmask.cpp:194](../spectralmask.cpp#L194)): direct C++ call; `sample_amplitude(sample, highpass, sz, smooth)`.

- [`amplitude_for`](#function-280) ([spectralmask.cpp:208](../spectralmask.cpp#L208)): direct C++ call; `sample_amplitude(set[0], highpass, sz, smooth)`, `sample_amplitude(set[i], highpass, sz, smooth)`.

**CLI invocations / variants:** [D](#scenario-d) [CD](#scenario-cd) [T](#scenario-t). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Mean/DC handling and periodic correction; highpass on/off; known sinusoid and constant sample; smoothing changes expected bins.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [D](#scenario-d) [CD](#scenario-cd) [T](#scenario-t) — Sinusoidal sample plus scale 1/2/4: expected peaks/cutoff and low out-of-band energy; multi-scan power pooling and deterministic rank masks.

<a id="function-274"></a>

### `wrapped` — line 95

Source: [spectralmask.cpp:95](../spectralmask.cpp#L95). Signature: `wrapped(const cv::Mat& m, int y, int x)`.

**Callers and how they call it**

- [`sample_amplitude_at`](#function-275) ([spectralmask.cpp:103](../spectralmask.cpp#L103)): direct C++ call; `wrapped(sample_amp, y0, x0)`, `wrapped(sample_amp, y0 + 1, x0)`, `wrapped(sample_amp, y0, x0 + 1)`, `wrapped(sample_amp, y0 + 1, x0 + 1)`.

**CLI invocations / variants:** [D](#scenario-d) [CD](#scenario-cd) [T](#scenario-t). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Toroidal lookup at all edges, multiple positive/negative wraps and non-square dimensions.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [D](#scenario-d) [CD](#scenario-cd) [T](#scenario-t) — Sinusoidal sample plus scale 1/2/4: expected peaks/cutoff and low out-of-band energy; multi-scan power pooling and deterministic rank masks.

<a id="function-275"></a>

### `sample_amplitude_at` — line 103

Source: [spectralmask.cpp:103](../spectralmask.cpp#L103). Signature: `sample_amplitude_at(const cv::Mat& sample_amp, double fy, double fx)`.

**Callers and how they call it**

- [`amplitude_grid`](#function-276) ([spectralmask.cpp:115](../spectralmask.cpp#L115)): direct C++ call; `sample_amplitude_at(sample_amp, sfy * sample_amp.rows, sfx * sample_amp.cols)`.

**CLI invocations / variants:** [D](#scenario-d) [CD](#scenario-cd) [T](#scenario-t). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Bilinear interpolation at exact bins and halfway points; periodic index wrap and band-limit behavior.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [D](#scenario-d) [CD](#scenario-cd) [T](#scenario-t) — Sinusoidal sample plus scale 1/2/4: expected peaks/cutoff and low out-of-band energy; multi-scan power pooling and deterministic rank masks.

<a id="function-276"></a>

### `amplitude_grid` — line 115

Source: [spectralmask.cpp:115](../spectralmask.cpp#L115). Signature: `amplitude_grid(const cv::Mat& sample_amp, cv::Size sz, double scale)`.

**Callers and how they call it**

- [`synthesise_spectral_order`](#function-279) ([spectralmask.cpp:194](../spectralmask.cpp#L194)): direct C++ call; `amplitude_grid(sample_amplitude(sample, highpass, sz, smooth), sz, scale)`.

- [`amplitude_for`](#function-280) ([spectralmask.cpp:208](../spectralmask.cpp#L208)): direct C++ call; `amplitude_grid(sample_amplitude(set[0], highpass, sz, smooth), sz, scale)`, `amplitude_grid(sample_amplitude(set[i], highpass, sz, smooth), sz, scale)`.

**CLI invocations / variants:** [D](#scenario-d) [CD](#scenario-cd) [T](#scenario-t). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Scale 1 and 2/4; stop frequencies outside source Nyquist instead of aliasing; rectangular target and conjugate symmetry.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [D](#scenario-d) [CD](#scenario-cd) [T](#scenario-t) — Sinusoidal sample plus scale 1/2/4: expected peaks/cutoff and low out-of-band energy; multi-scan power pooling and deterministic rank masks.

<a id="function-277"></a>

### `impose_amplitude` — line 144

Source: [spectralmask.cpp:144](../spectralmask.cpp#L144). Signature: `impose_amplitude(cv::Mat& f, const cv::Mat& amp, cv::Mat planes[2], cv::Mat& cplx)`.

**Callers and how they call it**

- [`order_from_amplitude`](#function-278) ([spectralmask.cpp:172](../spectralmask.cpp#L172)): direct C++ call; `impose_amplitude(f, amp, planes, cplx)`.

**CLI invocations / variants:** [D](#scenario-d) [CD](#scenario-cd) [T](#scenario-t). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** DFT magnitude matches supplied target before reranking; zero magnitude and DC, inverse transform normalization and finite real result.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [D](#scenario-d) [CD](#scenario-cd) [T](#scenario-t) — Sinusoidal sample plus scale 1/2/4: expected peaks/cutoff and low out-of-band energy; multi-scan power pooling and deterministic rank masks.

<a id="function-278"></a>

### `order_from_amplitude` — line 172

Source: [spectralmask.cpp:172](../spectralmask.cpp#L172). Signature: `order_from_amplitude(cv::Size sz, const cv::Mat& amp, int iters, uint64_t seed)`.

**Callers and how they call it**

- [`synthesise_spectral_order`](#function-279) ([spectralmask.cpp:194](../spectralmask.cpp#L194)): direct C++ call; `order_from_amplitude( sz, amplitude_grid(sample_amplitude(sample, highpass, sz, smooth), sz, scale), iters, seed)`.

- [`build_spectral_mask`](#function-281) ([spectralmask.cpp:224](../spectralmask.cpp#L224)): direct C++ call; `order_from_amplitude(sz, amplitude_for(set, sz, scale, highpass, smooth), iters, seed)`.

**CLI invocations / variants:** [D](#scenario-d) [CD](#scenario-cd) [T](#scenario-t). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Fixed-seed repeatability; alternating projection iteration count; final ordering is a permutation, including zero target amplitude.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [D](#scenario-d) [CD](#scenario-cd) [T](#scenario-t) — Sinusoidal sample plus scale 1/2/4: expected peaks/cutoff and low out-of-band energy; multi-scan power pooling and deterministic rank masks.

<a id="function-279"></a>

### `synthesise_spectral_order` — line 194

Source: [spectralmask.cpp:194](../spectralmask.cpp#L194). Signature: `synthesise_spectral_order(cv::Size sz, const cv::Mat& sample, double scale, double highpass, int iters, double smooth, uint64_t seed)`.

**Callers and how they call it**

- [`run`](#function-166) ([maketexture.cpp:239](../maketexture.cpp#L239)): direct C++ call; `synthesise_spectral_order(want, patch, scale, highpass, (int)P.integer(spectralmask::keys::Iters), P.num(spectralmask::keys::SmoothCells), r.seed)`.

**CLI invocations / variants:** [T](#scenario-t) (dft). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Single-sample public synthesis agrees with preparation+grid+projection; scaled/highpassed variants and seeded ordering.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [T](#scenario-t) (dft) — Sinusoidal sample plus scale 1/2/4: expected peaks/cutoff and low out-of-band energy; multi-scan power pooling and deterministic rank masks.

<a id="function-280"></a>

### `amplitude_for` — line 208

Source: [spectralmask.cpp:208](../spectralmask.cpp#L208). Signature: `amplitude_for(const samples::Set& set, cv::Size sz, double scale, double highpass, double smooth)`.

**Callers and how they call it**

- [`build_spectral_mask`](#function-281) ([spectralmask.cpp:224](../spectralmask.cpp#L224)): direct C++ call; `amplitude_for(set, sz, scale, highpass, smooth)`.

**CLI invocations / variants:** [D](#scenario-d) [CD](#scenario-cd) [T](#scenario-t). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Pool spectra by intended scan weighting on common target grid; one-scan equivalence, mixed dimensions and empty set precondition.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [D](#scenario-d) [CD](#scenario-cd) [T](#scenario-t) — Sinusoidal sample plus scale 1/2/4: expected peaks/cutoff and low out-of-band energy; multi-scan power pooling and deterministic rank masks.

<a id="function-281"></a>

### `build_spectral_mask` — line 224

Source: [spectralmask.cpp:224](../spectralmask.cpp#L224). Signature: `build_spectral_mask(cv::Size sz, const samples::Set& set, double scale, double highpass, int iters, double smooth, uint64_t seed)`.

**Callers and how they call it**

- [`calibrate_dft_for`](#function-088) ([dftcalib.cpp:95](../dftcalib.cpp#L95)): direct C++ call; `build_spectral_mask(probe, set, scale, highpass, iters, kLadder[i], seed)`.

- [`dft_build`](#function-121) ([generators.cpp:58](../generators.cpp#L58)): direct C++ call; `build_spectral_mask(q.size, q.samples, P.num(mask::keys::Scale), P.num(mask::keys::Highpass), (int)P.integer(spectralmask::keys::Iters), P.num(spectralmask::keys::SmoothCells), q.seed)`.

**CLI invocations / variants:** [D](#scenario-d) [CD](#scenario-cd) [T](#scenario-t). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Same-seed mask repeatability, flat histogram and range, one/multiple scans; scale and smoothing alter spectra without folding.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [D](#scenario-d) [CD](#scenario-cd) [T](#scenario-t) — Sinusoidal sample plus scale 1/2/4: expected peaks/cutoff and low out-of-band energy; multi-scan power pooling and deterministic rank masks.

<a id="function-282"></a>

### `dft_work` — line 237

Source: [spectralmask.cpp:237](../spectralmask.cpp#L237). Signature: `dft_work(int w, int h, int iters)`.

**Callers and how they call it**

- [`dft_work_of`](#function-120) ([generators.cpp:53](../generators.cpp#L53)): direct C++ call; `dft_work(w, h, (int)r.integer(spectralmask::keys::Iters))`.

**CLI invocations / variants:** [D](#scenario-d) [CD](#scenario-cd) [T](#scenario-t). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Finite positive cost; area/log-size/iteration dependence matches formula, including minimum dimensions.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [D](#scenario-d) [CD](#scenario-cd) [T](#scenario-t) — Sinusoidal sample plus scale 1/2/4: expected peaks/cutoff and low out-of-band energy; multi-scan power pooling and deterministic rank masks.

<a id="function-283"></a>

### `register_spectral_params` — line 243

Source: [spectralmask.cpp:243](../spectralmask.cpp#L243). Signature: `register_spectral_params(params::Params& P)`.

**Callers and how they call it**

- [`usage_generators`](#function-031) ([cli.cpp:568](../cli.cpp#L568)): indirect: generators() row .register_params(P), once for every generator.

- [`dither_main`](#function-096) ([dither.cpp:105](../dither.cpp#L105)): indirect: generators() row .register_params(P), once for every generator.

- [`list_options`](#function-161) ([maketexture.cpp:102](../maketexture.cpp#L102)): indirect: generators() row .register_params(P), once for every generator.

- [`run`](#function-166) ([maketexture.cpp:239](../maketexture.cpp#L239)): indirect: generators() row .register_params(P), once for every generator.

- [`cache_registry`](#function-345) ([unittest.cpp:981](../unittest.cpp#L981)): indirect: generators() row .register_params(P), once for every generator.

**CLI invocations / variants:** [H](#scenario-h) [E](#scenario-e) [R](#scenario-r) [T](#scenario-t) [U](#scenario-u) (registration); selected-generator scenarios exercise lazy derived rules. IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Register every spectral rule once; assert defaults, range endpoints, option visibility and applicability. Resolve derived rules at dependency boundaries and after overrides, check origins/dependency edges, and verify parameter-struct defaults agree. Test each lambda below through the registry.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [H](#scenario-h) [E](#scenario-e) [R](#scenario-r) [T](#scenario-t) [U](#scenario-u) (registration); selected-generator scenarios exercise lazy derived rules — Sinusoidal sample plus scale 1/2/4: expected peaks/cutoff and low out-of-band energy; multi-scan power pooling and deterministic rank masks.

<a id="file-42"></a>

## `spectralmask.h`

### Declaration: `build_spectral_mask(cv::Size sz, const samples::Set& set, double scale, double highpass, int iters, double smooth, uint64_t seed)`

Source: [spectralmask.h:30](../spectralmask.h#L30).

Callers, CLI variants, unit tests and integration tests: [`build_spectral_mask`](#function-281). This is the declaration of that same function; do not count it as separate executed coverage.

### Declaration: `synthesise_spectral_order(cv::Size sz, const cv::Mat& sample, double scale, double highpass, int iters, double smooth, uint64_t seed)`

Source: [spectralmask.h:37](../spectralmask.h#L37).

Callers, CLI variants, unit tests and integration tests: [`synthesise_spectral_order`](#function-279). This is the declaration of that same function; do not count it as separate executed coverage.

### Declaration: `register_spectral_params(params::Params&)`

Source: [spectralmask.h:51](../spectralmask.h#L51).

Callers, CLI variants, unit tests and integration tests: [`register_spectral_params`](#function-283). This is the declaration of that same function; do not count it as separate executed coverage.

### Declaration: `dft_work(int w, int h, int iters)`

Source: [spectralmask.h:54](../spectralmask.h#L54).

Callers, CLI variants, unit tests and integration tests: [`dft_work`](#function-282). This is the declaration of that same function; do not count it as separate executed coverage.

<a id="file-43"></a>

## `srgb.cpp`

<a id="function-284"></a>

### `vision::to_gray` — line 11

Source: [srgb.cpp:11](../srgb.cpp#L11). Signature: `to_gray(const cv::Mat& img)`.

**Callers and how they call it**

- [`vision::decode_gray`](#function-289) ([srgb.cpp:79](../srgb.cpp#L79)): direct C++ call; `to_gray(img)`.

- [`case_186`](#function-325) ([unittest.cpp:186](../unittest.cpp#L186)): direct C++ call; `to_gray(a)`, `to_gray(b)`, `to_gray(e)`.

**CLI invocations / variants:** [R](#scenario-r) [F](#scenario-f) [O](#scenario-o) [SR](#scenario-sr). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** 1/3/4 channels at 8/16 bits; preserve bit depth and absolute code values; unsupported type/channel behavior.

**Existing direct C++ test callers:** [`case_186`](#function-325). These do not establish full branch coverage.

**Integration tests should exist:** [R](#scenario-r) [F](#scenario-f) [O](#scenario-o) [SR](#scenario-sr) — Read native 8/16-bit input and all output ladders; independently decode saved frames to check mean light, resize result, dimensions and fine-input precision.

<a id="function-285"></a>

### `vision::build_decode_table` — line 36

Source: [srgb.cpp:36](../srgb.cpp#L36). Signature: `build_decode_table(int max_code, bool srgb)`.

**Callers and how they call it**

- [`vision::decode_table`](#function-286) ([srgb.cpp:43](../srgb.cpp#L43)): direct C++ call; `build_decode_table(255, false)`, `build_decode_table(255, true)`, `build_decode_table(65535, false)`, `build_decode_table(65535, true)`.

**CLI invocations / variants:** [R](#scenario-r) [F](#scenario-f) [O](#scenario-o) [SR](#scenario-sr). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Every 8-bit entry and representative/all 16-bit entries equal scalar decode_code; endpoints and monotonicity.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [R](#scenario-r) [F](#scenario-f) [O](#scenario-o) [SR](#scenario-sr) — Read native 8/16-bit input and all output ladders; independently decode saved frames to check mean light, resize result, dimensions and fine-input precision.

<a id="function-286"></a>

### `vision::decode_table` — line 43

Source: [srgb.cpp:43](../srgb.cpp#L43). Signature: `decode_table(int max_code, bool srgb)`.

**Callers and how they call it**

- [`vision::decode_gray`](#function-289) ([srgb.cpp:79](../srgb.cpp#L79)): direct C++ call; `decode_table(65535, srgb)`, `decode_table(255, srgb)`.

**CLI invocations / variants:** [R](#scenario-r) [F](#scenario-f) [O](#scenario-o) [SR](#scenario-sr). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Correct independent cached tables for 255/65535 and sRGB/linear, irrespective of first-call order; thread-safe initialization.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [R](#scenario-r) [F](#scenario-f) [O](#scenario-o) [SR](#scenario-sr) — Read native 8/16-bit input and all output ladders; independently decode saved frames to check mean light, resize result, dimensions and fine-input precision.

<a id="function-287"></a>

### `vision::decode_into` — line 59

Source: [srgb.cpp:59](../srgb.cpp#L59). Signature: `decode_into(const cv::Mat& gray, cv::Mat& out, const float* table)`.

**Callers and how they call it**

- [`vision::decode_gray`](#function-289) ([srgb.cpp:79](../srgb.cpp#L79)): direct C++ call; `decode_into<uint16_t>(gray, out, decode_table(65535, srgb))`, `decode_into<uint8_t>(gray, out, decode_table(255, srgb))`.

**CLI invocations / variants:** [R](#scenario-r) [F](#scenario-f) [O](#scenario-o) [SR](#scenario-sr). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** uint8_t/uint16_t template specializations, row stride and ROI; every output equals LUT entry.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [R](#scenario-r) [F](#scenario-f) [O](#scenario-o) [SR](#scenario-sr) — Read native 8/16-bit input and all output ladders; independently decode saved frames to check mean light, resize result, dimensions and fine-input precision.

<a id="function-288"></a>

### `vision::encode_gray8` — line 69

Source: [srgb.cpp:69](../srgb.cpp#L69). Signature: `encode_gray8(const cv::Mat& lin, bool srgb)`.

**Callers and how they call it**

- [`vision::resize_gray`](#function-294) ([srgb.cpp:158](../srgb.cpp#L158)): direct C++ call; `encode_gray8(out, srgb)`.

- [`vision::average_gray`](#function-296) ([srgb.cpp:171](../srgb.cpp#L171)): direct C++ call; `encode_gray8(acc, srgb)`.

**CLI invocations / variants:** [R](#scenario-r) [F](#scenario-f) [O](#scenario-o) [SR](#scenario-sr). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Clamp/round endpoints, sRGB versus linear and known half-code cases; output type CV_8U.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [R](#scenario-r) [F](#scenario-f) [O](#scenario-o) [SR](#scenario-sr) — Read native 8/16-bit input and all output ladders; independently decode saved frames to check mean light, resize result, dimensions and fine-input precision.

<a id="function-289"></a>

### `vision::decode_gray` — line 79

Source: [srgb.cpp:79](../srgb.cpp#L79). Signature: `decode_gray(const cv::Mat& img, bool srgb)`.

**Callers and how they call it**

- [`vision::decode_display`](#function-292) ([srgb.cpp:139](../srgb.cpp#L139)): direct C++ call; `decode_gray(img, srgb)`.

- [`vision::resize_gray`](#function-294) ([srgb.cpp:158](../srgb.cpp#L158)): direct C++ call; `decode_gray(img, srgb)`.

- [`vision::average_gray`](#function-296) ([srgb.cpp:171](../srgb.cpp#L171)): direct C++ call; `decode_gray(f, srgb)`.

- [`case_209`](#function-326) ([unittest.cpp:209](../unittest.cpp#L209)): direct C++ call; `decode_gray(wide, srgb)`, `decode_gray(narrow, srgb)`, `decode_gray(same, srgb)`.

- [`case_253`](#function-328) ([unittest.cpp:253](../unittest.cpp#L253)): direct C++ call; `decode_gray(ramp, true)`, `decode_gray(shown, true)`.

**CLI invocations / variants:** [R](#scenario-r) [F](#scenario-f) [O](#scenario-o) [SR](#scenario-sr). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** 8/16-bit color/gray, noncontinuous ROI and exact LUT mapping; no image-dependent normalization.

**Existing direct C++ test callers:** [`case_209`](#function-326), [`case_253`](#function-328). These do not establish full branch coverage.

**Integration tests should exist:** [R](#scenario-r) [F](#scenario-f) [O](#scenario-o) [SR](#scenario-sr) — Read native 8/16-bit input and all output ladders; independently decode saved frames to check mean light, resize result, dimensions and fine-input precision.

<a id="function-290"></a>

### `vision::make_levels` — line 89

Source: [srgb.cpp:89](../srgb.cpp#L89). Signature: `make_levels(int levels, bool srgb, Spacing spacing)`.

**Callers and how they call it**

- [`dither_main`](#function-096) ([dither.cpp:105](../dither.cpp#L105)): direct C++ call; `make_levels(opt.levels, opt.useSrgb, spacing)`.

- [`case_67`](#function-321) ([unittest.cpp:67](../unittest.cpp#L67)): direct C++ call; `make_levels(n, srgb, sp)`.

- [`case_93`](#function-322) ([unittest.cpp:93](../unittest.cpp#L93)): direct C++ call; `make_levels(n, srgb, sp)`.

- [`case_123`](#function-323) ([unittest.cpp:123](../unittest.cpp#L123)): direct C++ call; `make_levels(n, srgb, sp)`, `make_levels(n, true, vision::Spacing::Linear)`.

- [`case_158`](#function-324) ([unittest.cpp:158](../unittest.cpp#L158)): direct C++ call; `make_levels(n, true, vision::Spacing::Device)`, `make_levels(n, true, vision::Spacing::Srgb)`.

- [`case_253`](#function-328) ([unittest.cpp:253](../unittest.cpp#L253)): direct C++ call; `make_levels(256, true, vision::Spacing::Srgb)`.

- [`budget_holds`](#function-360) ([unittest.cpp:1545](../unittest.cpp#L1545)): direct C++ call; `make_levels(c.levels, c.srgb, c.spacing)`.

**CLI invocations / variants:** [R](#scenario-r) [F](#scenario-f) [O](#scenario-o) [SR](#scenario-sr). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** 2..256 levels across srgb/linear/device spacing and transfer curves; endpoints/order, uniform flag, unavoidable pixel-code collisions.

**Existing direct C++ test callers:** [`case_67`](#function-321), [`case_93`](#function-322), [`case_123`](#function-323), [`case_158`](#function-324), [`case_253`](#function-328). These do not establish full branch coverage.

**Integration tests should exist:** [R](#scenario-r) [F](#scenario-f) [O](#scenario-o) [SR](#scenario-sr) — Read native 8/16-bit input and all output ladders; independently decode saved frames to check mean light, resize result, dimensions and fine-input precision.

<a id="function-291"></a>

### `vision::to_display` — line 126

Source: [srgb.cpp:126](../srgb.cpp#L126). Signature: `to_display(const LevelSet& levels, float linear)`.

**Callers and how they call it**

- [`vision::decode_display`](#function-292) ([srgb.cpp:139](../srgb.cpp#L139)): call in enclosing function / its lambda callback; receiver/signature resolved by compiler.

**CLI invocations / variants:** [R](#scenario-r) [F](#scenario-f) [O](#scenario-o) [SR](#scenario-sr). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Exact rungs, interpolation in light, clamping endpoints, uniform shortcut and nonuniform ladder.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [R](#scenario-r) [F](#scenario-f) [O](#scenario-o) [SR](#scenario-sr) — Read native 8/16-bit input and all output ladders; independently decode saved frames to check mean light, resize result, dimensions and fine-input precision.

<a id="function-292"></a>

### `vision::decode_display` — line 139

Source: [srgb.cpp:139](../srgb.cpp#L139). Signature: `decode_display(const cv::Mat& img, const LevelSet& levels, bool srgb)`.

**Callers and how they call it**

- [`prepare_target`](#function-092) ([dither.cpp:26](../dither.cpp#L26)): direct C++ call; `decode_display(image, ladder, opt.useSrgb)`.

- [`case_253`](#function-328) ([unittest.cpp:253](../unittest.cpp#L253)): direct C++ call; `decode_display(*inputs[i], ladder, true)`.

- [`budget_holds`](#function-360) ([unittest.cpp:1545](../unittest.cpp#L1545)): direct C++ call; `decode_display(image, ladder, c.srgb)`.

**CLI invocations / variants:** [R](#scenario-r) [F](#scenario-f) [O](#scenario-o) [SR](#scenario-sr). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Decode_gray then to_display equivalence; 16-bit differences survive; all spacings/curve settings.

**Existing direct C++ test callers:** [`case_253`](#function-328). These do not establish full branch coverage.

**Integration tests should exist:** [R](#scenario-r) [F](#scenario-f) [O](#scenario-o) [SR](#scenario-sr) — Read native 8/16-bit input and all output ladders; independently decode saved frames to check mean light, resize result, dimensions and fine-input precision.

#### Anonymous function: `vision::decode_display::callback@141`

Source: [srgb.cpp:141](../srgb.cpp#L141); capture/signature: `[&](float& v, const int*)`.

- **Callers/how:** vision::decode_display invokes this local callback directly or through the algorithm receiving it; inspect linked source for the call site.

- **CLI invocations:** [R](#scenario-r) [F](#scenario-f) [O](#scenario-o) [SR](#scenario-sr); same enclosing branch, not an independently named CLI operation.

- **Unit tests should exist:** Exercise each branch of this callback through the enclosing function tests; verify captured-state updates and boundary values against the enclosing function oracle.

- **Integration tests should exist:** [R](#scenario-r) [F](#scenario-f) [O](#scenario-o) [SR](#scenario-sr); verify the enclosing function's observable assertion above with the callback branch actually reached.

<a id="function-293"></a>

### `vision::encode_levels` — line 145

Source: [srgb.cpp:145](../srgb.cpp#L145). Signature: `encode_levels(const cv::Mat& levels_mat, const LevelSet& levels)`.

**Callers and how they call it**

- [`encode_and_resize`](#function-094) ([dither.cpp:73](../dither.cpp#L73)): direct C++ call; `encode_levels(f, ladder)`.

- [`case_253`](#function-328) ([unittest.cpp:253](../unittest.cpp#L253)): direct C++ call; `encode_levels(frames[0], ladder)`.

**CLI invocations / variants:** [R](#scenario-r) [F](#scenario-f) [O](#scenario-o) [SR](#scenario-sr). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Each valid level index maps to correct code; dimensions/type, boundaries and index preconditions.

**Existing direct C++ test callers:** [`case_253`](#function-328). These do not establish full branch coverage.

**Integration tests should exist:** [R](#scenario-r) [F](#scenario-f) [O](#scenario-o) [SR](#scenario-sr) — Read native 8/16-bit input and all output ladders; independently decode saved frames to check mean light, resize result, dimensions and fine-input precision.

<a id="function-294"></a>

### `vision::resize_gray` — line 158

Source: [srgb.cpp:158](../srgb.cpp#L158). Signature: `resize_gray(const cv::Mat& img, cv::Size size, bool srgb)`.

**Callers and how they call it**

- [`encode_and_resize`](#function-094) ([dither.cpp:73](../dither.cpp#L73)): direct C++ call; `resize_gray(f, cv::Size(std::max(1, (int)std::lround(w * k)), std::max(1, (int)std::lround(h * k))), opt.useSrgb)`.

- [`vision::downsample_gray`](#function-295) ([srgb.cpp:166](../srgb.cpp#L166)): direct C++ call; `resize_gray(img, cv::Size(img.cols / factor, img.rows / factor), srgb)`.

**CLI invocations / variants:** [R](#scenario-r) [F](#scenario-f) [O](#scenario-o) [SR](#scenario-sr). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** No-change branch, shrink/enlarge, rectangular shape; average in linear light against independent small-array calculation.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [R](#scenario-r) [F](#scenario-f) [O](#scenario-o) [SR](#scenario-sr) — Read native 8/16-bit input and all output ladders; independently decode saved frames to check mean light, resize result, dimensions and fine-input precision.

<a id="function-295"></a>

### `vision::downsample_gray` — line 166

Source: [srgb.cpp:166](../srgb.cpp#L166). Signature: `downsample_gray(const cv::Mat& img, int factor, bool srgb)`.

**Callers and how they call it**

- [`encode_and_resize`](#function-094) ([dither.cpp:73](../dither.cpp#L73)): direct C++ call; `downsample_gray(f, opt.supersample, opt.useSrgb)`.

**CLI invocations / variants:** [R](#scenario-r) [F](#scenario-f) [O](#scenario-o) [SR](#scenario-sr). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Factor 1 identity, integer shrink, nondivisible dimensions and minimum size; compare resize_gray target dimensions.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [R](#scenario-r) [F](#scenario-f) [O](#scenario-o) [SR](#scenario-sr) — Read native 8/16-bit input and all output ladders; independently decode saved frames to check mean light, resize result, dimensions and fine-input precision.

<a id="function-296"></a>

### `vision::average_gray` — line 171

Source: [srgb.cpp:171](../srgb.cpp#L171). Signature: `average_gray(const std::vector<cv::Mat>& frames, bool srgb)`.

**Callers and how they call it**

- [`write_outputs`](#function-095) ([dither.cpp:92](../dither.cpp#L92)): direct C++ call; `average_gray(frames, opt.useSrgb)`.

**CLI invocations / variants:** [R](#scenario-r) [F](#scenario-f) [O](#scenario-o) [SR](#scenario-sr). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** One frame identity, black/white pair gives linear-light midpoint, many frames and shape/type preconditions.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [R](#scenario-r) [F](#scenario-f) [O](#scenario-o) [SR](#scenario-sr) — Read native 8/16-bit input and all output ladders; independently decode saved frames to check mean light, resize result, dimensions and fine-input precision.

<a id="function-297"></a>

### `vision::srgb_to_linear` — line 180

Source: [srgb.cpp:180](../srgb.cpp#L180). Signature: `srgb_to_linear(const cv::Mat& mat)`.

**Callers and how they call it**

- [`main`](#function-311) ([srgbtest.cpp:73](../srgbtest.cpp#L73)): direct C++ call; `srgb_to_linear(image)`.

**CLI invocations / variants:** [SR](#scenario-sr) (diagnostic Mat adapter). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Scalar and Mat overloads: endpoints, transfer knee, round-trip and monotonicity; Mat dimensions/type and code scaling.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [SR](#scenario-sr) (diagnostic Mat adapter) — Read native 8/16-bit input and all output ladders; independently decode saved frames to check mean light, resize result, dimensions and fine-input precision.

<a id="function-298"></a>

### `vision::linear_to_srgb` — line 193

Source: [srgb.cpp:193](../srgb.cpp#L193). Signature: `linear_to_srgb(const cv::Mat& linear)`.

**Callers and how they call it**

- No in-tree caller found. Retain as an explicitly tested library API or decide whether it is unused; no production CLI execution is claimed.

**CLI invocations / variants:** none: Mat overload has no in-tree caller; direct library test required. IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Scalar and Mat overloads: knee/endpoints, clamping/rounding and round-trip tolerance; correct unit versus 255 scale.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** none: Mat overload has no in-tree caller; direct library test required — No current CLI route. Add a linked library client to round-trip a three-channel float image through the Mat adapters and compare to scalar conversions.

<a id="file-44"></a>

## `srgb.h`

### Declaration: `make_levels(int levels, bool srgb, Spacing spacing)`

Source: [srgb.h:100](../srgb.h#L100).

Callers, CLI variants, unit tests and integration tests: [`vision::make_levels`](#function-290). This is the declaration of that same function; do not count it as separate executed coverage.

### Declaration: `to_display(const LevelSet& levels, float linear)`

Source: [srgb.h:106](../srgb.h#L106).

Callers, CLI variants, unit tests and integration tests: [`vision::to_display`](#function-291). This is the declaration of that same function; do not count it as separate executed coverage.

### Declaration: `to_gray(const cv::Mat& img)`

Source: [srgb.h:116](../srgb.h#L116).

Callers, CLI variants, unit tests and integration tests: [`vision::to_gray`](#function-284). This is the declaration of that same function; do not count it as separate executed coverage.

### Declaration: `decode_gray(const cv::Mat& img, bool srgb)`

Source: [srgb.h:121](../srgb.h#L121).

Callers, CLI variants, unit tests and integration tests: [`vision::decode_gray`](#function-289). This is the declaration of that same function; do not count it as separate executed coverage.

### Declaration: `decode_display(const cv::Mat& img, const LevelSet& levels, bool srgb)`

Source: [srgb.h:124](../srgb.h#L124).

Callers, CLI variants, unit tests and integration tests: [`vision::decode_display`](#function-292). This is the declaration of that same function; do not count it as separate executed coverage.

### Declaration: `encode_levels(const cv::Mat& levels_mat, const LevelSet& levels)`

Source: [srgb.h:127](../srgb.h#L127).

Callers, CLI variants, unit tests and integration tests: [`vision::encode_levels`](#function-293). This is the declaration of that same function; do not count it as separate executed coverage.

### Declaration: `downsample_gray(const cv::Mat& img, int factor, bool srgb)`

Source: [srgb.h:131](../srgb.h#L131).

Callers, CLI variants, unit tests and integration tests: [`vision::downsample_gray`](#function-295). This is the declaration of that same function; do not count it as separate executed coverage.

### Declaration: `resize_gray(const cv::Mat& img, cv::Size size, bool srgb)`

Source: [srgb.h:140](../srgb.h#L140).

Callers, CLI variants, unit tests and integration tests: [`vision::resize_gray`](#function-294). This is the declaration of that same function; do not count it as separate executed coverage.

### Declaration: `average_gray(const std::vector<cv::Mat>& frames, bool srgb)`

Source: [srgb.h:143](../srgb.h#L143).

Callers, CLI variants, unit tests and integration tests: [`vision::average_gray`](#function-296). This is the declaration of that same function; do not count it as separate executed coverage.

### Declaration: `srgb_to_linear(const cv::Mat& mat)`

Source: [srgb.h:146](../srgb.h#L146).

Callers, CLI variants, unit tests and integration tests: [`vision::srgb_to_linear`](#function-297). This is the declaration of that same function; do not count it as separate executed coverage.

### Declaration: `linear_to_srgb(const cv::Mat& linear)`

Source: [srgb.h:147](../srgb.h#L147).

Callers, CLI variants, unit tests and integration tests: [`vision::linear_to_srgb`](#function-298). This is the declaration of that same function; do not count it as separate executed coverage.

<a id="function-299"></a>

### `vision::srgb_to_linear_unit` — line 23

Source: [srgb.h:23](../srgb.h#L23). Signature: `srgb_to_linear_unit(float code)`.

**Callers and how they call it**

- [`vision::srgb_to_linear`](#function-301) ([srgb.h:35](../srgb.h#L35)): direct C++ call; `srgb_to_linear_unit(srgb / 255.0f)`.

- [`vision::decode_code`](#function-303) ([srgb.h:45](../srgb.h#L45)): direct C++ call; `srgb_to_linear_unit(code)`.

**CLI invocations / variants:** [R](#scenario-r) [F](#scenario-f) [O](#scenario-o) [SR](#scenario-sr). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Both branches around 0.04045, endpoints and monotonicity; independent IEC formula.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [R](#scenario-r) [F](#scenario-f) [O](#scenario-o) [SR](#scenario-sr) — Compare CLI output and diagnostic scalar values with independent transfer formulas and ladder expectations; cover both sRGB and --linear.

<a id="function-300"></a>

### `vision::linear_to_srgb_unit` — line 28

Source: [srgb.h:28](../srgb.h#L28). Signature: `linear_to_srgb_unit(float linear)`.

**Callers and how they call it**

- [`vision::linear_to_srgb`](#function-302) ([srgb.h:39](../srgb.h#L39)): direct C++ call; `linear_to_srgb_unit(linear)`.

**CLI invocations / variants:** [R](#scenario-r) [F](#scenario-f) [O](#scenario-o) [SR](#scenario-sr). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Both branches around 0.0031308, endpoints and monotonicity; inverse of decode within float tolerance.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [R](#scenario-r) [F](#scenario-f) [O](#scenario-o) [SR](#scenario-sr) — Compare CLI output and diagnostic scalar values with independent transfer formulas and ladder expectations; cover both sRGB and --linear.

<a id="function-301"></a>

### `vision::srgb_to_linear` — line 35

Source: [srgb.h:35](../srgb.h#L35). Signature: `srgb_to_linear(float srgb)`.

**Callers and how they call it**

- [`vision::make_levels`](#function-290) ([srgb.cpp:89](../srgb.cpp#L89)): direct C++ call; `srgb_to_linear(255.0f * code)`.

- [`vision::srgb_to_linear`](#function-297) ([srgb.cpp:180](../srgb.cpp#L180)): direct C++ call; `srgb_to_linear((*itr)[channel])`.

- [`print_mid_grey`](#function-310) ([srgbtest.cpp:62](../srgbtest.cpp#L62)): direct C++ call; `srgb_to_linear(black)`, `srgb_to_linear(white)`.

**CLI invocations / variants:** [R](#scenario-r) [F](#scenario-f) [O](#scenario-o) [SR](#scenario-sr). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Scalar and Mat overloads: endpoints, transfer knee, round-trip and monotonicity; Mat dimensions/type and code scaling.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [R](#scenario-r) [F](#scenario-f) [O](#scenario-o) [SR](#scenario-sr) — Compare CLI output and diagnostic scalar values with independent transfer formulas and ladder expectations; cover both sRGB and --linear.

<a id="function-302"></a>

### `vision::linear_to_srgb` — line 39

Source: [srgb.h:39](../srgb.h#L39). Signature: `linear_to_srgb(float linear)`.

**Callers and how they call it**

- [`vision::linear_to_srgb`](#function-298) ([srgb.cpp:193](../srgb.cpp#L193)): direct C++ call; `linear_to_srgb(source[channel])`.

- [`vision::encode_pixel`](#function-305) ([srgb.h:56](../srgb.h#L56)): direct C++ call; `linear_to_srgb(lin)`.

- [`print_ramp_samples`](#function-308) ([srgbtest.cpp:14](../srgbtest.cpp#L14)): direct C++ call; `linear_to_srgb(grey)`.

- [`print_column_mixes`](#function-309) ([srgbtest.cpp:38](../srgbtest.cpp#L38)): direct C++ call; `linear_to_srgb(avg_lin)`, `linear_to_srgb(col_lin)`.

- [`print_mid_grey`](#function-310) ([srgbtest.cpp:62](../srgbtest.cpp#L62)): direct C++ call; `linear_to_srgb(grey_lin)`.

**CLI invocations / variants:** [R](#scenario-r) [F](#scenario-f) [O](#scenario-o) [SR](#scenario-sr). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Scalar and Mat overloads: knee/endpoints, clamping/rounding and round-trip tolerance; correct unit versus 255 scale.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [R](#scenario-r) [F](#scenario-f) [O](#scenario-o) [SR](#scenario-sr) — Compare CLI output and diagnostic scalar values with independent transfer formulas and ladder expectations; cover both sRGB and --linear.

<a id="function-303"></a>

### `vision::decode_code` — line 45

Source: [srgb.h:45](../srgb.h#L45). Signature: `decode_code(float code, bool srgb)`.

**Callers and how they call it**

- [`vision::build_decode_table`](#function-285) ([srgb.cpp:36](../srgb.cpp#L36)): direct C++ call; `decode_code((float)i / (float)max_code, srgb)`.

- [`vision::decode_pixel`](#function-304) ([srgb.h:51](../srgb.h#L51)): direct C++ call; `decode_code(p / 255.0f, srgb)`.

- [`case_209`](#function-326) ([unittest.cpp:209](../unittest.cpp#L209)): direct C++ call; `decode_code((float)probe[i] / 65535.0f, srgb)`.

**CLI invocations / variants:** [R](#scenario-r) [F](#scenario-f) [O](#scenario-o) [SR](#scenario-sr). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** sRGB versus linear identity; endpoints and transfer-knee values.

**Existing direct C++ test callers:** [`case_209`](#function-326). These do not establish full branch coverage.

**Integration tests should exist:** [R](#scenario-r) [F](#scenario-f) [O](#scenario-o) [SR](#scenario-sr) — Compare CLI output and diagnostic scalar values with independent transfer formulas and ladder expectations; cover both sRGB and --linear.

<a id="function-304"></a>

### `vision::decode_pixel` — line 51

Source: [srgb.h:51](../srgb.h#L51). Signature: `decode_pixel(uint8_t p, bool srgb)`.

**Callers and how they call it**

- [`vision::make_levels`](#function-290) ([srgb.cpp:89](../srgb.cpp#L89)): direct C++ call; `decode_pixel(px, srgb)`.

**CLI invocations / variants:** [R](#scenario-r) [F](#scenario-f) [O](#scenario-o) [SR](#scenario-sr). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** All 256 codes against decode_code(p/255); both transfer modes.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [R](#scenario-r) [F](#scenario-f) [O](#scenario-o) [SR](#scenario-sr) — Compare CLI output and diagnostic scalar values with independent transfer formulas and ladder expectations; cover both sRGB and --linear.

<a id="function-305"></a>

### `vision::encode_pixel` — line 56

Source: [srgb.h:56](../srgb.h#L56). Signature: `encode_pixel(float lin, bool srgb)`.

**Callers and how they call it**

- [`vision::encode_gray8`](#function-288) ([srgb.cpp:69](../srgb.cpp#L69)): direct C++ call; `encode_pixel(in[x], srgb)`.

- [`vision::make_levels`](#function-290) ([srgb.cpp:89](../srgb.cpp#L89)): direct C++ call; `encode_pixel(code, srgb)`.

**CLI invocations / variants:** [R](#scenario-r) [F](#scenario-f) [O](#scenario-o) [SR](#scenario-sr). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Clamping below zero/above one, half-code rounding, transfer knee; round-trip every 8-bit code.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [R](#scenario-r) [F](#scenario-f) [O](#scenario-o) [SR](#scenario-sr) — Compare CLI output and diagnostic scalar values with independent transfer formulas and ladder expectations; cover both sRGB and --linear.

<a id="function-306"></a>

### `vision::level_to_pixel` — line 62

Source: [srgb.h:62](../srgb.h#L62). Signature: `level_to_pixel(int level, int levels, bool srgb)`.

**Callers and how they call it**

- No in-tree caller found. Retain as an explicitly tested library API or decide whether it is unused; no production CLI execution is claimed.

**CLI invocations / variants:** none: no in-tree caller; direct library test required. IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Endpoints and every valid level for representative counts; linear-light spacing formula and rounding.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** none: no in-tree caller; direct library test required — No current CLI route. Add a linked library client comparing the scalar helper against make_levels/encode_levels, or remove the unused API deliberately.

<a id="function-307"></a>

### `vision::LevelSet::count` — line 79

Source: [srgb.h:79](../srgb.h#L79). Signature: `count() const`.

**Callers and how they call it**

- [`Space::of`](#function-100) ([engine.cpp:92](../engine.cpp#L92)): direct C++ call; receiver/signature resolved by compiler.

- [`vision::to_display`](#function-291) ([srgb.cpp:126](../srgb.cpp#L126)): direct C++ call; receiver/signature resolved by compiler.

- [`vision::encode_levels`](#function-293) ([srgb.cpp:145](../srgb.cpp#L145)): direct C++ call; receiver/signature resolved by compiler.

- [`case_67`](#function-321) ([unittest.cpp:67](../unittest.cpp#L67)): direct C++ call; receiver/signature resolved by compiler.

**CLI invocations / variants:** [R](#scenario-r) [F](#scenario-f) [O](#scenario-o) [SR](#scenario-sr). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Empty and populated ladders return intended rung count; construction keeps px/lin lengths consistent.

**Existing direct C++ test callers:** [`case_67`](#function-321). These do not establish full branch coverage.

**Integration tests should exist:** [R](#scenario-r) [F](#scenario-f) [O](#scenario-o) [SR](#scenario-sr) — Compare CLI output and diagnostic scalar values with independent transfer formulas and ladder expectations; cover both sRGB and --linear.

<a id="file-45"></a>

## `srgbtest.cpp`

<a id="function-308"></a>

### `print_ramp_samples` — line 14

Source: [srgbtest.cpp:14](../srgbtest.cpp#L14). Signature: `print_ramp_samples(const cv::Mat& image, const cv::Mat& linear)`.

**Callers and how they call it**

- [`main`](#function-311) ([srgbtest.cpp:73](../srgbtest.cpp#L73)): direct C++ call; `print_ramp_samples(image, linear)`.

**CLI invocations / variants:** [SR](#scenario-sr). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Selected row/column bounds and printed decoded values agree with scalar curve; small-image preconditions.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [SR](#scenario-sr) — Capture printed probes for known ramp; compare numerical values, not only successful exit; missing and undersized input must fail.

<a id="function-309"></a>

### `print_column_mixes` — line 38

Source: [srgbtest.cpp:38](../srgbtest.cpp#L38). Signature: `print_column_mixes(const cv::Mat& linear)`.

**Callers and how they call it**

- [`main`](#function-311) ([srgbtest.cpp:73](../srgbtest.cpp#L73)): direct C++ call; `print_column_mixes(linear)`.

**CLI invocations / variants:** [SR](#scenario-sr). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Column mixtures computed from linear light and printed correctly; tiny-width behavior.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [SR](#scenario-sr) — Capture printed probes for known ramp; compare numerical values, not only successful exit; missing and undersized input must fail.

<a id="function-310"></a>

### `print_mid_grey` — line 62

Source: [srgbtest.cpp:62](../srgbtest.cpp#L62). Signature: `print_mid_grey()`.

**Callers and how they call it**

- [`main`](#function-311) ([srgbtest.cpp:73](../srgbtest.cpp#L73)): direct C++ call; `print_mid_grey()`.

**CLI invocations / variants:** [SR](#scenario-sr). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Reported midpoint corresponds to 50% linear light, not code 128; formatting tolerances.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [SR](#scenario-sr) — Capture printed probes for known ramp; compare numerical values, not only successful exit; missing and undersized input must fail.

<a id="function-311"></a>

### `main` — line 73

Source: [srgbtest.cpp:73](../srgbtest.cpp#L73). Signature: `main(int argc, char** argv)`.

**Callers and how they call it**

- Process runtime invokes this executable entry point with argc/argv; it is not called by another project function.

**CLI invocations / variants:** [SR](#scenario-sr). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Default/explicit input, unreadable image, below-256x128 dimensions and all diagnostic probes; the color reader intentionally supplies 8-bit pixels.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [SR](#scenario-sr) — Capture printed probes for known ramp; compare numerical values, not only successful exit; missing and undersized input must fail.

<a id="file-46"></a>

## `temporaldither.cpp`

<a id="function-312"></a>

### `dither_round` — line 12

Source: [temporaldither.cpp:12](../temporaldither.cpp#L12). Signature: `dither_round(float accumulator)`.

**Callers and how they call it**

- [`dither`](#function-315) ([temporaldither.cpp:32](../temporaldither.cpp#L32)): indirect: dither_fn(accumulator) and dither_fn(desired_brightness), selected by main strategy.

**CLI invocations / variants:** [TD](#scenario-td). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Nonnegative values around half steps; unsigned-range and negative-input preconditions are explicit.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [TD](#scenario-td) — Parse forty output rows for each strategy and seed; compare hand-computed accumulators/averages and reject invalid strategy/depth/value cases.

<a id="function-313"></a>

### `dither_floor` — line 16

Source: [temporaldither.cpp:16](../temporaldither.cpp#L16). Signature: `dither_floor(float accumulator)`.

**Callers and how they call it**

- [`dither`](#function-315) ([temporaldither.cpp:32](../temporaldither.cpp#L32)): indirect: dither_fn(accumulator) and dither_fn(desired_brightness), selected by main strategy.

**CLI invocations / variants:** [TD](#scenario-td). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Nonnegative fractional and integer values; truncation behavior and representability preconditions.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [TD](#scenario-td) — Parse forty output rows for each strategy and seed; compare hand-computed accumulators/averages and reject invalid strategy/depth/value cases.

<a id="function-314"></a>

### `dither_random` — line 20

Source: [temporaldither.cpp:20](../temporaldither.cpp#L20). Signature: `dither_random(float accumulator)`.

**Callers and how they call it**

- [`dither`](#function-315) ([temporaldither.cpp:32](../temporaldither.cpp#L32)): indirect: dither_fn(accumulator) and dither_fn(desired_brightness), selected by main strategy.

**CLI invocations / variants:** [TD](#scenario-td). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Seeded reproducibility, integer exactness and floor/ceiling distribution; stochastic confidence interval.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [TD](#scenario-td) — Parse forty output rows for each strategy and seed; compare hand-computed accumulators/averages and reject invalid strategy/depth/value cases.

<a id="function-315"></a>

### `dither` — line 32

Source: [temporaldither.cpp:32](../temporaldither.cpp#L32). Signature: `dither(unsigned value, unsigned sourcedepth, unsigned targetdepth, unsigned frames, unsigned (*dither_fn)(float) = dither_round)`.

**Callers and how they call it**

- [`main`](#function-316) ([temporaldither.cpp:73](../temporaldither.cpp#L73)): direct C++ call; `dither(value, source, target, frames, dither_round)`, `dither(value, source, target, frames, dither_floor)`, `dither(value, source, target, frames, dither_random)`.

**CLI invocations / variants:** [TD](#scenario-td). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Both accumulator and average strategies against a hand-computed sequence; each function-pointer rule, frame count and depth/value bounds.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [TD](#scenario-td) — Parse forty output rows for each strategy and seed; compare hand-computed accumulators/averages and reject invalid strategy/depth/value cases.

<a id="function-316"></a>

### `main` — line 73

Source: [temporaldither.cpp:73](../temporaldither.cpp#L73). Signature: `main(int argc, char *argv[])`.

**Callers and how they call it**

- Process runtime invokes this executable entry point with argc/argv; it is not called by another project function.

**CLI invocations / variants:** [TD](#scenario-td). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Missing arguments, each strategy and optional seed; unknown strategy nonzero; add strict depth/value validation tests before accepting invalid shifts/divisions.

**Existing direct C++ test callers:** none identified; indirect exercise by another test is not a substitute for the assertions above.

**Integration tests should exist:** [TD](#scenario-td) — Parse forty output rows for each strategy and seed; compare hand-computed accumulators/averages and reject invalid strategy/depth/value cases.

<a id="file-47"></a>

## `unittest.cpp`

<a id="function-317"></a>

### `cases` — line 45

Source: [unittest.cpp:45](../unittest.cpp#L45). Signature: `cases()`.

**Callers and how they call it**

- [`Register::Register`](#function-318) ([unittest.cpp:48](../unittest.cpp#L48)): direct C++ call; `cases()`.

- [`main`](#function-361) ([unittest.cpp:1599](../unittest.cpp#L1599)): direct C++ call; `cases()`.

**CLI invocations / variants:** [U](#scenario-u). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Shared stable registry instance; registration order preserved across calls.

**Integration tests should exist:** [U](#scenario-u) — Run the standalone test executable on a known ramp and missing input; a mutation/throwing-case fixture must produce FAIL and nonzero status while later cases still run.

<a id="function-318"></a>

### `Register::Register` — line 48

Source: [unittest.cpp:48](../unittest.cpp#L48). Signature: `Register(const char* name, std::function<std::pair<bool, std::string>()> f)`.

**Callers and how they call it**

- Compiler-generated static initialization invokes the constructor for every CASE registration with its name and callback; the test body runs later in unittest main.

**CLI invocations / variants:** [U](#scenario-u). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Each static CASE inserts one correct name/callback; duplicate-name handling policy checked by harness audit.

**Integration tests should exist:** [U](#scenario-u) — Run the standalone test executable on a known ramp and missing input; a mutation/throwing-case fixture must produce FAIL and nonzero status while later cases still run.

<a id="function-319"></a>

### `ok` — line 59

Source: [unittest.cpp:59](../unittest.cpp#L59). Signature: `ok(const std::string& detail)`.

**Callers and how they call it**

- [`case_67`](#function-321) ([unittest.cpp:67](../unittest.cpp#L67)): direct C++ call; `ok("3 spacings x 8 counts x 2 curves")`.

- [`case_93`](#function-322) ([unittest.cpp:93](../unittest.cpp#L93)): direct C++ call; `ok("1530 ladders, none wrongly flagged (" + std::to_string(missed) + " even ones conservatively unflagged)")`.

- [`case_123`](#function-323) ([unittest.cpp:123](../unittest.cpp#L123)): direct C++ call; `ok("srgb and device never collide; linear+sRGB first at 122 levels")`.

- [`case_158`](#function-324) ([unittest.cpp:158](../unittest.cpp#L158)): direct C++ call; `ok("levels 2..64, agree iff levels-1 divides 255")`.

- [`case_186`](#function-325) ([unittest.cpp:186](../unittest.cpp#L186)): direct C++ call; `ok("depth preserved, content-independent")`.

- [`case_209`](#function-326) ([unittest.cpp:209](../unittest.cpp#L209)): direct C++ call; `ok("both curves: exact, and 8/16 agree on shared codes")`.

- [`case_253`](#function-328) ([unittest.cpp:253](../unittest.cpp#L253)): direct C++ call; `ok(buf)`.

- [`case_306`](#function-329) ([unittest.cpp:306](../unittest.cpp#L306)): direct C++ call; `ok("11 integer defaults and inpaint.cap agree; markov.temp is derived")`.

- [`case_344`](#function-330) ([unittest.cpp:344](../unittest.cpp#L344)): direct C++ call; `ok("6 sizes, endpoints, monotone, quantile")`.

- [`case_369`](#function-331) ([unittest.cpp:369](../unittest.cpp#L369)): direct C++ call; `ok("permutation, ascending, stable")`.

- [`case_396`](#function-332) ([unittest.cpp:396](../unittest.cpp#L396)): direct C++ call; `ok("5 periods, one period either side and five for wrap_any")`.

- [`case_451`](#function-335) ([unittest.cpp:451](../unittest.cpp#L451)): direct C++ call; `ok(buf)`.

- [`case_483`](#function-336) ([unittest.cpp:483](../unittest.cpp#L483)): direct C++ call; `ok(buf)`.

- [`case_522`](#function-337) ([unittest.cpp:522](../unittest.cpp#L522)): direct C++ call; `ok(buf)`.

- [`case_568`](#function-338) ([unittest.cpp:568](../unittest.cpp#L568)): direct C++ call; `ok("DC, conjugate symmetry and the Nyquist bound, even and odd n")`.

- [`case_603`](#function-339) ([unittest.cpp:603](../unittest.cpp#L603)): direct C++ call; `ok("monotone, ties resolved by the shared order, ranges exact")`.

- [`case_655`](#function-340) ([unittest.cpp:655](../unittest.cpp#L655)): direct C++ call; `ok("fire probability tracks frac; interior flat, end bins half by design")`.

- [`case_715`](#function-341) ([unittest.cpp:715](../unittest.cpp#L715)): direct C++ call; `ok("interior clean; edge tilt " + std::to_string(tr) + " reflecting against " + std::to_string(tw) + " wrapping; off is exact; scale resizes")`.

- [`case_792`](#function-342) ([unittest.cpp:792](../unittest.cpp#L792)): direct C++ call; `ok("border " + std::to_string(border) + ", interior " + std::to_string(interior) + " against an exact circular filter")`.

- [`case_857`](#function-343) ([unittest.cpp:857](../unittest.cpp#L857)): direct C++ call; `ok("planted optimum recovered; closed, connected, deterministic")`.

- [`case_902`](#function-344) ([unittest.cpp:902](../unittest.cpp#L902)): direct C++ call; `ok("wrap/interior " + std::to_string(before) + " -> " + std::to_string(after) + "; no pixel averaged; both axes agree")`.

- [`case_990`](#function-346) ([unittest.cpp:990](../unittest.cpp#L990)): direct C++ call; `ok("derived sizes stay inside the sample on both axes, and 0 means unread")`.

- [`case_1044`](#function-347) ([unittest.cpp:1044](../unittest.cpp#L1044)): direct C++ call; `ok("fractional refused, derived rounding kept, wide seed exact")`.

- [`case_1080`](#function-348) ([unittest.cpp:1080](../unittest.cpp#L1080)): direct C++ call; `ok("close doubles key apart, value_of stays short, doubles round-trip")`.

- [`case_1145`](#function-350) ([unittest.cpp:1145](../unittest.cpp#L1145)): direct C++ call; `ok(detail)`.

- [`case_1235`](#function-351) ([unittest.cpp:1235](../unittest.cpp#L1235)): direct C++ call; `ok(std::to_string(checked) + " intermediates: each moves the key or leaves the mask alone")`.

- [`case_1292`](#function-352) ([unittest.cpp:1292](../unittest.cpp#L1292)): direct C++ call; `ok(std::to_string(rates) + " rates ignored; sample contents counted")`.

- [`case_1360`](#function-354) ([unittest.cpp:1360](../unittest.cpp#L1360)): direct C++ call; `ok(std::to_string(made) + " values made from 2, all rounded onto the grid")`.

- [`case_1389`](#function-355) ([unittest.cpp:1389](../unittest.cpp#L1389)): direct C++ call; `ok("endpoints exact; a two-level sample reads " + std::to_string(flip))`.

- [`case_1411`](#function-356) ([unittest.cpp:1411](../unittest.cpp#L1411)): direct C++ call; `ok("group correlation " + std::to_string(stable) + " stable -> " + std::to_string(shuffled) + " shuffled")`.

- [`case_1461`](#function-357) ([unittest.cpp:1461](../unittest.cpp#L1461)): direct C++ call; `ok("same seed reproduces, different seeds diverge")`.

- [`case_1473`](#function-358) ([unittest.cpp:1473](../unittest.cpp#L1473)): direct C++ call; `ok("featureless and gradient refused, textured accepted, each with " "and without rounding")`.

- [`budget_holds`](#function-360) ([unittest.cpp:1545](../unittest.cpp#L1545)): direct C++ call; `ok(buf)`.

**CLI invocations / variants:** [U](#scenario-u). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Returns true and preserves detail including empty text.

**Existing direct C++ test callers:** [`case_67`](#function-321), [`case_93`](#function-322), [`case_123`](#function-323), [`case_158`](#function-324), [`case_186`](#function-325), [`case_209`](#function-326), [`case_253`](#function-328), [`case_306`](#function-329), [`case_344`](#function-330), [`case_369`](#function-331), [`case_396`](#function-332), [`case_451`](#function-335), [`case_483`](#function-336), [`case_522`](#function-337), [`case_568`](#function-338), [`case_603`](#function-339), [`case_655`](#function-340), [`case_715`](#function-341), [`case_792`](#function-342), [`case_857`](#function-343), [`case_902`](#function-344), [`case_990`](#function-346), [`case_1044`](#function-347), [`case_1080`](#function-348), [`case_1145`](#function-350), [`case_1235`](#function-351), [`case_1292`](#function-352), [`case_1360`](#function-354), [`case_1389`](#function-355), [`case_1411`](#function-356), [`case_1461`](#function-357), [`case_1473`](#function-358). These do not establish full branch coverage.

**Integration tests should exist:** [U](#scenario-u) — Run the standalone test executable on a known ramp and missing input; a mutation/throwing-case fixture must produce FAIL and nonzero status while later cases still run.

<a id="function-320"></a>

### `no` — line 60

Source: [unittest.cpp:60](../unittest.cpp#L60). Signature: `no(const std::string& detail)`.

**Callers and how they call it**

- [`case_67`](#function-321) ([unittest.cpp:67](../unittest.cpp#L67)): direct C++ call; `no("count " + std::to_string(L.count()) + " for levels " + std::to_string(n))`, `no("endpoints " + std::to_string(L.px.front()) + ".." + std::to_string(L.px.back()) + " at levels " + std::to_string(n))`, `no("linear endpoints not 0..1 at levels " + std::to_string(n))`, `no("linear values not ascending at levels " + std::to_string(n))`, `no("pixel values decrease at levels " + std::to_string(n))`.

- [`case_93`](#function-322) ([unittest.cpp:93](../unittest.cpp#L93)): direct C++ call; `no("uniform set on an uneven ladder: levels " + std::to_string(n) + " spacing " + std::to_string((int)sp) + " srgb=" + std::to_string(srgb))`.

- [`case_123`](#function-323) ([unittest.cpp:123](../unittest.cpp#L123)): direct C++ call; `no("srgb/device spacing collided at levels " + std::to_string(n))`, `no("linear+sRGB first collides at levels " + std::to_string(first) + ", expected 122")`.

- [`case_158`](#function-324) ([unittest.cpp:158](../unittest.cpp#L158)): direct C++ call; `no("levels " + std::to_string(n) + ": ladders " + (same ? "agree" : "differ") + " but 255 % " + std::to_string(n - 1) + " = " + std::to_string(255 % (n - 1)))`.

- [`case_186`](#function-325) ([unittest.cpp:186](../unittest.cpp#L186)): direct C++ call; `no("16-bit input did not stay 16-bit")`, `no("a brighter pixel elsewhere changed this one: " + std::to_string(ga.at<uint16_t>(3, 3)) + " vs " + std::to_string(gb.at<uint16_t>(3, 3)))`, `no("8-bit input did not stay 8-bit")`.

- [`case_209`](#function-326) ([unittest.cpp:209](../unittest.cpp#L209)): direct C++ call; `no("16-bit code " + std::to_string(probe[i]) + " decoded wrong")`, `no("8-bit " + std::to_string(p) + " and 16-bit " + std::to_string(p * 257) + " disagree")`.

- [`case_253`](#function-328) ([unittest.cpp:253](../unittest.cpp#L253)): direct C++ call; `no(buf)`.

- [`case_306`](#function-329) ([unittest.cpp:306](../unittest.cpp#L306)): direct C++ call; `no(std::string(w.key) + ": struct says " + std::to_string(w.got) + ", registry says " + std::to_string(reg))`, `no("inpaint.cap differs")`.

- [`case_344`](#function-330) ([unittest.cpp:344](../unittest.cpp#L344)): direct C++ call; `no("rank 0 is not byte 0 at n=" + std::to_string(n))`, `no("top rank is not byte 254 at n=" + std::to_string(n))`, `no("not monotone at n=" + std::to_string(n))`, `no("byte " + std::to_string((int)t) + " is not quantile " + std::to_string(q) + " at n=" + std::to_string(n))`.

- [`case_369`](#function-331) ([unittest.cpp:369](../unittest.cpp#L369)): direct C++ call; `no("wrong length")`, `no("not a permutation")`, `no("not ascending")`, `no("equal values were reordered")`.

- [`case_396`](#function-332) ([unittest.cpp:396](../unittest.cpp#L396)): direct C++ call; `no("disagree at v=" + std::to_string(v) + " n=" + std::to_string(n))`, `no("wrap_any out of range")`.

- [`case_451`](#function-335) ([unittest.cpp:451](../unittest.cpp#L451)): direct C++ call; `no("reference field is not cross-free: " + std::to_string(cw))`, `no(buf)`.

- [`case_483`](#function-336) ([unittest.cpp:483](../unittest.cpp#L483)): direct C++ call; `no("cannot read examples/Kodak-4X-1.png")`, `no("s is ~0, so this proves nothing: range " + std::to_string(hi - lo) + " against the image's " + std::to_string(uhi - ulo))`, `no(buf)`.

- [`case_522`](#function-337) ([unittest.cpp:522](../unittest.cpp#L522)): direct C++ call; `no("p + (u - p) differs from u by " + std::to_string(worst) + ", far more than float rounding on values up to " + std::to_string(scale))`, `no("mean moved: " + std::to_string(du) + " -> " + std::to_string(dp))`.

- [`case_568`](#function-338) ([unittest.cpp:568](../unittest.cpp#L568)): direct C++ call; `no("bin 0 of " + std::to_string(n) + " is not DC")`, `no("Nyquist bin of " + std::to_string(n) + " is not +0.5")`, `no("bin " + std::to_string(i) + " of " + std::to_string(n) + " is not the negative of bin " + std::to_string(n - i))`, `no("bin " + std::to_string(i) + " of " + std::to_string(n) + " is past Nyquist at " + std::to_string(f))`, `no("lower half bin " + std::to_string(i) + " is not positive")`, `no("upper half bin " + std::to_string(i) + " is not negative")`.

- [`case_603`](#function-339) ([unittest.cpp:603](../unittest.cpp#L603)): direct C++ call; `no(std::string(which) + ": order does not cover the field")`, `no(std::string(which) + ": ranks do not ascend along the order")`, `no(std::string(which) + ": order is not sorted by the input")`, `no("signed: range is [" + std::to_string(lo) + ", " + std::to_string(hi) + "], not [-1, 1]")`, `no("unit: range is [" + std::to_string(lo) + ", " + std::to_string(hi) + "], not the half-open (0, 1)")`, `no("unit: touches an endpoint, which indexes off a bin table")`.

- [`case_655`](#function-340) ([unittest.cpp:655](../unittest.cpp#L655)): direct C++ call; `no("wrong size or type back")`, `no("emitted 255, which no frac can exceed")`, `no("interior value " + std::to_string(v) + " has " + std::to_string(hist[v]) + " pixels, expected about " + std::to_string(share))`, `no("end value " + std::to_string(v) + " has " + std::to_string(hist[v]) + " pixels, expected about " + std::to_string(share / 2.0))`, `no("frac " + std::to_string(frac) + " fired " + std::to_string(fired) + " of " + std::to_string(n) + ", expected about " + std::to_string(frac * n))`, `no("mask is not monotone in the supplied rank order")`, `no("endpoints are not 0 and 254")`.

- [`case_715`](#function-341) ([unittest.cpp:715](../unittest.cpp#L715)): direct C++ call; `no("wrong type or size back")`, `no("a ramp leaves " + std::to_string(interior) + " grey levels in the interior, where it should leave none")`, `no("reflecting leaves a tilt of " + std::to_string(tr) + " against wrapping's " + std::to_string(tw) + "; reflecting is supposed to be the smaller by far")`, `no("highpass off altered a value")`, `no("scale 0.5 gave " + std::to_string(half.cols) + " columns")`.

- [`case_792`](#function-342) ([unittest.cpp:792](../unittest.cpp#L792)): direct C++ call; `no("border differs from an exactly circular filter by " + std::to_string(border) + "; 4 sigma of wrap padding should hold it under 1e-3")`, `no("interior differs by " + std::to_string(interior))`, `no("px <= 1 should be a no-op and is not")`.

- [`case_857`](#function-343) ([unittest.cpp:857](../unittest.cpp#L857)): direct C++ call; `no("wrong length back")`, `no("did not recover the planted zero-cost path")`, `no("path does not close: starts at " + std::to_string(got.front()) + ", ends at " + std::to_string(got.back()))`, `no("path left the band")`, `no("path is not connected")`, `no("not deterministic")`, `no("degenerate width-1 band")`.

- [`case_902`](#function-344) ([unittest.cpp:902](../unittest.cpp#L902)): direct C++ call; `no("wrong size or type back")`, `no("the uncut crop does not even have a seam to fix (" + std::to_string(before) + "), so this proves nothing")`, `no("wrap is " + std::to_string(after) + "x the interior difference after cutting; it should join like texture")`, `no("pixel (" + std::to_string(y) + "," + std::to_string(x) + ") is neither candidate -- it has been averaged")`, `no("axis 0 is not axis 1 transposed")`, `no("overlap 0 is not the plain crop")`.

- [`case_990`](#function-346) ([unittest.cpp:990](../unittest.cpp#L990)): direct C++ call; `no(where + " derived " + std::to_string(w) + "x" + std::to_string(h) + ", past the limit " + std::to_string(limW) + "x" + std::to_string(limH))`, `no(where + " derived a degenerate " + std::to_string(w) + "x" + std::to_string(h))`, `no(std::string(g.name) + " clamped against an unread sample")`.

- [`case_1044`](#function-347) ([unittest.cpp:1044](../unittest.cpp#L1044)): direct C++ call; `no("supersample=2.9 was silently rounded")`, `no("a derived " + std::to_string(v) + " did not read as 4")`, `no("the largest permitted seed did not survive the trip")`.

- [`case_1080`](#function-348) ([unittest.cpp:1080](../unittest.cpp#L1080)): direct C++ call; `no("blue.sigma " + std::to_string(base) + " and " + std::to_string(base + step) + " share a cache key, so one " "run would be served the other's mask")`, `no("value_of got long: " + P.value_of(bluenoise::keys::Sigma) + " -- the fix belongs in serialization, not in the table")`, `no("no type tag: " + text)`, `no("serialize/parse lost " + text)`.

- [`case_1145`](#function-350) ([unittest.cpp:1145](../unittest.cpp#L1145)): direct C++ call; `no(std::string(g.name) + ": could not build a key")`, `no(std::string(g.name) + ": changing " + key + " from " + was + " to " + after.value_of(key) + " left the cache key identical")`, `no("swept only " + std::to_string(swept) + " parameters across " + std::to_string(generators().size()) + " generators")`.

- [`case_1235`](#function-351) ([unittest.cpp:1235](../unittest.cpp#L1235)): direct C++ call; `no("blue failed to build for " + key)`, `no(key + " is declared intermediate, but changing it from " + before.value_of(key) + " to " + after.value_of(key) + " changed the mask while leaving the cache key identical -- " "that is a stale-hi...`, `no("only " + std::to_string(checked) + " intermediates checked")`.

- [`case_1292`](#function-352) ([unittest.cpp:1292](../unittest.cpp#L1292)): direct C++ call; `no("could not key a plain markov run")`, `no(key + " is not declared an intermediate, so every build " "that re-measures it would miss the cache")`, `no("no applicable rate to test with")`, `no("a re-measured rate invalidated the cache")`, `no("could not key the alternate sample")`, `no("a different sample gave the same key")`, `no("an unreadable sample produced a usable key")`.

- [`case_1360`](#function-354) ([unittest.cpp:1360](../unittest.cpp#L1360)): direct C++ call; `no("expected the high-pass to manufacture detail, got " + std::to_string(made) + " distinct values")`, `no("quantise left a value off the grid")`, `no("step 0 changed the field")`.

- [`case_1389`](#function-355) ([unittest.cpp:1389](../unittest.cpp#L1389)): direct C++ call; `no("a constant field should read 1.0")`, `no("256 distinct values should read 1/256")`, `no("a two-level sample should read as mostly ties, got " + std::to_string(flip))`.

- [`case_1411`](#function-356) ([unittest.cpp:1411](../unittest.cpp#L1411)): direct C++ call; `no("not a full permutation")`, `no("not a permutation")`, `no("not sorted by value")`, `no("stable ranking should order a tie group by pixel index, got " + std::to_string(stable))`, `no("random ranking still correlates with pixel index at " + std::to_string(shuffled))`.

- [`case_1461`](#function-357) ([unittest.cpp:1461](../unittest.cpp#L1461)): direct C++ call; `no("same seed gave a different order")`, `no("different seeds gave the same order")`.

- [`case_1473`](#function-358) ([unittest.cpp:1473](../unittest.cpp#L1473)): direct C++ call; `no("a featureless sample produced a mask")`, `no("a smooth gradient produced a mask")`, `no("a textured sample was refused")`, `no("wrong size or type")`, `no("a featureless sample passed with quantising off")`, `no("a textured sample was refused with quantising off")`.

- [`budget_holds`](#function-360) ([unittest.cpp:1545](../unittest.cpp#L1545)): direct C++ call; `no("wrong frame count")`, `no("frames are not CV_8U")`, `no(buf)`.

**CLI invocations / variants:** [U](#scenario-u). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Returns false and preserves detail including empty text.

**Existing direct C++ test callers:** [`case_67`](#function-321), [`case_93`](#function-322), [`case_123`](#function-323), [`case_158`](#function-324), [`case_186`](#function-325), [`case_209`](#function-326), [`case_253`](#function-328), [`case_306`](#function-329), [`case_344`](#function-330), [`case_369`](#function-331), [`case_396`](#function-332), [`case_451`](#function-335), [`case_483`](#function-336), [`case_522`](#function-337), [`case_568`](#function-338), [`case_603`](#function-339), [`case_655`](#function-340), [`case_715`](#function-341), [`case_792`](#function-342), [`case_857`](#function-343), [`case_902`](#function-344), [`case_990`](#function-346), [`case_1044`](#function-347), [`case_1080`](#function-348), [`case_1145`](#function-350), [`case_1235`](#function-351), [`case_1292`](#function-352), [`case_1360`](#function-354), [`case_1389`](#function-355), [`case_1411`](#function-356), [`case_1461`](#function-357), [`case_1473`](#function-358). These do not establish full branch coverage.

**Integration tests should exist:** [U](#scenario-u) — Run the standalone test executable on a known ramp and missing input; a mutation/throwing-case fixture must produce FAIL and nonzero status while later cases still run.

<a id="function-321"></a>

### `case_67` — line 67

Source: [unittest.cpp:67](../unittest.cpp#L67). Signature: `CASE("make_levels: rungs ascend and span the range, every spacing and count")`.

**Callers and how they call it**

- [`main`](#function-361) ([unittest.cpp:1599](../unittest.cpp#L1599)): indirect: Case::run via std::function; CASE macro registers callback before main.

**CLI invocations / variants:** [U](#scenario-u). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Preserve the existing assertion contract: make_levels: rungs ascend and span the range, every spacing and count. Add a controlled mutation that violates the invariant and verify this case fails; extend boundary fixtures rather than testing the test line-for-line.

**Integration tests should exist:** [U](#scenario-u) — Run the standalone test executable on a known ramp and missing input; a mutation/throwing-case fixture must produce FAIL and nonzero status while later cases still run.

<a id="function-322"></a>

### `case_93` — line 93

Source: [unittest.cpp:93](../unittest.cpp#L93). Signature: `CASE("make_levels: uniform is never set on a ladder that is not even")`.

**Callers and how they call it**

- [`main`](#function-361) ([unittest.cpp:1599](../unittest.cpp#L1599)): indirect: Case::run via std::function; CASE macro registers callback before main.

**CLI invocations / variants:** [U](#scenario-u). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Preserve the existing assertion contract: make_levels: `uniform` is never set on a ladder that is not even. Add a controlled mutation that violates the invariant and verify this case fails; extend boundary fixtures rather than testing the test line-for-line.

**Integration tests should exist:** [U](#scenario-u) — Run the standalone test executable on a known ramp and missing input; a mutation/throwing-case fixture must produce FAIL and nonzero status while later cases still run.

<a id="function-323"></a>

### `case_123` — line 123

Source: [unittest.cpp:123](../unittest.cpp#L123). Signature: `CASE("make_levels: rungs share an 8-bit code only where 8 bits cannot separate them")`.

**Callers and how they call it**

- [`main`](#function-361) ([unittest.cpp:1599](../unittest.cpp#L1599)): indirect: Case::run via std::function; CASE macro registers callback before main.

**CLI invocations / variants:** [U](#scenario-u). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Preserve the existing assertion contract: make_levels: rungs share an 8-bit code only where 8 bits cannot separate them. Add a controlled mutation that violates the invariant and verify this case fails; extend boundary fixtures rather than testing the test line-for-line.

**Integration tests should exist:** [U](#scenario-u) — Run the standalone test executable on a known ramp and missing input; a mutation/throwing-case fixture must produce FAIL and nonzero status while later cases still run.

<a id="function-324"></a>

### `case_158` — line 158

Source: [unittest.cpp:158](../unittest.cpp#L158). Signature: `CASE("make_levels: device and srgb differ exactly when levels-1 does not divide 255")`.

**Callers and how they call it**

- [`main`](#function-361) ([unittest.cpp:1599](../unittest.cpp#L1599)): indirect: Case::run via std::function; CASE macro registers callback before main.

**CLI invocations / variants:** [U](#scenario-u). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Preserve the existing assertion contract: make_levels: device and srgb differ exactly when levels-1 does not divide 255. Add a controlled mutation that violates the invariant and verify this case fails; extend boundary fixtures rather than testing the test line-for-line.

**Integration tests should exist:** [U](#scenario-u) — Run the standalone test executable on a known ramp and missing input; a mutation/throwing-case fixture must produce FAIL and nonzero status while later cases still run.

<a id="function-325"></a>

### `case_186` — line 186

Source: [unittest.cpp:186](../unittest.cpp#L186). Signature: `CASE("to_gray: keeps the bit depth, and never rescales by content")`.

**Callers and how they call it**

- [`main`](#function-361) ([unittest.cpp:1599](../unittest.cpp#L1599)): indirect: Case::run via std::function; CASE macro registers callback before main.

**CLI invocations / variants:** [U](#scenario-u). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Preserve the existing assertion contract: to_gray: keeps the bit depth, and never rescales by content. Add a controlled mutation that violates the invariant and verify this case fails; extend boundary fixtures rather than testing the test line-for-line.

**Integration tests should exist:** [U](#scenario-u) — Run the standalone test executable on a known ramp and missing input; a mutation/throwing-case fixture must produce FAIL and nonzero status while later cases still run.

<a id="function-326"></a>

### `case_209` — line 209

Source: [unittest.cpp:209](../unittest.cpp#L209). Signature: `CASE("decode_gray: 16-bit is exact, and agrees with 8-bit where the codes coincide")`.

**Callers and how they call it**

- [`main`](#function-361) ([unittest.cpp:1599](../unittest.cpp#L1599)): indirect: Case::run via std::function; CASE macro registers callback before main.

**CLI invocations / variants:** [U](#scenario-u). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Preserve the existing assertion contract: decode_gray: 16-bit is exact, and agrees with 8-bit where the codes coincide. Add a controlled mutation that violates the invariant and verify this case fails; extend boundary fixtures rather than testing the test line-for-line.

**Integration tests should exist:** [U](#scenario-u) — Run the standalone test executable on a known ramp and missing input; a mutation/throwing-case fixture must produce FAIL and nonzero status while later cases still run.

<a id="function-327"></a>

### `shallow_ramp16` — line 242

Source: [unittest.cpp:242](../unittest.cpp#L242). Signature: `shallow_ramp16(int w, int h)`.

**Callers and how they call it**

- [`case_253`](#function-328) ([unittest.cpp:253](../unittest.cpp#L253)): direct C++ call; `shallow_ramp16(512, 256)`.

- [`main`](#function-361) ([unittest.cpp:1599](../unittest.cpp#L1599)): direct C++ call; `shallow_ramp16(256, 256)`.

**CLI invocations / variants:** [U](#scenario-u). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** CV_16U shape and intended fine increments; includes sub-8-bit differences and stays in range.

**Existing direct C++ test callers:** [`case_253`](#function-328). These do not establish full branch coverage.

**Integration tests should exist:** [U](#scenario-u) — Run the standalone test executable on a known ramp and missing input; a mutation/throwing-case fixture must produce FAIL and nonzero status while later cases still run.

<a id="function-328"></a>

### `case_253` — line 253

Source: [unittest.cpp:253](../unittest.cpp#L253). Signature: `CASE("16-bit input is exploited, not merely accepted")`.

**Callers and how they call it**

- [`main`](#function-361) ([unittest.cpp:1599](../unittest.cpp#L1599)): indirect: Case::run via std::function; CASE macro registers callback before main.

**CLI invocations / variants:** [U](#scenario-u). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Preserve the existing assertion contract: 16-bit input is exploited, not merely accepted. Add a controlled mutation that violates the invariant and verify this case fails; extend boundary fixtures rather than testing the test line-for-line.

**Integration tests should exist:** [U](#scenario-u) — Run the standalone test executable on a known ramp and missing input; a mutation/throwing-case fixture must produce FAIL and nonzero status while later cases still run.

<a id="function-329"></a>

### `case_306` — line 306

Source: [unittest.cpp:306](../unittest.cpp#L306). Signature: `CASE("a parameter struct's defaults are the registry's")`.

**Callers and how they call it**

- [`main`](#function-361) ([unittest.cpp:1599](../unittest.cpp#L1599)): indirect: Case::run via std::function; CASE macro registers callback before main.

**CLI invocations / variants:** [U](#scenario-u). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Preserve the existing assertion contract: a parameter struct's defaults are the registry's. Add a controlled mutation that violates the invariant and verify this case fails; extend boundary fixtures rather than testing the test line-for-line.

**Integration tests should exist:** [U](#scenario-u) — Run the standalone test executable on a known ramp and missing input; a mutation/throwing-case fixture must produce FAIL and nonzero status while later cases still run.

<a id="function-330"></a>

### `case_344` — line 344

Source: [unittest.cpp:344](../unittest.cpp#L344). Signature: `CASE("threshold_byte: spans 0..254 and a cell is the rank at its own quantile")`.

**Callers and how they call it**

- [`main`](#function-361) ([unittest.cpp:1599](../unittest.cpp#L1599)): indirect: Case::run via std::function; CASE macro registers callback before main.

**CLI invocations / variants:** [U](#scenario-u). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Preserve the existing assertion contract: threshold_byte: spans 0..254 and a cell is the rank at its own quantile. Add a controlled mutation that violates the invariant and verify this case fails; extend boundary fixtures rather than testing the test line-for-line.

**Integration tests should exist:** [U](#scenario-u) — Run the standalone test executable on a known ramp and missing input; a mutation/throwing-case fixture must produce FAIL and nonzero status while later cases still run.

<a id="function-331"></a>

### `case_369` — line 369

Source: [unittest.cpp:369](../unittest.cpp#L369). Signature: `CASE("rank_order: a permutation, and stable on ties")`.

**Callers and how they call it**

- [`main`](#function-361) ([unittest.cpp:1599](../unittest.cpp#L1599)): indirect: Case::run via std::function; CASE macro registers callback before main.

**CLI invocations / variants:** [U](#scenario-u). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Preserve the existing assertion contract: rank_order: a permutation, and stable on ties. Add a controlled mutation that violates the invariant and verify this case fails; extend boundary fixtures rather than testing the test line-for-line.

**Integration tests should exist:** [U](#scenario-u) — Run the standalone test executable on a known ramp and missing input; a mutation/throwing-case fixture must produce FAIL and nonzero status while later cases still run.

<a id="function-332"></a>

### `case_396` — line 396

Source: [unittest.cpp:396](../unittest.cpp#L396). Signature: `CASE("wrap_near and wrap_any agree wherever wrap_near is defined")`.

**Callers and how they call it**

- [`main`](#function-361) ([unittest.cpp:1599](../unittest.cpp#L1599)): indirect: Case::run via std::function; CASE macro registers callback before main.

**CLI invocations / variants:** [U](#scenario-u). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Preserve the existing assertion contract: wrap_near and wrap_any agree wherever wrap_near is defined. Add a controlled mutation that violates the invariant and verify this case fails; extend boundary fixtures rather than testing the test line-for-line.

**Integration tests should exist:** [U](#scenario-u) — Run the standalone test executable on a known ramp and missing input; a mutation/throwing-case fixture must produce FAIL and nonzero status while later cases still run.

<a id="function-333"></a>

### `periodic_field` — line 423

Source: [unittest.cpp:423](../unittest.cpp#L423). Signature: `periodic_field(int n)`.

**Callers and how they call it**

- [`case_451`](#function-335) ([unittest.cpp:451](../unittest.cpp#L451)): direct C++ call; `periodic_field(n)`.

- [`case_522`](#function-337) ([unittest.cpp:522](../unittest.cpp#L522)): direct C++ call; `periodic_field(64)`.

**CLI invocations / variants:** [U](#scenario-u). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Manufactured field tiles and has expected frequencies/amplitude; deterministic creation.

**Existing direct C++ test callers:** [`case_451`](#function-335), [`case_522`](#function-337). These do not establish full branch coverage.

**Integration tests should exist:** [U](#scenario-u) — Run the standalone test executable on a known ramp and missing input; a mutation/throwing-case fixture must produce FAIL and nonzero status while later cases still run.

<a id="function-334"></a>

### `cross_ratio` — line 435

Source: [unittest.cpp:435](../unittest.cpp#L435). Signature: `cross_ratio(const cv::Mat& f)`.

**Callers and how they call it**

- [`case_451`](#function-335) ([unittest.cpp:451](../unittest.cpp#L451)): direct C++ call; `cross_ratio(w)`, `cross_ratio(u)`, `cross_ratio(p)`.

**CLI invocations / variants:** [U](#scenario-u). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Known periodic field has low cross energy; injected boundary jump increases ratio; zero-energy handling.

**Existing direct C++ test callers:** [`case_451`](#function-335). These do not establish full branch coverage.

**Integration tests should exist:** [U](#scenario-u) — Run the standalone test executable on a known ramp and missing input; a mutation/throwing-case fixture must produce FAIL and nonzero status while later cases still run.

<a id="function-335"></a>

### `case_451` — line 451

Source: [unittest.cpp:451](../unittest.cpp#L451). Signature: `CASE("periodic_component: removes the DFT cross artifact, and only that")`.

**Callers and how they call it**

- [`main`](#function-361) ([unittest.cpp:1599](../unittest.cpp#L1599)): indirect: Case::run via std::function; CASE macro registers callback before main.

**CLI invocations / variants:** [U](#scenario-u). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Preserve the existing assertion contract: periodic_component: removes the DFT cross artifact, and only that. Add a controlled mutation that violates the invariant and verify this case fails; extend boundary fixtures rather than testing the test line-for-line.

**Integration tests should exist:** [U](#scenario-u) — Run the standalone test executable on a known ramp and missing input; a mutation/throwing-case fixture must produce FAIL and nonzero status while later cases still run.

<a id="function-336"></a>

### `case_483` — line 483

Source: [unittest.cpp:483](../unittest.cpp#L483). Signature: `CASE("periodic_component: s is harmonic inside, which is what makes it smooth")`.

**Callers and how they call it**

- [`main`](#function-361) ([unittest.cpp:1599](../unittest.cpp#L1599)): indirect: Case::run via std::function; CASE macro registers callback before main.

**CLI invocations / variants:** [U](#scenario-u). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Preserve the existing assertion contract: periodic_component: s is harmonic inside, which is what makes it smooth. Add a controlled mutation that violates the invariant and verify this case fails; extend boundary fixtures rather than testing the test line-for-line.

**Integration tests should exist:** [U](#scenario-u) — Run the standalone test executable on a known ramp and missing input; a mutation/throwing-case fixture must produce FAIL and nonzero status while later cases still run.

<a id="function-337"></a>

### `case_522` — line 522

Source: [unittest.cpp:522](../unittest.cpp#L522). Signature: `CASE("periodic_component: the mean is untouched, and p + (u - p) rounds back")`.

**Callers and how they call it**

- [`main`](#function-361) ([unittest.cpp:1599](../unittest.cpp#L1599)): indirect: Case::run via std::function; CASE macro registers callback before main.

**CLI invocations / variants:** [U](#scenario-u). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Preserve the existing assertion contract: periodic_component: the mean is untouched, and p + (u - p) rounds back. Add a controlled mutation that violates the invariant and verify this case fails; extend boundary fixtures rather than testing the test line-for-line.

**Integration tests should exist:** [U](#scenario-u) — Run the standalone test executable on a known ramp and missing input; a mutation/throwing-case fixture must produce FAIL and nonzero status while later cases still run.

<a id="function-338"></a>

### `case_568` — line 568

Source: [unittest.cpp:568](../unittest.cpp#L568). Signature: `CASE("bin_freq: the upper half of a row holds the negative frequencies")`.

**Callers and how they call it**

- [`main`](#function-361) ([unittest.cpp:1599](../unittest.cpp#L1599)): indirect: Case::run via std::function; CASE macro registers callback before main.

**CLI invocations / variants:** [U](#scenario-u). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Preserve the existing assertion contract: bin_freq: the upper half of a row holds the negative frequencies. Add a controlled mutation that violates the invariant and verify this case fails; extend boundary fixtures rather than testing the test line-for-line.

**Integration tests should exist:** [U](#scenario-u) — Run the standalone test executable on a known ramp and missing input; a mutation/throwing-case fixture must produce FAIL and nonzero status while later cases still run.

<a id="function-339"></a>

### `case_603` — line 603

Source: [unittest.cpp:603](../unittest.cpp#L603). Signature: `CASE("rank_uniform_unit and _signed: order-preserving onto their stated range")`.

**Callers and how they call it**

- [`main`](#function-361) ([unittest.cpp:1599](../unittest.cpp#L1599)): indirect: Case::run via std::function; CASE macro registers callback before main.

**CLI invocations / variants:** [U](#scenario-u). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Preserve the existing assertion contract: rank_uniform_unit and _signed: order-preserving onto their stated range. Add a controlled mutation that violates the invariant and verify this case fails; extend boundary fixtures rather than testing the test line-for-line.

**Integration tests should exist:** [U](#scenario-u) — Run the standalone test executable on a known ramp and missing input; a mutation/throwing-case fixture must produce FAIL and nonzero status while later cases still run.

<a id="function-340"></a>

### `case_655` — line 655

Source: [unittest.cpp:655](../unittest.cpp#L655). Signature: `CASE("threshold_mask: a threshold fires with exactly the probability asked of it")`.

**Callers and how they call it**

- [`main`](#function-361) ([unittest.cpp:1599](../unittest.cpp#L1599)): indirect: Case::run via std::function; CASE macro registers callback before main.

**CLI invocations / variants:** [U](#scenario-u). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Preserve the existing assertion contract: threshold_mask: a threshold fires with exactly the probability asked of it. Add a controlled mutation that violates the invariant and verify this case fails; extend boundary fixtures rather than testing the test line-for-line.

**Integration tests should exist:** [U](#scenario-u) — Run the standalone test executable on a known ramp and missing input; a mutation/throwing-case fixture must produce FAIL and nonzero status while later cases still run.

<a id="function-341"></a>

### `case_715` — line 715

Source: [unittest.cpp:715](../unittest.cpp#L715). Signature: `CASE("prepare_texture_sample: reflecting keeps a gradient's edge error small")`.

**Callers and how they call it**

- [`main`](#function-361) ([unittest.cpp:1599](../unittest.cpp#L1599)): indirect: Case::run via std::function; CASE macro registers callback before main.

**CLI invocations / variants:** [U](#scenario-u). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Preserve the existing assertion contract: prepare_texture_sample: reflecting keeps a gradient's edge error small. Add a controlled mutation that violates the invariant and verify this case fails; extend boundary fixtures rather than testing the test line-for-line.

**Integration tests should exist:** [U](#scenario-u) — Run the standalone test executable on a known ramp and missing input; a mutation/throwing-case fixture must produce FAIL and nonzero status while later cases still run.

#### Anonymous function: `case_715::tilt@761`

Source: [unittest.cpp:761](../unittest.cpp#L761); capture/signature: `[n](const cv::Mat& f)`.

- **Callers/how:** case_715 or the standard algorithm it calls invokes this test-local callback.

- **CLI invocations:** [U](#scenario-u); same enclosing branch, not an independently named CLI operation.

- **Unit tests should exist:** Preserve the enclosing case invariant with both passing and intentionally failing input; captures remain alive until invocation.

- **Integration tests should exist:** [U](#scenario-u); verify the enclosing function's observable assertion above with the callback branch actually reached.

<a id="function-342"></a>

### `case_792` — line 792

Source: [unittest.cpp:792](../unittest.cpp#L792). Signature: `CASE("highpass_toroidal: matches an exactly toroidal filter at the border")`.

**Callers and how they call it**

- [`main`](#function-361) ([unittest.cpp:1599](../unittest.cpp#L1599)): indirect: Case::run via std::function; CASE macro registers callback before main.

**CLI invocations / variants:** [U](#scenario-u). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Preserve the existing assertion contract: highpass_toroidal: matches an exactly toroidal filter at the border. Add a controlled mutation that violates the invariant and verify this case fails; extend boundary fixtures rather than testing the test line-for-line.

**Integration tests should exist:** [U](#scenario-u) — Run the standalone test executable on a known ramp and missing input; a mutation/throwing-case fixture must produce FAIL and nonzero status while later cases still run.

<a id="function-343"></a>

### `case_857` — line 857

Source: [unittest.cpp:857](../unittest.cpp#L857). Signature: `CASE("mincut_path: connected, closed, and it finds the cheapest route")`.

**Callers and how they call it**

- [`main`](#function-361) ([unittest.cpp:1599](../unittest.cpp#L1599)): indirect: Case::run via std::function; CASE macro registers callback before main.

**CLI invocations / variants:** [U](#scenario-u). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Preserve the existing assertion contract: mincut_path: connected, closed, and it finds the cheapest route. Add a controlled mutation that violates the invariant and verify this case fails; extend boundary fixtures rather than testing the test line-for-line.

**Integration tests should exist:** [U](#scenario-u) — Run the standalone test executable on a known ramp and missing input; a mutation/throwing-case fixture must produce FAIL and nonzero status while later cases still run.

<a id="function-344"></a>

### `case_902` — line 902

Source: [unittest.cpp:902](../unittest.cpp#L902). Signature: `CASE("quilt_axis: joins the wrap without averaging a single pixel")`.

**Callers and how they call it**

- [`main`](#function-361) ([unittest.cpp:1599](../unittest.cpp#L1599)): indirect: Case::run via std::function; CASE macro registers callback before main.

**CLI invocations / variants:** [U](#scenario-u). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Preserve the existing assertion contract: quilt_axis: joins the wrap without averaging a single pixel. Add a controlled mutation that violates the invariant and verify this case fails; extend boundary fixtures rather than testing the test line-for-line.

**Integration tests should exist:** [U](#scenario-u) — Run the standalone test executable on a known ramp and missing input; a mutation/throwing-case fixture must produce FAIL and nonzero status while later cases still run.

#### Anonymous function: `case_902::wrap_over_interior@917`

Source: [unittest.cpp:917](../unittest.cpp#L917); capture/signature: `[](const cv::Mat& f)`.

- **Callers/how:** case_902 or the standard algorithm it calls invokes this test-local callback.

- **CLI invocations:** [U](#scenario-u); same enclosing branch, not an independently named CLI operation.

- **Unit tests should exist:** Preserve the enclosing case invariant with both passing and intentionally failing input; captures remain alive until invocation.

- **Integration tests should exist:** [U](#scenario-u); verify the enclosing function's observable assertion above with the callback branch actually reached.

<a id="function-345"></a>

### `cache_registry` — line 981

Source: [unittest.cpp:981](../unittest.cpp#L981). Signature: `cache_registry()`.

**Callers and how they call it**

- [`case_990`](#function-346) ([unittest.cpp:990](../unittest.cpp#L990)): direct C++ call; `cache_registry()`.

- [`case_1044`](#function-347) ([unittest.cpp:1044](../unittest.cpp#L1044)): direct C++ call; `cache_registry()`.

- [`case_1080`](#function-348) ([unittest.cpp:1080](../unittest.cpp#L1080)): direct C++ call; `cache_registry()`.

- [`configured_registry`](#function-349) ([unittest.cpp:1134](../unittest.cpp#L1134)): direct C++ call; `cache_registry()`.

- [`case_1145`](#function-350) ([unittest.cpp:1145](../unittest.cpp#L1145)): direct C++ call; `cache_registry()`.

- [`case_1235`](#function-351) ([unittest.cpp:1235](../unittest.cpp#L1235)): call in enclosing function / its lambda callback; `cache_registry()`.

- [`case_1292`](#function-352) ([unittest.cpp:1292](../unittest.cpp#L1292)): direct C++ call; `cache_registry()`.

**CLI invocations / variants:** [U](#scenario-u). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Minimal fixture registers required keys/origins/ranges without production dependencies; independent registries do not share values.

**Existing direct C++ test callers:** [`case_990`](#function-346), [`case_1044`](#function-347), [`case_1080`](#function-348), [`case_1145`](#function-350), [`case_1235`](#function-351), [`case_1292`](#function-352). These do not establish full branch coverage.

**Integration tests should exist:** [U](#scenario-u) — Run the standalone test executable on a known ramp and missing input; a mutation/throwing-case fixture must produce FAIL and nonzero status while later cases still run.

<a id="function-346"></a>

### `case_990` — line 990

Source: [unittest.cpp:990](../unittest.cpp#L990). Signature: `CASE("mask size: a derived size never exceeds what the sample can supply")`.

**Callers and how they call it**

- [`main`](#function-361) ([unittest.cpp:1599](../unittest.cpp#L1599)): indirect: Case::run via std::function; CASE macro registers callback before main.

**CLI invocations / variants:** [U](#scenario-u). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Preserve the existing assertion contract: mask size: a derived size never exceeds what the sample can supply. Add a controlled mutation that violates the invariant and verify this case fails; extend boundary fixtures rather than testing the test line-for-line.

**Integration tests should exist:** [U](#scenario-u) — Run the standalone test executable on a known ramp and missing input; a mutation/throwing-case fixture must produce FAIL and nonzero status while later cases still run.

<a id="function-347"></a>

### `case_1044` — line 1044

Source: [unittest.cpp:1044](../unittest.cpp#L1044). Signature: `CASE("integer parameters: a fractional value is refused, a derived hair is not")`.

**Callers and how they call it**

- [`main`](#function-361) ([unittest.cpp:1599](../unittest.cpp#L1599)): indirect: Case::run via std::function; CASE macro registers callback before main.

**CLI invocations / variants:** [U](#scenario-u). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Preserve the existing assertion contract: integer parameters: a fractional value is refused, a derived hair is not. Add a controlled mutation that violates the invariant and verify this case fails; extend boundary fixtures rather than testing the test line-for-line.

**Integration tests should exist:** [U](#scenario-u) — Run the standalone test executable on a known ramp and missing input; a mutation/throwing-case fixture must produce FAIL and nonzero status while later cases still run.

<a id="function-348"></a>

### `case_1080` — line 1080

Source: [unittest.cpp:1080](../unittest.cpp#L1080). Signature: `CASE("mask cache key: values that differ, however slightly, key differently")`.

**Callers and how they call it**

- [`main`](#function-361) ([unittest.cpp:1599](../unittest.cpp#L1599)): indirect: Case::run via std::function; CASE macro registers callback before main.

**CLI invocations / variants:** [U](#scenario-u). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Preserve the existing assertion contract: mask cache key: values that differ, however slightly, key differently. Add a controlled mutation that violates the invariant and verify this case fails; extend boundary fixtures rather than testing the test line-for-line.

**Integration tests should exist:** [U](#scenario-u) — Run the standalone test executable on a known ramp and missing input; a mutation/throwing-case fixture must produce FAIL and nonzero status while later cases still run.

<a id="function-349"></a>

### `configured_registry` — line 1134

Source: [unittest.cpp:1134](../unittest.cpp#L1134). Signature: `configured_registry(const char* method, const std::string& sample)`.

**Callers and how they call it**

- [`case_1145`](#function-350) ([unittest.cpp:1145](../unittest.cpp#L1145)): direct C++ call; `configured_registry(g.name, sample)`.

**CLI invocations / variants:** [U](#scenario-u). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Every generator's fixture uses matching method, sample and resolved parameters; intended origins established.

**Existing direct C++ test callers:** [`case_1145`](#function-350). These do not establish full branch coverage.

**Integration tests should exist:** [U](#scenario-u) — Run the standalone test executable on a known ramp and missing input; a mutation/throwing-case fixture must produce FAIL and nonzero status while later cases still run.

<a id="function-350"></a>

### `case_1145` — line 1145

Source: [unittest.cpp:1145](../unittest.cpp#L1145). Signature: `CASE("mask cache key: every parameter that applies moves it, for every generator")`.

**Callers and how they call it**

- [`main`](#function-361) ([unittest.cpp:1599](../unittest.cpp#L1599)): indirect: Case::run via std::function; CASE macro registers callback before main.

**CLI invocations / variants:** [U](#scenario-u). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Preserve the existing assertion contract: mask cache key: every parameter that applies moves it, for every generator. Add a controlled mutation that violates the invariant and verify this case fails; extend boundary fixtures rather than testing the test line-for-line.

**Integration tests should exist:** [U](#scenario-u) — Run the standalone test executable on a known ramp and missing input; a mutation/throwing-case fixture must produce FAIL and nonzero status while later cases still run.

#### Anonymous function: `case_1145::attempt@1180`

Source: [unittest.cpp:1180](../unittest.cpp#L1180); capture/signature: `[&](params::Value v)`.

- **Callers/how:** case_1145 or the standard algorithm it calls invokes this test-local callback.

- **CLI invocations:** [U](#scenario-u); same enclosing branch, not an independently named CLI operation.

- **Unit tests should exist:** Preserve the enclosing case invariant with both passing and intentionally failing input; captures remain alive until invocation.

- **Integration tests should exist:** [U](#scenario-u); verify the enclosing function's observable assertion above with the callback branch actually reached.

<a id="function-351"></a>

### `case_1235` — line 1235

Source: [unittest.cpp:1235](../unittest.cpp#L1235). Signature: `CASE("mask cache key: an intermediate left out of it cannot change the mask")`.

**Callers and how they call it**

- [`main`](#function-361) ([unittest.cpp:1599](../unittest.cpp#L1599)): indirect: Case::run via std::function; CASE macro registers callback before main.

**CLI invocations / variants:** [U](#scenario-u). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Preserve the existing assertion contract: mask cache key: an intermediate left out of it cannot change the mask. Add a controlled mutation that violates the invariant and verify this case fails; extend boundary fixtures rather than testing the test line-for-line.

**Integration tests should exist:** [U](#scenario-u) — Run the standalone test executable on a known ramp and missing input; a mutation/throwing-case fixture must produce FAIL and nonzero status while later cases still run.

#### Anonymous function: `case_1235::setup@1253`

Source: [unittest.cpp:1253](../unittest.cpp#L1253); capture/signature: `[&]`.

- **Callers/how:** case_1235 or the standard algorithm it calls invokes this test-local callback.

- **CLI invocations:** [U](#scenario-u); same enclosing branch, not an independently named CLI operation.

- **Unit tests should exist:** Preserve the enclosing case invariant with both passing and intentionally failing input; captures remain alive until invocation.

- **Integration tests should exist:** [U](#scenario-u); verify the enclosing function's observable assertion above with the callback branch actually reached.

<a id="function-352"></a>

### `case_1292` — line 1292

Source: [unittest.cpp:1292](../unittest.cpp#L1292). Signature: `CASE("mask cache key: a rate does not move it, and the sample's contents do")`.

**Callers and how they call it**

- [`main`](#function-361) ([unittest.cpp:1599](../unittest.cpp#L1599)): indirect: Case::run via std::function; CASE macro registers callback before main.

**CLI invocations / variants:** [U](#scenario-u). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Preserve the existing assertion contract: mask cache key: a rate does not move it, and the sample's contents do. Add a controlled mutation that violates the invariant and verify this case fails; extend boundary fixtures rather than testing the test line-for-line.

**Integration tests should exist:** [U](#scenario-u) — Run the standalone test executable on a known ramp and missing input; a mutation/throwing-case fixture must produce FAIL and nonzero status while later cases still run.

<a id="function-353"></a>

### `manufactured_detail_field` — line 1345

Source: [unittest.cpp:1345](../unittest.cpp#L1345). Signature: `manufactured_detail_field()`.

**Callers and how they call it**

- [`case_1360`](#function-354) ([unittest.cpp:1360](../unittest.cpp#L1360)): direct C++ call; `manufactured_detail_field()`.

- [`case_1389`](#function-355) ([unittest.cpp:1389](../unittest.cpp#L1389)): direct C++ call; `manufactured_detail_field()`.

- [`case_1411`](#function-356) ([unittest.cpp:1411](../unittest.cpp#L1411)): direct C++ call; `manufactured_detail_field()`.

- [`case_1461`](#function-357) ([unittest.cpp:1461](../unittest.cpp#L1461)): direct C++ call; `manufactured_detail_field()`.

**CLI invocations / variants:** [U](#scenario-u). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Deterministic field with known low/high-frequency and quantization structure; dimensions/type/range.

**Existing direct C++ test callers:** [`case_1360`](#function-354), [`case_1389`](#function-355), [`case_1411`](#function-356), [`case_1461`](#function-357). These do not establish full branch coverage.

**Integration tests should exist:** [U](#scenario-u) — Run the standalone test executable on a known ramp and missing input; a mutation/throwing-case fixture must produce FAIL and nonzero status while later cases still run.

<a id="function-354"></a>

### `case_1360` — line 1360

Source: [unittest.cpp:1360](../unittest.cpp#L1360). Signature: `CASE("quantise: a high-pass manufactures detail, and rounding removes it")`.

**Callers and how they call it**

- [`main`](#function-361) ([unittest.cpp:1599](../unittest.cpp#L1599)): indirect: Case::run via std::function; CASE macro registers callback before main.

**CLI invocations / variants:** [U](#scenario-u). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Preserve the existing assertion contract: quantise: a high-pass manufactures detail, and rounding removes it. Add a controlled mutation that violates the invariant and verify this case fails; extend boundary fixtures rather than testing the test line-for-line.

**Integration tests should exist:** [U](#scenario-u) — Run the standalone test executable on a known ramp and missing input; a mutation/throwing-case fixture must produce FAIL and nonzero status while later cases still run.

<a id="function-355"></a>

### `case_1389` — line 1389

Source: [unittest.cpp:1389](../unittest.cpp#L1389). Signature: `CASE("tie_share: 1 for a constant field, 1/n for n distinct values")`.

**Callers and how they call it**

- [`main`](#function-361) ([unittest.cpp:1599](../unittest.cpp#L1599)): indirect: Case::run via std::function; CASE macro registers callback before main.

**CLI invocations / variants:** [U](#scenario-u). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Preserve the existing assertion contract: tie_share: 1 for a constant field, 1/n for n distinct values. Add a controlled mutation that violates the invariant and verify this case fails; extend boundary fixtures rather than testing the test line-for-line.

**Integration tests should exist:** [U](#scenario-u) — Run the standalone test executable on a known ramp and missing input; a mutation/throwing-case fixture must produce FAIL and nonzero status while later cases still run.

<a id="function-356"></a>

### `case_1411` — line 1411

Source: [unittest.cpp:1411](../unittest.cpp#L1411). Signature: `CASE("rank_order_random: a permutation, sorted by value, shuffled within ties")`.

**Callers and how they call it**

- [`main`](#function-361) ([unittest.cpp:1599](../unittest.cpp#L1599)): indirect: Case::run via std::function; CASE macro registers callback before main.

**CLI invocations / variants:** [U](#scenario-u). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Preserve the existing assertion contract: rank_order_random: a permutation, sorted by value, shuffled within ties. Add a controlled mutation that violates the invariant and verify this case fails; extend boundary fixtures rather than testing the test line-for-line.

**Integration tests should exist:** [U](#scenario-u) — Run the standalone test executable on a known ramp and missing input; a mutation/throwing-case fixture must produce FAIL and nonzero status while later cases still run.

#### Anonymous function: `case_1411::index_correlation@1430`

Source: [unittest.cpp:1430](../unittest.cpp#L1430); capture/signature: `[&q](const std::vector<int>& order)`.

- **Callers/how:** case_1411 or the standard algorithm it calls invokes this test-local callback.

- **CLI invocations:** [U](#scenario-u); same enclosing branch, not an independently named CLI operation.

- **Unit tests should exist:** Preserve the enclosing case invariant with both passing and intentionally failing input; captures remain alive until invocation.

- **Integration tests should exist:** [U](#scenario-u); verify the enclosing function's observable assertion above with the callback branch actually reached.

<a id="function-357"></a>

### `case_1461` — line 1461

Source: [unittest.cpp:1461](../unittest.cpp#L1461). Signature: `CASE("rank_order_random: seeded from its argument, never the global rng")`.

**Callers and how they call it**

- [`main`](#function-361) ([unittest.cpp:1599](../unittest.cpp#L1599)): indirect: Case::run via std::function; CASE macro registers callback before main.

**CLI invocations / variants:** [U](#scenario-u). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Preserve the existing assertion contract: rank_order_random: seeded from its argument, never the global rng. Add a controlled mutation that violates the invariant and verify this case fails; extend boundary fixtures rather than testing the test line-for-line.

**Integration tests should exist:** [U](#scenario-u) — Run the standalone test executable on a known ramp and missing input; a mutation/throwing-case fixture must produce FAIL and nonzero status while later cases still run.

<a id="function-358"></a>

### `case_1473` — line 1473

Source: [unittest.cpp:1473](../unittest.cpp#L1473). Signature: `CASE("direct: refuses a sample whose mask would be mostly coin flips")`.

**Callers and how they call it**

- [`main`](#function-361) ([unittest.cpp:1599](../unittest.cpp#L1599)): indirect: Case::run via std::function; CASE macro registers callback before main.

**CLI invocations / variants:** [U](#scenario-u). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Preserve the existing assertion contract: direct: refuses a sample whose mask would be mostly coin flips. Add a controlled mutation that violates the invariant and verify this case fails; extend boundary fixtures rather than testing the test line-for-line.

**Integration tests should exist:** [U](#scenario-u) — Run the standalone test executable on a known ramp and missing input; a mutation/throwing-case fixture must produce FAIL and nonzero status while later cases still run.

<a id="function-359"></a>

### `flat_mask` — line 1524

Source: [unittest.cpp:1524](../unittest.cpp#L1524). Signature: `flat_mask(cv::Size sz, uint64_t seed)`.

**Callers and how they call it**

- [`budget_holds`](#function-360) ([unittest.cpp:1545](../unittest.cpp#L1545)): direct C++ call; `flat_mask({64, 64}, 7)`.

**CLI invocations / variants:** [U](#scenario-u). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Fixed seed, byte range and flat histogram independent of global rng; requested dimensions.

**Integration tests should exist:** [U](#scenario-u) — Run the standalone test executable on a known ramp and missing input; a mutation/throwing-case fixture must produce FAIL and nonzero status while later cases still run.

<a id="function-360"></a>

### `budget_holds` — line 1545

Source: [unittest.cpp:1545](../unittest.cpp#L1545). Signature: `budget_holds(const cv::Mat& image, const BudgetCase& c)`.

**Callers and how they call it**

- [`main`](#function-361) ([unittest.cpp:1599](../unittest.cpp#L1599)): call in enclosing function / its lambda callback; `budget_holds(in, c)`.

**CLI invocations / variants:** [U](#scenario-u). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Use a deliberately incorrect frame sum to verify failure detection; exact round rule versus bounded stochastic rule; 8/16-bit and all ladders.

**Integration tests should exist:** [U](#scenario-u) — Run the standalone test executable on a known ramp and missing input; a mutation/throwing-case fixture must produce FAIL and nonzero status while later cases still run.

<a id="function-361"></a>

### `main` — line 1599

Source: [unittest.cpp:1599](../unittest.cpp#L1599). Signature: `main(int argc, char** argv)`.

**Callers and how they call it**

- Process runtime invokes this executable entry point with argc/argv; it is not called by another project function.

**CLI invocations / variants:** [U](#scenario-u). IDs expand to complete commands and assertions in the scenario table above; private branches may require the listed failure fixtures.

**Unit tests should exist:** Missing fixture fails; registered and dynamically added budget cases all run; false/std/non-std exception cases counted as failures without suppressing subsequent cases; overall status correct.

**Integration tests should exist:** [U](#scenario-u) — Run the standalone test executable on a known ramp and missing input; a mutation/throwing-case fixture must produce FAIL and nonzero status while later cases still run.

#### Anonymous function: `main::callback@1653`

Source: [unittest.cpp:1653](../unittest.cpp#L1653); capture/signature: `[&in, c]`.

- **Callers/how:** unittest main invokes Case::run through std::function.

- **CLI invocations:** [U](#scenario-u); same enclosing branch, not an independently named CLI operation.

- **Unit tests should exist:** Preserve the enclosing case invariant with both passing and intentionally failing input; captures remain alive until invocation.

- **Integration tests should exist:** [U](#scenario-u); verify the enclosing function's observable assertion above with the callback branch actually reached.

## Inventory audit and maintenance

Inventory contains **365 named function/API entries**, **61 anonymous function entries**, and **136 header declaration links**, across **48 project source/header files**. The named count includes the abstract/defaulted DitherFn interface, two explicitly deleted StackFrame operations, and the unmatched calibrate_markov declaration; it is not a count of executable definitions. Overloads have separate source entries.

When a function, callback, signature, generator-table row or command-line flag changes, update its entry and its callers. Keep a small public-header compile/link suite alongside runtime tests. Measure executed line/branch coverage separately, and inspect uncovered branches before removing planned tests. The source line anchors in this snapshot may move as the project evolves.
