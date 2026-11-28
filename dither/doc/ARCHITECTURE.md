# ARCHITECTURE.md

What the program is built out of, how the pieces depend on each other, and where
the structure is under strain.

This describes the code **as it stands**, after S1–S4 were implemented
(section 7). The problems in part 6 are findings and the suggestions in part 9
are options with their costs attached, not decisions.

Terms of art are defined in [GLOSSARY.md](GLOSSARY.md) and not redefined here.
Three other documents divide the rest of the ground: [DESIGN.md](DESIGN.md)
carries the design rationale and the references, [DEFAULTS.md](DEFAULTS.md) the
parameter inventory, and `CLAUDE.md` the measurement record behind individual
algorithms. None of them describes the *shape* of the program, which is what
this file is for.

---

## 1. What the program is

`dither` reduces a greyscale image to a small number of display levels while
preserving local density, optionally spreading the result over several frames.
Everything happens in linear light; the sRGB curve is applied only at the
encode/decode boundaries.

It is one binary over one static library, with no plugins and no runtime configuration
beyond a flat key/value cache. It is a batch command-line tool: read an image,
build or load a threshold mask, dither, write one or more PNGs.

Two further executables are built from the same tree and are not part of it:
`srgbtest` (prints what the sRGB curve does, asserts nothing) and
`temporaldither` (a standalone frame-sequence simulator with no OpenCV
dependency and its own small threshold functions, kept as a demonstration —
see README). Neither links against the dithering code.

---

## 2. Module map

Nineteen translation units in `dither_lib`, plus `main.cpp`, ~8,300 lines of
`.cpp` (the `dither` binary; `srgbtest` and `temporaldither` add ~230 more and
link none of it). The include graph is **acyclic**, and this is the structural
strength of the project.

```
  layer 5   main.cpp ............. the process entry point
                |
  layer 4   dither.cpp ........... the pipeline and dither_main
                |
  layer 3   cli.cpp    mask.cpp        generators.cpp        engine.cpp
            (options,  (size, cost,    (the five generators   (DitherFn, Space,
             usage)     obtain,         as one table, and      quantize_scan --
                        calibration     their calibrators)     depends on srgb.h
                        routing)                               and nothing else)
                |          |   maskcache.cpp    |
                +----------+-----------+--------+
                                       |
  layer 2b  markovcalib.cpp  inpaintcalib.cpp  dftcalib.cpp
                     \               |               /
                      +---- calib.h (shared scores, inputs_for) ----+
                                       |
  layer 2   bluenoise.h  spectralmask.h  markov.h  inpaint.h  directmask.h
                                          |
                                    markovmodel.h (markov.cpp <-> markovcalib.cpp)
                                       |
            maskparams.h ....... applicability predicates shared by
                |                 every mask parameter
            samples.h .......... what --mask-sample names, the film's crop
                |
  layer 1   defaults.h    maskutil.h    srgb.h    options.h
            (registry)    (ranking,     (curves,  (the Options
                           bands,        ladders)  struct alone)
                           wrapping)
```

Edges run one way only. `calib.cpp` is told whether a generator takes the
film's crop rather than asking the table, so it stays below `generators.h`.
Each generator's `.cpp` includes exactly its own header plus
`maskutil.h`, so no generator can reach another; `generators.cpp` is the only
file that knows all five exist.

| unit | lines | role |
|---|---|---|
| `markov.cpp/.h` | 1390 | the MRF generator, its band holding and its `--markov-report` diagnostics |
| `defaults.cpp/.h` | 1050 | the parameter registry: lazy rules, origins, reconcile |
| `cli.cpp/.h` | 874 | parsing, usage text, reconciliation reporting |
| `maskutil.cpp/.h` | 778 | ranking, thresholding, band power, equalising, wrapping — shared by generators |
| `inpaint.cpp/.h` | 703 | Efros–Leung generator |
| `mask.cpp/.h` | 510 | mask size and cost rules, `obtain_mask`, calibration routing |
| `markovcalib.cpp/.h` | 497 | markov's `--mask-calibrate` and its caching |
| `samples.cpp/.h` | 489 | what `--mask-sample` names: files, film directories, the library, crops, `reference_for` |
| `directmask.cpp/.h` | 458 | the sample used directly, equalised, with its seam cut |
| `generators.cpp/.h` | 445 | the generator table |
| `engine.cpp/.h` | 430 | the dither itself |
| `maskcache.cpp/.h` | 423 | the mask cache and its key |
| `calib.cpp/.h` | 380 | what the calibrators share: scores, reference, `inputs_for` |
| `srgb.cpp/.h` | 359 | `vision::` — curves, ladders, encode/decode, input depth |
| `spectralmask.cpp/.h` | 354 | alternating-projection (dft) generator |
| `dither.cpp` | 284 | the pipeline, and `dither_main` |
| `inpaintcalib.cpp/.h` | 282 | inpaint's sweep-count calibration |
| `bluenoise.cpp/.h` | 257 | void-and-cluster generator |
| `dftcalib.cpp/.h` | 248 | dft's smoothing calibration |
| `maskparams.h`, `options.h`, `markovmodel.h`, `app.h` | 218 | shared vocabulary |

Four properties hold and are worth preserving:

- **No generator knows another exists**, and only `generators.cpp` knows all of
  them. Adding one is a new module plus a row in its table — `direct` was added
  that way, and touched nothing else. The row carries the
  name, whether it imitates a sample, whether `reconcile()` may assume it,
  whether it pools scans and whether it takes the film's crop, its default
  high-pass, its registration, work formula, build, anything it needs
  validated, its sample limit and its calibrator. No other file names a
  generator.
- **The engine depends on nothing but `srgb.h`.** `engine.cpp` includes one
  project header. It does not know what a mask generator, an option or a config
  file is.
- **Four leaf modules depend on nothing**: `srgb.h`, `maskutil.h`, `defaults.h`
  and `options.h`.
- **The registry knows nothing about dithering.** `defaults.cpp` handles rules,
  origins, dependency recording and conflict reporting without a single
  reference to masks or levels.

---

## 3. The four load-bearing abstractions

**`DitherFn` / `SpatialFn`** (`engine.h`) — the threshold rule and the error
deposit, as two orthogonal axes. `DitherFn` is an abstract class with per-pixel
state (`RoundDither`, `RandomDither`, `MaskDither`); `SpatialFn` is a plain
function pointer. Every threshold works at every diffusion strength, and
`quantize_scan` is the single driver both are passed to. This is the cleanest
abstraction in the program.

**`Space`** (`engine.cpp`, private) — the conversion between display levels and linear
light, and the reason both the spatial and temporal ledgers conserve the same
quantity. It returns the identity for a uniform ladder, so default output stays
bit-identical.

**`vision::LevelSet`** (`srgb.h`) — where the output levels sit, in both pixel
value and linear light, under three spacings. The engine reads rung values from
it rather than computing them.

The input/output asymmetry is deliberate and worth stating: **input** may be 8-
or 16-bit and is decoded through one depth-generic path, because 16-bit is what
gives error diffusion and the multi-frame budget anything to distribute;
**output** is 8-bit throughout — level indices in `CV_8U`, `LevelSet::px` as
`uint8_t`, `--levels` capped at 256. The curve primitives live in the unit
domain (`decode_code`, `srgb_to_linear_unit`) precisely so that the file's bit
depth stops mattering above the boundary.

**`params::Params`** (`defaults.h`) — a lazy resolution graph. A rule runs once,
records what it read, and reports the origin of every value (`User`, `Measured`,
`Implied`, `Derived`, `Fallback`). It also carries `reconcile()`, which moves
defaults so a supplied option is never silently ignored, and `conflicts()`,
which refuses the run when it cannot. Generators register their own rules.

---

## 4. Lifecycle of a run

`dither_main()` in `dither.cpp`, in order — the step each belongs to in brackets:

1. Construct `Params`; a loop over the generator table registers each
   generator's rules, then `register_mask_params` the geometry that spans them.
   [`dither.cpp`, `generators.cpp`, `mask.cpp`]
2. Peek at `--config`/`--no-config` and load the cache file. Cached values enter
   as `Origin::Measured` so the command line still outranks them. [`cli.cpp`]
3. `parseOptions` — apply the command line, then `reconcile()` to move defaults
   toward what was supplied, then `conflicts()` to refuse what cannot be
   honoured. `Options` is filled from `Params` afterwards as a read-only cache.
   [`cli.cpp`]
4. Read the sample once, as the generator takes it (the film's crop for dft and
   direct), and record its size for the mask-size rule. [`dither.cpp`, `samples.cpp`]
5. Take a calibration for the sample from the config, or measure one under
   `--mask-calibrate` — before `--explain`, so it reports the values the build
   will use, and never measuring under it. [`mask.cpp` `maybe_calibrate`, the
   table's `calibrate`]
6. `--explain` returns here.
7. Build the `DitherFn`. For `mask`, `obtain_mask` either loads `--mask-in` or
   calls the chosen row's `build`, records the build rate, and writes
   `--mask-out`. [`mask.cpp`, `generators.cpp`]
8. Build the `LevelSet`, decode the image onto it — at whatever depth the file
   carried. [`srgb.cpp`]
9. `ditherPass` — one goal pass (multi-frame only) plus one pass per frame,
   each through `quantize_scan`. [`engine.cpp`]
10. Encode each frame through the ladder, resize to the output size (resolving
   `--supersample`, and `--out-size` in the same single resample), average in
   linear light, write.

---

## 5. Test architecture

Driven by `test/run`. The suite splits into four kinds, and the first is the one
S1 made possible:

- **Unit tests** (`unittest.cpp`, linking `dither_lib`): the checks that are
  about an invariant of the code rather than a property of an image — the
  multi-frame budget, the level ladder, the shared ranking. These call the
  functions. Everything else runs the binary and reads back a PNG, which is
  right for tone and density and wrong for arithmetic: `budgettest` was one of
  these before it was converted, and it had to reconstruct the ladder in Python
  to say what it expected, which it got wrong for two of the three spacings.

- **Behavioural checks** that pass or fail: `coltest` (per-column density),
  `tonetest` (whole-image tone), `masktest` (the four mask properties),
  `devicetest`, `impliedtest`, plus
  inline `scalecheck`/`budgetcheck`/`shellcheck` in `test/run`.
- **Measurement tools** that print numbers for a human: `maskstats`, `maskfit`,
  `levelfit`, `patchfit`, `colstats`, `colplot`.
- **Archived transcripts**: `base.txt`, `basefull.txt` — saved `coltest` output —
  and `lastrun.txt`, a whole `test/run` transcript. Read by no tool; kept so a
  change can be compared against what the suite said before it.

---

## 6. Problems

Split deliberately. The first group are gaps nobody chose; the second are costs
that were chosen, measured and recorded, and should not be "fixed" without
re-opening the evidence behind them.

### 6a. Unintended gaps

**P1–P4 are resolved.** S1 to S4 were implemented; what each did, and what it
cost, is in section 7. What they leave:

**P5 — The calibration criterion exists in two languages.** `markovcalib.cpp`'s
`level_set_low`/`clump_distance` optimise a metric; `test/levelfit` (via
`test/maskstats`) validates against a near-identical one. The divergence is
deliberate and documented — matching maskstats' ring averaging exactly was tried
and measured worse, so the C++ version averages cells instead — but *nothing
tests that the two stay near neighbours*. If `maskstats` changes,
`--mask-calibrate` silently begins optimising something the validation tool no
longer measures. This is a maintenance coupling with no guard, not a duplicate.

**P6 — The build produces a stale-binary hazard.** An untracked `dither`
executable in the repo root shadows `build/dither` for anyone with `.` on their
PATH, and is stale enough that it no longer loads its OpenCV libraries. Nothing
in the project produces it or mentions it. Related: `4Xcrop.png` was untracked
but required by three checks in `test/run`, so the suite did not run from a
fresh clone.

**P7 and P8 are resolved.** `inpaint` is a `reconcile()` candidate, and the
candidate list is a column in the generator table rather than a literal. The two
residual couplings are gone: `cli.cpp`, `mask.cpp` and `dither.cpp` include zero
generator headers. What that took is in section 7.

### 6b. Accepted costs, with recorded rationale

These look like faults and are not. Each has a measurement behind it; changing
one means re-opening that measurement, not tidying.

- **`Options` duplicates ~12 fields that also live in `Params`.** Two sources of
  truth kept in step by `fill_options_from_params`. It is a *read-only* cache
  filled after the whole command line is applied, precisely because a writable
  second copy silently broke `--set`. The duplication is the fix, not the bug.
- **`--spatial` applies to the goal pass too.** The goal pass takes the same
  `spatial_fn` and the same scale, so at zero nothing diffuses anywhere and
  multi-frame runs clip the shadows. It once pinned the scale at 1, which made
  every non-zero `--spatial` the same multi-frame image.
- **`--budget` is off by default.** On, it would make output machine-dependent.
  The price is listed in `DEFAULTS.md` and has not been paid.
- **`base.txt`/`basefull.txt` are refreshed manually, not by `test/run`.** They
  are transcripts for reading, and the two-step refresh procedure is documented.
- **`temporaldither` duplicates threshold logic.** It is a standalone
  demonstration with no OpenCV dependency, deliberately not sharing code.

---

## 7. What S1–S4 did

All four were implemented, each verified byte-identical against a 153-hash
matrix covering every threshold, spatial value, frame count, level count and
spacing, all four generators then in the table — `direct` came later — mask
round-trips, calibration, `--budget`,
`--explain`, `--help` and five error paths. The suite passed at every step.

**S1 — `libdither`.** `main()` moved to `main.cpp`; everything else became a
static library the executable links, and `srgbtest` links it too rather than
recompiling `srgb.cpp`. No file was split and no logic moved. This is what makes
a unit test possible, and nothing else here needed it to be done first.

**S2 — the generator table.** Each of the seven sites became a lookup, and
`dither.cpp` now dispatches on no generator name. Two things fell out that were
not planned. The pair-count special case disappeared: only markov's work formula
resolves a pair count, so no other generator pulls that parameter into the
graph, which is what the special case existed to prevent. And one user-facing
message was *wrong* and is now generated from the table — "name dft or markov to
imitate it" had never learned about inpaint. That was the only byte that changed
in the whole refactor, and it is the defect class the change exists to end.

**S3 — the split.** `dither.cpp` went from 1386 lines to 176. `Space`,
`for_serpentine` and `quantize_scan` are now private to `engine.cpp`: nothing
outside needs them, and `quantize_scan` is where both ledgers are kept, so it is
better not reachable from the CLI at all.

**S4 — half of it, and the other half deliberately not.** The calibrator moved
to `markovcalib.cpp` behind `markovmodel.h`, which is exactly fit-and-draw; it
never looks inside a model. The `--markov-report` diagnostics stayed, and the
estimate of "low, purely mechanical" was wrong about them: `report_fit` is
called from inside `fit_markov_model` on that function's local state — the
quantised sample, the expert tables, the pooled weights, the occupancy counts.
Moving it out means publishing markov.cpp's fitting internals through a header
so that a reporting file can read them. A fitting diagnostic is intimate with
the fitter by nature; extracting it would trade a cohesive private interior for
a leaky published one. markov.cpp is 1144 lines rather than the ~900 S4
predicted, and that is the right answer.

## 8. What P7 and P8 took

**P7 — one column, one behaviour change.** `offer_in_reconcile` on the inpaint
row. Nothing else in the matrix moved, and a plain `--mask-sample` still selects
dft, because dft is the default when a sample is named and `reconcile()` only
moves what it must. `impliedtest` gained the case that was broken. Worth noting
how the test failed first: it asserted an "assuming mask.method=dft" note for
dft, which correct code never prints — dft needs no move. The generators are
not symmetric and the test now says which is which.

**P8 — two more columns.** `options` (flag, key, kind) and `validate` took the
six generator flags and the one string check out of `cli.cpp`; `calibrate` took
`calibration_key` and `maybe_calibrate_markov_temp` out of `mask.cpp` and into
`markovcalib.cpp`, leaving a four-line router.

The `calibrate` hook takes the sample's *path*, not the image, and that is not
incidental: markov's first act is to check whether a command-line `--markov-temp`
has already outranked the measurement, which it reports without reading
anything. A hook taking a `cv::Mat` would have moved the read in front of that
check and changed which error an unreadable path produces.

One more stale message surfaced and was corrected. The weights validation said
"--mask-weights must be flat, mi or ml", naming a flag that does not exist —
the parameter was then reachable only as `--set markov.weights`, and is
`--markov-weights` today. Same class as "name dft or markov to imitate it": a
literal written once and never revisited.

## 9. Remaining suggestions

**S5 — Guard the two-language metric.** A check that scores one mask with both
`clump_distance` and `test/levelfit` and asserts they agree to a stated
tolerance. Addresses P5, and turns a documented near-neighbour into a tested
one. Cheap now: S1 made `clump_distance` linkable.

**S6 — Housekeeping. Done.** The root `dither` is in `.gitignore`, and the
three checks name `examples/Kodak-4X-1.png`, which is byte for byte the old
`4Xcrop.png`, so nothing re-baselined and the suite runs from a clean clone.
Addresses P6.

**S9 — Generate the generator sections of `--help` from the table. Done**, by
the naming rework recorded in [OPTIONS-PLAN.md](OPTIONS-PLAN.md). It had been
deferred on the grounds that the text is prose about what each generator *is*
and not just a flag list, so generating it meant deciding how much of that
prose belonged in the table — and the answer turned out to be a clean split
rather than a judgement call. **The facts generate and the prose does not.**
Each option's name, argument, default and one-line doc come from the rule that
already declares them; each generator keeps its authored paragraph, and so do
the eight shared `--mask-*` options a user meets first.

Two things followed from it that the suggestion did not anticipate. A flag added
to a row now appears in `--help` the day it is registered, which is what closes
this — but it also meant every parameter acquired an option, which made
`doc/DEFAULTS.md`'s Principle 1 ("it does not appear in `--help`") false and
needed an amendment there rather than a correction here. And the ten cost
ceilings, five generators × `max_size`/`max_repeats`, generated as ten rows and
read as repetition; they are one grid now, which is also the only form that
shows the comparison between generators that is the point of them.

**Not recommended.** Unifying `Options` into `Params` (see 6b — the duplication
is load-bearing); making `--budget` the default; sharing code with
`temporaldither`; extracting markov's diagnostics.

## 10. Summary

The layering is sound: acyclic, with three dependency-free leaves and four
generators that do not know about each other. The parameter registry is a real
piece of design and it does what it claims for *rules*.

The strain used to be concentrated in `dither.cpp`, where the CLI, the engine,
the mask policy and every generator name met. S1–S4 removed that: the engine
depends on one header, the generators are a table, and `dither.cpp` is 176 lines
of pipeline.

P7 and P8 are closed too, so the generator abstraction is complete: a row in one
table carries everything about a generator, and no other file names one. Two
generators have been added since through exactly that seam — `inpaint` and
`direct` — each as a new module plus a row, which is the claim the abstraction
was making.

What is left is smaller and of a different kind. P5 is a maintenance coupling
with a known guard. P6 is housekeeping. S9 is the last hand-written list that
can go stale, and is unfinished because deciding what belongs in the table is a
design question rather than a mechanical one. And S1's actual benefit — a test that
calls the code instead of shelling out to it — is now partly claimed rather than
unclaimed: `unittest` links the library and holds the budget, ladder, ranking
and periodic-decomposition checks. The rest of the suite stays where it belongs.
Tone, density and mask quality are properties of images, and a subprocess and a
PNG are the right way to ask about them, so "partly" is the correct end state
rather than a shortfall.
