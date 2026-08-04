# dither

Command-line image dithering. All quantization happens in linear light, with the
sRGB curve applied only at the encode and decode boundaries, so the dot density
of the output averages back to the tone of the input. Supports multi-level
output, blue-noise masks, and averaging over several dithered frames.

## Synopsis

```
dither <image> [options]
```

The defaults — round to nearest, full Floyd-Steinberg, two levels — need no
options:

```sh
dither photo.jpg --out out.png
```

## How a dither is specified

A dither is two independent choices: how each pixel picks a level
(`--threshold`), and how much of the leftover error is pushed onto its
neighbours (`--spatial`).

`round`
:   Deterministic round-to-nearest level. The default.

`random`
:   Stochastic threshold. Fast and grainy on its own; mixed with diffusion it
    breaks up Floyd-Steinberg's repeating patterns.

`blue`
:   Void-and-cluster blue-noise mask. Best perceptual quality, especially at low
    level counts and for print.

`--spatial` scales Floyd-Steinberg error diffusion from `1` (full) down to `0`
(none). Naming a `--threshold` drops it to `0`, on the grounds that a threshold
you picked deliberately is usually one you want to see on its own. Pass
`--spatial` explicitly, on either side of `--threshold`, to combine the two:
`--threshold blue --spatial 0.5` is blue noise with half-strength diffusion.

## Options

`--threshold <name>`
:   How each pixel picks its level: `round`, `random`, or `blue`. Default
    `round`.

`--spatial <frac>`
:   Fraction of each pixel's quantization residual diffused to its neighbours.
    `1` is full Floyd-Steinberg, `0` none, in between damps the diffusion and
    softens FS worming. Defaults to `1`, or to `0` when `--threshold` is given;
    an explicit value always wins.

`--out <path>`
:   Output file path. Default `dither.png`.

`--levels <n>`
:   Number of output tones, 2 to 256. Default `2` (black and white). More levels
    reduce visible dithering but need a display or printer that can reproduce
    the intermediate tones.

`--level-spacing <mode>`
:   Where those tones sit. `srgb` (default) spaces them evenly in pixel value —
    3 levels are 0/128/255 — putting the middle level at perceptual half and
    keeping shadow noise down. `linear` spaces them evenly in linear light, so 3
    levels are 0/188/255. Dithering stays density-correct either way. No effect
    at `--levels 2` or with `--linear`.

`--frames <n>`
:   Dither *n* independent frames and average them. Smooths noise and gives
    finer gradations at the cost of crisp dot edges. Error withheld by
    `--spatial` is not lost across frames: it stays in the pixel's budget and is
    paid back later, so the average still converges on the target tone.

`--save-frames`
:   Also write each frame beside the average, as `<stem>-N.png`.

`--linear`
:   Treat pixel values as raw linear light, skipping the sRGB conversion that
    normally wraps the dither.

`--seed <n>`
:   Random seed, for reproducible masks and stochastic thresholds. Default `42`.

`--debug-pixel <r,c>`
:   Print the per-frame `ideal`/`display`/`displayed` trace for one pixel to
    stderr.

`--help`, `-h`
:   Show the built-in option summary.

## Blue-noise mask options

`--mask-size <WxH>`
:   Build a WxH tile and repeat it over the image. Defaults to the image size
    capped at 256×256, because build time grows with area: an uncapped 1200×1600
    mask takes about a minute against a second for the 256×256 tile. Larger
    sizes are honoured when asked for explicitly. Keep the tile at least 4× sigma
    to avoid visible repetition.

`--mask-sigma <val>`
:   Kernel radius in pixels — how far dots repel each other. Default `1.8`; try
    `1.0`–`4.0` depending on output resolution.

`--mask-in <path>`
:   Use this image as the mask instead of building one. Read as raw grayscale
    and tiled like a generated mask, so a saved mask can be reused without
    paying to rebuild it. The mask-building options are then ignored.

`--mask-out <path>`
:   Write the mask in use to this path, for inspection or for feeding back in
    through `--mask-in`.

`--mask-eps <val>`
:   Gaussian kernel weight cutoff. Default `1e-4`; reducing it improves quality
    slightly at the cost of speed.

`--mask-radius <mul>`
:   Kernel half-width in sigma units. Default `6.0`, which is inert — `--mask-eps`
    prunes the kernel first, at about 4.3 sigma — so this only bites below that.

## Examples

Four-level blue dither, tiling a 128×128 mask for speed:

```sh
dither photo.jpg --threshold blue --levels 4 --mask-size 128x128 --out out.png
```

Smooth averaged result from 30 frames:

```sh
dither photo.jpg --frames 30 --out out.png
```

Blue noise with half-strength error diffusion, keeping the mask:

```sh
dither photo.jpg --threshold blue --spatial 0.5 --mask-out mask.png --out out.png
```

## Build

Requires OpenCV 4.x and a C++17 compiler. System packages are listed in
`apt-packages.txt`:

```sh
sudo apt install -y $(grep -v '^\s*#' apt-packages.txt)   # Ubuntu 22.04 / 24.04
cmake -B build
cmake --build build
```

On macOS, `xcode-select --install` then
`brew install cmake opencv imagemagick gnuplot pandoc` covers the same ground.
For a debug build, configure with `-DCMAKE_BUILD_TYPE=Debug`; the default is
Release. `./build.sh` configures, builds, and displays this README in one step.

The verification tools additionally need NumPy and OpenCV's Python bindings.
Ubuntu 24.04 marks its system Python externally managed (PEP 668), so install
them into a virtualenv — `dither_env` is already in `.gitignore`:

```sh
python3 -m venv dither_env
dither_env/bin/pip install -r requirements.txt
```

## Tools

<<<<<<< HEAD
These sit at the repo root. The Python ones find their interpreter through
`#!/usr/bin/env python3` and shell out to each other, so put the virtualenv on
`PATH` rather than invoking `dither_env/bin/python` directly:
=======
**3. Build**

```sh
cmake -B build
cmake --build build
```

The default build type is Release (`-O3`). For a debug build:

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

`./build.sh` does the same configure-and-build in one shot, then displays this README.

**4. Check it works**

```sh
./build/dither examples/ramp_256x256.png --out /tmp/out.png
export PATH="$PWD/dither_env/bin:$PATH"   # put the venv's python3 first
./coltest                                 # expects OVERALL PASS
```

## Usage

```
dither <image> [options]
```

### Options

| Option              | Default         | Description                                                  |
|---------------------|-----------------|--------------------------------------------------------------|
| `--threshold <name>`| `round`         | How each pixel picks its level: `round`, `random`, or `blue` (see above). |
| `--spatial <frac>` | `1`, or `0` with `--threshold` | Fraction of each pixel's quantization residual diffused to its neighbours by Floyd-Steinberg. `1` = full FS, `0` = none, in between = damped diffusion, which softens FS worming. |
| `--out <path>` | `dither.png` | Output file path |
| `--linear` | off | Treat pixel values as raw linear light, skipping the sRGB ↔ linear conversion that normally wraps the dither. |
| `--levels <n>` | `2` | Number of output tones. `2` = black+white. Higher levels reduce visible dithering but require a display or printer that can reproduce intermediate tones. |
| `--level-spacing <mode>` | `srgb` | Where those tones sit. `srgb` (the default) spaces them evenly in pixel value, so 3 levels are 0/128/255: the middle level lands at perceptual half, and shadows are dithered against a nearer neighbour instead of against white. `linear` spaces them evenly in linear light — 3 levels are 0/188/255 — which is the physically even ladder but pushes the middle tone up to light grey and makes the dark end noisier. |
| `--frames <n>` | `1` | Dither *n* independent frames and average them. For `blue`, each frame uses a random shift of the same mask. |
| `--save-frames` | off | Also write each frame alongside the average, as `<stem>-N.png`. |
| `--seed <n>` | `42` | Random seed, for reproducible masks and stochastic thresholds. |
| `--debug-pixel <r,c>` | off | Print the per-frame `ideal`/`display`/`displayed` trace for one pixel to stderr. |

With multiple frames, the withheld part of `--spatial` isn't lost: it stays in the
pixel's budget and is paid back by a later frame, so the average still converges on
the target tone.

#### blue-only options

| Option | Default | Description                    |
|--------|---------|-------------------------------|
| `--mask-sigma <val>` | `1.8` | Blue-noise kernel radius in pixels. Controls how far dots repel each other. Larger values spread dots further apart — try `1.0`–`4.0` depending on output resolution. |
| `--mask-size <WxH>` | image size, capped at 256×256 | Generate a WxH tile and repeat it over the image.  |
| `--mask-in <path>` | off | Use this image as the threshold mask instead of building one. Any 8-bit image works; it is read as raw grayscale (no sRGB decode) |
| `--mask-out <path>` | off | Write the mask in use to this path, for inspection or for feeding back in via `--mask-in`. |
| `--mask-eps <val>` | `1e-4` | Gaussian kernel weight cutoff. Reduce to improve quality slightly at the cost of speed. |
| `--mask-radius <mul>` | `6.0` | Kernel half-width in sigma units. Rarely needs changing unless sigma is very large. |

## Examples

Binary dither with Floyd-Steinberg (default):
```sh
dither photo.jpg --out out.png
```

Four-level blue dither, tiling a 128×128 mask for speed:
```sh
dither photo.jpg --threshold blue --spatial 0 --levels 4 --mask-size 128x128 --out out.png
```

Smooth averaged result using 30 frames:
```sh
dither photo.jpg --frames 30 --out out.png
```

High-quality blue dither with a tighter kernel, keeping the mask for inspection:
```sh
dither photo.jpg --threshold blue --spatial 0 --mask-sigma 2.5 --mask-out mask.png --out out.png
```

Blue noise with half-strength error diffusion:
```sh
dither photo.jpg --threshold blue --spatial 0.5 --out out.png
```

## Verification and analysis tools

These live at the repo root and are what the setup steps above install dependencies
for. The Python ones resolve their interpreter through `#!/usr/bin/env python3` and
shell out to each other, so put the virtualenv on `PATH` first rather than invoking
`dither_env/bin/python` directly:
>>>>>>> refs/remotes/origin/master

```sh
export PATH="$PWD/dither_env/bin:$PATH"
```

Without it `coltest` reports `COLSTATS PARSE FAIL` on every row, which means
`cv2` is missing, not that the dither is broken.

<<<<<<< HEAD
`./coltest`
:   Runs a matrix of `dither` invocations against `examples/ramp_256x256.png`
    and checks each output for density preservation.
=======
| Tool | Needs | What it does                      |
|------|-------|-----------------------------------|
| `coltest` | venv | Runs a matrix of `dither` invocations against `examples/ramp_256x256.png` and checks each output for density preservation. `./coltest` |
| `colstats` | venv | Per-column density check of one image against a reference: column means in linear light, z-scored against counting noise. `./colstats out.png` |
| `budgettest` | venv | Verifies the multi-frame invariant — with `--spatial 0`, the levels displayed for a pixel sum exactly to its budget. |
| `colplot` | gnuplot | Plots `colstats --csv` output as sixels. `gnuplot colplot` |
| `aliases` | imagemagick, gnuplot, pandoc, venv | Shell helpers, loaded with `source aliases`. `analyse out.png` gives a one-shot look at a result — the image, a scaled difference against the reference, and the colstats plot side by side; `test_dither <args>` dithers the ramp and analyses it in one step, and `run_suite` / `gallery` sweep that over the thresholds. `icat`, `rampdiff`, `isquash`, `iaverage`, `histo`, `icrop`, and `dim` inspect images directly. |
>>>>>>> refs/remotes/origin/master

`./colstats <image>`
:   Per-column density check against a reference: column means in linear light,
    z-scored against counting noise.

`./budgettest`
:   Verifies the multi-frame invariant — with `--spatial 0`, the levels
    displayed for a pixel sum exactly to its budget.

`gnuplot colplot`
:   Plots `colstats --csv` output as sixels.

`source aliases`
:   Shell helpers. `analyse out.png` shows the image, a scaled difference
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

## License

GPL-3.0-or-later — see [LICENSE](LICENSE).

This program is free software: you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later
version. It is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
PARTICULAR PURPOSE. See the GNU General Public License for more details.
