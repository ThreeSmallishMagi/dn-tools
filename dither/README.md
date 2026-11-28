# dither

Start by running "source aliases"

A command-line image dithering tool. All quantization happens in *linear light*
— light measured in proportion to actual radiant power — with the sRGB curve
(*standard Red Green Blue*, the transfer curve of IEC 61966-2-1, which an
ordinary image file is encoded through) applied only at the encode and decode
boundaries. The result is that the dot density of the output averages back to
the tone of the input, which is the one property everything else here serves.
Supports multi-level output, blue-noise masks, and averaging over several
dithered frames.

## Documentation

| | |
|---|---|
| [doc/GLOSSARY.md](doc/GLOSSARY.md) | Every term of art in one alphabetical list. **Start here if a word below is unfamiliar** — this file is where each is defined, and the others link to it rather than redefining. |
| [doc/DESIGN.md](doc/DESIGN.md) | What the program is, what it has to get right, and why each part is shaped the way it is. Carries the [References](doc/DESIGN.md#references) — every paper and web source the implementation draws on. |
| [doc/ARCHITECTURE.md](doc/ARCHITECTURE.md) | The module map, the load-bearing abstractions, and the record of structural problems and what has been done about them. |
| [doc/DEFAULTS.md](doc/DEFAULTS.md) | The full parameter inventory and the design of the mechanism that derives defaults from whichever options you actually supplied. Read it before adding a tuning knob. |
| [doc/UX.md](doc/UX.md) | How the command-line surface should behave: naming options, wording refusals, what a default owes the user. Ten principles, each with the bug or decision that earned it. |
| [doc/README.md](doc/README.md) | Index of the above, plus the working notes kept alongside them — use cases, the original markov brief, bug repros, and each round of external review with its triage. |

The rest of this file is the command-line reference.

## Synopsis

```
dither <image> [options]
```

The defaults — round to nearest, full Floyd-Steinberg, two levels — need no
options:

```sh
dither photo.jpg --out out.png
```

## Input depth

Input may be 8- or 16-bit; output is always 8-bit.

The difference is not cosmetic. Error diffusion and the multi-frame budget both
exist to render a tone that no single output pixel can show — and against an
8-bit input in an 8-bit ladder there is no such tone, because the target was
already rounded to the grid before either mechanism saw it. Give them 16 bits
and there is something to work with.

Measured on a gradient spanning four 8-bit codes, at `--levels 256`, scored as
root-mean-square (RMS) error against what the picture really is:

| frames | 16-bit in | 8-bit in |
|---|---|---|
| 1 | 61.9 | 793.0 |
| 4 | 25.0 | 793.0 |
| 16 | 12.7 | 793.0 |

The 8-bit column never moves: extra exposures buy nothing. So

```sh
# a 16-bit gradient on an 8-bit display, without banding
dither gradient16.png --levels 256 --spatial 1 --out out.png

# the same, exposed as 16 8-bit frames in sequence on a digital enlarger
dither gradient16.png --levels 256 --frames 16 --save-frames --out out.png
```

Note that the file `--out` writes for a multi-frame run is a *preview*: it is
the mean of the frames re-encoded to 8 bits, so it understates what the sequence
actually exposes. The frames written by `--save-frames` are the product.

## How a dither is specified

A dither is two independent choices: how each pixel picks a level
(`--threshold`), and how much of the leftover error is pushed onto its
neighbours (`--spatial`).

`round`
:   Deterministic round-to-nearest level. The default.

`random`
:   Stochastic threshold. Fast and grainy on its own; mixed with diffusion it
    breaks up Floyd-Steinberg's repeating patterns.

`mask`
:   Threshold against a noise mask. Best perceptual quality, especially at low
    level counts and for print. Which mask is `--mask-method` — void-and-cluster
    blue noise by default, or one synthesised to imitate a sample. Spelled
    `blue` before there was more than one generator; that spelling still works.

`--spatial` scales Floyd-Steinberg error diffusion from `1` (full) down to `0`
(none). Naming a `--threshold` drops it to `0`, on the grounds that a threshold
you picked deliberately is usually one you want to see on its own. Pass
`--spatial` explicitly, on either side of `--threshold`, to combine the two:
`--threshold mask --spatial 0.5` is a mask dither with half-strength diffusion.

## Options

`--threshold <name>`
:   How each pixel picks its level: `round`, `random`, or `mask`. Default
    `round`.

`--spatial <frac>`
:   How much of the Floyd-Steinberg error from its neighbours each pixel's
    threshold sees. `1` is full Floyd-Steinberg, `0` none, and in between damps
    the diffusion and softens the directional streaking a Floyd-Steinberg raster
    leaves; the error itself is always passed on in full, so the tone stays
    right. It applies to the total of a `--frames` run as well as to each frame.
    Defaults to `1`, or to `0` when `--threshold` is given; an explicit value
    always wins.

`--out <path>`
:   Output file path. Default `dither.png`.

`--levels <n>`
:   Number of output tones, 2 to 256. Default `2` (black and white). More levels
    reduce visible dithering but need a display or printer that can reproduce
    the intermediate tones.

`--level-spacing <mode>`
:   Where those tones sit, which is a statement about the display the output is
    going to. `srgb` (default) spaces them evenly in pixel value — 3 levels are
    0/128/255 — putting the middle level at perceptual half and keeping shadow
    noise down; it is right when the 8-bit file *is* the display signal.
    `linear` spaces them evenly in linear light, so 3 levels are 0/188/255.

    `device` is for a display that has exactly `--levels` of its own — a 3-bit
    mono panel, say. There the file only carries the level indices, and what the
    panel emits for index k is the curve applied to k/(levels-1), not what the
    rounded 8-bit stand-in decodes to. Those differ by up to 2% of a rung's
    light: a 3-bit panel's lowest rung emits sRGB(1/7) = 0.0180 while pixel 36
    decodes to 0.0176. Left on `srgb`, an 8-level dither comes out 0.39/1000
    dark on such a panel; on `device` it is 0.01/1000. The two agree exactly
    whenever `levels-1` divides 255 — so at 2, 4, 6, 16, 18, 52, 86 and 256
    levels there is nothing to choose.

    No effect at `--levels 2`, where every curve agrees at the endpoints.

`--frames <n>`
:   Dither *n* independent frames and average them; for `mask`, each frame uses
    a random shift of the same mask. Each pixel's total over the frames is
    dithered first, at the same `--spatial`, and the frames then divide it, so a
    shortfall in one frame is paid back by a later one.

`--supersample <n>`
:   Dither at *n*× resolution and average each *n*×*n* block back down in linear
    light. The output keeps the input's size but carries *n*²+1 tones per pixel,
    and — the reason it exists — the mask's grain can then sit finer than the
    picture detail, which is how film resolves grain.

`--out-size <side>`
:   Fit the output inside a *side*×*side* square, keeping its aspect ratio. With
    `--supersample` the result is scaled from the dither grid in one step rather
    than via the input's size, so nothing is quantised to 8 bits in between. At
    exactly the input's longer edge × `--supersample` the grid comes out whole,
    every pixel one of the `--levels` tones: supersampling without reducing.

`--save-frames`
:   Also write each frame beside the average, as `<stem>-N.png`. Without it, a
    multi-frame run says that its output is a preview.

`--linear`
:   Treat pixel values as raw linear light, skipping the sRGB conversion that
    normally wraps the dither.

`--seed <n>`
:   Random seed, for reproducible masks and stochastic thresholds. Default `42`.

`--debug-pixel <r,c>`
:   Print the per-frame `ideal`/`display`/`displayed` trace for one pixel to
    stderr.

`--budget <time>`
:   Spend about this long building the mask, and choose the mask size and the
    markov pair count to suit. Accepts `30`, `30s` or `2m`.

    Left unset — the default — each generator's fixed size ceiling applies and
    output is exactly what it always was. Given, the ceiling becomes "the largest
    that fits in the time", which is what a pixel cap was always standing in for.
    The floors still win: a tile that would visibly repeat is built anyway, since
    repetition is a defect and the budget is only a preference.

    The allocation spends on mask size first, then on pairs, and **stops at 256
    pairs** — beyond that the mask measurably gets worse, so there is nothing
    useful left to buy. `--markov-temp` follows the pair count automatically.

    Timing is machine-specific, so "about 30 seconds" means on the machine in
    front of you. Speeds are measured from real builds and cached; the first run
    on a new machine says it is working from a guess.

`--config <path>`, `--no-config`
:   Where to keep what has been measured, default `~/.config/dither/cache.conf`
    (`XDG_CONFIG_HOME` honoured), with built masks in `masks/` beside it. It
    holds this machine's build speeds, which only `--budget` reads, and the
    calibrations made for samples (see `--mask-calibrate`), which **every** run on
    that sample uses. Safe to delete: the speeds are measured again, the masks
    rebuilt, and the calibrations reverted to the built-in rules until
    `--mask-calibrate` or `calibrate_samples` measures them again. `--no-config`
    neither reads nor writes any of it, so it is also how to get the rules'
    values regardless of what has been calibrated.

`--explain`
:   Print every parameter as resolved — its value, where the value came from,
    and which other parameters it was derived from — then exit without
    dithering. The five origins are defined above the table it prints: `user`,
    `implied` (a default moved so that an option you did give is not ignored),
    `measured`, `derived` and `fallback`. Parameters that derive their defaults
    from other options are listed here; see `DEFAULTS.md`.

`--set <key=value>`
:   Override any parameter by name, including ones with no option of their own.
    `--explain` lists the names. Intended for experiments; anything worth
    setting regularly should get a rule instead.

`--help`, `-h`
:   Show the built-in option summary.

## Mask generators

`--threshold mask` thresholds against a noise mask, and `--mask-method` chooses
which mask. All of them yield a histogram-flat, density-preserving mask however
clumped it looks — that is what makes one usable as a threshold. All but
`direct` are also *toroidal* by construction — wrapping cleanly in both
directions, so that repeating the tile shows no join; `direct` repairs a seam
instead, and says so below.

`blue`
:   Void-and-cluster blue noise, built from scratch by repeatedly placing a
    pixel wherever the existing ones leave the largest gap. The default, and the
    right answer unless you are imitating a texture.

`dft`
:   Named for the *discrete Fourier transform* (DFT), which re-expresses an
    image as a sum of sinusoids, each with an amplitude and a phase. This
    generator matches a `--mask-sample`'s amplitude spectrum — how much
    structure it has at each scale — by alternating two projections: impose the
    spectrum, then re-rank to a flat histogram. The whole two-dimensional
    spectrum is used, so a directional sample keeps its direction, but phase is
    randomised, so anything higher-order (edges, motifs, a weave's alignment) is
    not reproduced: a woven sample yields woven-*looking* noise.

`markov`
:   Fit a *Markov random field* — a model in which each pixel's distribution
    depends only on its neighbours. Here that means conditional distributions
    for what a pixel is allowed to be given pairs of its neighbours, drawn from
    by *Gibbs sampling*: repeatedly replace each pixel with a draw from its
    distribution given the others. This does see higher-order structure: a
    film's darkest grain clumps several times harder than its lightest, and
    `markov` reproduces a good part of that asymmetry where `dft` flattens it
    away completely. After every sweep its power spectrum is matched to the
    film's (*spectrum matching*, `--markov-bands`), which keeps the structure it
    grows at the grain's scale. It costs seconds rather than tenths.

`inpaint`
:   Fill each pixel from the sample neighbourhood that most resembles the one
    around it. Where `markov` pools its neighbours in pairs, this conditions on
    the whole neighbourhood at once, which removes markov's grainy floor. Its
    sweeps still drift — each makes neighbourhoods agree a little more — so it
    too matches its spectrum to the film's after every sweep
    (`--inpaint-bands`). Of the three that synthesise it is the closest to a real
    patch of film, on the measurement that asks whether a patch of it would
    disappear inside the sample, and the slowest to build.

`direct`
:   Use the `--mask-sample` itself rather than imitating it: equalise it, take a
    tile, rank it flat. Everything true of the grain is true of the mask, so this
    is the best mask available and the upper bound the other three are measured
    against — it scores 9.0 and 1.8 on the two film samples where the closest
    imitator scores 9.4 and 4.5, and on the second it is below that film's own
    patch-to-patch floor.

    Two costs. It cannot invent, so the sample bounds the mask: left to derive
    its own size, the mask shrinks to what the sample can supply, and a size you
    pin with `--mask-size` that the sample cannot supply is refused rather than
    quietly shrunk. And a crop
    of a photograph has a left edge that has never met its right, so its join has
    to be made rather than being there already: the tile is taken from a slightly
    larger crop and cut along the path where the two overlapping candidates
    differ least (Efros & Freeman's *minimum error boundary cut*). Every output
    pixel still comes from exactly one place in the sample — nothing is
    averaged — which is what keeps the paragraph above literally true. The seam
    measures 12.5–17.9x better than an uncorrected crop and passes the same
    toroidality check as the four synthesised masks.

    `--direct-overlap <px>` widens or narrows that band; `0` disables the
    repair and gives a plain centred crop. Left alone it derives from
    `--mask-highpass`, and there is no measurable quality difference anywhere
    between 4 px and 64.

    It refuses a sample with no usable texture, and says what fraction of the
    mask would have been noise rather than your sample.

    The sample is *equalised* rather than high-passed: each pixel becomes its
    rank within a Gaussian neighbourhood (`--direct-equalise`, 24 px by
    default), which leaves every neighbourhood of the tile at the same density
    and so leaves no line where the tile repeats. `--direct-equalise 0` takes
    the subtractive high-pass instead; that manufactures detail — a blur of
    integers is not an integer — so the tile is then rounded to the sample's own
    resolution (`--direct-quantise <codes>`, `0` off) and ranked with ties broken
    at random.

`--mask-method` defaults to `blue`, or to `dft` when `--mask-sample` names a
sample to imitate. Naming any of `dft`, `markov`, `inpaint` or `direct` without
a sample takes `Kodak-4X` from the sample library, which `install_dither`
installs, and says so; without the library it is an error. Naming `blue` with
a sample is an error too.

Every mask built from a sample lights the sample's *brightest* grain first, so
the dither shows the grain the way the scan does rather than its negative.

## Mask options, any generator

`--mask-size <WxH>`
:   Build a WxH tile and repeat it over the image. Larger sizes are honoured
    when asked for.

    Left unset it is square, and one of 256, 1024 or 4096 — so that one mask
    serves many pictures and the cache can reuse it — chosen from the grid the
    dither runs on (image × `--supersample`), from `--mask-highpass` and
    `--mask-scale` — the tile has to be able to contain the coarsest structure it
    is allowed to have, so asking for coarser grain enlarges it — and from how
    much repetition the chosen generator can stand before it shows. For
    `direct` it is also capped at what the sample can supply. That last part differs sharply
    between generators: a `markov` tile repeated 16 times reads as banding in
    flat areas, while blue noise is featureless enough that a repeat of it is
    still featureless. `--explain` shows the value and everything it came from.

`--mask-in <path>`
:   Use this image as the mask instead of building one; read as raw grayscale
    and tiled like a generated mask. A mask-building option given with it is
    refused rather than ignored, since it could not mean anything.

`--mask-out <path>`
:   Write the mask in use to this path, for inspection or for feeding back in
    through `--mask-in`. Every generator seeds its own random number generator (RNG) from `--seed`, so
    `--mask-out` then `--mask-in` reproduces a run byte for byte.

## Blue-noise options

These apply to `--mask-method blue` only.

The kernel cutoff and half-width used to be `--mask-eps` and `--mask-radius`.
They are `--blue-eps` and `--blue-radius` now, and the half-width is *derived*
from the cutoff, since it can only truncate the kernel or waste time — so
setting it is an experiment rather than a control. `--explain` shows both
values and what the derived one came from.

`--blue-sigma <val>`
:   Kernel radius in pixels — how far dots repel each other. Default `1.8`; try
    `1.0`–`4.0` depending on output resolution.


## Sample options

These apply to every generator that takes a sample: `dft`, `markov`, `inpaint`
and `direct`.

`--mask-sample <img>`
:   The texture to imitate. Point it at a flat patch of film grain and the
    dither takes on that grain's character. It may also name a directory of
    scans of one film, or a film in the library at
    `~/.config/dither/samples/<film>/`, and markov and inpaint then pool every
    scan. `direct` and `dft` take a film name to mean the film's crop at
    `~/.config/dither/crops/<film>.png`: a square of clean grain, which
    `install_samples` cuts for the shipped films and you save yourself for your
    own. `direct` takes a tile of it, and `dft` would otherwise copy the scans'
    edges and horizons into the mask as streaks. Without a crop, `direct` uses
    the middle of the largest scan and `dft` pools the scans, and each says so.

`--mask-highpass <px>`
:   Ignore structure in the sample coarser than this, `0` to disable. The
    default is `32` for `dft` and `direct` and `0` for `markov` and `inpaint`,
    which model the sample's values as they are, tone included. A whole
    photograph is mostly its subject, so where the sample is whole scans that
    tone is learned too; `markov` and `inpaint` match their spectrum to the
    film's crop (`--markov-bands`, `--inpaint-bands`), which keeps the scale
    right but cannot remove what the scans taught them. For `markov` a high-pass
    also holds the field down past the modelled radius.

`--mask-scale <s>`
:   Feature size relative to the sample: `2` twice as coarse, `0.5` twice as
    fine. Default `1`. Grain coarser than the picture detail will swallow it —
    pair a coarse scale with `--supersample`. `dft` remaps frequency, which is
    exact; `markov` and `inpaint` resample the sample, so above `1` they also
    learn the interpolator's own smoothness as though it were grain.

The iteration count used to be `--mask-iters`, one option covering two different
quantities. It is now split in two — `--dft-iters` (alternating projections, 20)
and `--markov-iters` (Gibbs sweeps, 2). Beyond its default each stops improving,
and markov actively degrades: the clumps stretch into worm-like ridges that
film grain does not have. That is why the sweep count is 2 and not the 12 it
was — see *worming* in [doc/GLOSSARY.md](doc/GLOSSARY.md), which is not a tuning
accident but the field's ordering transition.

## Markov options

These apply to `--mask-method markov` only.

`--markov-radius <r>`
:   Half-width of the neighbourhood the conditioning pairs are drawn from,
    default `5`. Nothing coarser than this is modelled; what the field does at
    longer range is kept at the film's by spectrum matching instead
    (`--markov-bands`), because an unconstrained local model drifts there and
    grows blobs that are in no sample.

`--markov-bands <n>`
:   Spectrum matching, in *n* frequency bands. Default `16`; `0` turns it off.

    After every sweep the field's power spectrum is divided into *n* rings of
    frequency, from one cycle across the mask out to the finest detail the mask
    can hold, and every frequency in a ring is scaled by one factor so the
    ring's power equals the film's in the same ring. The sweep still decides
    where everything goes; this decides only how much structure there is at
    each scale — coarse blobs against fine grain — and holds it at the film's.
    The film here is its crop, equalised so that it describes grain rather than
    the window's soft shading.

    Fewer bands match only the broad slope of the spectrum; more follow it more
    closely, though the coarsest rings then hold very few frequencies and are
    noisy. Every measurement was made at 16. Without it the sweeps drift away from the film's scale in whichever
    direction the film's texture pushes them: across six films, matching roughly
    halves `test/patchfit`'s distance and divides `test/grainfit`'s by five.

`--markov-pairs <n>`
:   How many neighbour pairs condition each pixel, default `64`. They are chosen
    by mutual information with the centre pixel — a pair whose distribution is
    just the overall pixel distribution tells you nothing — stratified over
    distance so the near pairs, which always score highest, cannot take every
    slot. Cost is linear in this.

    Raising it **needs `--markov-temp` lowered to match**, or the texture washes
    out. Each pair's vote is `1/(n × temp)` and the votes sum to `1/temp`, so
    adding pairs divides a fixed total and the result drifts toward white noise.
    The derived temperature follows the pair count: about 8% lower for ×4 the
    pairs.

`--markov-bins <n>`
:   Quantisation of a conditioning neighbour, default `16`. Each pair is a table
    of n² conditioning cells, all of which have to be estimated from the sample,
    so raising this needs a bigger sample to stay meaningful.

`--markov-temp <t>`
:   Sharpness of the pairs' combined verdict. Lower couples the field more
    tightly, until it is smooth blobs; higher softens it towards white noise.

    Left unset it is derived: `0.50` at the defaults, from a rule that follows
    `--markov-bins`, `--markov-pairs`, `--markov-iters` and whether the sample is
    high-passed (`0.25` at `--mask-highpass 32`). A calibration for the sample
    replaces it — see `--mask-calibrate` — and the best value differs by more
    than 2× between films, so a calibrated film does noticeably better. With
    spectrum matching on, the default, it matters less than it did: matching
    sets the scale of structure, which used to be the temperature's job.

    It cannot be chosen by fitting the sample: predictive likelihood peaks well
    below the texture optimum, because the pool over-sharpens once the Gibbs
    chain starts feeding on its own samples. Matching a fixed share of the
    sample's neighbour mutual information was tried and does not generalise.
    What works is comparing the thing being judged, which is what calibration
    does.

The vote weighting used to be `--mask-weights` / `--mask-attend`. It is
`--markov-weights mi|ml` now, and `flat` is the default because it measures best
on film grain even though `ml` predicts the sample better — a determined value
rather than a decision to hand the user, which is why it reads as a tuning knob
rather than a control. See `--markov-temp`.

`--mask-calibrate`
:   Measure this generator's settings for the sample instead of deriving them.
    Three generators can, and they measure different things; the result is
    cached against the sample and the settings, and **every later run on that
    sample uses it without the flag**. `install_samples` calibrates each film it
    installs that way, at the default settings, so the shipped films are
    calibrated from the start without a run ever paying for it. `--no-config`
    reads no config and so gets the built-in rules.

    For **markov** it measures `markov.temp`, the weighting and the radius,
    rather than deriving the first from `--markov-bins` and `--markov-pairs`.
    Fits the model once, then samples small probe masks across the combinations
    and keeps the one whose grain clumps most like the sample's.

    For **inpaint** it measures `--inpaint-iters`, the sweep count. Sweeping
    trades clumping against ridging, and the rate is a property of the film, so
    there is no value that suits every sample — across seven scans the best is
    2, 3 or 6. It builds one probe mask per rung and keeps the default unless
    some rung beats it by a clear margin, because the score it uses is a
    two-term stand-in for `test/patchfit` and is reliable at a distance rather
    than at close range. Measured over four films it reads 0.96, 0.77, 1.00 and
    1.06 of the uncalibrated result.

    For **dft** it measures `--dft-smooth-cells`, stepping in the smoothing the
    build actually uses once that coefficient has been divided by the sample's
    area, and on a full-size crop it usually finds nothing to change. It fires on a small sample, where that division is near 1
    — on a 128² crop it picks 2 and beats the derived value by 18% — which is
    the case the parameter was introduced for.

    Worth having for any film. The built-in rules have no sample term, so they
    answer the same for every film, while the measured best temperature ranges
    from 0.35 to 1.08 across the six shipped films. Measured before
    spectrum matching existed, the calibrated mask scored closer on
    `test/patchfit` on five of them, by up to half; the bands have since taken
    over much of what the temperature controlled, so the gap is smaller now.
    Calibration scores against the film's crop, the window chosen to hold only
    grain. `--explain` shows the result as `measured`, and an explicit
    `--markov-temp` outranks it — naming one leaves nothing to measure, and the
    run says so rather than calibrating over the top of it.

## Inpaint options

These apply to `--mask-method inpaint` only.

`--inpaint-iters <n>`
:   Sweeps over the field at each resolution, default `2`. More sweeps make
    neighbourhoods agree more, which grows ridges; the best count is a property
    of the film, which is why `--mask-calibrate` measures it.

`--inpaint-bands <n>`
:   Spectrum matching for inpaint, exactly as `--markov-bands` describes for
    markov. Default `16`; `0` turns it off.

`--inpaint-db`, `--inpaint-radius`, `--inpaint-scales` and the rest are listed by
`dither --help` with their defaults.

`--markov-report`
:   Print a fitting diagnostic to stderr and carry on; the mask is byte-identical
    with or without it. Reports how many sample centres landed in each
    conditioning cell — the answer to "are my bins big enough" — and then a
    held-out log-loss: the model is fitted on the left half of the sample and
    scored on the right, so the gap between train and held-out is the part of
    what it learned that is noise rather than texture. Read it as a percentage of
    what was learned: a few percent means the sample can support a richer model
    (raise `--markov-bins`, then `--markov-pairs`), and a large gap means the tables
    are memorising speckle. Because it refits on half the data it overfits more
    than the shipped model does, so the figure is an upper bound. The split
    assumes the sample looks the same on both sides — true of a flat patch of
    grain, not of a whole photograph, and the report says so when the halves
    disagree.

## Mask cache

A mask is a pure function of the parameters, the sample's contents and the
program itself, and building one costs seconds — nine for a 256² `inpaint`
mask. So each is built once and kept in `~/.config/dither/masks/`, named for
the parameters that made it plus a digest of all of them:

```
inpaint-256x256-hp0-sc1-seed42-d44e7413a1028184.png
inpaint-256x256-hp0-sc1-seed42-d44e7413a1028184.txt
```

The `.txt` beside it lists the full key, so an entry can be read rather than
guessed at. A reused mask is byte-identical to a rebuilt one — the second run
above takes 0.06s instead of 4.70s and produces the same output file.

It misses, and rebuilds, whenever anything that decides the mask changes: any
parameter the run uses, the *contents* of `--mask-sample` (not just its path)
and of the film's crop, or the `dither` binary itself, so working on a generator can never serve you a
mask the old one built. `--no-config` turns it off along with the rest of
`~/.config/dither`; the cache is capped at 256 MB and evicts least-recently-used
entries, and deleting the directory is always safe.

## Examples

Four-level blue dither, tiling a 128×128 mask for speed:

```sh
dither photo.jpg --threshold mask --levels 4 --mask-size 128x128 --out out.png
```

Smooth averaged result from 30 frames:

```sh
dither photo.jpg --frames 30 --out out.png
```

Blue noise with half-strength error diffusion, keeping the mask:

```sh
dither photo.jpg --threshold mask --spatial 0.5 --mask-out mask.png --out out.png
```

Film-grain look: take the character from a film in the sample library, and
supersample so the grain sits below the picture detail:

```sh
dither photo.jpg --mask-sample Kodak-TriX --supersample 4 --out out.png
```

Or from a scanned patch of grain of your own:

```sh
dither photo.jpg --mask-sample grain.png --supersample 4 --out out.png
```

The same, but fitting the grain's clumping rather than its spectrum:

```sh
dither photo.jpg --mask-sample Kodak-TriX --mask-method markov --supersample 4 --out out.png
```

## Build

Requires OpenCV 4.x and a C++17 compiler. Dependencies are listed in
`apt-packages.txt` (system) and `requirements.txt` (Python, for the verification
tools). `install_deps` handles both:

```sh
source aliases
install_deps          # apt, then the dither_env virtualenv
build                 # cmake configure + build, then show this manual
```

`install_deps` leaves `dither_env/bin` on `PATH`, which is what the Python tools
need to find their interpreter. The virtualenv is separate because Ubuntu 24.04
marks its system Python externally managed (PEP 668); `dither_env` is already in
`.gitignore`.

To do it by hand, or on macOS where `install_deps` bows out:

```sh
sudo apt install $(cat apt-packages.txt)   # Ubuntu 22.04 / 24.04
python3 -m venv dither_env
dither_env/bin/pip install -r requirements.txt
cmake -B build && cmake --build build
```

The default build type is Release; configure with `-DCMAKE_BUILD_TYPE=Debug` for
a debug build. Nothing in the C++ build needs the virtualenv — skip it if you
only want the `dither` binary.

## Tools

These sit under `test/`. The Python ones find their interpreter through
`#!/usr/bin/env python3` and shell out to each other, so the virtualenv has to be
on `PATH` rather than invoked as `dither_env/bin/python`. `install_deps` does
that for the current shell; otherwise:

```sh
export PATH="$PWD/dither_env/bin:$PATH"
```

Without it `test/coltest` reports `COLSTATS PARSE FAIL` on every row, which means
`cv2` is missing, not that the dither is broken.

`test/coltest`
:   Runs a matrix of `dither` invocations against `examples/ramp_256x256.png`
    and checks each output for density preservation.

`test/colstats <image>`
:   Per-column density check against a reference: column means in linear light,
    z-scored against counting noise.

    `--group N` pools N adjacent columns per test instead of one. The noise
    falls by √N, which brings out a small systematic bias that per-column noise
    hides — a dither can pass all 256 columns at 3σ and still be measurably off,
    which is how one real tone shift survived. Default `1`, which behaves exactly
    as before. Reach for `--group 16` or `32` when a result looks wrong but every
    column passes.

`unittest`
:   The checks that are about an invariant of the code rather than a property of
    an image, and the only ones that call the functions instead of running the
    program and reading back a PNG. Verifies the multi-frame invariant — with
    `--spatial 0`, the levels displayed for a pixel sum exactly to its budget —
    along with the level ladder and the ranking every mask generator shares.
    Built by `cmake --build build` alongside `dither`; run it directly or let
    `test/run` do it.

`test/impliedtest`
:   Checks that naming a mask option selects the mask threshold on its own, and
    that the shorthand produces the same image as spelling every option out.
    Also that options you name are never overridden, that two that cannot both
    be honoured stop the run instead of one being dropped, and that a run with
    no options acquires nothing.

`test/seamlab <sample>…`
:   How well a mask tiles, and what each way of building one costs. Builds the
    `direct` mask six ways and reports the repair needed and the seam achieved;
    `--score` scores finished masks from any generator, `--check` confirms the
    Python model matches the compiled one.

`test/patchfit <sample> <mask>… --method M`
:   Asks whether a patch of the mask would disappear if dropped into the sample.
    Cuts both into blocks, turns each into eleven texture features — band energy
    and ridgeness at three scales, envelope correlation across scales, and
    level-set clumping at three quantiles — and reports how far the mask sits
    from real patches of that film, in units of how much real patches differ from
    each other. Prints that floor too, since film is not stationary and it is not
    zero. It sees two things `maskfit` and `levelfit` cannot: whether clumps are
    round or stringy, and whether detail at one scale rides on features at the
    next.

`test/maskstats <mask>`
:   Measures a threshold mask: histogram flatness and endpoints (the properties
    a mask must have to dither correctly), then per level set the low-frequency
    energy, spectral knee, anisotropy, and largest void. `--field` reports the
    spectrum of an ordinary image instead, for measuring a grain sample to aim
    at; `--highpass <px>` first strips structure coarser than that, so the grain
    is measured rather than the subject.

`test/maskfit <sample> <mask> … --method M`
:   Scores how closely each mask's radial spectrum matches the sample's,
    as the RMS difference in log10 power — 0 is exact. Frequencies below
    `1/--highpass` are excluded, since the sample was deliberately emptied there.

    These scoring tools prepare the sample the way the mask's generator did, so
    each takes `--method dft|markov|inpaint` and asks `dither --explain` which
    high-pass that is; `--highpass` overrides it, and with neither they refuse
    rather than guess. `--method direct` is refused while `direct` equalises,
    which is its default: these tools model a subtractive high-pass only, so
    score `direct` with `test/grainfit`, or pass `--highpass` to compare against
    a high-pass on purpose.

`test/levelfit <sample> <mask> … --method M`
:   The higher-order counterpart. A mask is used one level set at a time, so this
    compares how much the pattern shown at each density clumps, against the
    sample prepared, cropped and ranked flat the way the mask's was. The
    `slope` column — clumping of the sample's darkest grain over its lightest — is
    the part a spectrum cannot see: film runs ~6, and any method that randomises
    phase flattens it to ~1 however well it matches `test/maskfit`. Use
    `--reference-is-mask` when the reference is already a mask.

`test/grainfit <sample> <mask>…`
:   Compares the grain's *shape* — boundary, components and autocorrelation of
    each density's pattern, measured in blocks — against the sample under three
    preparations at once (raw, high-passed, log high-passed), so that a mask
    cannot score well merely by having been built the way the reference was.
    `--block 48` sees clumping up to that scale.

`test/run [name]`
:   Runs every check and prints one summary, or only those whose name contains
    `name`. Among them `workflowcheck` runs the commands in this file from a
    fresh install, and `findcropscheck` that `examples/films.tsv` still matches
    the crop search.

`gnuplot colplot`
:   Plots `colstats --csv` output as sixels.

`source aliases`
:   Shell helpers. `install_deps` installs dependencies, `build` configures and
    builds, `install_dither` installs the completion and the sample library and
    calibrates it, `deploy` copies the git-tracked files to the public repo.
    `analyse out.png` shows the image, a scaled difference
    against the reference, and the colstats plot side by side; `test_dither
    <args>` dithers the ramp and analyses it in one step; `run_suite` and
    `gallery` sweep that across thresholds. `icat`, `rampdiff`, `isquash`,
    `iaverage`, `histo`, `icrop`, and `dim` inspect images directly.

The `aliases` helpers draw inline images and need a sixel-capable terminal
(foot, WezTerm, xterm `-ti vt340`, mlterm).

## temporaldither

A standalone simulator (`temporaldither.cpp`, no OpenCV dependency) showing how
temporal dithering approximates an intermediate brightness by alternating
between adjacent levels across frames. Build with
`cmake --build build --target temporaldither`.

## References

The full annotated list, with what each source is used for, is in
[doc/DESIGN.md](doc/DESIGN.md#references). The primary sources behind the
algorithms named above:

- Floyd, R. W. and Steinberg, L. (1976). "An Adaptive Algorithm for Spatial
  Greyscale." *Proceedings of the Society for Information Display* 17(2):
  75–77. — `--spatial`.
- Ulichney, R. A. (1993). "The void-and-cluster method for dither array
  generation." Proc. SPIE 1913: 332–343.
  <https://doi.org/10.1117/12.152707> — `--mask-method blue`.
- Efros, A. A. and Leung, T. K. (1999). "Texture Synthesis by Non-parametric
  Sampling." *ICCV*: 1033–1038.
  <https://doi.org/10.1109/ICCV.1999.790383> — `--mask-method inpaint`.
- Efros, A. A. and Freeman, W. T. (2001). "Image Quilting for Texture Synthesis
  and Transfer." *SIGGRAPH*: 341–346. <https://doi.org/10.1145/383259.383296>
  — the minimum error boundary cut that makes `--mask-method direct`'s tile
  join.
- IEC 61966-2-1:1999 — the sRGB curve applied at the encode and decode
  boundaries. <https://webstore.iec.ch/publication/6169>

## License

GPL-3.0-or-later — see [LICENSE](LICENSE).

This program is free software: you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later
version. It is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
PARTICULAR PURPOSE. See the GNU General Public License for more details.
