# UX improvement report for `dither`

Reviewed 23 September 2026.

The first priority is to establish which user problem most needs solving and
where it occurs in real work. The documents explain the intended applications
and algorithms, but do not establish who currently uses the tool, which tasks
matter most, or what prevents a successful exposure, print or image. Improving
help text may be useful; it may also miss a larger problem with installation,
device handoff, physical output quality or repeated experimentation.

Three principles govern this revision: validate the problem before choosing a
solution, learn from actual users rather than assumed personas, and evaluate the
whole task in its working environment. Correct the directly observed
documentation contradictions now. Treat the other interface proposals below as
candidate responses to problems that still need investigation.

This is a documentation-based UX review, not a runtime audit or a usability
study. All 13 existing files in `doc/` were reviewed, including the historical
notes. The top-level README was consulted for its existing onboarding and CLI
guidance. Findings about inconsistent text are directly observed; risks to user
behavior and proposed improvements are hypotheses to validate. An undocumented
behavior is not assumed to be an absent implementation. Historical bug reports
are not treated as current defects.

**What we know about users, and what we still need to learn**

[uses.txt](uses.txt) and [DESIGN.md](DESIGN.md) §1 establish four intended uses.
They do not establish their frequency, relative importance, current adoption or
unmet demand. The author is a valuable potential research participant; an
author's workflow is evidence about that workflow, not automatically about every
user. Likewise, CLI experience, imaging expertise and familiarity with the
physical output process are separate abilities and should be investigated
separately.

| Intended use, documented | Candidate user problem to investigate | Evidence needed before prioritizing a solution |
|---|---|---|
| Monochrome enlarger exposure | Moving from a source image to a correctly interpreted frame sequence may require fragile manual handoffs. | Observe a recent job through the receiving exposure program; inspect file mapping, ordering, settings and any rework. |
| Multi-frame display of a 16-bit source | The user may not be able to deliver or verify the tonal benefit on the actual display. | Observe playback and comparison with the source on the target device; establish what counts as a useful improvement. |
| 1-bit photogravure mask | A digitally plausible pattern may fail the medium's physical resolution or tone requirements. | Follow a mask through the production workflow and examine an actual output or existing failed sample with its operator. |
| Film-like grain | Finding an acceptable texture may take too many iterations or sacrifice qualities the user values. | Observe sample choice, comparison and stopping decisions on a real image at its intended output size. |

These problem statements are hypotheses, not findings of user failure. Do not
give all four workflows equal design investment by default. Select the first
one using observed severity, recurrence, time or material loss, and importance
to the people the project intends to serve. Record how certain that assessment
is. A rare failed exposure could matter more than a frequent help lookup.

**Investigate the context in which the result is used**

The unit of observation should be a complete job: source preparation, running
`dither`, evaluating the result, handing it to another tool or device, and
returning to it later. The following are questions for research, not claims
about users' environments.

| Context to establish | What to observe or ask | How it could change the recommendation |
|---|---|---|
| Workstation and downstream equipment | Where are files prepared, processed, viewed and exposed? Which applications and devices interpret them? | A validated handoff recipe may matter more than a more detailed parameter explanation. |
| Display, lighting and viewing distance | At what zoom, physical size and lighting does the user judge grain, seams and tone? Does the preview resemble the final medium? | Comparisons may need physical-scale samples and output-device checks, rather than screen thumbnails alone. |
| Input and access needs | Does the person type commands, reuse scripts, use assistive technology, or switch between applications? What terminology do they understand? | Improve the actual interaction path; a novice tutorial may not help a script-driven workflow. |
| Compute, storage and connectivity | Is processing local or remote? Are sample files and documentation available on that machine? How long do representative jobs take? | Offline guidance, portable assets or clearer transfer steps may precede new CLI features. |
| Attention and physical environment | Can the user watch a terminal? Are they interrupted, moving between equipment, or working where lighting or noise affects feedback? | Persistent completion and recovery information may be more useful than transient progress; sound should not be assumed useful. |
| Time pressure and cost of failure | Is this exploration, a repeat production job or a deadline? What does a failed attempt cost in time, paper or other materials? | Preflight checks or repeatable job records may outrank speed or onboarding polish. |

Do not build mobile, remote, offline or darkroom-specific features merely because
these contexts are possible. Establish which occur. Test accessibility in the
relevant interaction paths without treating the absence of reported access needs
as proof that those paths are accessible.

**What is already working**

[UX.md](UX.md) identifies the right central concern: confidence in results whose
errors may be invisible. Preserve the following documented strengths:

- Explicit choices are honored or rejected with an explanation; reconciliation
  changes defaults instead of silently ignoring options.
- `--explain` exposes values and their origins. Expensive work has progress
  messages, cache reuse is announced, and short usage is separate from full help.
- Reproducibility is treated as a product requirement. Machine-dependent budget
  allocation remains opt-in.
- Quality claims include competing measurements and known limitations.
- Experimental controls remain accessible through `--set` without expanding the
  everyday option list.

These are implemented improvements according to the documents, not new work to
put back on the backlog. Similarly, the rejections in
[improvements.txt](improvements.txt) and [improvements2.txt](improvements2.txt)
are useful constraints: structural tidiness alone does not justify changing
output or scattering cohesive code.

**Priority and scope**

The order below separates work justified by the document review from research
and conditional design work. “Investigate first” is a research priority, not
authorization to implement the suggested interface. “Conditional” items should
move up if observed consequences warrant it. Effort is relative: small is editorial work, medium
involves a workflow or CLI change, and large crosses output, persistence and
compatibility behavior. These are planning estimates, not implementation quotes.

| Finding | Priority | Initial effort | User outcome |
|---|---|---|---|
| 9. Establish actual needs and working contexts | Start now | Small initial study | Select a problem worth solving |
| 1. Conflicting accounts of current behavior | Correct now | Small | Know which guidance to trust |
| 3. Preview, frame and device contracts need prominence | Investigate first | Medium | Use the correct artifact downstream |
| 5. Quality verification needs a user workflow | Investigate first | Medium | Judge success in the intended medium |
| 2. Entry points follow documents rather than tasks | Conditional on entry-point friction | Small–medium | Reach a first useful result |
| 4. Automatic choices need an outcome-focused explanation | Conditional on decision errors | Medium | Predict what the command will do |
| 6. Recovery and interruption contracts are incomplete | Conditional on failure cost and interruptions | Medium–large | Recover without losing completed work |
| 7. Cache and reproducibility guidance needs boundaries | Conditional on repeat-job needs | Small initially | Repeat and share a result reliably |
| 8. Terminal accessibility needs explicit evaluation | Include in research and each prototype | Small–medium | Complete the task using the person's actual setup |

For findings 2–8, the recommendations are prototypes to consider if the
underlying problem is supported. Their acceptance criteria describe a proposed
evaluation, not a proven need. A solution that improves terminal comprehension
but leaves the final exposure or print unusable has not solved the task.

**1. Establish one trustworthy account of current behavior**

The maintained documents contain contradictions that affect decisions, not just
wording:

| Evidence | Why it matters | Recommended correction |
|---|---|---|
| [DESIGN.md](DESIGN.md), “Choosing the markov temperature,” describes calibration against a mutual-information ratio of 0.65. [DEFAULTS.md](DEFAULTS.md), “The criterion, and one that does not work,” says that criterion failed and was replaced by a sample-relative clumping measure. [GLOSSARY.md](GLOSSARY.md), “mutual information” and “temperature,” repeat the older account. | Users cannot know what calibration improves or how to judge it. | Verify the implementation, state the current objective once, and label the rejected objective as history everywhere else. |
| [GLOSSARY.md](GLOSSARY.md), “minimum error boundary cut,” says it is not shipped. [DESIGN.md](DESIGN.md) §7.1 identifies it as shipped; the glossary's own “periodic plus smooth decomposition” entry also says it replaced the old method. | A user may avoid a usable generator or troubleshoot the wrong algorithm. | Align feature status and link historical alternatives to the current decision. |
| [DEFAULTS.md](DEFAULTS.md), the `--budget 30s` section, says “Default `30s`”; its opening, parameter table and reproducibility section say opt-in/unset. | Users may expect a time limit or adaptive allocation that is not active. | Separate the original proposal from the current budget contract, including what work the estimate covers. |
| [DEFAULTS.md](DEFAULTS.md), “Temperature is two-tier,” says fresh and cached calibration produce the same mask; “Open questions” says enabling calibration would make first and second runs differ. | It undermines the central reproducibility promise. | Resolve the obsolete question and distinguish calibration caching from machine-speed caching. |
| [DEFAULTS.md](DEFAULTS.md) says the current `markov.iters` is 3 in its parameter table, 12 in its dated testing record, and “both 20” in the earlier split-parameter row. The weights table and [DESIGN.md](DESIGN.md) still mention the removed `--mask-weights` flag. | Readers can copy obsolete settings or mistake a past experiment for a recommendation. | Mark snapshots with their date/context; current reference entries should use the supported option or `--set` key. |

There are smaller navigation signs of the same drift: [doc/README.md](README.md)
calls its five maintained entries “The four documents” and calls the root
measurement record the fifth. It omits `UXPrinciples.txt` from its inventory.
[ARCHITECTURE.md](ARCHITECTURE.md) mixes four- and five-generator descriptions.
These lower-impact errors reinforce doubt created by the substantive ones.

Acceptance: every contradiction above is reconciled against the implementation;
current guidance and historical records are visibly distinct. Add a lightweight
review check for public option names and generator coverage. Generate factual
lists from the existing registry where practical, while retaining authored
explanations of trade-offs, as architecture suggestion S9 anticipates.

**2. Give each intended task a short, complete starting path**

[uses.txt](uses.txt) and [DESIGN.md](DESIGN.md) §1 specify four intended jobs. However,
[doc/README.md](README.md) tells newcomers to read a glossary, design rationale,
architecture, UX principles and defaults in sequence. That is a useful maintainer
library, but a demanding introduction for someone preparing an exposure.
The [root README](../README.md) already supplies commands and examples; improve
their discoverability instead of creating a second competing reference.

Observe where people actually start: the README, shell history, an existing
script, another person's command or the documentation index. If entry-point
friction is demonstrated, make that entry point lead to the relevant task.
One candidate is to make the documentation index offer “Use the tool,” “Understand a result,” and
“Develop the tool” routes. Put task recipes before the full reference:

| Task | Decisions the recipe should answer | Completion evidence |
|---|---|---|
| Expose a monochrome enlarger | Device levels, spacing, number of frames and how the receiving program consumes them | Correct frame sequence and a clear handoff description |
| Display a 16-bit source over multiple 8-bit frames | Preserve input depth; explain why the averaged preview loses information | Saved frames, not merely an attractive preview |
| Produce a 1-bit photogravure mask | Two-level final artifact, physical output scale and pattern limits | Check final values and inspect a representative physical-scale sample |
| Add film-like grain | Suitable sample, generator trade-offs and grain scale | Compare texture, tone and repetition at the intended viewing size |

Each recipe should include a tracked sample, a copyable command using an explicit
built executable path, expected output names, one check, and the next useful
adjustment. Explain the distinction between bit depth and level count: a 3-bit
device has 8 levels, whereas `--levels 3` requests three tones. Keep algorithm
names stable; add plain-language descriptions beside them.

[ARCHITECTURE.md](ARCHITECTURE.md) P6 reports a stale root executable and an
untracked test sample. Treat these as reported setup risks requiring verification,
not newly reproduced bugs. Validate the introductory path from a clean checkout
without private aliases or untracked assets.

Acceptance: a newcomer can choose a recipe, produce its expected files and name
the next check without reading architecture or parameter internals.

**3. Make output roles explicit before and after a run**

[DESIGN.md](DESIGN.md) §§1, 3 and 5 distinguish exposure frames from their
averaged preview, 8-bit encoding from device levels, and supersampled grids from
final pixels. The root README already warns that the average is a preview.
Observe an actual handoff before deciding where extra feedback belongs. If the
operator never reads the terminal at that stage, filenames, a job record or the
receiving application's instructions may be the more effective intervention.

Extend the existing run summary and completion message to identify output roles:
preview path, frame paths/count, dimensions, encoding and intended level spacing.
For multiple frames without `--save-frames`, say that individual exposure frames
are not being saved and show how to request them. Preserve valid preview-only
workflows; do not add a mandatory confirmation.

For a 1-bit recipe, explain that an 8-bit PNG can be restricted to black and white,
but averaging frames or supersample blocks can introduce intermediate values.
The pre-averaging level count alone does not establish that the final artifact
is a usable two-level etching mask. Document the receiving device's expected
mapping rather than implying an ordinary image viewer verifies it.

Acceptance: readers can identify which file to expose, which file to inspect,
and whether the final artifact meets their medium's requirements. Test this with
multi-frame, supersampled and device-spacing examples.

**4. Explain the effective workflow, including what is not known yet**

[UX.md](UX.md) promises quick validation and resolved values. However,
[ARCHITECTURE.md](ARCHITECTURE.md) §4 places `--explain` before calibration and
mask generation. [DEFAULTS.md](DEFAULTS.md) also documents nominal dimensions
when no image is supplied and sample-dependent adjustments at the call site.
The distinction between a resolved registry value and the eventual effective
value needs to be visible.

Retain the detailed table, but lead with a short summary of threshold, diffusion,
generator, sample, mask dimensions and output intent. Label nominal dimensions,
pending calibration and sample-dependent clamps as provisional. After execution,
report any effective values that changed. Keep `--explain` fast; do not make it
build a mask merely to strengthen its promise.

The documented `--spatial` default also depends on whether a threshold was
explicitly named ([DESIGN.md](DESIGN.md) §4). Thus spelling out a default-looking
threshold can change another setting. Show that consequence prominently in help
and explain output, and pin diffusion explicitly in comparison recipes. Do not
silently change the default rule without evaluating compatibility.

Use the user's flag in recovery text when available. The existing example
“mask.scale … which mask.in rules out” requires translation back to the command.
A proposed alternative is:

```text
Cannot combine --mask-scale 2 with --mask-in m.png.
--mask-in loads a finished mask; --mask-scale applies when building one.
Remove --mask-scale to reuse m.png, or remove --mask-in and supply a sample.
```

This is proposed wording, not captured program output.

Acceptance: users can predict implied choices and identify provisional values;
on a conflict they can choose a valid correction without knowing registry names.

**5. Turn quality measurements into a practical verification journey**

[DESIGN.md](DESIGN.md) §9 provides strong checks but distributes them among
developer tools. Section 7.4 explicitly says seam visibility has no validated
measure. A numerical pass cannot therefore be presented as proof that a mask is
visually seamless or suitable for a particular print process.

Provide a short “Check your result” guide with distinct questions: was tone
preserved, does texture resemble the sample, does repetition show, and is the
artifact suitable for the target device? Link the relevant existing checks,
state their input requirements and explain what a pass does and does not mean.
Keep diagnostic preprocessing consistent with the run, especially high-pass
settings, as [DEFAULTS.md](DEFAULTS.md), “Cross-tool consistency,” already notes.

Use a small gallery made from tracked samples and actual program outputs:
identical crops, densities, dimensions and viewing scales, with settings and
captions. Include a tiled view and examples of worming, tone bias and seam
artifacts. Pair images with text descriptions and measurements; avoid a single
“best generator” score. The method comparison in [DESIGN.md](DESIGN.md) §6 is
the right foundation, but its measurements are sample-specific evidence.

Acceptance: a user can select an appropriate check, interpret its limitation and
choose a next action. In particular, no automated result is labeled a guarantee
of invisible seams or physical print quality.

**6. Specify interruption, file conflicts and recovery as part of the UX**

[UX.md](UX.md) covers progress and refusal well. The reviewed documents do not
establish a complete contract for cancellation during a long build, partial
frame sequences, output collisions, unwritable paths or disk exhaustion.
[UXPrinciples.txt](UXPrinciples.txt) §§6–7 explicitly calls for these states.

First audit existing behavior. Then document and fill gaps: identify the current
phase, support cancellation, distinguish completed files from partial work, and
report a useful nonzero exit status on failure. Check destination problems before
expensive work where practical. Define replacement behavior and handle each file
atomically where appropriate; a multi-file run must still report partial
completion honestly. Avoid routine interactive prompts that break batch use.

Acceptance: interruption or a failed write leaves an understandable account of
what was saved and what to rerun. Existing valid output is protected according to
an explicit replacement policy. Test long generation and multi-frame writing
separately; they have different recovery needs.

**7. Describe persistence and reproducibility in user terms**

[DEFAULTS.md](DEFAULTS.md) describes built-mask caching, calibration results and
machine throughput. These are three different kinds of saved state. Users need
to know which only saves time and which can affect a budgeted result.

Document their locations, the scope of `--config` and `--no-config`, and the
consequences of clearing each cache. Keep cache-hit reporting. Scope the
same-command/same-bytes promise to the relevant inputs, settings, program version
and environment; the executable identity in the mask cache already acknowledges
that a rebuild matters. A saved mask preserves the arrangement, but recreating
the final image also needs the source and pipeline settings.

Start with an archival recipe using existing mask export and a saved command
transcript. If user studies show repeated difficulty sharing results, consider a
versioned run manifest containing effective settings, input/mask identities and
program version. `--explain --json` and `--params` are deferred proposals in
DEFAULTS.md, not existing features. A manifest would need post-calibration values
and a defined replay contract before it could promise reproducibility.

Acceptance: a user can explain whether deleting cached state changes only run
time or may change a budget allocation, and can repeat a saved workflow within
the documented reproducibility boundary.

**8. Treat terminal accessibility as more than avoiding color**

[UX.md](UX.md) preserves plain-text messages, but its discussion of which general
principles transfer narrows accessibility too far. Narrow terminals, screen
readers, redirected logs, long paths and unfamiliar technical language all affect
this tool. This is an evaluation gap, not an observed accessibility failure.

Check usage, explain tables, progress and errors with an 80-column terminal,
redirected output and a screen reader. Keep complete text labels and predictable
reading order; avoid progress redraws that flood redirected logs. Explain the
stdout/stderr contract for automation. Give essential warnings concise wording
and link to detail. Turn glossary cross-references into navigable links and fix
its claimed alphabetical order, which currently places entries such as “mask
cache” and “tie share” outside their expected positions.

Acceptance: essential information remains understandable without color, visual
column alignment or animated progress. Long file paths remain identifiable and
the primary task does not require memorizing internal parameter names.

**9. Measure task success alongside image correctness**

The project has extensive algorithmic evidence, but these documents provide no
observed user-task results. [UXPrinciples.txt](UXPrinciples.txt) §§1 and 11 call
for that evidence. [todo.txt](todo.txt), [bugs.txt](bugs.txt) and
[markov.txt](markov.txt) reveal author intent and development history; they do not
establish how a newcomer understands the product.

Start with discovery, before showing proposed recipes or messages. Recruit a
small initial round of roughly four to six actual or likely users through the
maintainers and relevant practitioners. Include the author if they perform the
work, repeat users, and someone who attempted the task but abandoned the tool if
available. Seek different levels of CLI and imaging experience and relevant
access needs. Cover the intended uses enough to identify which merits a focused
follow-up; this small round cannot establish prevalence across all four.

Ask participants to show a recent job on their own setup, using their files
where they are comfortable doing so. Useful prompts are: “What were you trying
to achieve?”, “Show me how you did it last time”, “Where did you have to retry
or use another tool?”, and “How did you decide the result was usable?” Ask what
happened before and after `dither`, what a failed attempt cost, and what they
would use without it. Avoid leading with “Would clearer help be useful?”

Observe normal work before introducing artificial errors. With permission,
inspect commands, scripts, output artifacts and available support reports for
recurring workarounds. If physical production cannot be observed, review an
existing result and its production record; mark the remaining context as
unverified. Do not require a costly new print merely to conduct an interview.

For each candidate problem, record the participant's goal, observed obstacle,
environment, workaround, consequence and evidence source. Keep direct observation,
self-report and reviewer inference distinct. Use a problem statement of the form:
“When [context], [user] needs to [outcome], but [observed obstacle] causes
[consequence].” It should not name a preferred interface solution.

Choose the first intervention only after this review. For example, wrong-file
handoffs would support testing output labels; correctly selected files that fail
on the target device would redirect work toward the device workflow. If users
already navigate reliably through scripts, defer a broad onboarding redesign.
Record what evidence would make the team drop or revise each proposal.

Then test the smallest plausible change on representative tasks and equipment.
Compare it with the existing workflow, using comparable inputs. Evaluate first
use only where first use was a demonstrated problem, and repeat work where
repeatability was the issue. Include interruption, constrained displays or
assistive technology when those affect the task. Agree on success with the
participant before the test: an accepted exposure, a physically usable mask, or
an image that meets their texture and tone requirements.

Record unassisted completion through the downstream handoff, time to an accepted
result, retries, wrong-artifact choices, recovery effort and material waste where
applicable. Also record whether users can explain the limits of their checks.
Use consented observation and existing evidence rather than adding telemetry by
default. Establish a baseline before setting numerical targets; a small study
reveals friction but cannot estimate population-wide success rates. The maintainer
responsible for the chosen workflow should review the evidence and update the
priority table after each round.

**Recommended sequence**

Begin contextual observation and interviews while correcting verified
documentation contradictions. Use the evidence to select one consequential user
problem and define success in the user's output environment. Prototype the
smallest change that addresses that obstacle, then test the complete workflow
with affected users. Expand only when the result improves their task; revise or
drop proposals whose underlying assumptions are not supported. Keep the numbered
findings as a set of candidates, rather than treating them as a committed feature
roadmap.
