# Film samples

The film scans here are the shipped grain samples. `examples/films.tsv` lists
them, one row per scan, grouped into films, and `install_samples` (in
`../aliases`) does two things with it:

- copies every scan **whole** into `~/.config/dither/samples/<film>/`, so that
  `--mask-sample Kodak-4X` names all five 4X scans and dft, markov and inpaint
  pool them. It never overwrites, since the library is the user's own.
- cuts **one crop per film**, the window recorded on that film's one windowed
  row, into `~/.config/dither/crops/<film>.png`, for `--mask-method direct`
  and `dft`. `direct` takes a tile of its sample rather than fitting a model, so
  the tile must come from a window that holds grain and nothing else, and a
  whole scan's middle is wherever the photograph put its subject. `dft` imposes
  its sample's whole spectrum, so a scan's straight edges would come back as
  streaks. These are derived, so a
  changed window replaces the crop and keeps the old one as `<film>.png~`.

A film without a crop still works: `direct` then cuts from the middle of its
largest scan, and `dft` pools the scans, and each says so. To give your own film one, save a square crop
of clean grain as `~/.config/dither/crops/<film>.png`.

`test/findcrops` chose the windows and checks them; its docstring is the primary
source. There are two searches.

**drift** chose the original per-scan crops. In priority order:
- a **tone gate**: mean strictly inside (35, 215). Both quantities below are
  *minimised*, and a blown or crushed corner scores perfectly on both, so
  without this five of the twelve run straight to one — tone 246, 235, 231, 16
  and 6.
- a **texture gate**: **grain** — the standard deviation of a sigma-3 high-pass
  — at least 0.70 of the frame's own median. The same hazard caught a second
  way, for a frame with a mid-tone region that has no texture in it. It never
  binds on any of the twelve, and is recorded rather than removed because it is
  what ran.
- **defects**: the share of the window whose sigma-12 gradient is in the
  frame's top 3% — scratches, frame edges, the border of a cloned region —
  weighted a thousandfold, so it is a sort key rather than a term.
- **drift**: the standard deviation of the frame blurred at sigma 12, which is
  what is left when grain is removed, so it measures how much of the window is
  *not* texture. The tiebreak.

Eight of those twelve are therefore not their frame's drift minimum, which is
why the coordinates look wrong if you check them against the drift column
alone.

**equalise** is the search for what `--mask-method direct` does to a crop now.
`--direct-equalise` ranks each pixel within a 24 px neighbourhood, which removes
smooth tone outright, so drift no longer reaches the mask. What does is picture
content sharper than that neighbourhood — door frames, the outline of a
cloned-out subject — and the **residual** column measures it: how far the
equalised crop's level sets stray from uniform, ×1000. It replaces the tone gate
with a **blown** gate, at most 5% of the window in tie groups that hold over half
their neighbourhood, because equalising turns those into white noise rather than
grain. Residual has a floor set by each film's grain, so compare it within a
frame and not across films.

## The crops

Each film's window was chosen at σ 24 from all of its scans: equalise was run on
every scan, and each film's candidates judged through the binary — `grainfit`
`raw` at block 48 for a 256 mask against its own window (lower is closer), and
the tiled `--supersample 16` mask's level sets at g = 0.15, 0.5 and 0.85,
blurred, by eye.

| film | candidates, grainfit | chosen, and what is left |
|---|---|---|
| Ilford-Delta3200 | 832 at σ 12's window 2.25, 832 at σ 24's 2.08, **768 2.56** | the 832 windows carry door-frame arcs or a horizontal line; 768 has one faint line |
| Kodak-2484 | Kodak-2484-1 1.23, Mamiya 1024 1.11; Mamiya 2048, equalise's five best: 1.19, 1.34, 0.95, 1.54, **1.24** | the fifth, the plain wall, and clean: every better-ranked 2048 carries a diagonal line, an arc or a blotch from the furniture below. 2048 lets `direct` cut a larger tile: `dither examples/Kodak-4X-1.png --mask-method direct --mask-sample Kodak-2484 --supersample 4 --explain` derives 2036, where a 1024 crop caps it at 1012 |
| Kodak-4X | **AutoRex 0.67**, Kodak-4X-1 1.29 | clean |
| Kodak-OldTriX | **274a 0.89**, 274a's old window 1.37, 278a 1.18 | clean; 278a carries curved marks, 273a was not a candidate at tone 234 |
| Kodak-PanatomicX | **drift window 2.85**, equalise's 3.73 | dust specks; equalise's window sits at tone 17 with a vertical line |
| Kodak-TriX | **032a 1.76**, 032a's σ 12 window 2.30, 017 1.82 | one faint diagonal; the σ 12 window grows vertical bands at σ 24 and 017 a grid of lines |

**Most of these are the best available rather than good.** Every X570 frame,
both EOS3 frames and Mamiya-2484-154 are photographs whose in-focus subject was
cloned out, leaving defocused areas; none of them contains a region that is only
grain, and the residual column says how much structure the crop still carries.
AutoRex is a flat field.

`test/findcrops` reports the table below and checks it against `films.tsv`;
`test/findcrops --write <scan>` records a new window. Residual ranks windows
well and separates near-ties badly, so `--top K` lists the K best and
`--pick N --write` records the Nth as `equalise#N`, which a bare run still
re-derives. It does not write this
file — the prose is by hand and the table is pasted. The coordinates are
offsets into the named scan.

| film | scan | search | at | side | tone | grain | drift | residual | blown |
|---|---|---|---|---:|---:|---:|---:|---:|---:|
| Ilford-Delta3200 | `EOS3-Delta3200-088b.jpg` | equalise | +3968+832 | 768 | 50 | 6.1 | 6.0 | 13.66 | 0.0% |
| Kodak-2484 | `Mamiya-2484-154.jpg` | equalise#5 | +1600+992 | 2048 | 66 | 15.8 | 3.2 | 18.16 | 0.0% |
| Kodak-4X | `AutoRex-4X-449c.jpg` | equalise | +32+32 | 1024 | 52 | 58.6 | 3.7 | 7.24 | 0.0% |
| Kodak-OldTriX | `X570-TriX80-274a.jpg` | equalise | +4928+1888 | 1024 | 211 | 16.9 | 5.9 | 32.23 | 0.0% |
| Kodak-PanatomicX | `EOS3-PanatomicX-134a.jpg` | drift | +1504+1856 | 1024 | 35 | 3.7 | 6.5 | 11.82 | 0.0% |
| Kodak-TriX | `X570-Tri-X-032a.jpg` | equalise | +4384+32 | 1024 | 235 | 9.3 | 7.2 | 7.29 | 0.0% |

## The earlier per-scan crops

Until November 2026 there was a crop per scan, checked in under
`examples/crops/` and installed into the library in place of the scans, chosen
at σ 12, which is also what their residual column measures. They are gone,
but the measurements below were taken on them, so their windows are recorded
here; `examples/Kodak-4X-1-crop.png` is the Kodak-4X-1 one,
kept as the test suite's fixture.

| crop | from | search | at | side | tone | grain | drift | residual | blown |
|---|---|---|---|---:|---:|---:|---:|---:|---:|
| `AutoRex-4X-449c.png` | `AutoRex-4X-449c.jpg` | drift | +96+224 | 1024 | 52 | 58.0 | 3.5 | 4.85 | 0.0% |
| `DemiEE17-4X-316a.png` | `DemiEE17-4X-316a.jpg` | equalise | +800+2080 | 768 | 29 | 31.9 | 3.4 | 6.37 | 0.0% |
| `DemiEE17-4X-319d.png` | `DemiEE17-4X-319d.jpg` | drift | +0+320 | 1024 | 152 | 21.8 | 8.3 | 8.54 | 0.0% |
| `EOS3-Delta3200-088b.png` | `EOS3-Delta3200-088b.jpg` | equalise | +3552+800 | 832 | 55 | 6.3 | 7.7 | 8.41 | 0.0% |
| `EOS3-PanatomicX-134a.png` | `EOS3-PanatomicX-134a.jpg` | drift | +1504+1856 | 1024 | 35 | 3.7 | 6.5 | 7.02 | 0.0% |
| `Kodak-2484-1.png` | `Kodak-2484-1.png` | equalise | +0+1344 | 896 | 35 | 23.6 | 9.9 | 9.01 | 4.6% |
| `Kodak-4X-1.png` | `Kodak-4X-1.png` | equalise | +32+32 | 1024 | 51 | 59.1 | 3.7 | 4.84 | 0.0% |
| `Kodak-4X-2.png` | `Kodak-4X-2.png` | drift | +192+32 | 896 | 144 | 21.6 | 7.2 | 8.17 | 0.0% |
| `Mamiya-2484-154.png` | `Mamiya-2484-154.jpg` | equalise | +2208+5888 | 1024 | 44 | 18.1 | 6.4 | 9.57 | 0.0% |
| `X570-Tri-X-017.png` | `X570-Tri-X-017.jpg` | equalise | +3360+768 | 1024 | 230 | 6.1 | 15.5 | 6.65 | 0.0% |
| `X570-Tri-X-032a.png` | `X570-Tri-X-032a.jpg` | equalise | +2592+32 | 1024 | 209 | 12.4 | 26.1 | 4.53 | 0.0% |
| `X570-TriX80-273a.png` | `X570-TriX80-273a.jpg` | drift | +512+1280 | 1024 | 215 | 5.5 | 7.5 | 26.12 | 0.0% |
| `X570-TriX80-274a.png` | `X570-TriX80-274a.jpg` | drift | +4192+1568 | 1024 | 207 | 17.1 | 4.1 | 20.75 | 0.0% |
| `X570-TriX80-278a.png` | `X570-TriX80-278a.jpg` | equalise | +3744+1792 | 1024 | 151 | 17.7 | 14.5 | 16.50 | 0.0% |

## Best settings per crop

Measured with `maketexture --size 256x256 --highpass 32` on each crop's window
— inpaint at four sweep counts and two radii, dft at two smoothings, markov at
four temperatures, three seeds each — and scored by `test/patchfit` **against
the full frame** rather than against the crop. That distinction is the whole
measurement: a 1024 crop supplies only 16 real patches, which leaves patchfit's
covariance so ill-conditioned that seed noise swamps the setting and the winner
changes with the seed. Against the full frame, up to 64 patches, the ranking is
stable across seeds.

**Read the spread column first.** Where it is a few per cent the differences are
at the noise floor and the setting does not matter; only the films with a wide
spread have a winner worth the name, and those are the flat grain fields.
Settings within 1.2% of the best are listed as ties rather than ranked.

| crop | best | patchfit | spread | within noise of best |
|---|---|---:|---:|---|
| `AutoRex-4X-449c` | `markov --markov-temp 0.30` | 43.3 | 26% | — |
| `DemiEE17-4X-316a` | `markov --markov-temp 0.30` | 90.4 | 5% | `markov --markov-temp 0.20` |
| `DemiEE17-4X-319d` | `markov --markov-temp 0.20` | 104.3 | 10% | — |
| `EOS3-Delta3200-088b` | `markov --markov-temp 0.20` | 430.5 | 2% | `--inpaint-iters 4`, `markov --markov-temp 0.30`, `--inpaint-iters 3 (default)` and 6 more |
| `EOS3-PanatomicX-134a` | `markov --markov-temp 0.40` | 255.9 | 1% | `markov --markov-temp 0.30`, `--inpaint-radius 4`, `--inpaint-iters 1` and 8 more |
| `Kodak-2484-1` | `markov --markov-temp 0.50` | 54.7 | 11% | `markov --markov-temp 0.40` |
| `Kodak-4X-1` | `--inpaint-radius 4` | 50.8 | 18% | — |
| `Kodak-4X-2` | `--inpaint-iters 2` | 359.2 | 3% | `--inpaint-iters 3 (default)`, `--inpaint-iters 4`, `--inpaint-iters 1` and 4 more |
| `Mamiya-2484-154` | `markov --markov-temp 0.30` | 402.9 | 2% | `markov --markov-temp 0.40`, `--inpaint-radius 4`, `--inpaint-iters 2` and 5 more |
| `X570-Tri-X-017` | `--inpaint-iters 1` | 211.8 | 1% | `--inpaint-iters 4`, `--inpaint-iters 2`, `markov --markov-temp 0.30` and 8 more |
| `X570-Tri-X-032a` | `markov --markov-temp 0.30` | 188.0 | 2% | `markov --markov-temp 0.20`, `markov --markov-temp 0.40`, `--inpaint-iters 3 (default)` and 4 more |
| `X570-TriX80-273a` | `markov --markov-temp 0.20` | 115.2 | 6% | `dft (default)`, `dft --dft-smooth-cells 1` |
| `X570-TriX80-274a` | `--inpaint-iters 2` | 161.6 | 5% | `--inpaint-iters 1`, `markov --markov-temp 0.20`, `dft (default)` and 2 more |
| `X570-TriX80-278a` | `--inpaint-radius 4` | 178.6 | 4% | `--inpaint-iters 1`, `markov --markov-temp 0.30`, `--inpaint-iters 2` and 2 more |

### Three cautions

**The winners are only meaningful where the spread is wide.** Seed scatter, the
range over three seeds of one setting, is 0.6 absolute at the median and 2.2 at
worst — so a spread of 1–5% is noise and those films simply have no best setting
among these. The flat grain fields (AutoRex 26%, Kodak-4X-1 18%, Kodak-2484-1
11%, DemiEE17-4X-319d 10%) are the ones with a real answer, and they are the
films whose crops are clean. That is not a coincidence: a crop carrying
defocused subject is far from every synthesis, and the distance to it is
dominated by that rather than by the generator.

**Score against the frame, not the crop.** Scoring against the 1024 crop gives
patchfit only 16 real patches, which leaves its covariance ill-conditioned: the
distances inflate to 300–800, the spread between settings falls to 2%, and the
winner changes with the seed. The same textures scored against the full frame
read 43–431 with stable rankings. Anything measured against a crop this size
should be treated as unmeasured.

**The table is a measurement of one binary.** It was re-run when the crops
changed, and the four flat fields kept their winners on unchanged windows while
the figures moved — AutoRex 39.8 → 43.3 and Kodak-4X-1 46.4 → 50.8, Kodak-2484-1
52.8 → 54.7, DemiEE17-4X-319d 105.2 → 104.3 — so that movement is the binary's. A crop that changed reads differently for a second
reason — the score includes how much picture the crop carries — so a figure is
comparable within a row and not with an earlier version of it. The two dft
settings read identically on twelve crops and 0.1 apart on the 832 and 768
ones: at these sample sizes the area rule takes both to, or next to,
`dft.smooth_cells`' one-cell floor (see "Calibrating dft" in CLAUDE.md), so
that comparison is barely being made.
