# Plan: one naming rule for every option

**Status: done.** Both phases have landed; what follows is the record of what
was decided and why, kept because the reasoning outlives the change. The rule it
describes is now simply true, and `test/flagcheck` holds it.

## The goal

Today a parameter is reachable one of two ways. A few dozen have a flag of their
own, written by hand and mapped to a key by a table; the rest are reachable only
as `--set namespace.key=value`. The two spellings look nothing alike, and which
one a parameter gets is a matter of history rather than of anything a user could
predict.

**One rule instead: the key `namespace.key` is the flag `--namespace-key`.**
`blue.sigma` is `--blue-sigma`, `dft.smooth_cells` is `--dft-smooth-cells`. A
parameter's name in `--explain` and its name on the command line become the same
fact written twice, so neither can drift from the other.

`--set` stays. It is how the structure below is *enforced* rather than merely
described — a parameter reachable only by its key is a parameter whose flag the
generator forgot to declare, and after this work there is no such thing. But an
end user should never need it, and it leaves the primary reference.

## What is wrong now

### Generator-specific options wearing the `mask-` prefix

`--mask-*` reads as "applies whatever generator you chose", and six flags break
that promise:

| was | renamed to | belongs to |
|---|---|---|
| `--mask-sigma` | `--blue-sigma` | blue only |
| `--mask-pairs` | `--markov-pairs` | markov only |
| `--mask-bins` | `--markov-bins` | markov only |
| `--mask-temp` | `--markov-temp` | markov only |
| `--mask-report` | `--markov-report` | markov only |
| `--mask-nbhd` | `--markov-radius` | markov only, **and** see below |

What keeps `--mask-` afterwards is the set that genuinely spans generators:
`method`, `sample`, `scale`, `highpass`, `size`, `in`, `out`, `calibrate`.

The UI prefix follows the *registry* namespace, not the module: the keys are
already `blue.*` though the file is `bluenoise.cpp`, so `--blue-sigma` needs no
special case.

### One concept, two names

`blue.radius` and `inpaint.radius` both mean "how far out this generator looks".
`markov.nbhd` is the same idea under a different word. Renaming the key to
`markov.radius` leaves four families each spelled one way:

| family | generators |
|---|---|
| `radius` | blue, inpaint, markov |
| `iters` | dft, inpaint, markov |
| `max_size` | all five |
| `max_repeats` | all five |
| `rate` | all five |

Each stays inside its own namespace, because each is genuinely per-generator and
individually worth tuning — `dft.iters` is alternating projections and
`markov.iters` is Gibbs sweeps, and collapsing them would be a worse lie than
the one being fixed.

### `--mask-calibrate` is generic in name and markov-only in fact

It is declared `mask::generator("markov")`. Its applicability should instead be
*the selected generator has a `calibrate` function*, which is a property the
generator table already records. Then the name is honest, and a second generator
gaining calibration needs no change here. Its doc string also still says it
measures `markov.temp`; it now measures three things.

### `inpaint.levels` collides with `--levels`

`--levels` is the number of output tones. `inpaint.levels` is the number of
coarse-to-fine pyramid levels — a different concept that would land at
`--inpaint-levels`, one word away. It becomes **`inpaint.scales`** /
`--inpaint-scales`.

## Phase 1 — fix the names

No new syntax. Each step is separately committable and the suite passes between.

1. **`markov.nbhd` → `markov.radius`.** A registry key, so it appears in the mask
   cache key and the calibration key; both invalidate automatically and no output
   changes. Touches `markov.h`, `markovcalib.cpp`, `generators.cpp`, `cli.cpp`,
   `CLAUDE.md`, `README.md`, `doc/DEFAULTS.md`.
2. **`inpaint.levels` → `inpaint.scales`.** Same shape. Touches `inpaint.h`,
   `inpaint.cpp`, `generators.cpp`, `doc/DEFAULTS.md`.
3. **The six flag renames**, all in the generator table plus the usage text.
   User-visible, so `test/coltest`, `test/impliedtest`, `test/flagcheck`'s
   `RETIRED` list, `README.md` and `CLAUDE.md` move together.
4. **`mask.calibrate`'s applicability and doc string.**

Phase 1 changes no mask bytes. The 76-run hash matrix is the check.

## Phase 2 — generate the flags

The point of doing Phase 1 first is that this becomes mechanical.

- `cli.cpp` parses `--<ns>-<key>` by reflecting the flag back to a key, instead
  of one hand-written arm per option.
- The `GeneratorOption` column and each generator's `options` list are **deleted**
  — they exist only to map six flags to six keys, which the naming rule now does.
- `--help`'s per-generator sections are **generated** from the registry: name,
  type, default, range and the doc string each rule already carries. This closes
  `ARCHITECTURE.md`'s **S9**, which was deferred because generating the prose
  meant deciding how much of it belonged in the table; generating only the
  factual rows and keeping one authored paragraph per generator settles that.
- `test/flagcheck` keeps the retired-name check and loses the rest: a documented
  flag that does not exist stops being possible.

**Parameters that get no flag.** Two kinds are written by the program and never
by a user: the measured facts (`image.*`, `sample.*`) and the throughputs
(`*.rate`). `intermediate` is the wrong test — `supersample` and `budget` are
intermediate and are proper controls — so this needs one new declaration on the
rule.

**Hand-written exceptions that stay.** `--mask-size WxH` is one flag over two
keys, and better than a pair of generated per-axis flags. The pipeline flags (`--threshold`,
`--levels`, `--frames`, `--spatial`, `--seed`, `--supersample`, `--budget`,
`--out`, …) are not namespaced and do not change.

Phase 2 should be byte-identical by construction; the hash matrix is the check.

## Progress

| phase | step | state |
|---|---|---|
| 1 | `markov.nbhd` → `markov.radius` | **done** |
| 1 | `inpaint.levels` → `inpaint.scales` | **done** |
| 1 | six flag renames | **done** |
| 1 | `mask.calibrate` applicability | **done** |
| 2 | flag generation, table deletion, generated help | **done** |

### What Phase 2 landed

Byte-identical across the hash matrix, and the option surface roughly doubles:
**58 documented options against about 30**, because every parameter that was
reachable only by `--set` now has a flag. `--markov-bins 8` and
`--set markov.bins=8` produce the same mask byte for byte, which is the point —
they are one write through one path.

- `cli.cpp` parses `--<ns>-<key>` by reflecting the flag back to a key. Four
  hand-written mask arms went with the six generator ones.
- `GeneratorOption` and the `options` column are **deleted**. Adding a parameter
  is now adding a parameter; it is an option the day it is registered.
- The per-generator help blocks are **generated** from the registry — name,
  argument, default, doc. This closes `ARCHITECTURE.md`'s **S9**, and the answer
  to what held it up is that the facts generate and the prose does not: each
  generator keeps its authored paragraph, and so do the eight shared `--mask-*`
  options a user meets first.
- `option_takes_value`, which the config pre-scan needs, answers from the
  registry instead of a hand-kept list.

Four things worth recording:

- **A switch is a parameter whose only permitted values are 0 and 1.** No new
  declaration: a range of exactly `[0, 1]` says there is nothing to type after
  the flag. `--set key=0` remains the way to turn one off, which the switch
  spelling cannot express.
- **Nine parameters get no flag** — `image.*`, `sample.*` and the five
  `<generator>.rate`. These the program measures; a `--image-width` would be a
  lie, since the picture decides it and says so a moment later. They stay
  reachable by `--set`.
- **`hide_option` had to run after the registration it hides.** Placed at the top
  of each `register_*_params` it found no rule and silently did nothing, so
  `--blue-rate` went on working. A no-op that looks like a declaration.
- **`test/flagcheck` was reading string literals**, which is exactly what this
  work deleted — it reported four real options as nonexistent the moment the
  hand-written arms went. It asks `--help` now, which cannot drift from the
  parser by construction.

### What Phase 1 landed

All four steps, byte-identical across the 76-run hash matrix — every mask, every
threshold, every spacing, all five generators, calibration and `--budget`
included. The renames are names only.

Three things came up that the plan did not anticipate:

- **A renamed flag says where it went.** `--mask-nbhd` now answers *"is now
  --markov-radius: --mask-\* is for options that apply to every generator, and
  this one does not"* rather than `Unknown option`. A name that plainly *was*
  known deserves better than being told it is not, and the message carries the
  reason for the move rather than just the substitution.
- **A blanket rename edited its own escape hatch.** The table mapping old names
  to new was written before the bulk search-and-replace ran over `cli.cpp`, so it
  came out mapping every new name to itself — dead code that would never fire.
  Caught by re-reading the diff rather than by any test, since a lookup that
  never matches breaks nothing.
- **`test/flagcheck` could not tell a prefix from a flag.** This document
  discusses `--mask-*` as a family, and the check read that as a reference to a
  nonexistent option. A trailing `-` or `*` now marks a prefix under discussion.

Historical records keep the old spellings on purpose: `doc/improvements2.txt`,
`doc/improvements3.txt` and `test/lastrun.txt` describe what was true when they
were written, and rewriting them would make their figures harder to match against
the runs they came from.
