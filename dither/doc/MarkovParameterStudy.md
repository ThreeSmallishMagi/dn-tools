# Markov mask parameter study

2026-09-27. Source: `dd9b0a26144101a4f2dda85c0335ac1942597e1e`.

## Recommendation

**The strongest pooled result among the tested settings is two sweeps, temperature
0.10, maximum-likelihood weights, and no offset-reuse penalty.** Keep 16 bins,
64 pairs, radius 5, shortlist 40, highpass 32, and scale 1.

```sh
./dither examples/m039crop.png --no-config \
  --mask-method markov --mask-sample 4Xcrop.png \
  --mask-size 256x256 --mask-scale 1 --mask-highpass 32 \
  --mask-bins 16 --mask-pairs 64 --mask-nbhd 5 --mask-temp 0.10 \
  --set markov.iters=2 --set markov.weights=ml \
  --set markov.shortlist=40 --set markov.reuse=0 \
  --seed 17 --mask-out markov-quality-mask.png --out markov-quality.png
```

Use a freshly built executable; reproduction instructions are below. Replace the
input and sample paths as appropriate. `--no-config` prevents previous calibration
and configuration from affecting this comparison.

This is a **candidate preset, not a universal optimum**. It improved all three
multi-patch errors on five of six scans when tested with three new seeds. On
Panatomic-X it worsened shape and clumping despite improving the spectrum. It also
cost about 6.95 seconds versus 1.59 seconds for the default on the 4X sample.
Production defaults have not been changed.

For the three scans excluded from initial tuning, its normalized combined error
was 0.848 versus the default's 1.000. Across those scans, patch error improved
10%, spectral error improved 41%, and clumping error **worsened 5.5%**, averaging
per-scan ratios. Across all six scans, including the tuning set, the combined
error was 0.689. These are statistical feature errors, not percentages of
perceptual improvement.

## Other useful presets

Every row uses size 256x256, scale 1, highpass 32. All controls are explicit here;
changing pairs also changes the application's derived shortlist and reuse.

| Preset | Bins | Pairs | Radius | Shortlist | Reuse | Sweeps | Temperature | Weights | CLI time, seconds |
|---|---:|---:|---:|---:|---:|---:|---:|---|---:|
| Current default | 16 | 64 | 5 | 40 | 0.35 | 3 | 0.282 | flat | 1.59 |
| Quality candidate | 16 | 64 | 5 | 40 | 0 | 2 | 0.10 | ml | 6.95 |
| MI alternative | 16 | 32 | 3 | 29 | 0.5075 | 2 | 0.125 | mi | 1.25 |
| Fast candidate | 8 | 64 | 5 | 40 | 0.35 | 1 | 0.025 | flat | 0.95 |
| Flat-weight alternative | 16 | 64 | 5 | 40 | 0 | 2 | 0.20 | flat | 1.50 |

Times are medians of three serial, fresh-process CLI invocations on this machine,
using 4Xcrop and a 256x256 input, with configuration/cache disabled. They include
sample loading, model fitting, sampling, dithering, and PNG output; they are not
portable performance guarantees. ML fits the expert weights as part of each draw,
which accounts for much of its extra cost.

The fast candidate reduced patch error substantially on several scans, but its
clumping was worse on 4X and Panatomic-X. MI improved spectra but made patch error
worse on Kodak-4X-2 and Panatomic-X. The flat-weight alternative had the best mean
combined error on the three initially excluded scans (0.837), narrowly ahead of
quality and fast; this small, mixed sample does not establish a universal winner.

| Preset | Mean normalized error, all six scans | Three initially excluded scans |
|---|---:|---:|
| Default | 1.000 | 1.000 |
| Quality | 0.689 | 0.848 |
| MI | 0.791 | 0.907 |
| Fast | 0.811 | 0.852 |
| Flat | 0.805 | 0.837 |

## Results by sample

These are means over seeds **17, 123, 2026**, none used for initial setting
selection. Lower is better in every column. P = repository `patchfit` distance;
C = multi-patch clumping error; S = multi-patch spectral error. C and S are defined
below and are deliberately distinguished from the existing single-reference tools.

| Sample | Default P | Quality P | Default C | Quality C | Default S | Quality S |
|---|---:|---:|---:|---:|---:|---:|
| 4Xcrop | 21.130 | 12.355 | 0.201 | 0.151 | 0.374 | 0.178 |
| Kodak-2484-1 | 12.444 | 3.732 | 0.334 | 0.233 | 0.472 | 0.138 |
| AutoRex-4X | 18.967 | 6.849 | 0.167 | 0.129 | 0.391 | 0.210 |
| Kodak-4X-2, initially excluded | 24.816 | 15.835 | 0.144 | 0.076 | 0.501 | 0.303 |
| Delta3200, initially excluded | 27.306 | 17.121 | 0.087 | 0.062 | 0.491 | 0.294 |
| Panatomic-X, initially excluded | 13.252 | 19.013 | 0.075 | 0.145 | 0.427 | 0.243 |

Full standard deviations and component ratios are in
[summary.csv](../experiments/markov-20260927/summary.csv). For example, the 4X quality
patch score was 12.355 +/- 0.966 sample SD; the default was 21.130 +/- 0.276. Three
seeds test stochastic stability, not population-level uncertainty across films.
The median real-patch leave-one-out floors were 2.26–3.13: substantial mismatches
remain even after tuning.

### Sample-specific choices

For 4Xcrop, the best screened combined score used `weights=ml`, `iters=3`,
`temp=0.125`, `reuse=0.35`, with the other quality-preset controls unchanged.
On the three validation seeds it scored P/C/S = **10.815 / 0.128 / 0.196**.
That trades a little spectral accuracy for better shape and clumping than the
generic quality candidate.

For Kodak-2484-1, the generic quality candidate was also the best screened choice.
For AutoRex, use `weights=ml`, `iters=2`, `temp=0.10`, `reuse=0.35`, and radius **3**:
P/C/S = **5.241 / 0.127 / 0.230** on new seeds. Again, the generic quality candidate
has a better spectrum while this setting has a better patch score.

After observing the Panatomic-X failure, a separate 90-setting search found
**flat weights, two sweeps, temperature 0.20, radius 8, reuse 0.35**; keep 16 bins,
64 pairs, shortlist 40, highpass 32. On seeds 17/123/2026 it scored
**8.540 / 0.083 / 0.303**, versus the default's **13.252 / 0.075 / 0.427**.
Its patch and spectral errors improve, with a modest clumping regression. This
follow-up is sample-specific tuning, not independent validation of a general rule.
The original excluded-sample results above have not been replaced by it.

## What the search established

- **Tune temperature and sweep count together.** At default model settings on 4X,
  the best equal-weight combined result at two sweeps was temperature 0.20
  (P/C/S 17.425 / 0.191 / 0.317); at twelve sweeps it was 0.40
  (29.238 / 0.190 / 0.478), using screening seed 42. Longer sampling does not
  automatically produce more faithful grain.
- **Weighting changes the useful temperature range.** ML's useful range here is
  far below the default 0.282. Earlier comments favoring flat weighting do not
  settle the comparison at two sweeps and temperature 0.10. Selecting weighting
  at one fixed temperature would miss this result.
- **More pairs or bins is not a reliable quality upgrade.** Neither 256 pairs nor
  32 bins produced the strongest pooled result. Eight bins were useful for a
  single-sweep speed preset; 16 were retained for quality.
- **Pair selection matters.** Removing the reuse penalty helped the pooled ML
  result, especially Kodak-2484-1. 4X and AutoRex also had good sample-specific
  candidates with reuse 0.35. Reuse zero is not intrinsically preferable.
- **Neighborhood size is sample-dependent.** Radius 3 helped the MI alternative
  and AutoRex's ML fit; radius 8 helped Panatomic-X's flat fit. There is no support
  for simply increasing radius for all samples.
- **Keep the target texture fixed while comparing controls.** The main evaluation
  target is highpass 32 and scale 1. Highpass 16/64 sensitivity trials were scored
  against that same target; they did not justify changing it. A different desired
  grain size requires a newly scaled reference, not reuse of these scores.

## Calibration and existing-tool cross-checks

The existing `--mask-calibrate` picked temperatures 0.348836, 0.25, 0.371336,
0.514908, 0.457612, and 0.347411 for the six samples in table order, at default
settings. Its 128x128 probes search only **0.25–0.60**, so they cannot discover
0.10 or 0.025. It tunes temperature only, with a centre-crop, cell-weighted
clumping objective; it does not optimize the multi-patch objective used here.
The calibration is deterministic, but that does not make it the optimum for
this task. Saved masks preserve the full-precision CLI-selected temperatures;
the diagnostic logs print rounded values.

The original `test/maskfit` and `test/levelfit` were also run on every general
finalist for all six samples at seed 17. Quality improved the original spectral
score on all six. The **original clumping tool disagrees on AutoRex**:
0.235 -> 0.290, while the multi-patch mean improves. Both clumping comparisons
regress on Panatomic-X. For 4X, original spectral fit improves 0.6246 -> 0.4360
and original clumping improves 0.2015 -> 0.1320. All raw results are retained in
[official-summary.csv](../experiments/markov-20260927/official-summary.csv) and the
`official-*.txt` files. This disagreement is evidence that the choice of reference
region matters; it must not be hidden inside a single headline score.

## Method and coverage

**2,084 distinct sample/setting/seed combinations were scored.** Repeated entries
across stages reuse an identical saved mask. There were 198, 1,296, 324, and 150
entries in the four screening stages; 96 finalist validation entries; 90
Panatomic-X follow-up entries and three seed checks; and six calibrated outputs.
This is an adaptive search, not a complete Cartesian product or a claim to a
global optimum.

| Control | Values explored across stages |
|---|---|
| Sweeps | 1, 2, 3, 5, 8, 12 |
| Temperature | 0.01, 0.025, 0.05, 0.075, 0.10, 0.125, 0.15, 0.175, 0.20, 0.225, 0.25, 0.282, 0.30, 0.35, 0.40, 0.45, 0.50, 0.60 |
| Bins | 2, 4, 8, 16, 24, 32 |
| Pairs | 16, 32, 64, 128, 256 |
| Radius | 2, 3, 5, 8 |
| Weights | flat, mi, ml |
| Shortlist | 24, 64 explicitly; also pair-derived 21, 29, 40, 56, 79 |
| Reuse | 0, 0.15, 0.35, 0.7 explicitly; also 1.12 × shortlist / (2 × pairs) |
| Highpass | 16, 32, 64 |
| Fixed | 256x256 mask, scale 1; screening seed 42 |

The manifests specify which combinations were actually drawn. Early stages varied
model controls around the defaults while crossing temperature with sweeps; later
stages tested interactions between weighting, bins, pairs, radius, and reuse.
Candidates were frozen in `candidates.json` before consulting excluded-scan scores.
No further general-preset tuning used those scores.

Training samples were `4Xcrop.png`, `examples/Kodak-2484-1.png`, and
`examples/AutoRex-4X-449c.jpg`. Initially excluded samples were
`examples/Kodak-4X-2.png`, `examples/EOS3-Delta3200-088b.jpg`, and
`examples/EOS3-PanatomicX-134a.jpg`. `examples/Kodak-4X-1.png` is pixel-identical to
`4Xcrop.png` and was excluded as a duplicate. There are six image files, not six
independent film stocks. Some inputs include scene edges; highpass filtering does
not guarantee their complete removal.

The screening objective gives equal weight to three errors, each divided by its
sample's current-default error, then averages over the three training images:

1. **P:** exactly the repository's eleven-feature `patchfit` distance, including
   octave energy/ridgeness, cross-octave envelope correlation, and clumping.
2. **C:** RMS log10 clumping ratio at densities 0.10/0.25/0.50/0.75/0.90. Low power
   uses 64 radial bins below 0.1 cycles/pixel, following `maskstats`. The reference
   is the mean log clumping profile of real patches, rather than one central crop.
3. **S:** RMS log10 normalized radial-power ratio, excluding frequencies below
   1/32 cycles/pixel. Reference power is averaged over ranked real patches, rather
   than comparing a ranked mask directly against the original unranked scan.

Reference patches are the first up to 64 non-overlapping 256x256 patches in raster
order after whole-sample highpass filtering and per-patch rank flattening, as in
`patchfit`. Counts are 40, 27, 64, 21, 64, and 64 respectively. Their covariance
and leave-one-out floors use `patchfit`'s existing shrinkage rule. C normalizes
binary power by realized variance; the existing tool uses requested density,
a negligible rounding distinction here. P's energy/clumping features overlap
with C/S, so equal weights are an explicit engineering preference, not three
independent perceptual criteria. Scores do not test printer behavior, larger
mask sizes, other scales, subjective preference, or motion/temporal artifacts.

A C++ experiment driver calls the production `fit_markov_model` and
`sample_markov_mask`, reusing fitted models for settings that change sampling
only. OpenCV threads are limited to one per driver process; up to 12 processes
run concurrently. The final benchmark used the actual CLI. All 15 benchmark masks
were **byte-identical** to their experimental-driver counterparts. The baseline
patch distance matched the original `test/patchfit` output (20.311, floor 2.830).
All scored masks had endpoints 0 and 254 and passed the 1% histogram-CDF check
(maximum deviation 0.586%). A fresh Release build passed **59/59 unit checks**.

## Visual inspection and reproduction

[50% density comparisons](../experiments/markov-20260927/level50.png) and
[continuous fields](../experiments/markov-20260927/fields.png) show native-resolution
128x128 central crops, seed 17. The reference column is a highpassed, ranked
central patch. It illustrates one region, not the whole multi-patch distribution.
The comparisons expose both the improvement in 2484's structure and remaining
mismatches, including pronounced directional structure in some MI outputs.

![50 percent density comparisons](../experiments/markov-20260927/level50.png)

See [experiment instructions](../experiments/markov-20260927/README.md) for the
build and rerun commands, complete manifests, full-precision CSVs, source/sample
hashes, seed results, raw existing-tool output, and timing checks. Some film scans
are local untracked files; reproducing those rows requires the exact files listed
in `provenance.json`. Generated intermediate masks and reference caches are
ignored by Git but remain available locally and can be regenerated.
