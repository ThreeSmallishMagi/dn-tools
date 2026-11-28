# Commit review: 20 October–15 November 2026

Reviewed **67 project commits**, from `dff818c` through `fee20e7`, inclusive. The start is 2026-10-20 00:00 UTC. This extends the [7 November review](CommitReview-2026-11-07.md): its first 28 commits were already reviewed, and their findings were checked against the subsequent fixes. The 39 later commits were reviewed through their changes and the current calling code. The ledger below lists every commit, including documentation, data and recorded test results. Uncommitted changes to `aliases` and untracked files were excluded; the test installation used the committed `aliases`.

**Four actionable P2 findings remain:** two affect the new spectrum constraints and calibration, and two affect the tools used to judge changes. No P1 defect was established. The Release build and **19/19 test groups, including 72/72 C++ unit cases**, pass. These findings concern cases those tests do not cover. No production code was changed.

## 1. Calibration and production use different spectrum references for multiple scans

**Introduced by:** `5bf20ca`.

**Locations:** [markovcalib.cpp](../markovcalib.cpp), lines 170–171; [inpaintcalib.cpp](../inpaintcalib.cpp), lines 176–177; [calib.cpp](../calib.cpp), lines 220–221; [mask.cpp](../mask.cpp), lines 371–374.

For a directory of scans, or a film with no installed crop, `samples::reference_for()` returns the entire sample set. Production passes that set through `MaskRequest::reference` to the generator, which uses it to compute its spectral constraints.

Calibration reduces the same reference set to `ref.primary()` in `calib::inputs_for()`. That was reasonable when the reference was only an image used to score probes. The new band constraints also use it to *generate* probes: each calibrator wraps that one image in a new single-image set and passes it to the builder. It now optimises a different synthesiser from the one that subsequently builds the requested mask. Automatic reuse of calibration carries the mismatch into ordinary runs too.

**Reproduced:** two synthetic scans, 128² white noise and 120² Gaussian-smoothed noise, with a 64² Markov probe. Production's reference bands and calibration's differ by up to **0.999 log10**, approximately a factor of ten. With the same fit parameters and seed, **4,080 of 4,096 mask pixels** differ solely from changing the reference argument from the full set to its primary image. This demonstrates the model mismatch, not a claim that every selected temperature is necessarily worse.

The six installed films with readable crops avoid this specific problem because their reference set already contains one image; tests using those films will miss it.

**Fix:** keep separate fields for the synthesis reference set and the scoring image. Pass the same synthesis set through calibration and production; taking its primary image for a single scoring patch can remain a separate policy. Add an integration/property test using a multi-scan directory without a crop, comparing a calibration probe with an ordinary build at the same size, parameters and seed.

## 2. The band reference can ignore all but the first scan, and samples only the start of large images

**Introduced by:** `5bf20ca`.

**Location:** [maskutil.cpp](../maskutil.cpp), `band_reference`, lines 453–466.

The tile counter is shared across all scans, and both raster loops stop at 16. A first scan at least four tiles wide and high consumes the entire allowance. Every subsequent scan contributes zero to the spectrum that all synthesis sweeps are forced toward. For a very wide scan, the reference can consist entirely of part of its top strip. A wider sampling budget need not mean processing every pixel, but the selection must represent the inputs it claims to summarise.

**Reproduced:** use two 512² scans, white noise A and blurred noise B, with a 128² field. `band_reference({A}, ...)` and `band_reference({A,B}, ...)` are exactly equal. Reversing the scans changes the target by up to **4.022 log10**, over **10,000×** in one band. Thus file ordering can determine the entire enforced scale profile. The pairwise tables or inpaint database still see the other scans; the defect is specifically the imposed spectral target, not that the whole generator ignores those scans.

**Fix:** distribute the 16 samples across scans and spatial positions, using a documented weighting such as available area and a deterministic sampling scheme. If the cap is smaller than the number of scans, use an explicit representative-selection policy. Test that later scans can affect the target, that samples span a large scan, and that merely permuting a small set does not materially change the pooled estimate. The compact reproduction for findings 1 and 2 appears below.

## 3. `seamlab --check` no longer builds its comparison mask and still exits successfully

**Introduced by:** `630549a`, interacting with the existing `seamlab` invocation. The `dc320db` parameter check does not exercise this path.

**Locations:** [test/seamlab](../test/seamlab), lines 1155–1172; [dither.cpp](../dither.cpp), output preflight at lines 145–164.

The bench launches dither with `--out /dev/null`. The new preflight correctly refuses that extension before building or writing the mask. `seamlab` ignores the subprocess status, reads a fixed `/tmp/seamlab_<basename>.png` anyway, and returns success. On a fresh path it prints an empty `build failed:` message because it displays stdout while the useful error is on stderr. If a file remains from an earlier run, it can instead score that stale mask as the current executable's result.

**Reproduced:**

```bash
python3 test/seamlab examples/Kodak-4X-1-crop.png   --size 128x128 --variants equalise --check
```

The bench prints `(dither) build failed:` and exits **0**. Its subprocess fails with:

```text
Could not write /dev/null: no image format has that extension (use .png)
```

**Fix:** create a private temporary directory and give both outputs valid PNG names. Check the return code before reading either file, report stderr, and exit nonzero when comparison cannot run. Exercise `--check`, not just `--params`, in a small integration test; include a deliberately failed child process to catch accidental success and stale-file reuse.

## 4. `--method direct` still scores the wrong reference preparation

**Exposed by:** `f459f2b`'s new generator-aware scoring interface. The underlying difference dates to `d883950`'s local equalisation.

**Locations:** [test/maskref.py](../test/maskref.py), lines 36–53; [test/levelfit](../test/levelfit), `reference`; [test/patchfit](../test/patchfit), `real_patches`; [directmask.cpp](../directmask.cpp), lines 178–188.

`maskref` only obtains `mask.highpass`. Consequently `levelfit --method direct` obtains 32 and subtractively high-passes the sample before ranking it. The default direct generator does not do that: it ranks the raw sample within a local neighbourhood using `direct.equalise`. Its high-pass is a texture-validity check, not the field that gets ranked. The two treatments differ in both spatial statistics and tie handling. The new method option therefore does not deliver the stated comparison against the generator's own preparation for direct.

**Reproduced:** a 256² uint8 sample with sinusoidal shading and Gaussian noise; generate a same-sized direct mask with `--direct-overlap 0` so seam editing cannot explain the discrepancy. `levelfit --method direct` reports **clump=1.4322**. Constructing the reference with the actual `local_quantile(..., 4*0.1874*32, 42)` preparation and scoring through `--reference-is-mask` reports **clump=0.0000**. This is a reference-definition error, not evidence that the direct mask itself has poor texture.

**Fix:** represent reference preparation explicitly, including local equalisation versus subtraction, scale, relevant overrides and tie policy. Ideally expose the C++ preparation to these tools rather than independently implementing it in each Python script. At minimum, reject or clearly qualify `--method direct` until it can model the default; preserve a separate way to request intentionally different reference preparations.

A reproducible input and CLI run:

```bash
python3 - <<'PYCODE'
import cv2, numpy as np
r = np.random.default_rng(63)
y, x = np.mgrid[:256, :256]
f = 128 + 50*np.sin(2*np.pi*x/180) + 10*r.normal(size=(256,256))
cv2.imwrite('/tmp/review-shaded.png', np.clip(f,0,255).astype('uint8'))
PYCODE
build/dither examples/ramp_256x256.png --no-config   --mask-method direct --mask-sample /tmp/review-shaded.png   --mask-size 256x256 --direct-overlap 0   --mask-out /tmp/review-direct.png --out /tmp/review-dither.png
python3 test/levelfit /tmp/review-shaded.png /tmp/review-direct.png --method direct
```

## Earlier findings and the root-cause question

The five findings in the 7 November review are resolved in current code:

| Earlier defect | Fix checked |
| --- | --- |
| Direct texture guard used the wrong scale | `612835b`: prepares at source scale before taking the resized crop; includes a regression case |
| Temporal demonstration's narrow numeric domain | `c4a725c` and `6b79bc4`: double/64-bit arithmetic, signed strategy results, bounded display values |
| Seam bench's stale equalisation sigma | `dc320db`: correct sigma and registry comparison; finding 3 above is a separate later break in the end-to-end check |
| API scanner missed first declarations | `89eb12f`: strips directives, adds scanner fixtures and an undefined-symbol link check |
| Cache-rate poisoning changed nothing | `640efa3`: inserts missing rates, requires an actual mutation, isolates cache directories and resolves rates through budget/explain |

The three concrete issues identified during the `b771d8c` review are also fixed by `257e140`: orientation now lives in `threshold_mask`, direct library calls return correctly oriented masks, `levelfit --reference-is-mask` handles mask references consistently, and the orientation integration check examines rendered 10% and 90% tones. Current unit and integration tests cover those fixes.

The larger product goal—coarse grain in dark image regions and fine grain elsewhere—is still distinct from polarity. `04c3fb3` disables the default high-pass for Markov/inpaint, so the earlier explanation that their default preparation removes tone must not be repeated as a description of current code. However, these generators still build a single threshold ordering, and the new band constraints are global summaries of locally equalised references. No explicit target-tone-conditioned texture-selection mechanism was added. Treat that as an unresolved design requirement requiring its own rendered-image acceptance test, rather than declaring the polarity correction wrong or counting the feature gap as a new regression.

## Architecture and clarity

The central orientation conversion and generator table are good improvements. Preserve them. The next useful simplification is a shared, explicit reference description: training scans, synthesis constraint reference, scoring image, preparation and version. Current code uses the word “reference” for several of those, then collapses a set into one image; finding 1 is the resulting behavioural error. Sharing a contract would also let scoring tools avoid the partial high-pass-only model in finding 4.

Keep band estimation and projection independently testable. The new unit case checks one film, one size and one seed, using the same helper to define expected power. It cannot reveal a biased reference sampler. Add synthetic multi-scan and spatial-coverage properties, and keep quality assertions distinct from reference-selection correctness. No broad rewrite or additional abstraction around every generator is needed.

## Validation and limits

- Built all CMake targets in `/tmp/review-nov15-build`, Release, GCC 13.3/OpenCV 4.6. The existing workspace build cache points at another checkout, so it was not reused or modified.
- Ran the complete suite against that build through `/tmp/review-nov15-tree`: **19/19 groups passed**, including **72/72 unit cases**, crop-window reproduction, API linking, calibration-cache poisoning, sample resolution and fresh-install workflows. Log: `/tmp/review-nov15-suite.log`.
- Ran the separate reproductions above. Helper sources/binaries and generated images are under `/tmp/review-nov15-probes`.
- Did not rerun every historical parameter sweep, full six-film calibration search, or subjective quality study. Recorded measurements in documentation are not independently certified by this review. Passing tests and the absence of further findings are not a proof that all defects have been found.
- No production implementation, test, user configuration, or installed sample library was intentionally changed. The only repository addition is this report.

## Compact C++ reproduction for findings 1 and 2

Save the following as `/tmp/review-bands.cpp`, then compile against a current build:

```bash
c++ -std=c++17 -I. $(pkg-config --cflags opencv4) /tmp/review-bands.cpp   build/libdither_lib.a $(pkg-config --libs opencv4) -o /tmp/review-bands
/tmp/review-bands
```

```cpp
#include "maskutil.h"
#include "markov.h"
#include "markovmodel.h"
#include "inpaint.h"
#include <opencv2/imgproc.hpp>
#include <iostream>
#include <cmath>
double dist(const std::vector<double>& a,const std::vector<double>& b){double d=0;for(size_t i=0;i<a.size();i++) if(a[i]>0&&b[i]>0)d=std::max(d,std::abs(std::log10(a[i]/b[i])));return d;}
cv::Mat noise(int n,double sigma,int seed){cv::Mat f(n,n,CV_32F);cv::RNG(seed).fill(f,cv::RNG::NORMAL,0.,1.);if(sigma>0)cv::GaussianBlur(f,f,{},sigma);return f;}
int main(){cv::setNumThreads(4);
 auto a=noise(512,0,1), b=noise(512,2,2);
 auto one=maskutil::band_reference({a},{128,128},24);
 auto ab=maskutil::band_reference({a,b},{128,128},24);
 auto ba=maskutil::band_reference({b,a},{128,128},24);
 std::cout<<"band_reference {A} versus {A,B}: "<<dist(one,ab)<<"; {A,B} versus {B,A}: "<<dist(ab,ba)<<" log10\n";
 a=noise(128,0,1); b=noise(120,2,2);
 auto set=samples::Set::of(std::vector<cv::Mat>{a,b}); auto primary=samples::Set::of(set.primary());
 MarkovParams p; p.pairs=8;p.bins=4;p.radius=3;p.shortlist=8;p.iters=2;
 auto production=fit_markov_model(set,p,{64,64},&set);
 auto calibration=fit_markov_model(set,p,{64,64},&primary);
 std::cout<<"production versus calibration reference bands: "<<dist(production->bands,calibration->bands)<<" log10\n";
 auto x=sample_markov_mask({64,64},*production,p,1,nullptr);
 auto y=sample_markov_mask({64,64},*calibration,p,1,nullptr);
 std::cout<<"same parameters and seed, changed mask pixels: "<<cv::countNonZero(x!=y)<<" / "<<x.total()<<"\n";
}
```

## Commit ledger

The first 28 entries retain the earlier review's historical coverage; the current status of its findings is recorded above. Test-result-only commits were checked as records, not treated as substitutes for running tests. Intermediate implementations subsequently removed are distinguished from defects present at `fee20e7`.

| Commit | Date | Change |
| --- | --- | --- |
| `dff818c` | 2026-10-20 | seamlab: record why poly's rounds is inert and why robust is not safe |
| `efc8993` | 2026-10-21 | Fix the eleven defects in doc/TestCoverageReview.md |
| `8e60183` | 2026-10-22 | Tests from doc/TestCoverage.md: the invariants, not the inventory |
| `86885c6` | 2026-10-22 | A test for each defect in efc8993, written as the general property |
| `d3cecfb` | 2026-10-22 | Speed up the suite: 198s to 55s, every assertion unchanged |
| `357f99b` | 2026-10-22 | update coltest reference results |
| `23317f5` | 2026-10-24 | more film grain samples |
| `6e2fcaa` | 2026-10-24 | cleanup |
| `b4952b7` | 2026-10-26 | cleanup |
| `605fc2c` | 2026-10-26 | add untracked doc files |
| `921cbcd` | 2026-10-26 | untracked files |
| `e72cd68` | 2026-10-30 | direct: flatten the tone instead of high-passing it |
| `adcddff` | 2026-10-30 | direct: derive the flatten degree from the cutoff, not a constant |
| `719defd` | 2026-11-02 | crops: commit the search, and describe what it actually optimises |
| `d883950` | 2026-11-03 | direct: equalise the sample locally instead of flattening its tone |
| `5affa70` | 2026-11-03 | direct: test the pooled path, model equalise in seamlab, fix stale docs |
| `aefffc6` | 2026-11-03 | findcrops: add a search for windows that suit --direct-equalise |
| `5d16684` | 2026-11-03 | findcrops: record the independent checks on the equalise picks |
| `96cb04b` | 2026-11-04 | crops: adopt the equalise search where it is better, add the two unused frames |
| `84070d5` | 2026-11-04 | crops: correct three claims in the new crop record |
| `1c4abde` | 2026-11-05 | direct: equalise at four times the high-pass sigma, 24 px by default |
| `7cdf469` | 2026-11-05 | maskcache: leave --mask-out's path out of the key |
| `34a354a` | 2026-11-05 | samples: whole scans for the pooling generators, one crop per film for direct |
| `003109e` | 2026-11-05 | docs: tell the user where direct's crop comes from, and list mask.out as left out of the key |
| `81ce573` | 2026-11-05 | update todo |
| `2b05ebc` | 2026-11-06 | crops: Kodak-2484 takes Mamiya's plain wall, equalise's fifth 2048 window |
| `f7bff29` | 2026-11-07 | docs: define 4Xcrop, and close the housekeeping item it was part of |
| `255c9e5` | 2026-11-07 | doc: index every file, and drop two byte-identical copies |
| `13d5135` | 2026-11-07 | mask: a generator that imitates, named without a sample, takes Kodak-4X |
| `7341df5` | 2026-11-07 | test: refresh lastrun.txt |
| `612835b` | 2026-11-07 | direct: test for texture at the cutoff's own scale |
| `c4a725c` | 2026-11-07 | temporaldither: double and 64-bit levels throughout |
| `dc320db` | 2026-11-07 | seamlab: model direct's equalise sigma at the program's 4x, and check it |
| `89eb12f` | 2026-11-07 | apicheck: strip directives, so a header's first declaration is linked |
| `640efa3` | 2026-11-07 | test/run: poison cache rates that exist, in caches that are separate |
| `73f815e` | 2026-11-07 | test: refresh lastrun.txt |
| `6b79bc4` | 2026-11-07 | temporaldither: signed strategy levels, clamped at both ends |
| `53e0154` | 2026-11-07 | test: refresh lastrun.txt |
| `79159fc` | 2026-11-07 | add code review docs |
| `87e9c6c` | 2026-11-07 | todo |
| `a9467f8` | 2026-11-07 | doc: index the November commit review, answered by its fixes |
| `6e315a5` | 2026-11-07 | docs: 4Xcrop is the whole Kodak-4X-1 scan, and --mask-calibrate is not markov's |
| `4454674` | 2026-11-07 | aliases: run findcrops with the venv on PATH |
| `d10ec49` | 2026-11-07 | test/run: findcropscheck, so films.tsv and the search cannot drift apart |
| `f8d6d67` | 2026-11-07 | cli: conflict messages name the flags the user typed |
| `630549a` | 2026-11-07 | dither: refuse an unwritable output before the work, and say the average is a preview |
| `a1e4395` | 2026-11-07 | cli: name hand-parsed flags too, and hold impliedtest to flags |
| `49a2178` | 2026-11-07 | test: workflowcheck, the README's commands from a fresh install |
| `ddbcb7b` | 2026-11-07 | doc: triage the September UX review |
| `04c15d9` | 2026-11-08 | CLAUDE.md: what inpaint's speckle on Kodak-2484 is, and why the default stays |
| `bbd4540` | 2026-11-08 | test: refresh lastrun.txt |
| `caa2b76` | 2026-11-08 | CLAUDE.md, triage7: correct the speckle reference and the scope of the early path check |
| `2b81703` | 2026-11-08 | dft: a film name means the film's crop, as it does for direct |
| `c0a86dc` | 2026-11-08 | test: refresh lastrun.txt |
| `b771d8c` | 2026-11-10 | masks: a sample-based mask lights the sample's bright grain first |
| `0ef87e5` | 2026-11-10 | test: refresh lastrun.txt |
| `257e140` | 2026-11-11 | masks: state the orientation once, in threshold_mask, and drop the compensations |
| `716a9c5` | 2026-11-11 | test: refresh lastrun.txt |
| `04c3fb3` | 2026-11-12 | markov, inpaint: no high-pass by default |
| `6041824` | 2026-11-12 | test: refresh lastrun.txt |
| `f459f2b` | 2026-11-12 | scoring tools: prepare the reference the way the mask's generator did |
| `df509a2` | 2026-11-12 | calibrate: score against the film's crop, read samples as the build does, refit markov's ranges |
| `399f4cf` | 2026-11-12 | test: refresh lastrun.txt |
| `833c52c` | 2026-11-12 | defaults from the calibration runs, and calibrations used by default |
| `5bf20ca` | 2026-11-15 | markov, inpaint: hold every sweep to the sample's band power |
| `586adb7` | 2026-11-15 | colplot for higher res images |
| `fee20e7` | 2026-11-15 | docs, help and comments: bring them up to date, and three small fixes they turned up |
