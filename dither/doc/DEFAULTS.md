# Deriving defaults from the options actually given

**Status: all six steps implemented.** What remains is the deliberate
decision described under Reproducibility below, not unfinished work. `defaults.h` /
`defaults.cpp` carry the mechanism, `--explain` and `--set` exist, `markov.temp`
derives from bins and pairs, `--mask-eps` / `--mask-radius` / `--mask-iters`
have been demoted to internal parameters, `markov.shortlist` is derived
rather than fixed, and `mask.size` derives from the image, supersample, scale and
generator, and every mask parameter declares when it applies so the parser no
longer hand-maintains misplaced-option warnings. `--budget` and the config file exist, opt-in.

Terms of art are defined in [GLOSSARY.md](GLOSSARY.md) and not redefined here;
this document assumes them. [DESIGN.md](DESIGN.md) gives the shorter account of
why the mechanism exists at all, and [ARCHITECTURE.md](ARCHITECTURE.md) shows
where it sits in the module graph. Two conventions used throughout:
`--like-this` is a command-line option of the `dither` program, and `like.this`
is an internal parameter name in the registry, reachable with `--explain` and
`--set`.

## The problem

*This section states the problem as it stood **before** the redesign, and is kept
as the argument for it rather than as a description of today. Several options it
names as command-line flags have since been retired to `--set` — `--mask-eps`,
`--mask-radius` and `--mask-iters` — and `kShortlist` is now the derived
`markov.shortlist`. That those names have moved is the work having been done.*

`dither` had around thirty tuning parameters and the number kept growing. Some
were on the command line, some were `constexpr` inside `markov.cpp` and
`spectralmask.cpp`. Almost none of them have a single best value:

- `--markov-temp` depends on `--markov-bins` and `--markov-pairs` (measured: the
  optimum moves from 0.38 to 0.44 across the range we tested).
- The best `--mask-size` depends on the image, `--supersample` and `--mask-scale`.
- `blue.eps` and the iteration counts — formerly the flags `--mask-eps` and
  `--mask-iters` — are quality-for-time trades with no best value in the
  abstract, only a best value for a given time budget.
- `kShortlist` in `markov.cpp` is fixed at 40, which silently caps
  `--markov-pairs` at C(40,2) = 780.

Two consequences. A user who changes one option often has to change three
others to keep the result good, and there is no way to know which. And a
parameter whose best value is always computable is exposed on the command line
anyway, where it reads as a decision the user is expected to make.

The goal: **given whichever options the user supplied, choose the best values
for everything else.** A parameter that can always be determined should not be a
command-line option at all.

## Principles

1. If the best value is always computable from other parameters, it is
   **internal**. It does not appear in `--help`.
2. If it encodes user intent (how coarse should the grain be, how many tones,
   which file), it is a **command-line option**.
3. If it is a pure quality-for-time trade, it is internal and driven by a single
   `--budget` knob.
4. Every internal parameter stays reachable for experiments through a generic
   `--set key=value`, so demoting an option never costs the ability to re-measure
   it.
5. The defaults system must not know how any generator works. Rules live beside
   the code they configure.

**Principle 1's second sentence did not survive, and the reason is worth
stating rather than quietly editing.** It conflates two things that turned out
to be separable: whether a user has to *decide* a parameter, and whether the
program will *tell* them it exists. The naming rework (see
[OPTIONS-PLAN.md](OPTIONS-PLAN.md)) gave every parameter an option derived from
its key, so `markov.reuse` is `--markov-reuse` and is listed in `--help` with
its default beside it. Nothing became a decision by being listed — the default
is still derived, `--explain` still says so, and a user who never types it gets
the same mask.

What hiding it actually cost was discoverability, and twice it cost correctness:
a validation message named `--mask-weights` for a parameter that had no option
(recorded in [ARCHITECTURE.md](ARCHITECTURE.md)), and four documents went on
advertising retired flags because nothing enumerable contradicted them. A
parameter that is listed, defaulted and explained is the honest form of
principle 1, and the honest reading of the sentence is its first half.

Principle 4 stands as the stronger claim it always was: `--set` reaches a
parameter by key, so nothing is ever unreachable — which now serves the nine
parameters with deliberately no option, and serves as the check that no
generator has forgotten to declare one.

## Mechanism

The parameters form a small dependency graph: `markov.temp` needs `bins` and
`pairs`; `mask.size` needs `supersample`, `scale` and `method`. Writing that as
ordered assignments inside `parseOptions` is exactly what couples the defaults
system to every part of the program, and it breaks the moment two rules are
added in the wrong order.

Instead, parameters resolve **lazily, on demand**, and the dependency graph is
discovered by the act of asking.

```cpp
// defaults.h — no OpenCV, no domain knowledge. Target: ~150 lines.

enum class Origin { User, Measured, Derived, Fallback };

class Resolver {                 // handed to a rule while it runs
public:
    double      num(std::string_view key);   // resolves recursively, records the edge
    long        integer(std::string_view key);
    std::string str(std::string_view key);
    template <class T> const T& resource(std::string_view key);  // sample Mat, image size
};

struct Rule {
    std::string key;                            // "markov.temp"
    std::function<bool(Resolver&)>  applies;    // e.g. method == "markov"
    std::function<Value(Resolver&)> derive;
    std::string doc;                            // shown by --explain
};

class Params {
public:
    void add(Rule);                                    // modules register their own
    void set(std::string_view, Value, Origin);         // CLI and calibration write here
    double num(std::string_view);                      // memoised; cycle-checked
    void explain(std::ostream&) const;
};
```

`num()` returns a user-set value if there is one, otherwise runs the rule once
and memoises it. A resolving-stack catches cycles. Because rules request their
inputs by name, nothing has to declare an evaluation order, and the dependency
edges are recorded for free.

### Keeping it decoupled

Each module registers its own rules, in its own translation unit:

```cpp
// markov.h / bluenoise.h / spectralmask.h
void register_markov_params(Params&);
```

`main()` calls those three functions and then reads values. The core never
learns what a Gibbs sweep is. The formula for `markov.temp` lives in
`markov.cpp` next to the code that consumes it, which is also where anyone
re-measuring it will be looking. Dependencies point inward only: generators
depend on the mechanism, the mechanism depends on nothing.

Keys are strings, which is the usual weakness of this pattern. Mitigation: each
module declares its own key constants (`namespace markov::keys { inline
constexpr auto Temp = "markov.temp"; }`) so names are centralised rather than
scattered as literals.

### Three things that fall out

**`--explain`** prints every parameter with its value, origin and derivation
chain. For a tool with this much tuning surface this is arguably the most
valuable single feature here — it answers "why is my mask 256 wide" directly.

**Misplaced-option warnings become automatic.** The `applies` predicate replaces
the hand-written `misplaced()` logic in `parseOptions`: if the user sets a
parameter whose predicate is false, the registry says so and names it. That is
existing code deleted, not added.

**And then mostly stop being needed.** A warning is the wrong answer whenever
the option is fine and the *default* is what makes it inapplicable.
`dither in.png --mask-sample grain.png` is an unambiguous request for a mask, and
answering it with "mask.sample is ignored here" because `--threshold` was left on
`round` is a tool refusing to do the obvious thing. `Params::reconcile()` moves
the default instead: a rule that gates others offers `candidates`, and the search
picks the assignment leaving the fewest supplied options stranded, ties going to
whatever moves least. Moved values become `Origin::Implied` and are printed —
`Note: assuming threshold=mask, so that mask.sample applies` — because a default
that changes silently is worse than the warning it replaced.

Three things this gets right by construction:

- It reuses the applicability conditions rather than restating them. Naming the
  implying options in the `threshold` rule would put "what needs a mask" in two
  places, and the second copy is the one that rots. Nothing in `reconcile` knows
  what a mask is.
- The search is exhaustive over the candidate product, not greedy, because the
  moves are not independent. `--markov-temp 0.3` needs `mask.method=markov`
  **and** `threshold=mask`; neither alone reduces the stranded count, so a
  one-at-a-time pass finds nothing and reports the parameter as ignored.
- It only moves what the user left alone.

**And what it cannot rescue is a conflict, not a warning.** If a parameter is
still inapplicable after the search, the thing blocking it is something the user
pinned — had it been a default, it would have moved. `--mask-scale 2 --mask-in
m.png` asks for a mask to be both loaded ready-made and synthesised from a
scaled sample. There is no assignment that satisfies both, so `conflicts()`
reports it and the run stops:

```
--mask-scale applies to building a mask from a sample, which --mask-in rules
out; drop one of them
```

The blocking option is found by giving the applicability test a stack frame of
its own, so the dependency recorder that already exists captures which keys it
reads; the blockers are those the user set. Nothing has to be declared per
parameter, and the message names the real other side instead of restating the
condition.

Together the two halves give one property worth stating plainly: **an option the
user typed is never silently dropped.** Either the defaults move to accommodate
it, or the run refuses. The `Note: … is ignored here` this replaced was printed
next to a zero exit status, which says "your option had no effect and everything
is fine" — the two worst things to say together.

It also caught one of these by construction: `mask.in` had no `applies`
condition at all, so `--mask-in m.png` under the default threshold silently
built nothing and used nothing. Giving it `mask::thresholding()` both fixes the
warning and lets `reconcile` imply the threshold from it.

**`--set key=value`** reaches any parameter by key. It used to be the whole
reason demoting an option was safe — the parameter stayed reachable without
appearing in `--help` — and that argument has expired: every parameter now has
an option of its own derived from its key, so `markov.reuse` is
`--markov-reuse 0.4` and is in `--help` beside the rest. What `--set` is for now
is the nine parameters with deliberately no option (`image.*`, `sample.*` and
the five `<generator>.rate`, all of which the program measures), and enforcing
the naming rule: a parameter reachable only by key is one whose option its
generator forgot to declare, and there is no longer any such thing.

## `--budget 30s` — "the best you can do in about thirty seconds"

> **As shipped, `--budget` is unset by default** and every generator keeps its
> fixed size ceiling; output is exactly what it was before the option existed.
> This section is the original proposal, kept for the cost models and the
> allocation policy, which are implemented as described. Where it says the
> default is `30s`, that is the proposal and not the contract — see
> [Reproducibility](#reproducibility) for why turning it on by default has a
> price that has not been paid.

Several parameters are pure quality-for-time trades: mask size, `--markov-pairs`,
`--markov-bins`, `blue.eps`, dft iteration count. Today each is tuned separately
against an implicit budget nobody wrote down, which is how the mask-size cap came
to be the value that was right before anyone was supersampling.

`--budget` makes it explicit and lets all of them move together. The proposal was
for it to default to `30s`; it ships unset, as above.

**Cost models, in machine-independent work units:**

| generator | work |
|---|---|
| blue | `N · K · log N` — heap operations, `K` = sparse kernel taps |
| dft | `iters · N · log N` — FFTs |
| markov | `N · iters · pairs` Gibbs cell updates, plus `pairs · sample_area / step²` to fit |

**Machine speed is measured once and cached** in the config file below. A short
probe of each generator gives a rate in work-units per second, and the budget
converts to a work allowance that means the same thing everywhere. It will not be
exact — cache sizes and core counts differ — but the framing that matters is the
honest one: *about* thirty seconds on the machine in front of you, not a portable
guarantee.

**Allocation policy.** Several parameters compete for the same allowance, so the
order has to be stated, and it should follow how visible each defect is:

1. `mask.size`, up to `grid` — tile repetition is the most visible failure and
   the one measured to be plainly wrong today at `--supersample 8`.
2. `markov.pairs`, recompensating `markov.temp` at each step per the rule above.
3. `markov.bins`, while held-out overfit stays under ~5%.

Everything else takes its quality-preferred value and is not traded.

## The config file

`--mask-calibrate` should not require a recompile, and machine speed has to be
remembered somewhere. One file serves both.

Location `${XDG_CONFIG_HOME:-~/.config}/dither/cache.toml`, overridable with
`--config <path>` and disabled entirely with `--no-config`.

A `masks/` directory sits beside it holding built masks, governed by the same
two flags. **Its key is built by walking this registry**, which is the design
decision worth recording here rather than in `maskcache.cpp`: every parameter
that `applies()` to the run goes in, at the value `--explain` would print, so a
parameter or a whole generator added later is covered on the day it is added.
The alternative — a list of relevant keys maintained alongside the cache — is
the same second-source-of-truth that `Options`-versus-`Params` already cost
this project once, and here it would fail silently as a *wrong hit* rather than
loudly as a dropped option.

Three inputs to a mask are not parameters and are added by hand: the sample's
contents (hashed, because `mask.sample` is a path), the executable's identity
(so rebuilding a generator invalidates its masks), and nothing else — `seed`
used to be a fourth and was moved into the registry instead, which is the
general fix rather than the local one.

What is excluded is every parameter whose rule declares it an **intermediate**:
one that reaches the output only by deciding another parameter, which is then in
the key in its own right. `image.width` and `image.height` are intermediates —
they exist so `mask.size.*` can be derived from the picture — as are
`supersample`, `budget`, and the five `<generator>.rate` values. `out.size` and
`mask.out` are there for a simpler reason: one scales the finished frames and
the other names the file the mask is written to, and neither reaches the mask
by any route at all. `mask.out` was in the key until it was noticed that every
run saving its mask therefore missed. A rate is
re-measured on most runs, so including it would stop the cache ever hitting
twice; an image dimension changes with every new picture, which used to rebuild
a byte-identical mask each time.

The flag defaults to *not* intermediate, which is the safe direction: forgetting
it on a new parameter costs a needless rebuild, while claiming it wrongly costs
a stale result. That claim is about which values the C++ build reads, so the
registry cannot check it and a unit test builds masks to do so instead.

```toml
[machine]
measured = "2026-08-31T10:14:00Z"
blue     = 1.8e7        # work units per second
dft      = 4.2e8
markov   = 9.1e6

[calibration.7f3a91c2]  # key described below
sample   = "4Xcrop.png"
temp     = 0.407
clump    = 0.0163       # the level-set clump distance it was calibrated to
```

**The cache key** is a hash of everything that changes the answer: the sample's
*content*, `mask.bins`, `mask.pairs`, `mask.nbhd`, `mask.highpass`, `mask.scale`
and `markov.weights`. It deliberately **excludes `mask.size`**, because the
optimum was measured to transfer across it — 0.41 at 64², 128² and 256². That one
fact is what makes calibration affordable and the cache worth keeping: probe on a
cheap 64² mask, reuse the answer at 1024².

A miss recalibrates, or falls back to the closed-form rule when
`--mask-calibrate` was not asked for. Nothing in the file is authoritative;
deleting it costs time and nothing else.

## Reproducibility

This is the part that can do damage if left implicit.

Today the guarantee is simple: the same command line and the same `--seed` give a
byte-identical mask, which is why `--mask-out` then `--mask-in` reproduces a run
exactly. A budget-derived default breaks that across machines — a faster machine
buys a bigger mask, so a different result — and a warm cache differs from a cold
one on the *same* machine.

**Most of this section is deferred, deliberately.** Because `--budget` is
opt-in, nothing about the default path became machine-dependent: a run without
`--budget` is byte-identical to every earlier version, and cached speeds are read
only when a budget is set. `test/coltest` and `unittest` therefore need no
insulation, and `--params` would guard nothing.

The list below is what turning `--budget` on *by default* would require. It is
the price of that decision, and should be paid at the same time rather than
discovered afterwards:

- **`--explain --json` and `--params <file>`.** The first writes the fully
  resolved parameter set; the second replays it with every value pinned as
  `Origin::User`. That restores exact cross-machine reproduction for anyone who
  needs it, and it is the honest answer to "what did this run actually do".
- **The test suites must opt out.** `test/coltest` and `unittest` compare against
  saved baselines; if defaults become machine-dependent those baselines stop
  meaning anything. Both should pass `--no-config` and pin sizes explicitly.
- **`--mask-out` stays byte-exact.** A written mask remains a complete and
  portable description of the arrangement, whatever chose its size. This one
  already holds.

Shipped now: `--no-config`, which exists because the cache needs a disable
switch, not as test insulation.

## Temperature is two-tier

A rule cannot run the Gibbs sampler, and measuring the temperature has to. So:

- **Tier 1, always, free** — the closed-form rule from `bins` and `pairs`.
- **Tier 2, opt-in `--mask-calibrate`** — fits the model once, samples small
  probe masks at a ladder of temperatures, and keeps the one whose grain clumps
  most like the sample's. Written back as `Origin::Measured`, cached in the
  config against the sample's decoded pixels and every setting that moves the
  answer. Affordable because fitting dominates the cost (1.25s against 60ms to
  sample a probe) and does not depend on the temperature — which is why
  `build_markov_mask` is now `fit_markov_model` + `sample_markov_mask`.

**Where it stands: it earns its place, and the earlier verdict here was wrong.**
That verdict was reached on two samples only, one of which was the very sample
the formula's coefficients were fitted to. With two more film scans added, the
picture is clear:

| sample | formula | calibrated | level-set fit |
|---|---|---|---|
| 4Xcrop — *the formula's own fit* | 0.40 | 0.425 | 0.183 → 0.202 |
| blockCrop | 0.40 | 0.374 | 0.073 → 0.077 |
| Kodak-2484-1 | 0.40 | 0.374 | 0.535 → **0.387** |
| Kodak-4X-2 | 0.40 | 0.600 | 0.304 → **0.063** |

**The formula has no sample term at all** — it reads only `bins` and `pairs` — so
it returns 0.40 for every one of these. The measured optimum runs from 0.34 to
0.60 across them. It could hardly be otherwise: the coefficients were fitted on
one film scan, and a constant cannot follow a texture. On the two samples near
its fitted point it ties or wins by a hair; on the two away from it, it loses by
28% and by a factor of five.

So the rule is a starting point and calibration is the answer, which is what this
document assumed from the beginning. Its residual error — it picks 0.374 for
Kodak-2484-1 where a hand sweep finds 0.34 — is now the small term.

**Calibration is deterministic and machine-independent**, unlike `--budget`. It
depends only on the sample, the settings and `--seed`, so a cached run and a
fresh one produce the same mask and the cache is purely a time saving. None of
the reproducibility argument that keeps `--budget` opt-in applies to it. The only
cost of turning it on by default would be a few seconds on the first run against
a new sample.

### The criterion, and one that does not work

The first design here matched a **fixed fraction of the sample's neighbour mutual
information**, target 0.65. That number was derived across three configurations
of one film sample, and **it does not generalise**: at their own optima a film
scan reads 0.63 and a coarser sample 0.79. Targeting 0.65 on the coarse sample
returned 0.64 where the truth was 0.37 — far worse than the formula it was meant
to improve on. *The ratio at the optimum is a property of the texture*, so
moment-matching on a constant cannot work, however well the constant was measured
on one texture.

What replaced it compares the thing actually being judged: how much the pattern
shown at each density clumps, against the sample's own ranked crop — `levelfit`'s
measure, computed in process. That is sample-relative by construction. Note the
circularity: calibrating to minimise a `levelfit`-like distance and then scoring
with `levelfit` only confirms the search worked. The independent check is
agreement with the temperatures found by hand sweeps the calibrator cannot see,
which is what the table above reports.

The in-process metric averages over frequency cells where `maskstats` averages
over radial rings. Matching the ring averaging exactly was tried and is worse —
at 128² the lowest rings hold a handful of cells each, and weighting them equally
made the minimum noisy enough that the coarse sample walked to the end of the
bracket.

## Parameter table

Evidence: **M** measured here · **R** reasoned, untested · **?** rule not yet known.

### Pipeline — all remain command-line

| parameter | depends on | rule | ev |
|---|---|---|---|
| `threshold` | — | user; default `round` | |
| `spatial` | `threshold` | `0` if a threshold was named, else `1` *(already implemented)* | M |
| `levels`, `level-spacing`, `frames`, `supersample`, `out.size`, `seed`, `linear`, `out`, `save-frames`, `debug-pixel` | — | user intent; no best value exists | |
| `budget` | — | **done** — cmdline, opt-in. `0` (unset) leaves each generator's fixed ceiling in charge; given, converted to a work allowance via cached per-generator rates | M |
| `<gen>.rate` | — | **done** — internal, *measured*. Work units per second, recorded from real builds into the config file; compiled-in first guess | M |

### Mask, any generator

| parameter | now | proposed | depends on | rule | ev |
|---|---|---|---|---|---|
| `mask.method` | cmdline | keep | `mask.sample` | `blue` if no sample, else `dft` *(already implemented)* | M |
| `mask.size` | cmdline | keep, better default | image size, `supersample`, `mask.scale`, `mask.highpass`, `method`, `budget` | see below | M/R |
| `mask.in`, `mask.out` | cmdline | keep | — | paths | |

**`mask.size` rule** (deliberately requires no measurement of the sample):

```
grid    = image_size × supersample
floor   = max(128,                                # threshold resolution
              4 × mask.highpass × mask.scale,     # must contain its coarsest feature
              grid / <method>.max_repeats)        # repeat visibility
cap     = <method>.max_size
side    = max(floor, min(grid, cap))              # floors override the cap
```

Computed per axis, so a wide image gets a wide mask. Four constraints, none
needing the grain period:

- **`grid`** — a mask larger than the grid only ever exposes a window of itself,
  and a window of a clustered mask is not histogram-flat.
- **`128`** — threshold resolution. A mask spreads 255 threshold values over its
  pixels; 128² gives 64 per value and works, 32² gives 4 and measurably fails.
- **`4 × highpass × scale`** — the mask must be able to contain the coarsest
  structure it is allowed to have. `--mask-highpass` is already the claim that
  nothing coarser exists, and `--mask-scale` multiplies it, so asking for coarser
  grain enlarges the tile with nothing measured.
- **`grid / max_repeats`** — repeat visibility, **and this is a property of the
  generator, not of the mask**. Each one declares its own limit alongside its
  cost ceiling, and the rule looks both up by method name, so the driver holds no
  generator knowledge.

**How much repetition shows depends entirely on the generator, which the first
draft of this rule got wrong.** A markov tile repeated 16 times reads as regular
banding in flat areas, and 4 does not — so `markov.max_repeats` is 4. Applying
the same limit to blue noise would have forced a 1024² tile at `--supersample 8`,
costing **30 s a run instead of 1 s**, and the two outputs are visually
indistinguishable: blue noise is locally featureless, so a repeat of it is still
featureless. `blue.max_repeats` is therefore 64, which never binds. The lesson is
general — a floor justified by measuring one generator must not be applied to
another without measuring it too.

The cap remains a stand-in for a cost limit. It is now at least per-generator and
named — 256 for `blue`, `markov` and `inpaint`, which pay for a tile by the
pixel, and 1024 for `dft` and `direct`, which are a transform and a crop
respectively and so cost almost nothing per pixel — and floors override it, so
`--supersample 8` gets the 1024² markov tile it needs.
`--budget` would let the two be priced against each other properly rather than
one always winning.

### Blue

| parameter | now | proposed | depends on | rule | ev |
|---|---|---|---|---|---|
| `blue.sigma` | cmdline | keep | `supersample` | `1.8 × supersample` — grain should land at a fixed *output* size, not a fixed grid size | R |
| `blue.eps` | ~~cmdline~~ | **done** — internal | `budget` | fixed at `1e-4` until `--budget` exists to spend on it | R |
| `blue.radius` | ~~cmdline~~ | **done** — internal, *derived* | `blue.eps` | `sqrt(-2 ln eps)` = 4.29σ at the default — exactly where the Gaussian reaches `eps`, so it can neither truncate the kernel nor scan taps `eps` will reject. Verified byte-identical to the old fixed 6.0 | M |
| `blue.jitter` | internal | internal | — | `1e-6`; a tie-break epsilon, not a tuning knob | |

### Sample-based (dft, markov, inpaint and direct)

| parameter | now | proposed | depends on | rule | ev |
|---|---|---|---|---|---|
| `mask.sample` | cmdline | **done** | `mask.method` | user intent; `Kodak-4X`, implied, when a generator that imitates a sample is named without one and the library has it | |
| `mask.scale` | cmdline | keep | — | user intent | |
| `mask.highpass` | cmdline | **done** — 32, and 0 for markov and inpaint (the generator table's `default_highpass`; a choice to expose what the filter hid, not a measurement) | `mask.method` | **Measured, and the proposed derivation does not hold.** The knee is real and findable: a three-segment log-log fit of the radial spectrum separates subject (slope −2.4 to −6.3), grain (−0.14 to −1.07) and scanner/film rolloff (−3.7 to −5.5), and the grain→rolloff knee is strikingly stable at 4.4–5.2 px across six films. But the subject→grain knee — the one this row proposed to use — runs 39 px to 443 px, while the measured best `--mask-highpass` sits at 32–64 on every film regardless: 4Xcrop's knee is 443 and it wants 64, Kodak-2484's is 395 and it wants 32. No correlation. Nor does the optimum track mask size (best is 256/256/32 at a 128² mask, 64/32/32 at 256², 64/–/32 at 512²), so the obvious alternative rule fails too. **A two-segment fit must not be used**: it locks onto the rolloff rather than the subject knee and reports the slopes backwards. Keep 32 | M |
| ~~`mask.iters`~~ | ~~cmdline~~ | **done** — split into `dft.iters` and `markov.iters`, both internal | `budget` | both 20. Splitting retires the wart of one option covering two different quantities | M |

### dft

| parameter | now | proposed | depends on | rule | ev |
|---|---|---|---|---|---|
| `dft.iters` | ~~`--mask-iters`~~ | **done** — internal | `budget` | 20, where quality plateaus: spectral fit 0.0367 / 0.0073 / 0.0030 / 0.0023 / 0.0023 at 5 / 10 / 15 / 20 / 30 rounds (re-measured after `dft.smooth_cells` was scaled; the plateau is unmoved). **No convergence test is possible** — the projections never reach an exact fixed point, so 30 rounds still differ from 20 on 1.6% of pixels. The rule this table previously proposed does not exist | M |
| `dft.smooth_cells` | internal `8.0` | **done** — internal `4.0`, scaled at the call site | sample area / mask area | `cells / sqrt(sample_area/mask_area)`, floored at one mask cell and never raised above what was asked. The dependence posited here is real and was measured: three films, sample area swept 64× at a fixed 256² mask, every mask scored against the full sample at a fixed `--highpass 32`. The optimum tracks `r^-0.5` — the exponent the periodogram argument predicts — and a free fit gives `r^-0.35` with a coefficient within 3% of 4. The old flat 8 was the *worst* value tried on all five full film scans (0.0120 vs 0.0023 on 4Xcrop); over the whole domain the rule reads 0.0263 against 0.0331. Scaled at the call site rather than in the registry because the rule needs the sample's dimensions, which are not loaded when `--explain` runs — the same shape as `markov.field_highpass` | M |

### markov

| parameter | now | proposed | depends on | rule | ev |
|---|---|---|---|---|---|
| `markov.iters` | internal `12` | **done** — internal `2`, by way of `3` | — | 12 came from a level-set sweep at *fixed* temperature, which judges every sweep count at 12's temperature; it could not be fitted until `markov.temp` gained an `iters` term. With both free (7×8 grid, iters 2…20 × temp 0.20…0.50, two films, `patchfit` and `levelfit`) the ordering reverses and fewer sweeps wins. patchfit falls monotonically to 2; levelfit on AutoRex says 2 is too few — the field has not coarsened and its clumping reads 0.26 against 0.18. 3 satisfies both and is 4× faster, and was the default until a six-film measurement at the derived temperature took it to 2 (CLAUDE.md, the markov section, has that table). Against 12: patchfit 30.1→20.4, 23.6→17.9, and 14.3→12.4 on a held-out third film; coarse ridgeness (the worming) +10.0→+1.7 | M |
| `markov.temp` | cmdline | **done** — derived default | `bins`, `pairs`, `iters`, `mask.highpass` | `A + 0.059·log₂(iters/12) + 0.030·log₂(bins/16) − 0.015·log₂(pairs/64)`. `A` is 0.40 with a high-pass above 1 px and 0.65 without one — 0.50 at the defaults, the best single value over six films swept with patchfit (mean 50.7, worst 79, against the old 0.247's 63.0 and 96); see CLAUDE.md, "Calibration under the no-high-pass default". The `iters` term was fitted from the same grid: all four film-metric surfaces put the optimum rising with `log₂(iters)` at 0.072 / 0.060 / 0.054 / 0.048 per octave, mean 0.059. Anchored at 12 so it is a strict generalisation — at the old sweep count it still returns 0.40. Fitted over 64–256 pairs and it under-compensates beyond that (1000 pairs wants ~0.32, the rule gives 0.341); the true relationship steepens with pair count. Left linear because more than ~256 pairs measures worse regardless, so the extrapolation costs nothing in practice. Base pinned at 0.40 rather than the fitted 0.41: at the defaults 0.40 measured slightly better (0.190 vs 0.198) and it keeps existing output byte-identical. The two are within noise | M |
| `markov.weights` | cmdline | keep | — | `flat`; `mi` and `ml` both measured worse | M |
| `markov.bins` | cmdline | keep, derived default | sample area, `budget` | largest value keeping held-out overfit under ~5%. CLI caps at 32, where occupancy is still 43 counts/entry | M |
| `markov.pairs` | cmdline | keep, derived default | sample area, `budget` | more is better *provided* temp compensates; bounded by time (cost is linear) | M |
| `markov.radius` | cmdline | keep | `mask.scale`, sample correlation length | ≈ correlation length × scale. 8 measured worse than 5 at scale 1, so the default is not simply "bigger is better" | M/? |
| `markov.field_highpass` | internal | internal | `mask.highpass`, `mask.scale` | `highpass × scale` *(already implemented)* | M |
| `markov.Qv` | internal | internal | `bins` | `min(2 × bins, 64)` — formalise the existing expression as a rule | M |
| `markov.shortlist` | internal `40` | **done** — internal, *derived* | `pairs` | `ceil((1+√(1+8·12·pairs))/2)`, i.e. the smallest shortlist whose `C(n,2)` candidate pairs give the greedy pick 12 to choose from per pair it needs. 12 is what the old fixed 40 happened to give at 64 pairs, so the default is unchanged; `--markov-pairs 2000` now delivers 2000 instead of silently capping at 780. Clamped at the call site to the offsets a shrunk neighbourhood can supply, with a message when that binds | M |
| `markov.counts_per_cell` | internal `24` | internal | — | sets the sample-thinning `step`; a statistical sufficiency constant | R |
| `markov.prior` | internal `8` | internal | `counts_per_cell` | pseudo-counts should scale with typical cell occupancy rather than being absolute | R |
| `markov.reuse` | internal `0.35` | **done** — internal, *derived* | `pairs`, `shortlist` | `1.12·shortlist/(2·pairs)`, which holds `reuse × uses-per-offset` constant. The dependence posited here was **backwards**. The shortlist rule pins `C(shortlist,2)` at `12·pairs`, so uses-per-offset already grows as √pairs (1.5 at 16 pairs, 3.2 at 64, 6.5 at 256, 12.8 at 1000) and the penalty strengthens on its own; holding the coefficient fixed makes the *total* grow, and the pick starts refusing good repeats. Measured on three films at 64 and 256 pairs by both `levelfit` and `patchfit`, three seeds each: all six film-metric cells agree the optimum falls with pair count, by about half for four times the pairs. Returns exactly 0.35 at the default, so output is byte-identical. At 256 pairs it asks 0.173 against the old 0.35 and five of six cells improve. The per-sample optimum varies widely (2484 and AutoRex want 0 at 256 pairs, 4Xcrop wants 0.15), so this is the direction and scaling, not a per-sample fit — the same limit `markov.temp` has | M |
| `markov.near_band` / far-pair reservation | internal `2`, `0.5` | internal | `nbhd` | fixed: band 2, half the slots reserved for far pairs | R |
| ML-fit `iters` (30), `gstep` (`step×3`) | internal | internal | — | diagnostic path (`--markov-weights ml`) only | |

### inpaint

Added after the worming investigation, and the one thing to know before tuning
it is that four of these are the *same* constraint seen from different sides:
the search has to find a genuinely close neighbour, and `db`, `checks`, `dims`
and `radius` each control whether it can.

| parameter | now | depends on | rule | ev |
|---|---|---|---|---|
| `inpaint.db` | internal `250000` | sample area, `budget` | more is better and it is the binding constraint — 40k/100k/250k measure patchfit 15.6/12.9/10.7. Bounded by index build time and by the sample having that many distinct neighbourhoods | M |
| `inpaint.checks` | internal `64` | `db` | k-d tree leaves per query. 32/128/512 measured 11.9/9.4/10.8 on one film and 128 was read as where the search becomes exact; across six films 64 and 128 are a wash, the largest move either way 4%, and 64 is about a quarter off the search | M |
| `inpaint.dims` | internal `12` | `radius` | PCA dimensions matched in. 24 measures 28.3 against 12's 20.0 in the prototype: matching too precisely over-commits and over-clumps | M |
| `inpaint.radius` | internal `3` | `mask.scale` | 4 and 5 measure 31.8 and 50.9 — the database thins out in a bigger space, which is `db` again | M |
| `inpaint.groups` | internal `16` | — | in-place passes per sweep. 1 is Jacobi and measures 19.6 against 15.6; past 4 it is flat (15.7 vs 15.6), so this is a threshold not a dial | M |
| `inpaint.iters` | internal `2`, or calibrated | — | sweeps per level. **Not a convergence parameter**: sweeps worm, so this trades ridging against clumping and has no convergent value. 12 against 6 measured 15.9/15.6 on one film and was read as converged; mid-octave ridgeness on Kodak-4X-2 runs 12.7/16.5/32.2/44.1 at 2/3/6/12. 3 beats 6 by ~13% on the mean over seven films, with the high-pass on; without it 2 beats 3 on five films of six and ties the sixth. `--mask-calibrate` measures it per sample | M |
| `inpaint.cap` | internal `1.25` | — | stop sweeping once the field is this much more directional than the sample. The sweep loop adds structure monotonically and the gain scales with the structure present, so a sample with strong grain direction runs away: nine of eleven films finish within ±25% of their sample and two at +45%/+50%. Byte-identical on those nine; the ends are measured, below ~1.2 it cuts sound films short and above ~1.3 it misses the case it is for | M |
| `inpaint.bands`, `markov.bands` | cmdline `16`; `0` off | — | spectrum matching: after every sweep, rescale the field's power in this many log-spaced bands, out to the corner of the frequency plane, to the film's crop equalised at 24 px (`maskutil::impose_band_power`). Over six crops patchfit 22.9 → 15.1 (inpaint) and 37.4 → 17.1 (markov), grainfit 6.72 → 3.34 and 16.80 → 3.50; four other ways to hold the scale were measured worse. CLAUDE.md, "Holding the sweeps to the sample's band power" | M |
| `inpaint.scales` | internal `2` | `radius`, grain scale | a radius-3 window says nothing about structure at 20 px; one coarse level took `Rmid` from +7.6 to +3.5. 3 levels measured no better than 2 | M |
| `inpaint.max_size` | internal `256` | — | a cost limit, not a quality one: 4.6/6.5/14.0 s at 128²/256²/512² on 64 cores, from 13/40/146 s before the search was threaded and its concurrency capped. Same figure as markov's and for the same reason | R |
| `inpaint.rate` | internal `2.2e7` | machine | measured from real builds and cached, like the other generators' | M |

None of these is a decision a user has to make: each is derived or measured, and
a run that names none of them gets the same mask. Every one except
`inpaint.rate` nonetheless has an option of its own — `--inpaint-db`,
`--inpaint-checks` and so on — because the naming rework gave one to every
parameter; being listed in `--help` beside its default is not the same as being
handed to the user to choose, and see the amendment to Principle 1 above for why
that distinction had to be drawn. `inpaint.rate` has no option because the
program measures it.

**The two cost ceilings every generator declares are not tabulated here.**
`max_size` and `max_repeats` are ten parameters that are really one family with
two numbers per generator, and `dither --help` prints the whole grid from the
registry, so it cannot disagree with the code the way seven hand-copied rows
here would. Three of the ten had rows and seven did not, which is what that kind
of table converges on. The values, and the reasoning, are under Mask size above.

### Cross-tool consistency

`mask.highpass` is derived — 32 for dft and direct, 0 for markov and inpaint
— so `test/levelfit`, `test/patchfit` and `test/maskfit` take `--method` and ask
`dither --explain` for the value (`test/maskref.py`), and refuse to guess
without it: they are Python and cannot share the registry, and a constant of
their own is what once scored every markov and inpaint mask against the wrong
reference. `test/maskstats --highpass` takes a value, since it measures a field
rather than comparing it with a mask's generator.

## Suggested order of work

1. ~~Core `Params`, `--explain`, `--set`, with one rule migrated
   (`markov.temp`) to prove the shape end to end.~~ **Done.** `markov.bins` and
   `markov.pairs` came along as its inputs. Notes from doing it:
   - `Origin::Derived` vs `Origin::Fallback` needs no declaration — a rule that
     asked for nothing produced a constant, so the distinction falls out of the
     recorded dependency list.
   - Range checking stayed at the CLI boundary (`need_num`), with the checked
     value then pinned as `Origin::User`. Rules never validate.
   - The payoff is already visible: `--markov-pairs 256` now derives temp 0.37,
     the value that previously had to be found by hand.
2. ~~Demote `--mask-radius`, `--mask-eps`, `--mask-iters` to internal.~~
   **Done.** Notes:
   - `blue.radius` turned out to be genuinely *derivable* from `blue.eps` rather
     than merely constant, which is the better outcome: it can no longer be set
     to a value that silently truncates the kernel.
   - `--mask-iters` was split into `dft.iters` and `markov.iters`. One option
     covering two unrelated quantities was worth losing.
   - The convergence rule this document proposed for dft **does not exist** —
     see the `dft.iters` row. Recorded so nobody tries it twice.
   - Nothing outside `dither.cpp` used the three options, so they were removed
     rather than kept as deprecated aliases.
   - **Demoting an option removed its range check with it**, because validation
     lived in `need_num` at the CLI boundary. `--set blue.eps=0` gave an infinite
     kernel radius and `--set markov.bins=0` dumped core. Fixed by moving the
     bounds onto `Rule::range`, checked on every write *and* on every derived
     result. Bounds are now declared once, beside the parameter, and `--set`
     gets the same guarantee an option had. **Any future demotion must move its
     range at the same time.**
3. ~~Fix `markov.shortlist` — a real bug, independent of this work.~~ **Done.**
   Notes:
   - The naive fix (`shortlist` = the minimum that can form `pairs` pairs) would
     have been much *worse* than the bug: 64 pairs needs only 12 offsets, so the
     greedy selection would have had 66 candidates instead of 780 and almost no
     choice. What the shortlist controls is not feasibility but how much the
     selection gets to be selective, hence the pool factor.
   - Fixing it exposed a ceiling rather than unlocking headroom: `--markov-pairs`
     above ~256 measures worse even with the temperature hand-tuned (256 scores
     0.158; 1000 scores 0.241 on the derived temp and 0.186 at its own best).
     The fix removes a silent lie, it does not buy quality. It also showed the
     temp rule under-compensating as it extrapolates, and that the relationship
     is not linear in log2(pairs) — noted against `markov.temp` rather than
     re-fitted, since one coarse far point would spoil the near ones.
   - Established a pattern worth reusing: **a rule states intent, the call site
     clamps to reality.** `markov.shortlist` cannot know that `radius` will be
     shrunk to fit a small sample, so `build_markov_mask` clamps and reports.
4. ~~`mask.size`, which has the most user-visible payoff.~~ **Done.** Notes:
   - Required publishing `supersample`, `mask.scale`, `mask.highpass`,
     `mask.method` and the image dimensions as parameters. Image size is read
     from the input and set with `Origin::Measured`, which reads well in
     `--explain`: a fact, not a choice.
   - `Resolver::resource<T>()` was never needed — the only non-scalar input was
     the image size, and two integers serve better than a type-erased handle.
     The declaration has been removed rather than left as unimplemented API.
   - Generators declare `max_size` **and** `max_repeats`; the rule looks both up
     by method name. Adding a generator needs no change to the size rule.
   - Only markov's default moved, and only at `--supersample` 4 and 8, which is
     exactly the case measured as broken. Everything else is byte-identical.
5. ~~Replace `misplaced()` with `applies` predicates.~~ **Done.** Notes:
   - Applicability became a small struct rather than a bare predicate: the test
     and the phrase naming it travel together, so the condition and the sentence
     the user reads cannot drift apart.
   - A scope phrase must cover the *whole* condition. Naming only the method
     produced "blue.sigma applies to --mask-method blue" when the real reason was
     that `--mask-in` had supplied a mask, sending the reader to the wrong place.
   - Completing it meant migrating the last mask options (`blue.sigma`,
     `mask.sample`, `markov.radius`/`weights`/`report`) and giving `threshold`,
     `mask.in` and `mask.method` rules of their own. `mask.method`'s deferred
     default is now a rule rather than imperative code in the parser.
   - **This exposed a two-sources-of-truth bug.** With values in both `Options`
     and `Params`, `--set mask.scale=2` moved the mask size and left the
     generator on 1. `Options` is now a read-only cache filled from `Params`
     after the whole command line has been applied. Anything that lives in
     `Params` must be read from `Params`.
   - Warnings are more precise than the ones they replaced: they name the
     parameter rather than "some mask option", and `--explain` marks every
     parameter that does not apply to the run.
6. ~~`--budget` plus the config file.~~ **Done, opt-in.** Notes:
   - **`--budget` unset means exactly the old behaviour**, so the byte-for-byte
     invariant held through all six steps. Given, it replaces each generator's
     fixed size ceiling with "the largest that fits in the time".
   - That decision retired most of the reproducibility guardrail list. See below.
   - Cost models are one pure function per generator (`blue_work`, `dft_work`,
     `markov_work`), taking sizes as arguments rather than resolving parameters,
     so they can be called *while* choosing the mask size without a cycle.
   - Measured rates were consistent within 1.7x across a 16x size range, which
     closes the spec's open question: the allocation only picks between
     powers of two, so a rate 30% out changes nothing.
   - Calibration is self-correcting rather than a probe: every build long enough
     to time records what it cost. The first `--budget` run on a new machine says
     it is working from a guess, so the second run's improvement is legible.
   - **The allocation stops at 256 pairs.** Spending a larger budget on more
     pairs would make the mask worse, which is measured. "Spend the budget" and
     "best result" diverge here, and best result wins.

Each step leaves the tool working, and step 1 is reversible if the shape turns
out wrong.

## Open questions

*Settled: `--budget` over `--effort` (it is the only formulation that fixes the
size cap at high `--supersample`), and calibration results go to a config file
rather than being compiled in.*

- **How accurate must the machine-rate probe be?** The allocation policy only
  needs to pick between roughly power-of-two mask sizes and pair counts, so a
  rate that is 30% off changes nothing. If that holds, the probe can be very
  short. Worth confirming before building anything elaborate.
- **Does the machine rate need re-measuring, and how would we know?** A laptop
  on battery is materially slower than the same laptop on mains. Options: a
  timestamp and a staleness window, or simply comparing predicted against actual
  build time each run and re-probing when they diverge. The latter is
  self-correcting and needs no policy.
- **What is the sample content hash over?** File bytes are simplest, but two
  identical images with different EXIF would miss. Hashing the decoded grayscale
  pixels is barely more work and is the honest key.
- ~~**Three rules are `?`**~~ — all three have since been measured:
  `dft.smooth_cells` (formerly `spectral.smooth_cells`) scales with sample area,
  `markov.reuse` is derived from `pairs` and `shortlist`, and `mask.highpass`
  stays a fixed 32 because the proposed derivation does not hold. Each has its
  row in the inventory above.
- ~~**Should `--mask-calibrate` become the default**~~ — done another way: a
  calibration in the config is used by every run on that sample, and
  `install_samples` makes one for each shipped film at the defaults, so no run
  pays for measuring. The original question, kept for its reasoning:
  **Should `--mask-calibrate` become the default** once the cache makes it nearly
  free on a repeat run? The first run on a new sample would pay a few probe
  builds; every later run would hit the cache. It does **not** make a first and a
  second run differ — a calibration is a pure function of the sample and the
  settings, so a warm run reproduces the cold one's mask byte for byte. What it
  does make differ is a run with the config and one without: `--no-config`
  would then calibrate every time and pay for it, and a run on a machine whose
  config holds an older answer under a different key would recompute it. Both
  are costs in time rather than in output, which is the case for doing it.

## Evidence behind the measured rules

Recorded so the numbers can be re-checked rather than trusted. All against
`4Xcrop.png` as the sample, scored with `test/levelfit`; see the markov section of
`CLAUDE.md` for the full write-up.

- **temp from bins and pairs** — optima (bins 16, pairs 64) → 0.41,
  (16, 256) → 0.38, (32, 64) → 0.44. The formula is exact on all three. Held-out
  check at (24, 128): predicted 0.41, measured optimum 0.45 with 0.42 a tie
  (0.182 vs 0.186) against 0.31 and 0.29 either side.
- **temp transfers across mask size** — 0.41 at 64², 128² and 256². This is what
  makes `--mask-calibrate` affordable.
- **temp does not transfer across bins or pairs** — hence the rule above.
- **flat weights beat fitted weights** — at each scheme's own best temperature,
  level-set fit reads flat 0.198, mi 0.226, ml 0.239, with directionality
  drifting from the sample's −0.2 dB to +0.2. ML weights predict the sample
  0.09 nats *better*, which is why likelihood is the wrong objective.
- **more Gibbs sweeps do not help** — at 256 pairs, iters 20/40/80 gives
  0.167 / 0.190 / 0.198.
- **mask size floor** — 32² fails density preservation badly, 128² is
  acceptable, and a mask larger than the grid exposes only a non-flat window of
  itself.

## Testing

Worked through 2026-09-01. What the pass found, and what is left.

### Addressed

- **`--explain` needed an image.** It is a question about the options, so
  `argv[1]` is only an input path when it does not look like a flag, and
  `image.width`/`image.height` gained nominal rules a real image overrides.
- **`--explain` readability.** Parameters belonging to a generator the run will
  not use are named once at the end instead of a row each; the description block
  aligns on the same key column; the run banner no longer prints first.
- **No-argument usage.** 18 lines instead of the 111-line reference.
- **`--help` comprehensiveness.** Audited by diffing the option strings in
  `dither.cpp` against those in the help text: no gaps in either direction.
- **`--mask-weights` should not have been an option.** `flat` measures best, so
  by this document's own first principle it is determined. Demoted to
  `--set markov.weights=`.
- **Explain strings.** Several had grown into paragraphs; trimmed to one line,
  with the justification left in the rule's comment where it belongs.
- **`markov.iters` was too high.** Confirmed: past ~20 sweeps the clumps stretch
  into worm-like ridges film grain does not have. Now 12 (since 3, then 2 — see
  the inventory above). `levelfit` cannot see
  this — it measures how much a level set clumps, not whether the clumps are
  round or stringy — so it had to be checked by eye. Recorded in CLAUDE.md.
- **`build_markov_mask` was 356 lines.** Now 133, with `select_pairs`,
  `fit_experts`' reporting, and `gibbs_field` lifted out. `select_pairs` is the
  one that pays: the index bookkeeping now lives and dies inside it.
- **Tests moved to `test/`** with a `test/run` wrapper, plus a `maskcheck` that
  did not exist: every generator's mask must still be *usable* — flat histogram,
  endpoints spanning 0..254 — which is the property a new generator is most
  likely to break.
- **Code review pass.** Findings acted on: two dead `Options` fields
  (`useConfig`, `configPath`, written and never read, since the config has to
  load before parsing); `run()` at 188 lines, with `obtain_mask` lifted out to
  108; a duplicated `--mask-out` write that the extraction exposed; `aliases`
  calling `colstats`/`colplot` bare, which relied on the repo root being on PATH.
- **Parameter audit.** Every derived rule was checked against the measurements
  it claims: `markov.temp` reproduces its three fitted optima, `markov.shortlist`
  yields C(40,2)=780 candidates at the default, and `blue.radius` lands exactly
  where the Gaussian reaches `blue.eps` at every `eps` tried.

### Not addressed, deliberately

- **`markov.temp` under-predicts off its fitted points** — 0.403 at (24, 128)
  against a measured 0.42–0.45. Real, and left alone: one held-out point is not
  enough to re-fit on, and `--mask-calibrate` is the proper answer.
- **`mask.out` is not a parameter** while `mask.in` is. `mask.in` is one because
  applicability tests need it; `mask.out` is needed by nothing. Consistency
  would argue for both, usefulness for neither.
- **`parseOptions` is still 199 lines.** It is one option table; splitting it
  would scatter something that reads better in one place.
