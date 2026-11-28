# User experience principles for `dither`

Distilled from the general list in [UXPrinciples.txt](UXPrinciples.txt) 
which covers interfaces, journeys, accessibility, consent and commerce. 

What it does have is a user who cannot see whether the output is right. A
dithered image that is one part in a thousand too light looks correct. A mask
with a seam looks like grain until it is tiled. A parameter left at a default
measured on someone else's film scan produces a plausible picture that is wrong
for yours. **The interface is a command line, but the thing being designed is
confidence in a result that cannot be checked by eye.**

These are the general principles that survive that translation, restated in this
project's terms and with the thing that earned each one. They are the ones to
apply when adding an option, writing a message, or choosing a default.

---

## 1. Never silently drop what the user typed

Either move the defaults so the option applies, or stop and name both sides.

`--mask-sample grain.png` is a request for a mask, so `--threshold` has no
business staying on `round` and then reporting the sample as inapplicable:
`Params::reconcile()` moves it and prints `Note: assuming …`. What it cannot
rescue is, by construction, blocked by something the user pinned, so
`Params::conflicts()` stops the run and names both:

```
--mask-scale applies to building a mask from a sample, which --mask-in rules
out; drop one of them
```

That replaced a `Note: … is ignored here` printed beside a zero exit status —
the shape of *"your option had no effect and everything is fine"*.

## 2. A default should be able to explain itself

Prefer a rule that derives the value from what the user did supply over a
constant; prefer a measured constant over a guessed one; and make every one of
them inspectable. `--explain` prints each parameter, its value, where that came
from (`user`, `measured`, `implied`, `derived`, `fallback`) and what it depended
on.

A fixed default is a claim that one number suits every sample, and that claim is
usually false — `markov.temp`'s measured optimum runs 0.34 to 0.60 across four
film scans.

## 3. Say how much you know

Distinguish what was measured from what was derived from what is a guess, in the
running text and not only in the documentation:

```
Note: no measured speed for this machine yet, so --budget is working from a
compiled-in guess and this run may overshoot. It is recorded afterwards, so
the next one will be closer.
```

The same applies to a number in a document. `--mask-highpass 32` is a *measured*
constant and the obvious derivation was tried and does not work; saying so is
what stops the next person re-deriving it.

## 4. The same command must produce the same bytes

Determinism is not a nicety here, it is the product: a mask is reused, compared,
cached, and exposed frame by frame onto paper. So `--mask-out` then `--mask-in`
reproduces a run byte for byte; a cache hit is byte-identical to a build; the
parallel `inpaint` search is byte-identical to the serial one rather than merely
equivalent; and a refactor is checked against a hash matrix rather than eyeballed.

Anything that would make output depend on the machine is opt-in for that reason
alone — which is why `--budget` is off by default.

## 5. Refuse with a measurement, not an opinion

An error should give the number, what it was measured against, and what to do
instead:

```
the sample has almost no texture left after --mask-highpass 32: 51% of this
mask's ordering would be decided at random rather than by the sample, against
7% for the worst real film scan measured. Use a smaller --mask-highpass to
keep more of it, a sample with some texture in it, or --mask-method blue if
noise is what you want.
```

And refuse on the quantity that actually matters. The guard this replaced
counted distinct values in the tile — a quantity the high-pass inflates, so the
sample it was meant to catch passed with 13277 of them.

## 6. One option, one quantity

Before adding a flag, ask whether it names a single thing. `--mask-iters` set
two unrelated quantities — the `dft` generator's alternating projections and
`markov`'s Gibbs sweeps — and was split. `--threshold blue` named a generator
where a threshold rule was meant, and became `--threshold mask` with
`--mask-method blue`.

A name that is nearly right is worse than one that is obviously wrong, because
nobody checks it.

## 7. The edge case is the common case

A sample smaller than the mask is not exotic: `mask_side` derives the mask size
from the *image*, so a `--supersample 4` run on a 256×256 picture asks for a
1024 tile as a matter of course. Handle it as normal operation — shrink the
derived size to what the sample can supply — and reserve refusal for the case
where the user pinned the size, which is a statement about what they asked for.

## 8. A knob has to earn its place

Every parameter is something to document, test, validate, put in a cache key and
explain. Add one only when a measurement shows it changes the result, and demote
it to internal (`--set`) when it turns out to be for experiments rather than for
use — as `--mask-eps`, `--mask-radius` and `--mask-iters` were. `blue.radius`
went further and became *derived*, because the only correct value is where the
kernel reaches its cutoff.

## 9. State the trade-off in both directions

No generator wins everywhere and saying so is part of the interface. `dft` owns
the spectrum by construction and reproduces none of the clumping; `markov` is
the reverse. Quoting either metric alone misrepresents the result, so the
documentation and the tools quote both.

## 10. Answer quickly, and say what you are doing

`--explain` validates the whole command line and prints every resolved value
without building anything, so a ten-second mistake is caught in a tenth of one.
Anything slower than about a second reports progress; anything reused says so
(`Reusing cached 128x128 mask …`); and every run opens with a line naming the
threshold, the generator, the sample and the output path, so a transcript says
what produced it.

---

## What does not transfer, and why

Recorded so the question is not reopened each time. Sections 8 (accessibility of
visual interfaces), 9.2–9.5 (dark patterns, consent, data minimisation, account
closure) and 10 (aesthetics and emotional design) of the general list address a
product with a screen, an account and a business model. This has a terminal, a
file, and a person who already owns both.

Two are worth *not* dismissing too quickly. **Accessibility** still means
something: output goes to a terminal, so colour is never used to carry meaning
and every message is plain text — which is a property worth keeping rather than
a box already ticked. And **aesthetics** are not absent, they are the product:
"does this look like film grain" is the goal `test/patchfit` exists to make
measurable. The principle that transfers is 10.1 — visual appeal must not
conceal a defect — which is exactly why `swing` is measured on the field rather
than judged on the finished mask.
