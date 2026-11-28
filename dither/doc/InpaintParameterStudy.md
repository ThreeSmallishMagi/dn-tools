# Inpaint mask parameter study

2026-09-27. Source: `dd9b0a26144101a4f2dda85c0335ac1942597e1e`.

## Recommendations

**For the lowest average measured error among the preselected general presets,
use `inpaint.db=4000000` and `inpaint.checks=512`; leave the other generator
controls at their defaults.** This lowered the combined validation error by 7.9%
across all six scans, but only 3.3% across the three scans excluded from tuning.
It is not a universal quality upgrade: the gain was substantial on 4X, AutoRex,
and Panatomic-X, small on Kodak-2484, negligible on Delta3200, and negative on
Kodak-4X-2. These are reductions in statistical error, not perceptual percentages.

```sh
/tmp/inpaint-explore-build/dither examples/m039crop.png --no-config \
  --mask-method inpaint --mask-sample 4Xcrop.png \
  --mask-size 256x256 --mask-scale 1 --mask-highpass 32 \
  --set inpaint.db=4000000 --set inpaint.checks=512 \
  --set inpaint.radius=3 --set inpaint.dims=12 \
  --set inpaint.iters=6 --set inpaint.groups=16 --set inpaint.levels=2 \
  --seed 17 --mask-out inpaint-quality-mask.png --out inpaint-quality.png
```

Build instructions are linked below. The strongest screening preset used the same
four-million-row database with **128** checks; increasing to 512 was a separate
candidate selected for patch fidelity before inspecting validation results. That
512-check candidate subsequently had the best combined mean on the new seeds.
The names `quality` and `shape` in the data preserve those original selection
rules; they are not a claim that the first always beats the second.

**For speed, keep the 250,000-row database and use three sweeps with 64 checks:**

```sh
--set inpaint.db=250000 --set inpaint.iters=3 --set inpaint.checks=64
```

With other defaults unchanged, this took about **5.74 seconds versus 8.39 seconds**
for the default on 4X, a 32% reduction in elapsed time. It particularly helped the
ridge-prone Kodak-4X-2 and Delta scans, but made Kodak-2484's patch error 27% worse.
The existing defaults remain a sensible starting point when the sample is unknown;
the experiments do not justify silently replacing them with one new preset.

## Presets, validation, and cost

Every row uses radius 3, 12 PCA dimensions, 16 update groups, two resolution levels,
highpass 32, scale 1, and a 256x256 mask. The only changes are listed below.

| Preset / data label | Database rows requested | Checks | Sweeps per level | Mean combined ratio, six scans | Three excluded scans | CLI seconds on 4X |
|---|---:|---:|---:|---:|---:|---:|
| Default | 250,000 | 128 | 6 | 1.000 | 1.000 | 8.39 |
| 4M/128 (`quality`) | 4,000,000 | 128 | 6 | 0.949 | 0.987 | 31.77 |
| 4M/512 (`shape`) | 4,000,000 | 512 | 6 | **0.921** | 0.967 | 28.38* |
| Fast | 250,000 | 64 | 3 | 0.963 | **0.895** | 5.74 |
| 1M/64 (`balanced`) | 1,000,000 | 64 | 6 | 0.969 | 0.996 | 15.19* |

Ratios below one are better. The main timings are medians of six serial,
fresh-process CLI calls: two repeat cohorts across seeds 17/123/2026, with the
application cache disabled. They include input/sample loading, fitting, sampling,
dithering a 256x256 ramp, and PNG output. **Asterisks mark single measurements**
at seed 17 taken later, not medians. Do not interpret the one-off 512-check timing
as evidence that more checks run faster than 128. All times are machine-specific;
the larger-database options cost roughly 3–4 times the default in these observations.

The `balanced` candidate met the existing work estimate's default-cost constraint,
but actually took about 1.8 times the default in its serial measurement. Database
construction is expensive, and that estimate should not substitute for timing.

### Validation details

These are means over new seeds **17, 123, and 2026**, not the screening seed 42.
P = patch distance, C = multi-patch clumping error, S = multi-patch spectral error.
All are lower-is-better; their definitions and limitations appear below.

| Sample | Default P | 4M/512 P | Default C | 4M/512 C | Default S | 4M/512 S |
|---|---:|---:|---:|---:|---:|---:|
| 4Xcrop | 11.679 | 8.020 | 0.245 | 0.225 | 0.414 | 0.367 |
| Kodak-2484-1 | 4.180 | 3.702 | 0.208 | 0.235 | 0.391 | 0.355 |
| AutoRex-4X | 13.135 | 10.511 | 0.286 | 0.223 | 0.439 | 0.384 |
| Kodak-4X-2, excluded | 50.197 | 54.206 | 0.287 | 0.284 | 0.673 | 0.659 |
| Delta3200, excluded | 26.913 | 27.140 | 0.180 | 0.180 | 0.702 | 0.690 |
| Panatomic-X, excluded | 7.724 | 6.090 | 0.160 | 0.149 | 0.538 | 0.506 |

Across all six scans, mean per-scan error ratios correspond to reductions of 12.5%
in P, 4.1% in C, and 7.1% in S. Averages conceal the exceptions: Kodak-2484's
clumping worsened, and Kodak-4X-2's patch error worsened. Delta is effectively a tie.
Full means and sample SDs are in
[summary.csv](../experiments/inpaint-20260927/summary.csv). For example, 4X's P was
11.679 +/- 1.553 for the default and 8.020 +/- 0.348 for 4M/512. No perceptual
significance or population confidence interval is implied by those three draws.

The fast preset's P/C/S values were **30.373 / 0.202 / 0.674** on Kodak-4X-2 and
**21.835 / 0.134 / 0.720** on Delta. It improves shape and clumping there while
leaving a poor spectrum. Kodak-4X-2's mid-octave ridgeness is about **+30 real-patch
standard deviations** with default/large-database settings; fast reduces it to
+16.6. Delta's corresponding figure falls from +8.7 to +2.4. Long ridges are visible
in the comparisons. The source header's suggestion that inpaint removes worming
is therefore not a general guarantee. These samples still have substantial
mismatches after tuning, and making the database larger does not solve them.

### Sample-specific choices

- **4Xcrop:** 4M/128 was the best screening choice; 4M/512 performed better on the
  new seeds. Both improve the three multi-patch errors relative to the default.
- **Kodak-2484-1:** `db=1000000`, `dims=14`, `checks=128`, with six sweeps and other
  defaults. It scored **P/C/S = 3.409 / 0.143 / 0.381**, improving all three default
  errors. This is a better clumping compromise than the general 4M/512 preset.
- **AutoRex:** `db=4000000`, `iters=3`, `levels=1`, `checks=128`, with other defaults.
  It scored **7.753 / 0.247 / 0.409**. It improves patch fidelity more than the
  general preset, while the general preset has better clumping and spectral scores.

These choices were selected on each training scan and then checked with new seeds.
They are sample-specific, not evidence that those settings transfer to other scans.

## What matters in the parameter space

At otherwise default settings, the three-training-scan mean combined ratios were:

| Control | Values -> normalized error (screening seed 42) |
|---|---|
| Database | 25k -> 1.250; 100k -> 1.097; 250k -> 1.000; 1M -> 0.959; 2M -> 0.933; 4M -> 0.906 |
| Checks | 16 -> 1.129; 64 -> 1.031; 128 -> 1.000; 512 -> 0.990 |
| Sweeps | 1 -> 1.215; 2 -> 1.087; 3 -> 1.029; 6 -> 1.000; 12 -> 1.058 |
| PCA dimensions | 8 -> 3.006; 10 -> 2.576; 12 -> 1.000; 14 -> 1.016; 16 -> 1.065; 24 -> 1.205 |
| Radius | 2 -> 1.521; 3 -> 1.000; 4 -> 1.398 |
| Update groups | 1 -> 1.191; 4 -> 1.053; 16 -> 1.000; 64 -> 1.021 |
| Resolution levels | 1 -> 1.004; 2 -> 1.000; 3 -> 1.043 |
| Highpass, fixed target 32 | 16 -> 1.649; 32 -> 1.000; 64 -> 1.087 |

The defaults for radius, dimensions, groups, and levels are already well chosen
for the training mix. More sweeps did not help. The larger-database interaction
check did not make radius 4 the winning general model. Four million is the largest
supported database request, and the best screened point reached that boundary;
this says nothing about an unconstrained optimum beyond the supported range.

**Database size is a request, not a count of distinct neighborhoods.** Rows are
sampled with replacement and capped by usable centers at each resolution level.
At a four-million request, 4X actually uses **702,799 coarse and 2,835,549 fine**
rows; Kodak-2484 uses **550,649 and 2,226,249**. AutoRex uses **2,748,960 and
4,000,000**. This matters for both fidelity and runtime, and explains why one
nominal setting does not imply the same model size for every sample.

## Comparison with tuned Markov

This compares the inpaint 4M/512 preset with the earlier study's general Markov-ML
preset (two sweeps, temperature 0.10, reuse 0, 16 bins, 64 pairs). The same input
hashes, reference metrics, and three validation seeds are used.

| Sample | Inpaint patch error | Markov-ML patch error | Inpaint spectral error | Markov-ML spectral error |
|---|---:|---:|---:|---:|
| 4Xcrop | 8.020 | 12.355 | 0.367 | 0.178 |
| Kodak-2484 | 3.702 | 3.732 | 0.355 | 0.138 |
| AutoRex | 10.511 | 6.849 | 0.384 | 0.210 |
| Kodak-4X-2 | 54.206 | 15.835 | 0.659 | 0.303 |
| Delta3200 | 27.140 | 17.121 | 0.690 | 0.294 |
| Panatomic-X | 6.090 | 19.013 | 0.506 | 0.243 |

Inpaint offers better patch fidelity on 4X and Panatomic-X; Kodak-2484 is nearly
a tie. The Markov preset matches spectra better on all six. On Kodak-4X-2 and Delta,
these tested inpaint controls do not close the gap. The general Markov preset was
already known to be weak on Panatomic-X: its sample-specific alternative scored
8.540 / 0.083 / 0.303, so inpaint still wins that comparison on P while losing on
C/S. Neither method is the winner for every property of film grain.

## Checks, timing correction, and visual evidence

**381 distinct sample/setting/seed combinations were scored:** 282 screening
configurations and 99 new-seed validation configurations. Repeated entries across
stages reuse masks. The initial and final validation manifests each contain 96
entries; only the AutoRex-specific setting changed, giving 99 unique validation
cases. The earlier three cases are retained rather than silently discarded.

Every mask passed the 0..254 endpoint and 1% histogram-CDF checks (maximum CDF
deviation 0.586%). A fresh Release build passed **59/59 unit checks**. The original
`test/patchfit` reproduced the seed-42 baseline distance **9.437**, floor **2.830**.
All 20 timed CLI repeats matched their saved masks byte-for-byte.

The original spectral/clumping tools also provide an important cross-check.
For 4X, original `maskfit` improves **0.6821 -> 0.6426** and `levelfit` improves
**0.2056 -> 0.2012** for 4M/512. On Kodak-2484 and AutoRex, original clumping
**worsens** (0.3314 -> 0.3825 and 0.3888 -> 0.4024). AutoRex disagrees with the
multi-patch clumping comparison because the reference construction differs.
Neither disagreement is hidden in the headline mean; all original results are in
[official-summary.csv](../experiments/inpaint-20260927/official-summary.csv).

**Thread-count correction:** the initial harness requested 1 or 8 threads through
`OPENCV_FOR_THREADS_NUM`. The linked OpenCV 4.6.0 library ignored those requests
and reported 64 in both cases. The timing metadata has been corrected; the two
benchmark cohorts are repeated measurements at configured count 64, not a scaling
test. A separate experimental entry point then called `cv::setNumThreads`
explicitly: the baseline at seed 42 took **30.18 seconds at 1 thread** and
**7.38 seconds at 8**, one observation each. Both masks were byte-identical to
the unchanged CLI result. Thus explicit thread control works, but the environment
variable used initially does not work in this build. No production code changed.

The following figure shows native-resolution 128x128 crops of 50%-density level
sets, seed 17. The reference is a highpassed, ranked central sample patch, not the
entire multi-patch distribution. Continuous fields are available
[separately](../experiments/inpaint-20260927/fields.png).

![Inpaint and Markov density comparisons](../experiments/inpaint-20260927/level50.png)


## Experiment design

The search uses the same 256x256 masks, scale 1, six scans, and texture measures as
[the Markov study](MarkovParameterStudy.md). This makes the generated-mask scores
comparable; it does not make different kinds of error interchangeable.

Screening used seed 42 on `4Xcrop.png`, `examples/Kodak-2484-1.png`, and
`examples/AutoRex-4X-449c.jpg`. General candidates were selected without consulting
inpaint scores on the three excluded scans: `examples/Kodak-4X-2.png`,
`examples/EOS3-Delta3200-088b.jpg`, and `examples/EOS3-PanatomicX-134a.jpg`.
`examples/Kodak-4X-1.png` duplicates 4Xcrop pixel-for-pixel and is not another test
sample. These are six scans, not six independent film stocks.

Four adaptive screening stages contain 63, 102, 114, and 15 entries:

1. Cross sweeps with resolution levels; check update groups at three/six sweeps.
2. Cross database size with search checks, and radius with PCA dimensions; check
   highpass sensitivity against a fixed highpass-32 reference.
3. Refine dimensions, combine large databases with sweeps/levels/search accuracy,
   extend the database to its supported maximum, and test cheaper configurations.
4. Check whether four-million-row databases rescue larger neighborhoods or change
   the preferred dimensions, sweep count, or resolution hierarchy.

This is not the full Cartesian product and does not establish a global optimum.
Validation generation overlapped the last screening stage, but excluded-scan
scores were not inspected when choosing the final candidates.

| Control | Values explored |
|---|---|
| `inpaint.db` | 25,000; 100,000; 250,000; 1,000,000; 2,000,000; 4,000,000 |
| `inpaint.checks` | 16, 32, 64, 128, 512 |
| `inpaint.radius` | 1, 2, 3, 4 |
| `inpaint.dims` | 4, 8, 10, 12, 14, 16, 24 |
| `inpaint.iters` | 1, 2, 3, 6, 12 |
| `inpaint.groups` | 1, 4, 16, 64 |
| `inpaint.levels` | 1, 2, 3 |
| `mask.highpass` | 16, 32, 64, always evaluated against the highpass-32 target |
| Fixed | Mask size 256x256; scale 1; no budget or application cache |

Each trial uses a fresh production CLI process with all settings explicit and
`--no-config`. This avoids carrying FLANN's internal random state from one trial
to the next. Screening used up to 16 concurrent processes; the last interaction
check used 12, and validation used up to 24. Each process actually used OpenCV's
64-thread default: this build ignored the environment variable initially intended
to limit it. Recorded thread labels were corrected after probing the linked
library. Concurrent timings were not used as serial performance comparisons.

The `quality` candidate minimizes the mean of three errors, each normalized by
its sample's default error. `fast` minimizes the existing `inpaint_work` estimate
subject to screening combined error <=1.10 and no training sample exceeding 1.20.
Additional candidates minimize patch error (`shape`) or combined error at estimated
work no greater than default (`balanced`). These labels record selection rules;
validation and measured runtime determine whether those promises hold.

## Scores and their limits

P, C, and S all have lower-is-better interpretation:

- **P:** the repository's `test/patchfit` Mahalanobis distance over eleven features:
  octave energies/ridgeness, envelope correlations, and density-dependent clumping.
- **C:** RMS log10 clumping error at densities 0.10/0.25/0.50/0.75/0.90, against the
  mean log profile of real patches. Clumping measures radial power below 0.1
  cycles/pixel.
- **S:** RMS log10 radial-power error against the mean spectrum of ranked real
  patches, excluding frequencies below 1/32 cycles/pixel.

Reference preprocessing, covariance shrinkage, FFT bins, and all score functions
are identical to the Markov study. Real patches are the first up to 64
non-overlapping 256x256 crops in raster order after whole-sample highpass filtering
and per-patch rank flattening. Sample counts are 40, 27, 64, 21, 64, and 64;
leave-one-out patch-score floors span 2.26–3.13. Reference caches were reused only
after checking the input hashes, and can be regenerated by the experiment script.

P includes features also represented in C/S, so equal weights are an engineering
choice, not three independent perceptual criteria. The combined ratio averages
per-sample ratios, rather than pooling unlike units. Standard deviations describe
three random realizations of fixed scans; they do not quantify uncertainty across
all film stocks or applications. Scene edges in some scans can survive highpass
filtering, and raster-order patch selection is not a random survey of the image.

C/S differ from the original `test/levelfit` centre-crop reference and
`test/maskfit` unranked whole-sample spectrum. The original tools were also run
on each general finalist at seed 17; their separate results are retained.
These measurements do not validate print output, different mask sizes/scales,
subjective preferences, or temporal artifacts. Production defaults are unchanged.

## Reproduction and artifacts

[Experiment instructions](../experiments/inpaint-20260927/README.md) include build
and replay commands. The directory contains full-precision trial CSVs, exact
manifests, candidate-selection code, source/sample hashes, validation means and
sample SDs, original-tool output, timing checks, and selected masks. Some inputs
are local untracked scans; reproduction requires the exact files identified in
`provenance.json`. Intermediate masks and reference caches are ignored by Git but
remain available locally and can be regenerated.
