// SPDX-License-Identifier: GPL-3.0-or-later
// The dither engine. Space, for_serpentine and quantize_scan are private to
// this file: nothing outside needs them, and quantize_scan in particular is the
// one place both ledgers are kept, so it is better not reachable from the CLI.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <opencv2/opencv.hpp>
#include "engine.h"

std::mt19937 rng;

void spatial_none(int /*row*/, int /*col*/, int /*dir*/,
                         cv::Mat& /*spatial_err*/, float /*value*/) { }

// Floyd-Steinberg diffusion, serpentine-aware. On the current row the "next"
// pixel we haven't visited yet is (row, col+dir); the "previous" is col-dir.
void spatial_floydsteinberg(int row, int col, int dir,
                                   cv::Mat& spatial_err, float value)
{
    const int W = spatial_err.cols, H = spatial_err.rows;
    const int next_col = col + dir;
    const int prev_col = col - dir;

    struct Tap { int r, c; float w; };
    const Tap taps[4] = {{row,     next_col, 7.0f / 16},
                         {row + 1, prev_col, 3.0f / 16},
                         {row + 1, col,      5.0f / 16},
                         {row + 1, next_col, 1.0f / 16}};
    // The two conditions differ on purpose, and this has been read as a bug
    // twice. The denominator tests the *column* only, so a tap off the left or
    // right edge is excluded from it and the residual is renormalised over the
    // taps that remain: nothing leaks sideways, which is what fixing the edge
    // leak meant. The deposit also tests the row, so on the last row the three
    // below-row taps stay in the denominator and are not paid out, and that
    // share falls off the bottom. That is the intended end of the ledger --
    // there is no next row to carry it, and renormalising instead would empty
    // the whole residual into the last row's next pixel.
    float total = 0;
    for (const Tap& t : taps)
        if (0 <= t.c && t.c < W) total += t.w;
    if (total <= 0) return;            // the very last pixel has nowhere to put it
    for (const Tap& t : taps)
        if (t.r < H && 0 <= t.c && t.c < W)
            spatial_err.at<float>(t.r, t.c) += value * t.w / total;
}

// Serpentine iteration over a `size` grid: even rows go left→right, odd rows
// right→left. `body` receives (row, col, dir) where `dir` is ±1.
template <typename Body>
static void for_serpentine(cv::Size size, Body&& body)
{
    for (int row = 0; row < size.height; row++) {
        int dir = (row & 1) ? -1 : +1;
        int col = (dir > 0) ? 0 : size.width - 1;
        for (int i = 0; i < size.width; i++, col += dir)
            body(row, col, dir);
    }
}

// The units the spatial ledger is kept in.
//
// A threshold rule has to work in *display levels*: `frac(v)` only means
// anything on a scale whose rungs sit at the integers. But what the eye, and
// vision::average_gray, and vision::downsample_gray all add up is *linear
// light*, and under `--level-spacing srgb` the two are not proportional — the
// rungs are equally spaced in pixel value, so each step spans a different
// amount of light.
//
// Diffusing the residual in display units therefore conserves the wrong
// quantity. It is exact only while the levels a pixel gets pushed to stay
// inside the rung interval its target sits in, because there the two scales
// agree; that is why `--spatial 0` is clean. Once diffusion pushes further —
// and at `--spatial 1` it routinely does — the linear value of the levels used
// is a *convex* function of the level index, so their mean lands above the
// target however exactly the display-unit ledger balances. The bias was
// positive, peaked at the intermediate rungs, and scaled with how uneven the
// ladder is: +49/1000 linear at `--levels 3`, +23 at `--levels 4`, +4 at 8, and
// nothing at all at `--levels 2` or `--level-spacing linear`, where the ladder
// is uniform and the question does not arise.
//
// So the ledger is kept in linear light and the threshold is shown display
// units, converted at each end. A uniform ladder short-circuits to `ladder ==
// nullptr`: the conversion would be a constant factor that cancels out of both
// `decide` and the residual, so skipping it keeps that output bit-identical
// rather than merely equal. The goal pass uses the identity too — its scale is
// [0, frames*(levels-1)] and an integer there is a whole level-step, which is
// what makes the temporal budget exact.
struct Space {
    const vision::LevelSet* ladder = nullptr;

    static Space of(const vision::LevelSet& l) {
        Space s;
        if (!l.uniform && l.count() > 1) s.ladder = &l;
        return s;
    }

    // A fractional display value to linear: rung k plus a fraction of the way to
    // rung k+1, interpolated in linear light — the exact inverse of
    // vision::to_display(), but extended past both ends by the end spans instead
    // of clamping, because an accumulated error that falls outside the ladder
    // must still be carried rather than silently dropped.
    // The rung interval a value falls in, as an index into `lin` with a
    // successor. Two lookups rather than one because the two conversions arrive
    // from different sides: a display value names its interval directly, a
    // linear one has to be searched for. Both clamp rather than refuse, because
    // accumulated error routinely puts a value outside the ladder and it still
    // has to convert.
    int interval_of_display(double display) const {
        return std::clamp((int)std::floor(display), 0, (int)ladder->lin.size() - 2);
    }

    int interval_of_linear(double conserved) const {
        const auto& L = ladder->lin;
        const int top = (int)L.size() - 1;
        if (conserved < L.front())      return 0;
        if (conserved >= L.back())      return top - 1;
        return (int)(std::upper_bound(L.begin(), L.end(), conserved) - L.begin()) - 1;
    }

    double to_conserved(double display) const {
        if (!ladder) return display;
        const auto& L = ladder->lin;
        const int k = interval_of_display(display);
        return (double)L[k] + (display - (double)k) * ((double)L[k + 1] - (double)L[k]);
    }

    // Sitting exactly on a rung has to survive the round trip. The temporal
    // budget pays itself down to a whole number of rungs and then asks for that
    // number back; in display units that was integer arithmetic and came back
    // exact, but of_level() sums and a division do not, and they leave a rounding
    // residue of about 1e-6. A threshold cannot ignore it: MaskDither fires on
    // `frac * 254 > thr`, so against a mask cell of 0 a residue of 1e-6 is
    // indistinguishable from a genuine fraction and buys a whole extra level.
    // Snapping below kSnap kills that and nothing else. It sits in a wide band:
    // ten times above the worst residue that survives narrowing the ledger back
    // to float here, and four hundred times below the 1/254 a mask can resolve at
    // all, so a fraction large enough to mean anything is untouched.
    static constexpr double kSnap = 1e-5;

    double to_display(double conserved) const {
        if (!ladder) return conserved;
        const auto& L = ladder->lin;
        const int k = interval_of_linear(conserved);
        const double span = (double)L[k + 1] - (double)L[k];
        if (span <= 0.0) return (double)k;
        const double t = ((double)conserved - (double)L[k]) / span;
        if (std::abs(t) < kSnap)       return (double)k;
        if (std::abs(t - 1.0) < kSnap) return (double)k + 1;
        return (double)k + t;
    }

    double of_level(int level) const {
        if (!ladder) return (double)level;
        return ladder->lin[std::clamp(level, 0, (int)ladder->lin.size() - 1)];
    }
};

// One serpentine quantizing scan over a `size` grid, with its own private
// spatial-error channel. Per pixel it forms two values: `decide = target +
// spatial * spatial_err` goes to dither_fn, which quantizes it to a level in
// [0, max_level], while `account = target + spatial_err` is what the residual is
// measured against -- and that residual is handed to spatial_fn *in full*.
// `sink(row, col, level, v)` records the result. Used for both the goal pass and
// each output frame.
//
// This comment said `spatial` of the residual was deposited and the rest
// withheld, which is what the code used to do and is the bug that lifted
// shadows by 27/1000 under a mask threshold. Keeping the ledger exact is the
// fix; see the --spatial section of CLAUDE.md.
//
// Everything here is in `space`'s conserved units — value_fn returns them and
// the ledger accumulates them — except the value handed to dither_fn and to
// `sink`, which is converted to display levels because that is the only scale a
// threshold rule can read.
template <typename ValueFn, typename Sink>
static void quantize_scan(cv::Size size, DitherFn& dither_fn, SpatialFn spatial_fn,
                          float spatial, int max_level, const Space& space,
                          ValueFn value_fn, Sink sink)
{
    cv::Mat spatial_err = cv::Mat::zeros(size, CV_32F);
    for_serpentine(size, [&](int row, int col, int dir) {
        // Two values, and the distinction is what keeps the tone honest.
        //
        // `decide` is what the threshold rule sees: the target plus only
        // `spatial` of the accumulated error, which is what makes --spatial a
        // dial between a pure mask dither and full Floyd-Steinberg.
        //
        // `account` is the target plus *all* of it, and the residual measured
        // against that is diffused in full. Scaling the deposit instead -- what
        // this did before -- destroys a fraction of every residual, and with a
        // mask threshold those residuals are not zero-mean: the mask is a fixed
        // function of position, so it correlates with the very error field that
        // feeds back into it. Discarding part of a correlated quantity lifted
        // the shadows by 27/1000 in linear light at --spatial 0.5 and crushed
        // the highlights by 19/1000. A random threshold showed none of it,
        // which is what identified the correlation as the cause.
        //
        // Keeping the ledger exact makes the output average to the target for
        // any `spatial`, whatever the residuals correlate with. At spatial 1 the
        // two values coincide and this is ordinary Floyd-Steinberg.
        const double target  = value_fn(row, col);
        const float  acc     = spatial_err.at<float>(row, col);
        const double decide  = target + spatial * acc;
        const double account = target + acc;

        const double shown = space.to_display(decide);
        const int chosen = dither_fn(shown, row, col);
        const int level = std::clamp(chosen, 0, max_level);
        spatial_fn(row, col, dir, spatial_err, account - space.of_level(level));
        sink(row, col, level, shown);
    });
}

// Produce all output frames. Returns CV_8U raw display levels (0..levels-1);
// the caller applies vision::encode_levels() to encode to pixel values.
//
// Each pixel is given a per-frame-total budget `goal = target * frames`. For
// multi-frame runs the goal is dithered to an integer in [0, frames*(levels-1)]
// so the running sum of displays converges exactly to it; for single-frame
// runs the raw float target*1 is used, and the ideal formula
//
//     ideal = (goal - displayed) / (frames - f) + spatial_err
//
// reduces to the classical `ideal = target + spatial_err`. The goal pass runs at
// the same `spatial` and `spatial_fn` as the frames: it decides each pixel's
// total, and so the average, and the frames only divide that total between
// them. Run at full strength, as it was, it made --spatial 0.01 over two frames
// the same image as --spatial 1.
//
// Both ledgers — the temporal budget here and the spatial one inside each
// frame's quantize_scan — are kept in `Space`'s conserved units, which is what
// makes the whole thing add up. Running them in different units does not merely
// leave the old bias in place, it makes it worse: the spatial pass pushes
// towards the right amount of light while the temporal pass drags the total
// back to a display-level sum, and at `--levels 3 --frames 8 --spatial 1` the
// two fighting read +101/1000 against +73 for the original bug.
//
// The goal pass is the one place display units are still the right scale, and
// it is deliberate. Snapping `target * frames` to an integer there is snapping
// it to the nearest *achievable* total, because a pixel that spends every frame
// on one of the two rungs bracketing its target produces totals spaced exactly
// one level-step apart. Converting that integer back through the ladder
// afterwards is what puts the budget into conserved units without giving up the
// exact close.
std::vector<cv::Mat> ditherPass(const cv::Mat& target,
                          const vision::LevelSet& ladder,
                          DitherFn& dither_fn, SpatialFn spatial_fn,
                          float spatial, int levels,
                          int frames, int debug_row, int debug_col)
{
    const Space space = Space::of(ladder);

    // Per-pixel budget across all frames. Multi-frame dithers it to an integer
    // via the chosen threshold; single-frame leaves target*1 as a float so the
    // main loop's formula collapses to `ideal = target + sp`.
    // Double, and it has to be. In display units this ledger held small exact
    // integers, so single precision was free; in conserved units it accumulates
    // `frames` rung values and the residue that leaves is measured against the
    // *smallest* span on the ladder — 0.017 of full scale at 8 levels, 0.0056 at
    // 16. At 512 frames that ratio puts a float32 rounding error above a whole
    // level, which is how it presents: a pixel whose budget has been paid off
    // exactly asks for one more level than it is owed.
    cv::Mat goals;
    target.convertTo(goals, CV_64F, frames);
    if (frames > 1)
        quantize_scan(goals.size(), dither_fn, spatial_fn, spatial, frames * (levels - 1),
                      Space{},
                      [&](int row, int col) { return goals.at<double>(row, col); },
                      [&](int row, int col, int goal, float) {
                          goals.at<double>(row, col) = (double)goal;
                      });
    // Display levels to conserved units. A no-op for a uniform ladder, and for
    // `frames == 1` it is just the target itself in those units.
    if (space.ladder)
        goals.forEach<double>([&](double& g, const int*) {
            g = (double)frames * space.to_conserved(g / (double)frames);
        });

    cv::Mat displayed = cv::Mat::zeros(target.size(), CV_64F);
    std::vector<cv::Mat> outputs;
    outputs.reserve(frames);
    for (int f = 0; f < frames; f++) {
        dither_fn.new_frame();
        cv::Mat output(target.size(), CV_8U);
        quantize_scan(target.size(), dither_fn, spatial_fn, spatial, levels - 1,
            space,
            [&](int row, int col) {
                return (goals.at<double>(row, col) - displayed.at<double>(row, col))
                       / (double)(frames - f);
            },
            [&](int row, int col, int display, float ideal) {
                double d = displayed.at<double>(row, col) + space.of_level(display);
                displayed.at<double>(row, col) = d;
                output.at<uint8_t>(row, col) = (uint8_t)display;
                if (row == debug_row && col == debug_col)
                    fprintf(stderr, "  frame=%d ideal=%+.4f display=%d displayed=%+.4f\n",
                            f, ideal, display, d);
            });
        outputs.push_back(output);
    }
    return outputs;
}

