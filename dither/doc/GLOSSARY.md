# Glossary

Every term of art this project uses, in one alphabetical list. Each entry is
meant to be readable on its own: where one entry needs another, it names it in
**bold** rather than assuming you have read it.

The other documents — [DESIGN.md](DESIGN.md), [ARCHITECTURE.md](ARCHITECTURE.md),
[DEFAULTS.md](DEFAULTS.md) and the top-level [README.md](../README.md) — expand
each acronym where they first use it and then link here. This file is the one
place that defines everything, so if a term is used anywhere in the project and
is not here, that is a defect in this file.

Two conventions throughout the project and therefore throughout this list.
`--like-this` is a command-line option of the `dither` program.
`like.this` is an internal parameter name in the defaults registry, reachable
with `--explain` and `--set` (see **parameter registry**).

---

### 4Xcrop

The sample most measurements in this project were first taken on, and a name
that no longer matches any file. It was an untracked `4Xcrop.png` at the
repository root, byte for byte the scan now tracked as
`examples/Kodak-4X-1.png` — 1300×2200, the whole frame, not a crop — and
`test/masktest` records the identity. Every figure quoted against 4Xcrop is
therefore a figure for that file. It was once described as "a 128² crop";
the 128² that recurs nearby is the reference **mask** `test/levelfit` and
`--mask-calibrate` cut from a sample. Not to be confused with
`examples/Kodak-4X-1-crop.png`, a 1024² window of the same scan used as a
fixture.

### aliasing

A frequency higher than a signal can represent, reappearing as a lower one.
Sampling at `N` points per unit length can represent frequencies only up to
`N/2` — the **Nyquist frequency** — and anything above that folds back below it
and is indistinguishable from a genuine low frequency. The `dft` **mask
generator** had exactly this bug: asking its sample for a frequency past the
sample's Nyquist wrapped the index and returned a low frequency's amplitude
instead of zero. See **band-limit**.

### amplitude spectrum

The magnitude of each frequency component of an image, discarding **phase**.
Obtained as the absolute value of the **discrete Fourier transform**. The `dft`
mask generator works by forcing a random field to have the sample's amplitude
spectrum; because it keeps none of the phase, it reproduces how much structure
the sample has at each scale and nothing about how that structure is arranged.

### anisotropy

Having different statistics along different directions. Film grain is mildly
anisotropic — on the sample `Kodak-4X-1` adjacent rows differ by 41 grey levels
and adjacent columns by 30 — and a **mask generator** that imitates a sample
should reproduce that rather than averaging it away. `test/maskstats` reports it,
normalised so that direction-free noise reads 1.0.

### band-limit

To set every frequency above some cutoff to zero, rather than letting it wrap
around (see **aliasing**). `--mask-scale` reads the sample's spectrum at scaled
frequencies, and frequencies past the sample's **Nyquist frequency** are set to
zero because a sample says nothing about them.

### band power

How much of a field's **power spectrum** falls in each of a set of frequency
bands — 16 by default, spaced evenly in log frequency from one cycle per field to the
corner of the frequency plane, as power per cell per pixel so a small tile and a
large field can be compared band for band. markov and inpaint rescale their
field's band power to the sample's after every sweep — **spectrum matching**,
`--markov-bands` and `--inpaint-bands` giving the number of bands and 0 turning
it off (`maskutil::impose_band_power`) — which keeps the structure they grow at the
grain's scale: the sweeps decide the arrangement, the sample how much there is
at each size. The sample here is the film's crop, **locally equalised** first
so that it describes grain rather than the window's soft shading.

### blue noise

Noise whose power is concentrated at high spatial frequencies and nearly absent
at low ones. As a **threshold mask** this is the ideal: the dots it places are
spread evenly at every density, so a flat tone comes out looking flat rather
than clumped. The `blue` mask generator produces it by **void-and-cluster**.
Contrast **white noise**, which has equal power at all frequencies, and a
*clustered* pattern, which has excess power at low frequencies.

### budget, temporal

The mechanism by which several output frames average to a tone no single frame
can show. Each pixel is given `goal = target * frames`, an integer total it must
reach across all frames; each frame aims at
`ideal = (goal - displayed) / (frames - f)`, where `displayed` is the running
total already shown and `f` is the frame index. Because the shortfall stays
inside `goal - displayed`, the total converges *exactly* on the final frame.
See **goal pass**, and note the separate meaning of `--budget` under **time
budget**.

### clumping

How much the pixels at a given output density group together rather than
spreading out. Measured per **level set** by `test/levelfit`, normalised so that
**white noise** reads 1.0. Film grain clumps about six times harder in the
shadows than in the highlights; reproducing that asymmetry is what the `markov`
and `inpaint` generators buy and the `dft` generator cannot.

### coarsening

The growth of a pattern's characteristic length scale over time. In the `markov`
generator's **Gibbs sampling** the field starts as **white noise**, which has no
scale at all, and grows one as sweeps accumulate — +37% from 2 sweeps to 32 at
**temperature** 0.25. It is what sizes the synthesised grain, and it is
inseparable from **worming**, because both are the same **phase separation**.

### conserved marginal

A constraint that fixes the overall distribution of values while leaving their
arrangement free. The `markov` generator re-ranks its field flat after every
sweep (see **rank-flattening**), which pins the histogram exactly. A locally
agreeing field under a conserved marginal is the textbook setup for **phase
separation**, which is why that generator **worms**.

### cross artifact

The line of high-energy coefficients along the horizontal and vertical axes of a
**discrete Fourier transform**, caused by the image's opposite edges not
matching. A transform treats its input as periodic, so a crop has a
discontinuity all around its frame, and a straight-line discontinuity spreads
power along the perpendicular frequency axis. Named for Figure 1 of Moisan's
paper (see **periodic plus smooth decomposition** and [DESIGN.md][r]'s
References). `unittest` uses it as the correctness test for that decomposition:
a deliberate ramp raises the on-axis to off-axis power ratio from 0.99 to 224,
and the decomposition brings it back to 1.18.

[r]: DESIGN.md#references

### cumulative distribution function

Written **CDF**. For a given value, the fraction of a distribution that falls at
or below it. Mapping a field through its own CDF gives a uniform distribution,
which is what **rank-flattening** does discretely; composing that with the
inverse CDF of a normal distribution gives a Gaussian one, which is what
**histogram-preserving blending** needs.

### CV_8U, CV_32F, CV_64F

OpenCV's matrix element type codes: unsigned 8-bit integer, 32-bit float, and
64-bit float respectively. The project uses them precisely. Output frames and
**level** indices are `CV_8U`, which is why `--levels` caps at 256. The
per-scan diffusion channel `spatial_err` is `CV_32F`. The **temporal budget**
ledgers `goals` and `displayed` are `CV_64F`, and that is load-bearing rather
than cautious: at 16 levels a rung spans 0.0056 of full scale, and a 32-bit
float's rounding error accumulated over many frames exceeds a whole level.

### DC

Short for *direct current*, borrowed from electrical engineering to mean the
zero-frequency component of a transform — that is, the mean. "The mask's
spectrum peaks at DC" means it has acquired a large-scale brightness variation,
which for a **threshold mask** is a density defect.

### device spacing

See **level spacing**.

### discrete Fourier transform

Written **DFT**, and computed by the **fast Fourier transform** (**FFT**)
algorithm. It re-expresses an image as a sum of sinusoids, giving an amplitude
and a phase per frequency. Central to three parts of this project: the `dft`
mask generator imposes a sample's **amplitude spectrum**; the **periodic plus
smooth decomposition** solves its Poisson equation as one division in the
frequency domain; and `test/maskfit` and `test/maskstats` measure masks by their
spectra. A transform treats its input as periodic, which is the origin of the
**cross artifact**.

### display units

Also *display levels*. Position on the **ladder**, measured so that the rungs
sit at the integers `0 … levels-1`. A **threshold rule** has to work in these
units, because `frac(v)` — the fractional part — only means anything on a scale
whose rungs are at the integers. Contrast **linear light**, which is what the
frame and block averagers actually add up. Under a uniform ladder the two are
proportional; under `--level-spacing srgb` with more than two levels they are
not, and conflating them was a real tone bug. `Space` is the conversion between
them.

### dither

To render an image with fewer available tones than it has, by arranging the
available tones so that their local average matches the original. This program
does it in **linear light** throughout, so that the local average is the
physically correct one.

### DitherFn

The abstract class for a **threshold rule**. `operator()(float v, int row, int
col)` returns a **level** index for the value `v`; the coordinates let a rule
hold per-pixel state, which is how a **mask** rule looks up its cell.
`new_frame()` is called at the top of each output frame, for rules with
per-frame state. The three implementations are `RoundDither`, `RandomDither` and
`MaskDither`.

### dynamic programming

Written **DP** where space is tight. Solving a problem by building the best
answer for each prefix from the best answers for shorter prefixes. Used by
`test/seamlab`'s `quilt` construction to find the lowest-cost path through an
overlap band: the best path reaching each cell of a row depends only on the best
paths reaching the three cells above it. See **minimum error boundary cut**.

### Efros–Leung

The non-parametric texture synthesis method the `inpaint` **mask generator**
implements. To decide a pixel, find the neighbourhoods in the sample that most
resemble the one already around it, and copy what those had in the middle.
Matching is done in a **principal component analysis** subspace through a
**k-d tree**. See [DESIGN.md][r]'s References.

### envelope correlation

How the local amplitude at one spatial scale tracks the local amplitude at the
next. It lives entirely in the **phase**, so a generator that matches only the
**amplitude spectrum** gets it wrong for free — which is how `test/patchfit`
catches the `dft` generator. Reported as `Cfm` (fine-to-mid) and `Cmc`
(mid-to-coarse).

### error diffusion

Carrying the error made at one pixel into its not-yet-visited neighbours, so
that the error cancels over a small region instead of accumulating. See
**Floyd–Steinberg** for the specific pattern used, **SpatialFn** for the
interface, and `--spatial` for the control.

### fast Fourier transform

See **discrete Fourier transform**.

### FLANN

*Fast Library for Approximate Nearest Neighbors*, bundled with OpenCV as
`cv::flann`. The `inpaint` generator uses its **k-d tree** to find similar
sample neighbourhoods; the search is approximate, and `inpaint.checks` sets how
hard it looks.

### Floyd–Steinberg

The standard **error diffusion** pattern, and the only one this program
implements. The residual at a pixel is split 7/16 to the next pixel in the scan
direction, and 3/16, 5/16, 1/16 to the three below it, mirrored according to the
scan direction so that it always pushes error towards pixels not yet visited.
See [DESIGN.md][r]'s References.

### frac

The fractional part of a value in **display units** — how far it sits between
the **rung** below and the one above. `MaskDither` thresholds `frac(v) * 254`
against its **mask** cell; the 254 rather than 255 is what makes the level
average to the target, and the difference was a measurable tone bug. See
**threshold_byte**.

### Gibbs sampling

Drawing from a joint distribution by repeatedly replacing each variable with a
draw from its distribution given all the others. The `markov` generator uses it
to turn its fitted **Markov random field** into a field of values, one sweep
over the image at a time. Because it proposes each pixel from its neighbours and
then restores the histogram, it is subject to **phase separation** — see
**worming**.

### goal pass

The first pass of a multi-frame run, which turns each pixel's real-valued
`target * frames` into the integer total it must actually reach. It uses the
same **threshold rule**, **SpatialFn** and `--spatial` as the frames themselves,
and decides the average image: the frames only divide its total between them. It is the one place **display units** remain the right
scale, because a whole number of rungs is exactly what a pixel spending every
frame on the two rungs bracketing its target can produce. It runs before the
first `new_frame()`, so a **mask** rule always sees offset `(0, 0)`. Skipped
entirely when `--frames 1`.

### grain

The random texture of a photographic film emulsion, and the thing the four
sample-based **mask generators** exist to imitate. It is stochastic, roughly
stationary, mildly **anisotropic**, and its dark grain clumps harder than its
light grain (see **clumping**), which a dither shows as dark clumps in the
highlights.

### high-pass

To remove structure coarser than some size, keeping the fine detail. Applied to
a **mask sample** by `--mask-highpass` (default 32 pixels for dft and direct, 0 for
markov and inpaint) so that what gets
imitated is the **grain** and not the photograph's subject. Implemented as the
image minus a Gaussian blur of itself. The project's rule is: **wrap the filter
when the input is a torus, reflect when it is not one yet** — so
`maskutil::highpass_toroidal` wrap-pads and `prepare_texture_sample` reflects.

dft high-passes by default. markov and inpaint do not — they model the sample's
values as they are, and are held to the grain's scale by **band power** instead
— and `direct` equalises: see **local equalisation**.

### histogram-preserving blending

A way of mixing two texture fields without the contrast loss that ordinary
averaging produces. Map each field onto a standard normal distribution by rank,
mix with weights whose *squares* sum to one rather than whose values do, and map
back. Because a weighted sum of independent normals has variance equal to the
sum of the squared weights, this leaves the distribution exactly as it was.
Implemented as `test/seamlab`'s `blend` family. Measured, not shipped: it
preserves the marginal and the autocovariance and still averages two pieces of
sample together inside the band, which the **minimum error boundary cut** does
not. It remains the better answer for a sample the cut cannot serve — one with
structure rather than stochastic texture, where the two candidates never agree
anywhere and there is no good place to cut. See [DESIGN.md][r]'s References.

### ideal

The value a single frame of a multi-frame run aims at:
`(goal - displayed) / (frames - f) + spatial_err`. See **temporal budget**.

### image quilting

See **minimum error boundary cut**.

### intermediate

A parameter that reaches the program's output *only* through other parameters —
it shapes them, and nothing downstream reads it itself. `image.width` is the
example: it exists so the mask size can be derived from the picture, and once
that size is known the width has no further say. Declared on the rule, and used
by the **mask cache** to keep such parameters out of its key, since one that
moves without moving anything downstream is a miss that gains nothing. The
declaration defaults to *not* intermediate, so forgetting it costs a rebuild
rather than a stale result.

### k-d tree

*k-dimensional tree*, a structure for finding nearest neighbours in a space of
`k` dimensions by recursively splitting it along one axis at a time. The
`inpaint` generator searches one, supplied by **FLANN**, over a database of
sample neighbourhoods projected into a 12-dimensional **principal component
analysis** subspace.

### ladder

The set of output values a run can produce, held as `LevelSet`. It carries each
**level**'s 8-bit pixel value in `px` (an array of `uint8_t`) and that level's
true **linear light** value in `lin`. `make_levels(levels, useSrgb,
srgb_spaced)` builds it, and `LevelSet::uniform` records whether the rungs are
evenly spaced, which enables a fast path. See **level spacing** and **rung**.

### level

One of the `--levels` output values a pixel may take, identified by its index in
`0 … levels-1`. Level indices travel through the pipeline in a **CV_8U** matrix,
which is why `--levels` caps at 256.

### level set

The set of pixels of a **mask** whose value falls below a given threshold — that
is, the pattern of dots the mask produces at one particular output density.
A mask is judged level set by level set, because a mask that disperses well at
50% may clump badly at 10%. `test/levelfit` compares a synthesised mask's
**clumping** against the sample's across nine densities.

### level spacing

Where the **rungs** of the **ladder** are placed, selected by `--level-spacing`,
and it is a statement about what display the file is for. `srgb` puts them at
evenly spaced 8-bit pixel values and takes each rung's light to be what that
pixel decodes to — right when the file *is* the display signal. `linear` puts
them at evenly spaced amounts of light. `device` puts them at the evenly spaced
steps of a display that has exactly `--levels` of its own, such as a 3-bit
monochrome panel, where the file carries only level indices. The three differ by
up to 2% of a rung's light, and only when `levels-1` does not divide 255.

### linear light

Light measured in proportion to actual radiant power, as opposed to the
perceptually spaced encoding a file usually holds. All averaging in this project
happens here — frame averaging, block averaging for `--supersample`, and both
error ledgers — because that is what physically adding light does. The **sRGB**
curve is applied exactly at the encode and decode boundaries and never inside
the dither loop. Contrast **display units**.

### local equalisation

What `direct` does to its sample instead of a **high-pass**:
`--direct-equalise <sigma>` replaces each pixel by its quantile within a
Gaussian neighbourhood of that sigma, ties broken at random from the seed — the
randomised probability integral transform against the local distribution.
Every neighbourhood of the result is uniform, so a ranked tile cut from it has
the same density everywhere. A high-pass equalises only a neighbourhood's mean
and leaves its amplitude, its shape and any clipping, and for `direct` alone
that matters, because its field *is* the mask. It runs on the sample as
recorded, since subtracting anything first breaks a clipped tie group into a
gradient. 0 restores the high-pass. `maskutil::local_quantile`.

### log-opinion pool

Combining several probability distributions by taking a weighted *geometric*
mean — equivalently, a weighted sum of their logarithms — and renormalising.
The `markov` generator pools its pairwise conditional tables this way. The total
weight is `1/temp`, so **temperature** controls how sharp the pooled
distribution is, and adding more pairs dilutes each one's vote rather than
adding to the total.

### lookup table

Written **LUT**. An array indexed by input value holding the corresponding
output value, used in place of computing it. `vision::encode_levels` maps level
indices to pixel values through one, and the **sRGB** decode of an 8- or 16-bit
input is done through one sized by the input's bit depth.

### Mahalanobis distance

A distance that accounts for how much the reference data naturally varies in
each direction, so that a difference along a direction the data varies in counts
for less than the same difference along one it does not. `test/patchfit` scores
a mask by this distance against the spread of real patches of the film, which is
why its unit is "real-patch standard deviations" and why its **floor** is not
zero.

### Mann–Whitney U

A rank-sum test for whether two samples are drawn from the same distribution,
making no assumption about the shape of either. `test/seamlab` and
`test/masktest` use it for **seam** detection: are the differences across the
wrapping pair of columns a plausible draw from the differences across all the
interior pairs? Reported as a z-score, and *signed*, because a repair that
overshoots makes the wrapping pair *more* alike than the texture and that is a
defect too.

### marginal

The distribution of a single variable, ignoring the others. For a **threshold
mask** the required marginal is flat — every value equally often — which is what
**rank-flattening** imposes. The `markov` generator quantises both its sample and
its field by *rank* rather than by value, so that the model's marginal and the
mask's requirement are the same object. See **conserved marginal**.

### Markov random field

Written **MRF**. A model in which each pixel's distribution depends only on its
neighbours. The `markov` generator fits a *pairwise* one — a separate table of
`P(centre | two neighbours)` for each of `--markov-pairs` neighbour pairs — and
combines them with a **log-opinion pool**.

### mask

Short for **threshold mask**.

### mask cache

Built masks kept in `~/.config/dither/masks/` and reused. A **threshold mask**
is a pure function of the parameters, the sample's contents and the program, so
a reused one is byte-identical to a rebuilt one. The key is derived by walking
the **parameter registry** for everything that `applies()` to the run, plus a
hash of the sample's bytes and the executable's own identity — the last of which
means rebuilding a **mask generator** invalidates every mask it made. The
failure it is designed against is not a miss but a *wrong hit*: a mask built for
one configuration served silently for another. Parameters declared
**intermediate** are left out.

### mask generator

One of the five ways this program obtains a **threshold mask**, selected by
`--mask-method`: `blue` (**void-and-cluster**, needs no sample), `dft` (imposes
a sample's **amplitude spectrum**), `markov` (fits a **Markov random field** to
a sample), `inpaint` (**Efros–Leung** synthesis from a sample) and `direct` (uses
a tile of the sample itself). All five return a histogram-flat **CV_8U** mask
spanning 0..254. Four are **toroidal** by construction; `direct` has to be made
so.

### maximum likelihood

Written **ML**. Choosing model parameters to make the observed data as probable
as possible. Tried for the `markov` generator's pooling weights
(`--markov-weights ml`) and for its **temperature**, and it is the wrong objective
for both: it predicts the sample's own pixels better and produces worse texture,
because prediction rewards concentrating on strong, mutually redundant near
pairs while the texture needs the long-range and off-axis constraints that only
the weak pairs carry.

### minimum error boundary cut

Efros and Freeman's technique for joining two overlapping texture patches: in
the region where they overlap, find the path along which they differ least and
switch from one to the other there, so that the join falls where the two
already agree. Found by **dynamic programming**. The `direct` **mask generator**
applies it to a tile and its own continuation, once per axis, to make the tile
**toroidal**; the band it cuts through is `direct.overlap`. It replaced the
**periodic plus smooth decomposition**. Also called *image quilting*. See
[DESIGN.md][r]'s References.

### mutual information

Written **MI**. How much knowing one variable tells you about another, in nats
or bits. The `markov` generator uses it to choose which neighbour pairs to
model, and `--markov-weights mi` optionally uses it to weight their votes.

It is *not* what `--mask-calibrate` optimises, though it was in the first design
of that: matching a fixed share of the sample's mutual information, target 0.65,
turned out not to generalise, because the share at the optimum is a property of
the texture rather than a constant. See **temperature**.

### nats

The unit of information when logarithms are taken to base *e*, as they are
throughout this project's model diagnostics. One nat is about 1.44 bits.

### Nyquist frequency

Half the sampling rate: the highest frequency a sampled signal can represent.
See **aliasing** and **band-limit**.

### orientation

Which end of a **mask**'s order a sample's bright grain lands at. A mask lights
its lowest cells first, so a mask built from a sample has to give the
sample's *brightest* pixels the lowest thresholds, or the dither shows the
grain's negative — dark clumps as light ones. Every generator ends in
`maskutil::threshold_mask`, which states that convention once. It was the other
way round until November 2026, unnoticed because the scoring tools ranked their
references the same way and the inversion cancelled.

### origin

Where a parameter's value came from, recorded by the **parameter registry** and
shown by `--explain`. `User` is a command-line value and outranks everything.
`Measured` is one loaded from the config file. `Implied` is one moved by
**reconcile**. `Derived` is one a rule computed. `Fallback` is a fixed default.
The distinction is load-bearing: a guard that tested `Measured` where it meant
`User` silently ignored `--markov-temp`.

### parameter registry

`params::Params`, the lazy resolution graph behind `--explain` and `--set`. Ask
for a value with `P.num(key)` and its rule runs once, recording what it
depended on. A value the user supplied is pinned and never recomputed. Each rule
carries its own permitted range and declares when it applies. Every generator
registers its own rules, so the mechanism knows nothing about dithering. See
[DEFAULTS.md](DEFAULTS.md), and **origin**.

### periodic plus smooth decomposition

Moisan's method for removing the **cross artifact**. Any image splits uniquely
as `u = p + s`, where `s` is smooth and carries the whole of the boundary
mismatch and `p` is therefore periodic — its opposite edges meet as well as its
interior neighbours do. `s` is the solution of a discrete Poisson equation whose
data is the edge mismatch, which in the frequency domain is one division. The `direct` mask
generator used it to make its tile **toroidal** until the **minimum error
boundary cut** replaced it; `maskutil::periodic_component` remains, tested
against the paper's own criterion, with nothing calling it. It lost twice. `s`
is subtracted from the whole tile, not just its edges, leaving a broad tilt that
**rank-flattening** does not remove (see **swing**); and a solve drives both
axes' boundary jumps toward zero at once, so on anisotropic texture one axis is
necessarily over-corrected into a join smoother than its surroundings. See
[DESIGN.md][r]'s References.

### phase

The positional information in a **discrete Fourier transform** — where each
sinusoid's peaks fall, as distinct from how tall they are. Everything about how
texture is *arranged*, as opposed to how much of it there is at each scale,
lives here: **clumping**, **ridgeness** and **envelope correlation** are all
phase properties. The `dft` generator randomises phase, which is precisely why
it matches spectra perfectly and reproduces none of those.

### phase separation

The tendency of a system whose local rule favours agreement, under a **conserved
marginal**, to segregate into regions of like value. Its equilibrium at a 50:50
split is a labyrinth rather than a field of blobs, which is what **worming** is.

### principal component analysis

Written **PCA**. Finding the directions along which data varies most, so it can
be described with fewer numbers. The `inpaint` generator projects each sample
neighbourhood into a 12-dimensional subspace this way before searching for
matches, because a nearest-neighbour search is far cheaper in 12 dimensions than
in the full neighbourhood's.

### quantise

To round values onto a coarser grid. Two distinct uses here. The dither itself
quantises tone onto the **ladder**. Separately, `test/seamlab --quantise` rounds
a field to just below the sample's own grey-level step, which matters because an
8-bit sample cannot distinguish differences finer than one level, so anything
finer that survives a **high-pass** is filter residue rather than data — and
ranking it renders that residue at full contrast.

### rank-flattening

Replacing each value by its rank, scaled so the result covers `0..254` evenly.
This is what makes a field into a **threshold mask**, because a mask is required
to have a flat histogram. It is a *global* monotone map, which has a consequence
that caused a real defect: it flattens the histogram over the whole tile while
leaving any broad tilt intact as a variation in the *local* mean. It is also
**scale-free** — it maps whatever ordering exists onto the full range regardless
of amplitude — so a low-frequency component of any size at all becomes a
full-scale density modulation, and so does the sub-code residue a **high-pass**
manufactures. The `direct` generator therefore **quantises** first and then
breaks the resulting ties at random; see **tie share**.

### reconcile

`Params::reconcile()`, which runs once before anything reads a value and moves
defaults so that an option the user actually supplied is not ignored because
some *other* parameter was left at its default. Naming `--mask-sample` is a
request for a mask, so the **threshold rule** default has no business staying on
`round`. It searches the product of the gating parameters' candidate lists for
the assignment stranding the fewest supplied options, breaking ties towards
moving nothing. What it cannot rescue becomes an error naming both sides, rather
than a note printed beside a zero exit status.

### ridgeness

Whether the clumps of a pattern are round or stringy, measured as the structure
tensor coherence of each octave: 0 for a field of blobs, 1 for a field of
ridges. It is what makes **worming** a number rather than something to judge by
eye. Reported by `test/patchfit` as `Rfine`, `Rmid`, `Rcoar`.

### root mean square

Written **RMS**. The square root of the mean of the squares — the standard way
of summarising a set of deviations into one number. `test/maskfit` and
`test/levelfit` both report the RMS of the base-10 logarithm of the ratio
between the mask's statistic and the sample's, so that a factor of two too big
and a factor of two too small count equally.

### rung

One step of the **ladder** — a single output **level**, together with its 8-bit
pixel value (`px`) and its **linear light** value (`lin`). Under
`--level-spacing srgb` the rungs are evenly spaced in pixel value and therefore
*unevenly* spaced in light, which is the whole reason **display units** and
linear light have to be distinguished.

### seam

The join where a **tile** meets its own opposite edge when repeated. A crop of a
photograph has a left edge that has never met its right, so tiling it puts a
discontinuity at a fixed period, which reads as a grid over the picture. Made
visible as a number by the **Mann–Whitney U** rank sum in `test/seamlab` and
`test/masktest`; the sign matters, because a repair can overshoot into a join
that is *smoother* than the surrounding texture, which is equally visible.

### serpentine scan

Walking even rows left-to-right and odd rows right-to-left, rather than always
left-to-right. It avoids the directional "worming" that a pure raster scan
produces in **error diffusion** — a different and older use of that word from
the **worming** defined below. `for_serpentine` implements it, and `dir` is `+1`
or `-1` accordingly.

### Space

The conversion between **display units** and **linear light** inside the engine,
and the object that decides which quantity the error ledgers conserve. Both
ledgers run in linear light; `Space::of` returns the identity for a uniform
**ladder**, so default output stays bit-identical rather than merely equal.
`Space::to_display` snaps to a **rung** within a tolerance, because the
**temporal budget** pays itself down to a whole number of rungs and has to be
able to ask for that number back.

### spatial_err

The per-scan **error diffusion** channel — a **CV_32F** matrix, freshly zeroed
inside every `quantize_scan`. It carries error between pixels within one pass
and never across frames, which is why single precision is right for it where the
**temporal budget** ledgers need double.

### SpatialFn

The function type `void(*)(int row, int col, int dir, cv::Mat& spatial_err,
float value)` — deposit `value` into neighbouring cells of the diffusion
channel, or discard it. `spatial_none` is the no-op; `spatial_floydsteinberg`
implements **Floyd–Steinberg**. `dir` is the **serpentine scan** direction.

### sRGB

*standard Red Green Blue*, the colour space of IEC 61966-2-1 and the default
assumption for an image file. Its transfer curve is roughly a 2.2 power law,
and its purpose is to spend the available codes where the eye can tell them
apart. This program decodes it on input and encodes it on output, so that
everything between is **linear light**. `--linear` disables it entirely.
See [DESIGN.md][r]'s References.

### supersample

`--supersample N` runs the dither on a grid `N` times larger in each direction
and then averages each `N`×`N` block back down, in **linear light**. It is how
grain coarser than the picture detail is achieved without destroying the detail.

### swing

`test/seamlab`'s measure of the low frequency a seam repair leaves behind: the
standard deviation of the field's *local* mean, in the sample's own grey levels.
It exists because **rank-flattening** guarantees the global histogram and says
nothing about local flatness, so a broad tilt can survive it while every other
check reports the mask as fine — and a dithered ramp then fails `test/colstats`.

### temperature

`markov.temp`, and the single scalar that decides the `markov` generator's
output. It is the total weight of the **log-opinion pool**, inverted: low
temperature means sharply peaked conditionals and a strongly coupled field.
There is a *critical temperature*, written **T_c** and measured at about 0.48,
below which the field orders and keeps ordering — see **phase separation** and
**worming**. `--mask-calibrate` measures the right value per sample, by drawing
probe masks across a range of temperatures and taking the one whose **level
set** **clumping** best matches the sample's own ranked crop — the measure
`test/levelfit` reports, computed in process. Matching a fixed share of the
sample's **mutual information** was tried first and abandoned; see that entry.

### threshold mask

A tile of values that a **threshold rule** compares against, so that the
decision to round up or down depends on position. `MaskDither` compares
`frac(v) * 254` against `mask[(row + sy) % rows][(col + sx) % cols]`, where `sx`
and `sy` are a per-frame **toroidal** offset. The mask must have a flat
histogram spanning 0..254 — see **rank-flattening** and **threshold_byte** —
or the output tone is wrong.

### threshold rule

The decision of which **level** to round a value to. Selected by `--threshold`:
`round` (nearest), `random` (stochastic), `mask` (compare against a **threshold
mask**). `blue` is the old spelling of `mask`, rewritten at parse time.
Implemented as a **DitherFn**.

### threshold_byte

The function placing rank `r` at quantile `r/254` when building a **threshold
mask**. The 254 is what makes `MaskDither`'s `frac(v) * 254 > t` fire with
probability `frac` and so average to the target; against 255 every mask-dithered
image came out slightly light.

### tie share

The chance that two pixels drawn at random from a field hold the same value —
the sum of the squared class frequencies. After **quantising**, it is the
fraction of a **threshold mask**'s ordering that is decided by the tie-break
rather than by the sample, so it says how much of the mask is really an
imitation. The `direct` generator refuses above 0.25. It is used in place of a
count of distinct values because a count does not separate the cases: a
fine-grained Panatomic-X scan survives quantising with only 50 distinct levels,
fewer than several synthetic patterns that must be refused, yet its tie share is
0.065 against their 0.39 to 1.00.

### tile

A **threshold mask**, viewed as something laid out repeatedly over the image.
`MaskDither` indexes the mask modulo its size, so the mask always tiles,
and whether that repetition shows is a property of the generator —
`markov.max_repeats` is 4 because its tile reads as banding at 16, while
`blue.max_repeats` is 64 and never binds because **blue noise** is locally
featureless.

### time budget

`--budget <time>`, which picks the mask size and the `markov` pair count to fit
a wall-clock time, using per-generator work formulas divided by a throughput
measured from real builds and cached. Opt-in: unset, every generator keeps its
fixed ceiling and output is unchanged. Distinct from the **temporal budget**,
which is about frames.

### toroidal

Wrapping in both directions, so that the right edge joins the left and the
bottom joins the top — the surface of a torus. A **threshold mask** must be
toroidal, because it is indexed modulo its size and any mismatch at the wrap
becomes a **seam** repeated across the whole picture at a fixed period.

### void-and-cluster

The method the `blue` **mask generator** uses, and the classical way to build a
**blue noise** mask: repeatedly place a pixel at the lowest-energy empty
location and update a toroidal Gaussian energy map. Named for the two things it
alternately fixes — the largest empty region and the tightest group. See
[DESIGN.md][r]'s References.

### white noise

Noise with equal power at all spatial frequencies, and therefore no
characteristic scale. It is the reference against which `test/maskstats`
normalises, so an uncorrelated field reads 1.0 on every statistic. As a
**threshold mask** it is poor — it clumps and leaves voids at every density —
which is what **blue noise** improves on.

### worming

The tendency of the `markov` generator's clumps to stretch into connected
ridges. It is **phase separation** under a **conserved marginal**, not a quirk
of the sampler: it peaks at the median density, has a **critical temperature**
of about 0.48, and coarsens the longer the field is swept. It cannot be
separated from the **clumping** that generator exists to produce, because they
are the same dynamics — which is what motivated the `inpaint` generator, which
has no **temperature** and therefore no transition to fall below. Measured as
**ridgeness**. Distinct from the older sense of the word used under **serpentine
scan**.
