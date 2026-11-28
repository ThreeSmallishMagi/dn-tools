# Design

What this program is, what it has to get right, and why each part is shaped the
way it is.

Every term of art used here is defined in [GLOSSARY.md](GLOSSARY.md), and each
acronym is written out where this document first uses it. Two companion
documents go deeper on particular parts and are referenced rather than repeated:
[ARCHITECTURE.md](ARCHITECTURE.md) holds the module map and the record of
structural problems, and [DEFAULTS.md](DEFAULTS.md) holds the parameter
inventory and the full design of the defaults mechanism. The command-line
surface is documented in the top-level [README.md](../README.md).

Papers and web sources are listed under [References](#references) at the end,
and cited in the text as `[1]`, `[2]` and so on.

---

## 1. The problem

A display or a print has fewer tones than the picture does. A monochrome panel
may have two; a digital photographic enlarger exposing paper may have 256 but
be fed from a 16-bit scan. *Dithering* is the business of arranging the tones
you do have so that their local average matches the tone you wanted.

The concrete jobs this program is built for, which is why it has the features it
has rather than the ones a general dithering library would:

- **Expose frames for a 1-, 3- or 8-bit monochrome digital enlarger.** The
  output is a sequence of frames shown in turn, not a single image, which is
  what `--frames` and `--save-frames` are for and why the averaged file they
  write is only a preview.
- **Show a 16-bit image on an 8-bit display without banding**, by spreading it
  over several frames. This is what makes 16-bit input worth accepting: see
  section 3.
- **Make a 1-bit photogravure etching mask** that both looks attractive and
  stays inside the resolution limit of the medium — which is a constraint on the
  *pattern*, not just on the tone, and is why the mask generators exist.
- **Add realistic film grain to a digital image**, by building the threshold
  mask out of a real film scan. That is what `--mask-sample` and the four
  sample-based generators of section 6 are for.

The whole value of a dither rests on one property, and this program calls it
**density preservation**: over any region large enough to matter, the light
coming off the output must equal the light the input asked for. Everything else
— how fine the pattern is, whether it clumps, whether it looks like film — is
secondary to that, and is only worth discussing in a program that has it.

Density preservation is easy to claim and easy to lose. Three separate tone
bugs shipped in this program's history, each invisible to the checks that
existed at the time:

- Rounding a mask threshold against 255 rather than 254, which made every
  mask-dithered image about one part in a thousand too light.
- Running the error ledgers in *display units* rather than *linear light*,
  which on a non-uniform ladder biased the result by up to 49 parts in a
  thousand.
- Scaling the diffusion deposit by `--spatial` rather than scaling what the
  threshold rule sees, which lifted shadows by 27 parts in a thousand and
  crushed highlights by 19.

Section 8 is about the checks that now exist because of them. The rest of this
document is about the decisions those bugs were hiding in.

## 2. The governing decision: linear light

A pixel value in an ordinary image file is not proportional to light. It is
encoded through a curve — for the **sRGB** colour space (*standard Red Green
Blue*, IEC 61966-2-1) [1], roughly a 2.2 power law — whose purpose is to spend
the limited number of available codes where the eye can tell them apart.

Adding light is linear. Adding pixel values is not. So **every average in this
program happens in linear light**: the average of several frames, the average of
a block when `--supersample` reduces a larger grid, and both error ledgers. The
sRGB curve is applied at exactly two places, the decode on input and the encode
on output, and never inside the dither loop. `--linear` removes it entirely, for
input that is already linear.

This sounds like a detail and is the spine of the program. It is why
`vision::average_gray` decodes, averages and re-encodes rather than averaging
bytes; it is why the temporal ledgers are 64-bit floats; and it is the reason
for the distinction in the next section, which is the single subtlest thing in
the codebase.

### Display units are not linear light

A *threshold rule* — the decision of which output level to round a value to —
has to work in **display units**: position on the ladder of available output
values, scaled so that the rungs sit at the integers `0 … levels-1`. This is
forced. The rule needs the fractional part of the value, `frac(v)`, and a
fraction only means anything on a scale whose rungs are at the integers.

What the averagers add up is linear light. Under a uniform ladder the two scales
are proportional and the distinction never surfaces. Under `--level-spacing
srgb` with more than two levels it is not: the rungs are evenly spaced in
*pixel* value, so each step spans a different amount of light.

`Space` is the conversion, and it decides which quantity the ledgers conserve.
Diffusing a residual in display units is exact only while the levels a pixel
gets pushed to stay inside the rung interval its target sits in. Past that, the
linear value of a level is a *convex* function of its index, so spreading levels
around a target raises the mean however exactly the display-unit ledger
balances. That is Jensen's inequality, and it measured as a positive bias of
+49/1000 at `--levels 3 --spatial 1`.

Two things about `Space` are not optional. **Both ledgers must agree** —
converting the spatial one and leaving the temporal one in display units is
worse than the original bug, because they then pull against each other
(+101/1000 against the bug's +73). And **`to_display` must snap to a rung within
a tolerance**, because the temporal budget pays itself down to a whole number of
rungs and has to be able to ask for that number back; in display units that was
integer arithmetic and exact, but summing rung values in linear light and
dividing is not, and any residue at all moves a threshold decision.

For a uniform ladder `Space::of` returns the identity, so default output is
bit-identical to what the program produced before ladders existed, rather than
merely equal to it.

## 3. The pipeline

`dither_main` in `dither.cpp` runs five steps. `vision::` names live in
`srgb.h`/`srgb.cpp`; the engine is `engine.h`/`engine.cpp`.

1. **Decode to linear light**, at the input's own bit depth.
   `vision::decode_gray` coerces any input to a single channel without changing
   its depth, and decodes through the sRGB curve to 32-bit float in `[0, 1]`.
   One path serves 8- and 16-bit files: the depth decides only how many distinct
   codes there are, and so how large the **lookup table** (LUT) is.
2. **Place on the ladder.** `vision::decode_display` folds step 1 in and maps
   each value onto the `LevelSet` built by `make_levels`, giving display units
   in `[0, levels-1]`.
3. **Dither.** `ditherPass` returns one 8-bit matrix of raw level indices per
   frame. This is the whole of the algorithm, and section 4 describes it.
4. **Encode.** `vision::encode_levels` maps level indices back to the ladder's
   pixel values through a lookup table, then `vision::downsample_gray` resolves
   a `--supersample` run back to the input size by averaging each block in
   linear light.
5. **Average the frames**, in linear light, and re-encode. With `--save-frames`
   the individual frames are written too.

**Output is 8-bit single-channel throughout.** Level indices travel in an 8-bit
matrix, which is what caps `--levels` at 256.

**Input may be 8- or 16-bit**, and the difference is the point rather than
tidiness: *an 8-bit target cannot express anything below one output level, so
error diffusion and the multi-frame budget have nothing to distribute.* Both
mechanisms exist to render a value no single pixel can show. Measured on a
gradient spanning four 8-bit codes at `--levels 256`, scored as root mean square
(**RMS**) of the column means in units of 1e-6 linear:

| frames | 16-bit in | 8-bit in | gain |
|---|---|---|---|
| 1 | 61.9 | 793.0 | 12.8x |
| 4 | 25.0 | 793.0 | 31.7x |
| 16 | 12.7 | 793.0 | 62.7x |

The 8-bit column does not move, because the target was quantised before the
budget ever saw it. Float inputs are taken as `[0,1]` and widened to 16 bits by
a constant, never by content.

## 4. The dither mechanism

`ditherPass` is a single driver parameterised by two callables, and the two are
independent axes — every threshold rule works at every diffusion strength.

**`DitherFn`** is the threshold rule, an abstract class. `operator()(float v,
int row, int col)` returns a level index; the coordinates let a rule hold
per-pixel state, which is how a mask rule finds its cell. `new_frame()` is
called at the top of each output frame.

| `--threshold` | class | rule |
|---|---|---|
| `round` (default) | `RoundDither` | nearest level |
| `random` | `RandomDither` | stochastic threshold |
| `mask` | `MaskDither` | compare against a threshold mask |

**`SpatialFn`** is the error diffusion deposit: `void(*)(int row, int col, int
dir, cv::Mat& spatial_err, float value)`. `spatial_none` discards;
`spatial_floydsteinberg` implements the 7/16, 3/16, 5/16, 1/16 pattern of Floyd
and Steinberg [2], mirrored by `dir` so it always pushes error towards pixels not
yet visited.

Scanning is **serpentine** — even rows left-to-right, odd rows right-to-left —
which avoids the directional artefact a pure raster scan produces.

`quantize_scan` is the shared workhorse: one serpentine pass with its own
private diffusion channel `spatial_err`, freshly zeroed. Per pixel it computes a
value, clamps the rule's answer to the legal range, hands the residual to the
deposit function, and reports the level to a callback. `ditherPass` uses it once
for the goal pass and once per output frame; only the value source and the
callback differ.

### What `--spatial` actually scales

`--spatial <frac>` scales **how much of the accumulated error the threshold rule
sees, not how much is diffused**. `quantize_scan` computes two values per pixel:
`decide = target + spatial * accumulated` goes to the rule, while `account =
target + accumulated` is what the residual is measured against, and that
residual is always diffused in full.

It used to scale the deposit, which discarded a fraction of every residual. With
a mask rule those residuals are not zero-mean — the mask is a fixed function of
position, so it correlates with the error field feeding back into it — and at
`--spatial 0.5` that was the +27/-19 per thousand bug listed in section 1. A
random threshold showed none of it, which is what identified correlation as the
cause. Keeping the ledger exact makes the output average to the target for any
`spatial`, whatever the residuals correlate with. `0` and `1` are byte-identical
either way.

Its default is resolved after the whole command line is parsed: `0` if
`--threshold` was named, `1` otherwise, with an explicit `--spatial` always
winning, so the two flags are order-independent.

### The multi-frame budget

Temporal accumulation is a **budget**, not a carried residual. Each pixel gets
`goal = target * frames`; for `frames > 1` that goal is itself dithered to an
integer, and every frame then aims at

```
ideal = (goal - displayed) / (frames - f) + spatial_err
```

where `displayed` is the running total already shown. Because the shortfall
stays inside `goal - displayed`, the total converges *exactly* on the final
frame. No accumulator is needed and stochastic rules need no special case.

The **goal pass** is the one place display units remain the right scale, and
deliberately so: snapping `target * frames` to an integer there snaps it to the
nearest *achievable* total, since a pixel spending every frame on one of the two
rungs bracketing its target produces totals exactly one level-step apart. The
integer is then converted back through the ladder, putting the budget in linear
light without giving up the exact close.

`--spatial` applies to the goal pass as well as the frames. The goal decides
each pixel's total and so the average; the frames only divide it. A goal pass
pinned at full strength made `--spatial 0.01` over several frames the same image
as `--spatial 1`. At `--spatial 0` the goal is just `round(target * frames)`, and
anything below half a frame-step shows black — why `--spatial 0` multi-frame runs
clip the shadows, a hard threshold at budget granularity.

`goals` and `displayed` are 64-bit float and that is load-bearing: in linear
light this ledger accumulates `frames` rung values and the residue is measured
against the *smallest* span on the ladder — 0.0056 of full scale at 16 levels —
so at high frame counts a 32-bit rounding error exceeds a whole level.

## 5. The ladder

`LevelSet` holds, per level, its 8-bit pixel value and its true linear-light
value. `--level-spacing` chooses where the rungs go, and the three answers are
statements about *what display the file is for*:

- **`srgb`** puts rungs at evenly spaced 8-bit pixel values and takes each
  rung's light to be what that pixel decodes to. Right when the file *is* the
  display signal.
- **`linear`** puts them at evenly spaced amounts of light.
- **`device`** puts them at the evenly spaced steps of a display with exactly
  `--levels` of its own — a 3-bit monochrome panel — where the file carries only
  level indices, and a rung emits the curve applied to `k/(levels-1)` rather
  than what its rounded 8-bit stand-in decodes to.

They differ by up to 2% of a rung's light, and only when `levels-1` does not
divide 255. At `--levels 2` all three coincide and the output is 0 and 255,
which are sRGB-invariant.

Two facts about the ladder were established by unit test rather than assumed.
`LevelSet::uniform` is decided by the spacing rather than by inspecting the
rungs, so it is *conservative*: across 1530 ladders none is wrongly flagged
uniform, but ten even ones are missed and take the slower ladder-search path to
the same answer. And under `linear` spacing two rungs can share one 8-bit code,
first at 122 levels, because near white an even step of light is finer than
8-bit sRGB can carry — a limit of the format, but anything reading a level back
*from* a pixel value is ambiguous there.

## 6. Masks

`--threshold mask` compares `frac(v) * 254` against
`mask[(row + sy) % rows][(col + sx) % cols]`, where `frac` is the fractional
part in display units and `sx`, `sy` are a per-frame offset. A **threshold
mask** must therefore satisfy three requirements, and every generator meets them
or is refused:

1. **Flat histogram spanning 0..254.** The 254 is not cosmetic: a cell holding
   `t` is the rank at quantile `t/254`, so firing on `frac > t/254` fires with
   probability `frac` and the level averages to the target. Against 255 it did
   not, and every mask-dithered image came out about one part in a thousand
   light. What remains is the mask's own resolution, ±1/508 of a level, which no
   offset can remove.
2. **Toroidal.** The mask is indexed modulo its size, so any mismatch where it
   wraps becomes a **seam** repeated over the whole picture at a fixed period.
3. **Sized to the grid.** A mask *larger* than the dither grid is a trap: only a
   window of it is ever indexed, and a window of a clustered mask is not
   histogram-flat even when the whole mask is — blue noise survives this at 0.6%
   deviation, a grain mask does not at up to 6%. So the default size tracks the
   grid, derived by `mask_side()` from the grid, `--mask-highpass`,
   `--mask-scale`, and two values each generator declares: the tile it can
   afford and how often that tile may repeat before the repetition shows.

How much repetition shows is a property of the generator and nothing else. A
`markov` tile repeated 16 times reads as banding and 4 does not, so its limit is
4. Blue noise is locally featureless, so a repeat of it is still featureless —
its limit is 64 and never binds.

### The five generators

| method | sample? | what it does | owns |
|---|---|---|---|
| `blue` | no | void-and-cluster | the default; no sample needed |
| `dft` | yes | imposes the sample's amplitude spectrum | the spectrum; speed |
| `markov` | yes | fits a pairwise Markov random field | — |
| `inpaint` | yes | Efros–Leung synthesis | the aggregate; the clumping slope |
| `direct` | yes | uses a tile of the sample itself | everything, at a cost |

**`blue`** runs progressive void-and-cluster [3]: repeatedly place a pixel at the
lowest-energy empty location and update a toroidal Gaussian energy map with a
min-heap. It takes no sample. Its kernel half-width is *derived* rather than
fixed — the build scans a box and then drops every tap under a cutoff, so the
only correct half-width is where the Gaussian reaches that cutoff.

**`dft`** alternates two projections on a random field — impose the sample's
amplitude spectrum, then re-rank to a flat histogram — over 20 rounds. It
converges in distribution but never to a fixed point, so there is nothing for a
convergence test to detect; 20 is where quality plateaus. The full two-dimensional
spectrum is used, not a radial average, so an oriented sample keeps its
direction. **Phase is randomised**, so nothing higher-order survives: a weave
comes back as woven-looking noise. That is the whole trade — it owns the
spectrum by construction and reproduces none of the arrangement.

**`markov`** fits a table of `P(centre | two neighbours)` for each of 64
neighbour pairs, counted off the sample and smoothed toward the marginal, then
draws from the pooled verdict by **Gibbs sampling** [4], re-ranking flat after
each sweep. Both the sample and the field are quantised by *rank*, so the
model's marginal and the mask's flat-histogram requirement are the same object.

**`inpaint`** implements Efros–Leung [5]: to decide a pixel, find the
neighbourhoods in the sample most like the one around it and copy what they had
in the middle, coarse to fine. Matching runs in a 12-dimensional principal
component analysis (**PCA**) subspace through a k-dimensional tree supplied by
the Fast Library for Approximate Nearest Neighbors (**FLANN**) [6].

**`direct`** does not imitate the sample — it *is* the sample: high-pass, take a
centred tile, rank it flat. Everything true of the grain is true of the mask,
including whatever no model has captured, so it is the upper bound the other
three are measured against. What it gives up is section 7.

Scored against the film's own patches by `test/patchfit`, in units of real-patch
standard deviations, where one real patch of `Kodak-4X-1` already scores 2.8:

| | patchfit 4Xcrop | patchfit 2484 | clump | spectrum |
|---|---|---|---|---|
| `direct` | **9.0** | **1.8** | 0.235 | 0.271 |
| `inpaint` | 9.4 | 4.5 | **0.197** | 0.678 |
| `dft` | 15.5 | 4.7 | 0.431 | **0.002** |
| `markov` | 19.9 | 12.1 | 0.192 | 0.616 |

### Worming, and why `inpaint` exists

The `markov` generator's clumps stretch into connected ridges, and that is not a
quirk of the sampler. The Gibbs pass proposes each pixel from its neighbours'
pooled verdict and the sweep ends by re-ranking flat, so the marginal is
*conserved* while the arrangement is free. A locally agreeing field under a
conserved marginal is the textbook setup for **phase separation**, whose
equilibrium at a 50:50 split is a labyrinth rather than a field of blobs.

Four measurements establish it. The excess coherence peaks at the median density
and nearly vanishes at 10:90, where the minority phase can form droplets. There
is a critical temperature at about 0.48, below which the field orders and above
which 32 sweeps do nothing. Below it the characteristic scale grows with sweeps.
And that growth is also *what sizes the grain* — the field starts as white
noise with no scale at all — so sweeps buy the grain size and the worming
together.

Two fixes were tried and neither is kept. Seeding the field at the right scale
fixes the size and the worming still climbs, because ordering is per-sweep and
does not care where the field started. Re-ranking less often works exactly as
predicted and is useless: the worming falls and everything else collapses with
it, because the bin index assumes the flat marginal the re-rank maintains.

**The clumping and the worming are the same conserved dynamics, so no constant
separates them.** Getting one without the other needs a different generator, and
`inpaint` was believed to be that generator on the argument that it has no
temperature, hence no ordering transition to fall below, hence no sweep count
that can worm.

**The argument is wrong, and the way it is wrong is the lesson.** Inpaint worms
with sweeps too — mid-octave ridgeness runs 12.7 / 16.5 / 32.2 / 44.1 at 2 / 3 /
6 / 12 sweeps on Kodak-4X-2. What the absent temperature removes is the
*transition*, not the ordering: with no threshold to cross the ridges accumulate
steadily from the first sweep instead of switching on, which makes the defect
quieter and not smaller. Reasoning from the mechanism predicted the wrong sign
of an effect that one sweep of the parameter would have shown, and the parameter
was not swept because the mechanism seemed to settle it.

So both generators trade clumping against worming through their sweep count, and
neither has a convergent value for it. That is why `inpaint.iters` joins
`markov.temp` as a parameter `--mask-calibrate` measures per sample rather than
a constant this document can quote.

### Choosing the markov temperature, and two objectives that fail

*The calibrator is `markovcalib.cpp`; its own header comment is the primary
source for this section.*

Temperature is the total weight of the **log-opinion pool**, inverted. It is the
scalar that decides the outcome, it is not constant across samples (the measured
optimum runs 0.34 to 0.60 over four scans), and two natural ways of choosing it
are both wrong in instructive ways.

**Fitting the sample** — maximum likelihood, reachable as
`--markov-weights ml` — predicts the sample's own pixels 0.09 nats better
than flat weights and produces worse texture. A pair scores low exactly when it
is distant or off-axis, which is also when its information is least redundant;
prediction rewards concentrating on the strong, mutually redundant near pairs,
while the texture needs the long-range constraints only the weak pairs carry.
Held-out likelihood picks a temperature of 0.21 where the texture optimum is
0.41 — the pool over-sharpens by about 2x, because the chain then feeds on its
own samples and the errors compound.

**Matching a fixed share of the sample's neighbour mutual information** was the
first design for `--mask-calibrate`, targeting a ratio of about 0.65. It does
not generalise, and the reason is worth keeping: *that share is a property of
the texture, not a constant*. At their own optima a film scan reads 0.63 and a
coarser sample 0.79, and targeting 0.65 on the coarse sample returned 0.64 where
the truth was 0.37 — worse than the closed-form rule it was meant to improve on.
No amount of care measuring a constant fixes a quantity that is not one.

**Comparing the thing actually being judged** is what works, and is what
`--mask-calibrate` does. A threshold mask is used one **level set** at a time,
so the question is whether the pattern shown at each density clumps like the
sample's does — `test/levelfit`'s measure, computed in process against the
sample's own ranked crop, and sample-relative by construction. Note the
circularity that leaves: calibrating to minimise a levelfit-like distance and
then scoring with `test/levelfit` only confirms the search ran. The independent
check is agreement with temperatures found by hand sweeps the calibrator cannot
see.

The optimum is stable across mask size, so calibration runs on a small probe and
applies at full size; it is *not* stable across pair count or bin count, so
those must trigger a recalibration.

## 7. The seam

`direct` is the only generator that cannot make its output toroidal by
construction, because a crop of a photograph has a left edge that has never met
its right. It was the project's live open question for a long time; 7.1 is the
answer that was settled on, 7.3 is the one that was measured and not taken, and
7.4 is the part of it that remains genuinely open.

### 7.1 What is shipped

Efros and Freeman's **minimum error boundary cut** [10]. Take the tile out of a
crop `direct.overlap` larger on each axis, so its leading band has two
candidates — the crop's own start, and the continuation of what is about to
become the far edge — and switch between them along the path where they differ
least, found by dynamic programming. Once per axis, with both ends of each path
pinned to the same index; a step of one at the wrap would splice in a pixel from
a whole tile away, which is an arbitrary value rather than a bounded error.

**Every output pixel comes from exactly one place in the sample.** Nothing is
averaged, so nothing about the grain is altered anywhere, and this generator's
claim — that it *is* the sample — survives whole. Measured at 256² on
`Kodak-4X-1`, the wrapping pairs go from a rank-sum z of 13.79 and 8.65 uncut to
0.04 and 0.50, and `test/patchfit` from 3.04 to 2.606, under that film's own
real-patch floor of 2.83.

`direct.overlap` defaults to two sigma of the high-pass that produced the field.
An overlap sweep from 4 px to 64 finds no knee, so the floor of 8 exists only to
give the search somewhere to go.

Because `mask_side` derives the mask size from the **image**, a sample too small
for the derived tile is routine rather than exceptional — `--supersample 4` on a
256x256 picture asks for a 1024 tile. Two mechanisms meet it. A *derived* size
is clamped to what the sample can supply, per axis, and that clamp beats the
size floors: a floor is about how much repetition is tolerable, and no amount of
tolerance produces pixels the sample has not got. A size the user *pinned* is
refused instead, because that is a statement about what they asked for. Where
the sample can supply the tile but not the overlap, the overlap shrinks to what
is left.

### 7.1a What it replaced, and why that is worth recording

Moisan's **periodic plus smooth decomposition** [7]. Any image splits uniquely as
`u = p + s` with `s` smooth and carrying the whole boundary mismatch, so `p` is
periodic; `s` solves a discrete Poisson equation which in the frequency domain
is one division. Nothing is blurred and nothing is mirrored. It is a correct
implementation of a published method and it lost on measurement, twice, in ways
that generalise past it.

**A solve must over-correct on anisotropic texture.** It drives both axes'
boundary jumps toward zero at once, so the axis with the larger interior
difference ends up with a join *smoother* than its surroundings — a soft seam
rather than a hard one, and no less visible. On `Kodak-4X-1` its wrapping pairs
came out 0.84x and 0.51x as different as its interior pairs. A cut has no
strength to get wrong: it selects a route through existing pixels, so each axis
lands where it lands. This is also what killed the per-axis damping in 7.2.

**And its smooth component reached the interior**, which is the next subsection.
`maskutil::periodic_component` remains, still held against the paper's own
criterion by `unittest`; nothing calls it.

The implementation is checked against the paper's own criterion in `unittest`,
using the **cross artifact** the method is named for. Build a field with no jump
and broadband spectrum, add a ramp to create a jump without adding texture, and
measure mean on-axis power over mean off-axis power: 0.99 for the reference,
**224** with the ramp, **1.18** after the decomposition. Two further properties
are asserted because the cross test can pass for the wrong reason — that `s` is
harmonic in the interior to 3e-7 of its range, and that `u = p + s` with the
mean untouched. The harmonic test also refuses a trivial `s`, since a
decomposition returning `u` unchanged would leave `s` identically zero.

**The solve had to be cleaned up after, and forgetting that was a density bug.**
`s` is subtracted from the whole tile, not just its edges, so it leaves a broad
tilt. Rank-flattening does *not* remove it: ranking is a global monotone map, so
it flattens the histogram over the whole tile while preserving the tilt as a
swing in the **local** mean — a mask that is flat overall and not flat anywhere
in particular. `--supersample` samples exactly that, and a dithered ramp failed
three columns of `test/colstats`. The fix was one more high-pass after the
repair, safe because the field was a torus by then.

The cut needs no such step, and that is the point rather than a convenience: it
moves pixels the sample already held instead of adding a field to them, so there
is nothing to take back out. The same command now passes at group 1, 16 and 32.
The rule inverts — **nothing may filter after the cut**, because a filter there
would put values *between* the sample's own back in, which is the sub-level
ordering the high-pass is placed before the crop to avoid.

### 7.2 Five alternatives that are worse

Mirroring is exactly periodic and exactly wrong — the eye finds the symmetry at
once. Blending a band across the seam destroys texture in a cross, at the same
fixed period. Keeping only low frequencies is periodic by construction and
discards the fine detail, which for grain is the whole content. Damping the
correction per axis to land on the interior level helps three samples of five
and breaks the others. Searching 30,000 crop positions for one whose edges
happen to match does nothing, because the edges are independent.

### 7.3 The one that was measured and not taken

`test/seamlab` is the bench this question was settled on. Two constructions from
the texture synthesis literature beat the decomposition; 7.1 is one of them, and
this is the other.

**`blendpre`** is histogram-preserving blending [8][9] applied to the tile's own
half-shifts: rank onto a standard normal, blend each axis with its half-shift
under weights whose squares sum to one, map back. Because a weighted sum of
independent normals has variance equal to the sum of the squared weights, the
distribution is untouched, and with slowly varying weights so is the
autocovariance. Like the cut it has no global solve, so it cannot put a smooth
field into the interior.

One thing about its construction is forced and worth recording, because the
obvious simpler version is impossible rather than merely worse: the blend must
run one separable pass per axis. A single two-copy blend in 2D cannot work,
because the tile breaks along the whole cross `{x=0} ∪ {y=0}` and its half-shift
along `{x=W/2} ∪ {y=H/2}`, those two crosses intersect, and at the intersection
a single weight would have to be 0 and 1 at once.

| sample | construction | seam c | seam r | wrap/interior | patchfit |
|---|---|---|---|---|---|
| 4X-1 | decomposition | 1.34 | −4.84 | 0.84 / 0.51 | 3.12 |
| | `blendpre` | −0.94 | 2.70 | 1.01 / 1.22 | 3.42 |
| | cut (7.1) | **−0.51** | **0.12** | **0.99 / 1.03** | **2.64** |
| 4X-2 | decomposition | −10.69 | −7.34 | 0.57 / 0.73 | 2.74 |
| | `blendpre` | 1.84 | 2.94 | 1.07 / 1.24 | 3.76 |
| | cut (7.1) | 1.78 | −3.38 | 1.11 / 0.83 | **2.35** |

**The cut wins on the argument, not just the numbers.** A blend has the right
marginal and the right autocovariance and still averages two pieces of sample
together inside the band; the cut does not average anything. `direct` exists to
be the sample, so that distinction decides it.

`blendpre` is kept in the bench because the cut has one failure mode it does not
share: **the cut needs somewhere to cut.** Stochastic grain offers agreement
constantly and structured content does not — on a test image of lines over a
gradient, where the two edges never agree anywhere, the cut reads 16.2 and the
blend −5.5. If a sample of that kind ever needs serving, the blend is the
measured answer for it.

**The signed seam statistic is itself a finding.** It used to return an absolute
value, under which the decomposition read 1.34 and 4.84 as though they were
residual seams. They are not: its wrapping pairs are 0.84x and 0.51x as
*different* as its interior pairs, so the repair was overshooting into a join
smoother than the surrounding texture. Under the magnitude the two failure
directions were indistinguishable.

### 7.3a A separate defect the same investigation turned up

`direct` is the only generator whose mask *is* the ranked sample, which exposes
it to something the other four are immune to. A **high-pass** manufactures
detail — subtracting a blur of integers leaves non-integers — so a two-valued
sample yields a tile with 15713 distinct values, none of which the sample
recorded. **Rank-flattening is scale-free**, so it renders that residue at full
contrast as a swing in the local mean.

The cure is two steps that only work together: quantise the tile to just under
one input code, then rank it with the resulting ties broken at random. On a flat
dithered sample the finished mask's local-mean spread reads 5.35 untouched,
**36.40** quantised and ranked stably — seven times worse than doing nothing,
because a tie group in pixel order is a gradient across the tile — and **0.70**
quantised with the ties shuffled. Shuffling alone measures identical to stable,
since it only permutes within equal-value groups while the damage is in which
group a region falls into.

Film is indifferent to both: 0.1% of its pixels tie, and `test/patchfit` reads
3.040 unquantised against 3.074. That is why the defect went unnoticed.

**The guard was testing a quantity the high-pass defeats.** It counted distinct
values in the tile and demanded 255; the flat sample passes with 13277. The
honest measure is the **tie share** — the fraction of the ordering decided by
the coin flip. Over nineteen images, every real film scan lands between 0.006
and 0.065, and flat fields, ramps and line patterns between 0.39 and 1.00, with
nothing in between 0.16 and 0.39. A count cannot separate them: a fine-grained
Panatomic-X scan survives quantising with only 50 distinct levels, fewer than
several of the synthetic patterns that must be refused.

### 7.4 Seam visibility has no validated measure

Five proxies have been tried — the one-pixel rank test, that test on blurred
copies, the local-contrast profile across the join, the cross artifact, and edge
mismatch at several scales — and they disagree with each other and with the
tiled images. The one-pixel test sees a hard discontinuity and is blind to a
broad band of altered texture; the contrast-profile test is the reverse and
rates an uncorrected crop *better* than a corrected one on film, which the
images do not support. `test/seamlab --write` emits each construction's mask and
a 2×2 tiling, and **that tiling is the check**.

## 8. Defaults

A tuning parameter with a fixed default is a lie in two directions: it is wrong
for anyone whose situation differs from the one it was measured in, and it gives
no account of itself. The design in [DEFAULTS.md](DEFAULTS.md) replaces fixed
defaults with a lazy resolution graph; all six steps of it are implemented.

Ask the **parameter registry** for a value with `P.num(key)` and its rule runs
once, recording what it depended on. A user-supplied value is pinned and never
recomputed. A rule carries its own permitted range, checked on every write and
every derived result — bounds belong to the parameter, not to whichever option
exposes it, so demoting an option to internal cannot quietly take its validation
with it. Each generator registers its own rules, so the mechanism stays ignorant
of the generators, and adding one means a new module plus a row in the table.

Two behaviours are worth stating here because they are what the mechanism is
*for*.

**`reconcile` moves defaults so a supplied option is not stranded.** Naming
`--mask-sample` is a request for a mask, so the threshold default has no
business staying on `round` and then reporting the sample as inapplicable. The
search is exhaustive rather than greedy, because the moves are not independent:
`--markov-temp 0.3` needs both `mask.method=markov` and `threshold=mask`,
and neither alone reduces the count. Nothing in it knows what a mask is.

**`conflicts` stops the run when reconcile cannot rescue.** Whatever remains is
by construction blocked by a parameter the *user* pinned — had it been a
default, it would have moved — so it is two irreconcilable requests, not a
misplaced option, and it is reported as an error naming both sides. Between the
two, an option the user typed is never silently dropped. That replaced a note
printed alongside a zero exit status, which is the shape of "your option had no
effect and everything is fine".

`--budget <time>` is opt-in. Unset, every generator keeps its fixed ceiling and
output is unchanged, which is why the test suites need no insulation. Given, it
picks mask size and pair count from per-generator work formulas divided by a
measured throughput. It deliberately stops at 256 pairs, because past that the
mask measurably gets worse: spending the whole budget and getting the best
result are not the same thing.

## 9. Verification

`test/run` runs everything and prints one summary. The suite is shaped by the
bugs that got through it, and each check below names the blind spot it closes.

- **`test/colstats`** — per-column density against a reference, in linear light,
  scored as a z-score rather than a raw pixel difference because column-mean
  noise near mid-grey is around five sRGB pixels even for a perfect dither.
  `--group N` pools adjacent columns, which tightens the test by √N and brings
  out a small bias applied everywhere. That is not a refinement: the `--spatial`
  bug passed all 256 columns ungrouped and fails four of sixteen groups at
  `--group 16`. A grouped FAIL is strong evidence; a grouped PASS is weaker than
  its z-score looks, because the standard error assumes independent samples and
  diffusion actively cancels error.
- **`test/coltest`** — a matrix of runs against a ramp, each scored at three
  group sizes. A row is identified by configuration *and* group, because the two
  ask genuinely different questions. An `expected_failure` is a claim that a
  configuration cannot preserve density for a known reason, with the measurement
  establishing it — not a way to quieten an unexplained result.
- **`test/tonetest`** — whether the *whole image* averages to the input. It
  exists because `colstats` is nearly blind to a small bias applied everywhere:
  a few parts per thousand vanish into per-column noise of thirty. Its
  tolerances are per threshold rule, because the three have different floors and
  one number for all of them is as loose as the loosest — `mask` is
  deterministic *and* error-cancelling, so it is held ten times tighter than
  `random`, and that split is what separated the `frac * 255` bias from honest
  counting noise.
- **`test/devicetest`** — the only tone check that does not read output as an
  8-bit sRGB image, because under `--level-spacing device` the file carries
  level indices for a panel with its own levels. It asserts both directions, so
  that if `srgb` ever stops being worse there, the mode has been silently
  unified away.
- **`unittest`** — the only test that calls the functions rather than running
  the binary and reading back a PNG. It holds the multi-frame budget's exact
  close across a 22-case matrix, the ladder, the ranking every generator shares,
  and the Moisan correctness test of section 7.1.
- **`test/masktest`** — holds every generator to the four properties claimed of
  all of them: usable, toroidal, any shape, and reloadable. Toroidality is the
  one that needs care, and two designs failed before the rank sum worked; every
  run re-scores a deliberately broken copy and requires it to fail, because a
  toroidality test that cannot see a seam is worse than none.
- **`test/impliedtest`** — that naming a mask option selects the mask threshold
  on its own, that the shorthand is byte-identical to the spelled-out form, and
  that two irreconcilable options stop the run.
- **`test/maskstats`, `maskfit`, `levelfit`, `patchfit`, `seamlab`** — mask
  quality rather than program correctness. They measure deliberately different
  things and a method can win one and lose another by an order of magnitude, so
  quoting one alone misrepresents the result.
- **`shellcheck`** — greps the sources for `std::system`, `popen` and `exec*p`.
  It is structural because the rule worth holding, never hand a path to a shell,
  is broader than the one call that broke it.

Two lessons generalise. **A new axis needs a new sweep**: every check swept
threshold rules and diffusion strengths at the default two levels, where the
ladder is uniform and display units cannot disagree with linear light, so the
unit bug was invisible to all of them; `--levels` joined the matrix, and then
`--level-spacing` joined it one turn later for the same reason. And **the
measurement must not charge the dither for the file format**: `tonetest` reads
each output back to its level index and maps that to the ladder's exact linear
value, because the mid rung of a three-level linear ladder writes as pixel 188
and reads back as 0.5030, a standing +1.1/1000 that would leave a real bias of
0.6 nowhere to show.

## 10. What is not solved

- **Seam visibility has no validated measure.** Section 7.4. The seam itself is
  closed — the boundary cut of 7.1 lands both axes on every sample tried — but
  what settled it was a mixture of five disagreeing proxies and looking at the
  tiled images, and that is not a thing a test suite can hold.
- **The cut needs somewhere to cut.** Section 7.3: it serves stochastic texture,
  which is what `--mask-sample` is for, and fails on structured content over a
  gradient. `blendpre` is the measured answer for that case and is not in the
  program, because no sample that needs it has come up.
- **`markov`'s white-noise floor.** Pooled pairwise conditionals are broader
  than the true full-neighbourhood conditional, so every pixel carries extra
  independent noise — visible as roughly 8x excess power above 0.3 cycles per
  pixel. Lowering the temperature removes it and over-clumps instead; the two
  cannot be had together from a log-opinion pool. Fixing it properly means
  fitting the joint rather than pooling conditionals.
- **`--mask-highpass` is a measured constant, not a derived one.** The obvious
  derivation was tried: a three-segment fit of the sample's radial spectrum
  cleanly separates subject, grain and rolloff, and the grain-to-rolloff knee is
  stable across six films — but the subject-to-grain knee runs from 39 to 443
  pixels and the measured optimum does not follow it.
- **`--budget` is off by default**, because turning it on would make output
  machine-dependent. The price is listed under Reproducibility in
  [DEFAULTS.md](DEFAULTS.md) and has not been paid.

---

## References

### Papers

1. IEC 61966-2-1:1999, *Multimedia systems and equipment — Colour measurement
   and management — Part 2-1: Colour management — Default RGB colour space —
   sRGB.* International Electrotechnical Commission.
   <https://webstore.iec.ch/publication/6169>
2. Floyd, R. W. and Steinberg, L. (1976). "An Adaptive Algorithm for Spatial
   Greyscale." *Proceedings of the Society for Information Display* 17(2):
   75–77.
3. Ulichney, R. A. (1993). "The void-and-cluster method for dither array
   generation." *Human Vision, Visual Processing, and Digital Display IV*,
   Proc. SPIE 1913: 332–343. <https://doi.org/10.1117/12.152707>
4. Geman, S. and Geman, D. (1984). "Stochastic Relaxation, Gibbs Distributions,
   and the Bayesian Restoration of Images." *IEEE Transactions on Pattern
   Analysis and Machine Intelligence* PAMI-6(6): 721–741.
   <https://doi.org/10.1109/TPAMI.1984.4767596>
5. Efros, A. A. and Leung, T. K. (1999). "Texture Synthesis by Non-parametric
   Sampling." *Proceedings of the Seventh IEEE International Conference on
   Computer Vision (ICCV)*: 1033–1038.
   <https://doi.org/10.1109/ICCV.1999.790383>
6. Muja, M. and Lowe, D. G. (2009). "Fast Approximate Nearest Neighbors with
   Automatic Algorithm Configuration." *International Conference on Computer
   Vision Theory and Applications (VISAPP)*: 331–340. The library is bundled
   with OpenCV as `cv::flann`. <https://www.cs.ubc.ca/research/flann/>
7. Moisan, L. (2011). "Periodic Plus Smooth Image Decomposition." *Journal of
   Mathematical Imaging and Vision* 39(2): 161–179.
   <https://doi.org/10.1007/s10851-010-0227-1> — author's copy at
   <https://helios2.mi.parisdescartes.fr/~moisan/papers/2009-11.pdf>
8. Heitz, E. and Neyret, F. (2018). "High-Performance By-Example Noise using a
   Histogram-Preserving Blending Operator." *Proceedings of the ACM on Computer
   Graphics and Interactive Techniques (High-Performance Graphics)* 1(2): 31.
   <https://doi.org/10.1145/3233304>
9. Deliot, T. and Heitz, E. (2019). "Procedural Stochastic Textures by Tiling
   and Blending." In *GPU Zen 2: Advanced Rendering Techniques*. Interactive
   demonstration at
   <https://unity-grenoble.github.io/website/demo/2020/10/16/demo-histogram-preserving-blend-make-tileable.html>
10. Efros, A. A. and Freeman, W. T. (2001). "Image Quilting for Texture
    Synthesis and Transfer." *Proceedings of SIGGRAPH 2001*: 341–346.
    <https://doi.org/10.1145/383259.383296>
11. Lutz, N., Sauvage, B. and Dischler, J.-M. (2023). "Preserving the
    autocovariance of texture tilings using importance sampling." *Computer
    Graphics Forum* 42(2): 347–358. <https://doi.org/10.1111/cgf.14766>
12. Wendling, Q., Sauvage, B., Lutz, N. and Dischler, J.-M. (2025). "Deformed
    tiling and blending: application to the correction of distortions implied by
    texture mapping." *Computer Graphics Forum*.
    <https://doi.org/10.1111/cgf.70011>

### On the state of the periodic plus smooth literature

Reference [7] has 123 citing papers as enumerated through the Semantic Scholar
graph API, and **none of them is a methodological successor**. They are
overwhelmingly applications — electron microscopy, Fourier ptychography,
twisted bilayer graphene, cell morphometry, magnetic anomaly mapping — using the
decomposition to clean a transform before measuring something else. The only one
that touches the algorithm is efficiency work — a field-programmable gate array
(**FPGA**) reformulation for real-time use [13] — and the nearest thing to a
methodological discussion is [14], which applies the decomposition unchanged to
electron microscopy and compares it only against windowed transforms. Nothing
addresses the smooth component's effect on the interior, which is the property
section 7.1 has to clean up after. The live line of work for that problem is the
texture synthesis one — references [8] through [12].

13. Mahmood, F., Toots, M., Öfverstedt, L.-G. and Skoglund, U. (2015). "2D
    Discrete Fourier Transform with Simultaneous Edge Artifact Removal for
    Real-Time Applications." *IEEE International Conference on
    Field-Programmable Technology (FPT)*.
    <https://arxiv.org/abs/1603.05154>
14. Hovden, R., Jiang, Y., Xin, H. L. and Kourkoutis, L. F. (2015). "Periodic
    Artifact Reduction in Fourier Transforms of Full Field Atomic Resolution
    Images." *Microscopy and Microanalysis* 21(2): 436–441.
    <https://doi.org/10.1017/S1431927614014639> — preprint at
    <https://arxiv.org/abs/2210.09024>

### Tools

15. OpenCV. <https://opencv.org/> — the image, transform, filter and
    approximate-nearest-neighbour primitives this program is built on.
16. Semantic Scholar Academic Graph API.
    <https://api.semanticscholar.org/> — used to enumerate the citing
    literature for reference [7].
