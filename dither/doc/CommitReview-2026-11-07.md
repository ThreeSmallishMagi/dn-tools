# Commit review: 20 October–7 November 2026

Reviewed the 28 project commits from `dff818c` through `255c9e5`, inclusive. “Since 20 October” is interpreted as starting at 2026-10-20 00:00 UTC. The final two commits arrived while the review was running; both change documentation only and are included below. Uncommitted edits to `aliases` and `.gitignore`, and untracked files, are outside this review.

**Five actionable findings remain at the reviewed HEAD.** One affects production mask generation, one affects the separate temporal demonstration, one invalidates a research-tool comparison, and two weaken newly added tests. All were checked against current source; focused reproductions are included. No production source was changed.

The Release build succeeded with GCC 13.3/OpenCV 4.6. The complete `test/run` suite passed **16/16 groups**, including **70/70 C++ unit cases**. Passing those tests does not cover the conditions below. P2 denotes a functional issue worth fixing; P3 denotes lower-impact tooling or test coverage defects. No P1 finding was established.

## 1. P2 — Apply the texture guard in the same coordinates as its cutoff

**Introduced by:** `d883950`.

**Location:** [directmask.cpp](../directmask.cpp), `build_direct_mask`, line 163.

The equalisation path first resizes the raw sample by `scale`, then tests its texture with `prepare_texture_sample(crop, highpass, 1.0)`. That crop is already in output-pixel coordinates, but `highpass` is still in source-pixel coordinates. At scale 4, a 32-pixel source cutoff should cover 128 output pixels. The current check instead filters at 32 output pixels and can discard real grain before computing its tie share. The subtractive path applies its filter before resizing; the new guard therefore does not actually test “what the high-pass would have left.”

**Observed:** a seeded, low-contrast grain fixture passes the old high-pass preparation but fails the default equalisation path at `--mask-scale 4`: exit 255, with a reported **41%** tie share. The correctly scaled guard measures approximately **15%**, below the 25% rejection limit. At scale 8 the erroneous guard reports **87%**, versus approximately **17%** with the scaled cutoff. A separate scratch build changing only this call to use `highpass * scale` accepted the scale-4 fixture and produced its mask.

Reproduce from the project root:

```bash
# B points to a fresh build.
B=/tmp/dither-nov-review-build

dither_env/bin/python - <<'PYCODE'
import cv2, numpy as np
r = np.random.default_rng(177)
f = r.normal(0, 1, (128, 128)).astype('float32')
f = cv2.GaussianBlur(f, (0, 0), 2)
im = np.clip(np.rint(128 + 2 * f / f.std()), 0, 255).astype('uint8')
assert cv2.imwrite('/tmp/review-grain.png', im)
PYCODE

"$B/dither" examples/ramp_256x256.png --no-config \
  --mask-method direct --mask-sample /tmp/review-grain.png \
  --mask-scale 4 --mask-size 256x256 --out /tmp/review-scaled.png
# Fails: approximately 41% ties.
# Add --direct-equalise 0: succeeds.
```

**Fix:** compute the guard from a source-space high-pass followed by the same resize, or correctly transform the cutoff when filtering the resized crop. Preserve the documented high-pass-disabled behavior at cutoffs <=1; blindly multiplying that sentinel by scale would turn filtering back on. Account explicitly for crop-boundary behavior if exact equivalence with the old preparation is required.

**Regression:** run low-contrast but genuinely textured fixtures at scales below and above 1 through both preparations. Test the guard against a reference that filters before resizing, and retain flat-field rejection. Current equalisation tests use scale 1 and miss this.

## 2. P2 — Complete the numeric widening in the temporal demonstration

**Commit reviewed:** `efc8993`, the F10 fix.

**Location:** [temporaldither.cpp](../temporaldither.cpp), line 39, the accepted depths at lines 95–97, and `dither_random` at line 23.

The fix widens the initial product and validates bit depths up to 31, but the result still passes through `float`. At full-scale 31-bit output, 2147483647 rounds to 2147483648. `dither_random` then casts it to `int`, outside the signed range. The other strategies can emit a level above the declared maximum. Both accumulated totals remain 32-bit unsigned, so forty high-depth frames also overflow their accounting.

**Reproduced with a command the new parser accepts:**

```bash
c++ -std=c++17 -fsanitize=undefined,float-cast-overflow \
  -fno-sanitize-recover=all temporaldither.cpp -o /tmp/review-temporal
/tmp/review-temporal 31 31 2147483647 2
# temporaldither.cpp:23: runtime error:
# 2.14748e+09 is outside the range of representable values of type 'int'
```

This is the demonstration executable, **not `dither --frames`**. The production engine's double-precision goal fix passes its new boundary tests. The demo's old shift/product problems have been addressed, but the supported-domain claim is still false; the widened product now delivers the large value to the remaining narrow conversion.

**Fix:** use double throughout the demonstration's threshold arithmetic, avoid the narrow signed cast when extracting a fraction, and use sufficiently wide totals and matching output formats. Alternatively reduce the accepted domain to one that every stage can represent, with explicit bounds.

**Regression:** maximum source value at every supported target depth, all three strategies, and enough frames to exercise total accumulation; assert each output is within the target range, totals do not wrap, and sanitizer runs are clean. `errorcheck` currently checks rejected depths and one ordinary successful case, not the largest accepted depths.

## 3. P2 — Update the seam bench when the production equalisation radius changes

**Introduced by:** `1c4abde`, relative to the bench added in `5affa70`.

**Location:** [test/seamlab](../test/seamlab), `build(..., 'equalise')`, line 914; compare [directmask.cpp](../directmask.cpp), the `direct.equalise` rule at lines 382–390.

The bench labels its `equalise` variant as the production default and uses it for comparison with `--check`. It still calls `local_quantile(raw, 2 * SIG)`. Production was changed to **4 times** the high-pass sigma. With highpass 32 and scale 1, the bench therefore models **11.9936 px**, while the executable uses **23.9872 px**.

This is a different treatment, not the documented difference between exact and coarse-grid computation. The project changed this default specifically because the larger window preserves clumping that the smaller one removes. Comparing the bench's field statistics against the binary's mask now conflates that parameter change with implementation differences and can lead subsequent tuning in the wrong direction. `test/findcrops` was updated to 24; `test/seamlab` was not.

**Fix:** resolve the production parameter for the comparison and pass an explicit identical value to both implementations, or at least update this multiplier and its nearby documentation together. Make the effective sigma visible in the output. The exact and coarse-grid variants can remain separate, provided their parameters agree.

**Regression:** compare the bench's effective sigma with the registry for several high-pass values, including disabled filtering. A pixel-identity assertion between the two algorithms is inappropriate; parameter identity is the missing assertion.

## 4. P2 — Do not silently omit the first declaration in public-header link checks

**Introduced by:** `8e60183`; retained by the concurrent version in `d3cecfb`.

**Location:** [test/apicheck](../test/apicheck), `declared_functions`, lines 99–101 and 129–134.

The scanner removes comments but leaves preprocessor directives. For headers whose first declaration precedes any namespace/body boundary, the first semicolon-delimited statement contains `#pragma once`, the includes, and that declaration. The declaration regex cannot match `#`, so it drops the function. The generated probe then never takes its address, and a missing implementation still links successfully.

**Observed in the passing suite:** `app.h`, `dftcalib.h`, `inpaintcalib.h` and `markovcalib.h` each report **“links 0 declarations”**, despite declaring a free function. `cli.h` misses `parseOptions`; `mask.h` misses `register_mask_params`. This defeats the purpose of adding a broad link check after the stale calibration declaration.

Minimal mutation reproduction:

```bash
python3 - <<'PYCODE'
from pathlib import Path
import runpy, subprocess
m = runpy.run_path('test/apicheck', run_name='review_probe')
h = Path('/tmp/review-api-missing.h')
h.write_text('#pragma once\nint nonexistent_review_api();\n')
_, names, _ = m['declared_functions'](str(h))
print(names)  # []: declaration incorrectly omitted
p = Path('/tmp/review-api-missing.cpp')
p.write_text('#include "review-api-missing.h"\nint main() {}\n')
r = subprocess.run(['c++', str(p), '-o', '/tmp/review-api-missing'])
print(r.returncode)  # 0: no definition exists, but the probe never asks for it
PYCODE
```

**Fix:** at minimum remove preprocessing directives correctly before scanning, and assert expected declaration counts for known fixtures. Prefer compiler-backed declaration discovery or explicit typed link probes over extending an ad-hoc C++ parser indefinitely. Keep overloads/methods that are intentionally unchecked explicitly outside the claimed link coverage.

**Regression:** mutation fixtures with an undefined first declaration after includes/pragma, a declaration inside a namespace, multiline declarations and overloads. A missing first definition must fail the generated link probe.

## 5. P3 — Make the cache-rate poisoning cases actually change a rate

**Introduced by:** `8e60183`.

**Location:** [test/run](../test/run), `cachecheck` / `poison`, lines 300–323.

The “rate of zero” and “negative rate” cases substitute an existing `markov.rate` line. The small calibration/build used to create `good.conf` need not write a rate: calibration can save its results before that key has been resolved, and the subsequent 32×32 build is below the rate-recording time threshold. In that normal case both substitutions do nothing. The function still increments its hostile-entry count, runs the program, and reports success.

**Reproduced:** the baseline command produced exactly three calibration entries (`calib.<hash>`, `.r`, `.w`) and no `markov.rate`. Applying each rate substitution left the file byte-identical. The suite nevertheless reported “9 hostile entries, the same mask from every one.” This is a false-positive test, not evidence that runtime rate validation is currently broken.

**Fix:** insert or replace the poisoned key unconditionally and assert that the intended mutation occurred before running the binary. Seed a valid rate explicitly if the test needs an original value. Inspect the result through a rate-resolution path such as `--budget`/`--explain`, in addition to successful output.

Also correct the cache-isolation comment: changing `XDG_CONFIG_HOME` does not give these runs different mask caches when every explicit `--config` has the same parent; `maskcache::dir_for` uses `<config-parent>/masks`. Calibration runs before mask-cache lookup, so the shared mask cache does **not** by itself bypass calibration. Distinct config directories are needed if independent mask builds are part of the assertion.

**Regression:** deliberately remove the baseline rate and require the poison helper still to insert zero/negative values. Assert the mutated file content, not just the command's output hash.

## Verification and limits

- Built all CMake targets in `/tmp/dither-nov-review-build`. The existing workspace `build/` cache points to an old absolute source directory; this was an environment artifact, not a commit defect. It was left in place.
- Ran the full suite using a scratch test tree pointing at the fresh binaries and current source/assets, because the scripts hardcode `ROOT/build`. All **16 groups passed**: coltest, unittest, tonetest, impliedtest, devicetest, masktest, scalecheck, budgetcheck, shellcheck, outsizecheck, errorcheck, cachecheck, apicheck, requestcheck, flagcheck and samplecheck. The unit group passed **70 cases**.
- Reproduced finding 1 through the actual CLI; a temporary, single-call correction outside the project confirmed the coordinate mismatch. No project fix was applied.
- Ran the temporal demonstration reproduction under undefined-behavior and float-cast-overflow sanitizers. This was a targeted probe, not a sanitizer pass over every generator.
- Reproduced the omitted API declaration with a missing-definition fixture, and verified the rate substitutions were byte-identical no-ops on the actual baseline config.
- Decoded all **14 manifest scans**, checked their depths and dimensions, and verified all **six recorded crop windows** lie within the corresponding images. All shipped scans are 8-bit. I did not re-run the expensive full-frame crop searches or independently repeat every historical grain-quality measurement.
- The two final documentation commits do not change the tested source. New uncommitted work was excluded.

The earlier fixes to sample precision, pooled inpaint ranking, generated flag lookup, string-path typing, mask export reconciliation, budget-rung selection, production temporal budgets, Mat rounding and calibration signatures are present and covered by the passing tests/review. No new confirmed production defect was found in those fixes apart from the separate temporal demonstration issue above.

Two historical changes should not be treated as outstanding findings: `efc8993` initially made `mask.out` part of the cache key, but `7cdf469` excludes it; the polynomial direct-mask preparation from `e72cd68`/`adcddff` was replaced by `d883950`. Reviewing only intermediate commits would incorrectly report those as current problems.

## Commit-by-commit disposition

“Nothing additional confirmed” means no actionable defect established here, not proof of correctness. Imported dated reports were reviewed as historical records, not as instructions describing current behavior.

| Commit | Change / review result |
|---|---|
| `dff818c` | Seamlab explanation of polynomial rounds/robust behavior; documentation only, nothing additional confirmed. |
| `efc8993` | Prior eleven-defect fix: finding 2 remains in the demonstration. Production fixes checked; temporary mask-output cache-key issue subsequently fixed. |
| `8e60183` | New API/error/cache/unit checks: findings 4 and 5. |
| `86885c6` | Additional property tests and pooled-downsample test boundary; passes, nothing additional confirmed. |
| `d3cecfb` | Parallel test execution and isolated mask reuse; full suite passes, finding 4 persists. |
| `357f99b` | Updated recorded coltest output; current coltest passes. Reference text is not itself the test oracle. |
| `23317f5` | Added two scan assets; both decode and are present in the final manifest. |
| `6e2fcaa` | Ignore/helper/working-note cleanup; nothing additional confirmed. |
| `b4952b7` | Ignore/helper cleanup; nothing additional confirmed. |
| `605fc2c` | Imported dated studies and reviews; historical records, no new executable behavior. |
| `921cbcd` | Imported coverage plan/review and use notes; historical records. |
| `e72cd68` | Polynomial direct preparation and tests; inspected as an intermediate implementation, now removed. |
| `adcddff` | Polynomial degree/cost revisions; superseded by local equalisation. |
| `719defd` | Initial crop-search implementation and record; current search/installer inspected, no additional confirmed finding. |
| `d883950` | Local equalisation and pre-transform texture guard: finding 1. |
| `5affa70` | Fractional-value test and seam-bench model; new test passes, bench later diverges in finding 3. |
| `aefffc6` | Equalisation-based crop search/gates; inspected, no additional confirmed finding. |
| `5d16684` | Recorded independent crop checks; no runtime change, measurements not independently repeated. |
| `96cb04b` | Crop choices and new derived assets; final source/window validity checked. |
| `84070d5` | Corrections to crop-search claims; nothing additional confirmed. |
| `1c4abde` | Production sigma multiplier changed to four; finding 3, seam bench left at two. |
| `7cdf469` | Mask export path removed from key; fix is present and exercised by tests. |
| `34a354a` | Whole-scan/crop separation, resolver/cache/installer/test updates; full suite and manifest checks pass. |
| `003109e` | Help/README/cache-key documentation for preceding changes; nothing additional confirmed. |
| `81ce573` | Working notes; no executable change. |
| `2b05ebc` | Ranked crop selection and Kodak-2484 window update; window is valid, full selection search not rerun. |
| `f7bff29` | 4Xcrop definition and housekeeping status; documentation only. |
| `255c9e5` | Document index and removal of duplicate reports; documentation only. |
