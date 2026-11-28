// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <memory>
#include <random>
#include <vector>
#include <opencv2/core.hpp>
#include "srgb.h"

// The dither engine: the threshold rules, the error deposit, and the driver
// that walks an image through them. It knows nothing about the command line,
// about masks beyond being handed one, or about where a mask came from.

// The global RNG the stochastic rules draw from, seeded once from --seed. It is
// shared so that a run is reproducible from the seed alone: every mask builder
// keeps its own generator precisely so that building one cannot disturb this.
extern std::mt19937 rng;

// All work happens in "display units": linear-light brightness on a scale of
// [0, levels-1]. The dither functions pick an integer level given a float
// accumulator value; the residual is what's left after that choice and is
// carried forward (across pixels via --spatial, across frames via the budget).

// `dir` is +1 for left-to-right scans, -1 for right-to-left (serpentine).
using SpatialFn = void (*)(int row, int col, int dir, cv::Mat& spatial_err, float value);

// Pick a display level given the accumulated display value at (row, col).
// `new_frame()` is called at the top of each output frame; thresholds with
// per-frame state (blue noise) override it, others ignore.
class DitherFn {
public:
    virtual ~DitherFn() = default;
    // double, not float: the goal pass asks this to round `target * frames`,
    // which passes 2^24 at high frame counts, and a float cannot separate
    // adjacent integer budgets there. The per-frame passes never exceed
    // `levels-1`, so nothing else changes -- the 76-run hash matrix is
    // unmoved by this.
    virtual int operator()(double v, int row, int col) = 0;
    virtual void new_frame() {}
};

class RoundDither : public DitherFn {
public:
    int operator()(double v, int /*row*/, int /*col*/) override {
        return (int)std::lround(v);
    }
};

class RandomDither : public DitherFn {
public:
    int operator()(double v, int /*row*/, int /*col*/) override {
        int lo = (int)std::floor(v);
        float frac = v - lo;
        return lo + (frac > dist(rng) ? 1 : 0);
    }
private:
    std::uniform_real_distribution<float> dist{0.0f, 1.0f};
};

// Mask dither: threshold frac(v) against a precomputed mask value with per-frame
// toroidal (sx, sy) offsets drawn in new_frame(). Which generator produced the
// mask is not its concern; its lowest cells light first, which is the
// convention maskutil::threshold_mask builds every mask to.
class MaskDither : public DitherFn {
public:
    explicit MaskDither(cv::Mat m) : mask(std::move(m)) {}
    int operator()(double v, int row, int col) override {
        // floor, not a cast: (int) truncates toward zero, so a negative v --
        // which error diffusion produces in the shadows -- gave lo = 0 and a
        // negative frac. The threshold test could then never fire and the level
        // was pinned at 0, whatever the mask said.
        //
        // This used to shift the tone as well, because a one-sided residual was
        // then partly discarded by --spatial. It no longer does: with the ledger
        // in quantize_scan kept exact, reintroducing the truncation moves the
        // whole-image tone by under 0.11/1000. It stays because the decision is
        // still wrong without it -- frac has to be in [0, 1) for the comparison
        // against the mask to mean what it says.
        const int lo = (int)std::floor(v);
        const float frac = v - lo;                    // now genuinely in [0, 1)
        uint8_t thr = mask.at<uint8_t>((row + sy) % mask.rows, (col + sx) % mask.cols);
        // 254, not 255, because that is the scale threshold_byte ranked the
        // mask onto: a cell holding t is the rank at quantile t/254, so firing
        // on frac > t/254 fires with probability frac and the level averages to
        // the target. Comparing against 255 instead biased every mask-dithered
        // image up by half a mask step -- +0.5/255 of a level, measured as
        // +1.06/1000 linear at the defaults and +1.87 at --levels 3
        // --level-spacing linear. It survived because it is invisible wherever
        // error diffusion is on: the ledger absorbs it, so only --spatial 0
        // shows it, and no whole-image check covered that corner.
        //
        // What is left is the mask's own resolution: frac meets a 255-step
        // staircase, so a given image's residual is its frac histogram against
        // that staircase, bounded by 1/508 of a level either way and not
        // removable by any offset. It measures -0.02/1000. Scaling by 255 and
        // offsetting by half a step instead corrects the mean but leaves a
        // 255/254 gain about mid-grey, which reads -0.60 on the same image --
        // matching the ranking is not the same as matching it on average.
        return frac * 254.0f > thr ? lo + 1 : lo;
    }
    void new_frame() override {
        sx = std::uniform_int_distribution<int>(0, mask.cols - 1)(rng);
        sy = std::uniform_int_distribution<int>(0, mask.rows - 1)(rng);
    }
private:
    cv::Mat mask;
    int sx = 0, sy = 0;
};


// Deposit nothing; the two are equivalent at --spatial 0 and this skips the
// arithmetic.
void spatial_none(int row, int col, int dir, cv::Mat& spatial_err, float value);
// Floyd-Steinberg's 7/16, 3/16, 5/16, 1/16, mirrored by `dir` so it always
// pushes error toward pixels not yet visited.
void spatial_floydsteinberg(int row, int col, int dir, cv::Mat& spatial_err, float value);

// `frames` images of raw level indices in [0, levels-1], CV_8U. `target` is in
// display units on `ladder`.
std::vector<cv::Mat> ditherPass(const cv::Mat& target, const vision::LevelSet& ladder,
                                DitherFn& dither_fn, SpatialFn spatial_fn,
                                float spatial, int levels, int frames,
                                int debug_row, int debug_col);
