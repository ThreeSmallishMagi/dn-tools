# doc/

What is in here, and which file answers which question.

## The five documents

Read in this order if you are new to the project. Each says at its top what it
covers and what the others cover, so none of them repeats another.

| file | question it answers |
|---|---|
| [GLOSSARY.md](GLOSSARY.md) | *What does this word mean?* Every term of art in one alphabetical list, each entry readable on its own, every acronym written out. The other files link here rather than redefining. |
| [DESIGN.md](DESIGN.md) | *Why is it built this way?* What the program is for, the decisions that shape it, and what is still unsolved. Carries the [References](DESIGN.md#references) — every paper and web source the implementation draws on. |
| [ARCHITECTURE.md](ARCHITECTURE.md) | *What is it built out of?* The module map, the dependency graph, the load-bearing abstractions, and the record of structural problems with what was done about each. |
| [UX.md](UX.md) | *How should this behave for the person running it?* Ten principles for the command-line surface — naming an option, wording a refusal, choosing a default — each with the thing in this project's history that earned it. |
| [DEFAULTS.md](DEFAULTS.md) | *Where does this number come from?* The full parameter inventory and the design of the mechanism that derives defaults from whichever options were actually supplied. Read before adding a tuning knob. |

`../CLAUDE.md` is a sixth and sits at the repository root because that is where
its reader looks for it. It is the **measurement record**: what was tried, what
it measured, what that rules out. A new finding goes there; a new definition
goes in GLOSSARY.md.

## Records of finished work

Not maintained, and not out of date either: each describes one piece of work
as it was decided, and says at its top when.

| file | what it is |
|---|---|
| [OPTIONS-PLAN.md](OPTIONS-PLAN.md) | *Why is this option called that?* The naming rule — `namespace.key` is `--namespace-key` — and how the command line was moved onto it. Both phases have landed; `test/flagcheck` holds the rule, and this is kept for the reasoning. |
| [MarkovParameterStudy.md](MarkovParameterStudy.md), [InpaintParameterStudy.md](InpaintParameterStudy.md) | Parameter sweeps of the two synthesising generators over six scans, with held-out samples, from September 2026. Their recommendations are inputs to the defaults, not the defaults: CLAUDE.md records what was adopted. |
| [TestCoverage.md](TestCoverage.md) | A function-by-function map of which tests reach which code, and a specification for the missing ones, as of 18 October 2026. Read from source and the call graph, not measured. |
| [UXPrinciples.txt](UXPrinciples.txt) | General principles of user-experience design. [UX.md](UX.md) is this project's distillation of it. |

## Reviews and their triage

Each round of external review arrived as one file and was answered in another,
one line per suggestion, marked Accepted / Rejected / Ask / Done — except the
last, whose five findings were all accepted and are answered by the commits
that fixed them. The **rejections
carry the reasoning** and are the more useful half — they record which
plausible-looking changes were examined and found to cost more than they save,
so the same suggestion does not get relitigated on a later pass. Every
suggestion in every round is resolved, except the Ask items in triage7.txt.

| review | triage |
|---|---|
| [code_improvement_suggestions.txt](code_improvement_suggestions.txt) | [improvements.txt](improvements.txt) |
| [code_improvement_suggestions2.txt](code_improvement_suggestions2.txt) | [improvements2.txt](improvements2.txt) |
| [code_improvement_suggestions3.txt](code_improvement_suggestions3.txt) | [improvements3.txt](improvements3.txt) |
| [improvements4.txt](improvements4.txt) | [triage4.txt](triage4.txt) |
| [improvements5.txt](improvements5.txt) — review 4 again, then a follow-up from line 638 | [triage5.txt](triage5.txt) |
| [TestCoverageReview.md](TestCoverageReview.md) | [triage6.txt](triage6.txt) |
| [UXImprovements.md](UXImprovements.md) — a UX review from 23 September 2026, nine findings | [triage7.txt](triage7.txt) — several findings are marked Ask, because they turn on how the tool is used downstream |
| [CommitReview-2026-11-15.md](CommitReview-2026-11-15.md) — the 67 commits from `dff818c` to `fee20e7` | All four fixed in one commit, each with a test that fails on the old code: the calibration's reference set, `band_reference`'s sampling, `seamlab --check`, and `--method direct` |
| [CommitReview-2026-11-07.md](CommitReview-2026-11-07.md) — the 28 commits from `dff818c` to `255c9e5` | All five fixed, each with a regression test that fails on the old code: 1 `612835b`, 2 `c4a725c` and `6b79bc4`, 3 `dc320db`, 4 `89eb12f`, 5 `640efa3` |

## The tour page

[tour/dither-tour.html](tour/dither-tour.html) is the source of the published tour of the
program, https://claude.ai/artifact/HbXkfenepb2N9rCDRZqzac, with a `{{name}}` placeholder
for each figure. [tour/build.sh](tour/build.sh) makes every figure with the current build,
from the shipped films installed and calibrated in a private config, and writes the
self-contained page to its work directory (`/tmp/dither-tour` by default). It is the page
to republish whenever an option or a default it shows changes.

## Working notes

These are not maintained documents. They are kept because they record intent or
history that the documents above deliberately do not carry, and because
several of them are the only account of *why* a piece of work was started.

| file | what it is |
|---|---|
| [uses.txt](uses.txt) | The jobs the program is built for, in the author's own words. Now also stated in [DESIGN.md](DESIGN.md) section 1, which is the maintained version. |
| [markov.txt](markov.txt) | The original design brief for the `markov` generator, written before it existed — including the guess that it might be a Markov random field, which it turned out to be. Kept as the record of what was asked for, against which what was built can be judged. |
| [bugs.txt](bugs.txt) | Bug repros in the form they were first noticed. Several are now closed: the `--threshold blue --spatial 0.5` family was the diffusion-scaling bug (see [DESIGN.md](DESIGN.md) section 4), and the `--levels 3`/`--levels 4` biases were the display-units-versus-linear-light bug (section 2). Not pruned, because a repro that no longer reproduces is a regression test waiting to be written. |
| [todo.txt](todo.txt) | The author's working list, `x` marking done. |
