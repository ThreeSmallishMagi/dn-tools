// SPDX-License-Identifier: GPL-3.0-or-later
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <random>
#include <string>
#include <vector>
#include <opencv2/opencv.hpp>
#include "bluenoise.h"
#include "srgb.h"

std::mt19937 rng;

// ===========================================================================
// All work happens in "display units": linear-light brightness on a scale of
// [0, levels-1]. The dither functions pick an integer level given a float
// accumulator value; the residual is what's left after that choice and is
// carried forward (across pixels via --spatial, across frames via the budget).
// ===========================================================================

// `dir` is +1 for left-to-right scans, -1 for right-to-left (serpentine).
using SpatialFn = void (*)(int row, int col, int dir, cv::Mat& spatial_err, float value);

// Pick a display level given the accumulated display value at (row, col).
// `new_frame()` is called at the top of each output frame; thresholds with
// per-frame state (blue noise) override it, others ignore.
class DitherFn {
public:
    virtual ~DitherFn() = default;
    virtual int operator()(float v, int row, int col) = 0;
    virtual void new_frame() {}
};

class RoundDither : public DitherFn {
public:
    int operator()(float v, int /*row*/, int /*col*/) override {
        return (int)std::lround(v);
    }
};

class RandomDither : public DitherFn {
public:
    int operator()(float v, int /*row*/, int /*col*/) override {
        int lo = (int)std::floor(v);
        float frac = v - lo;
        return lo + (frac > dist(rng) ? 1 : 0);
    }
private:
    std::uniform_real_distribution<float> dist{0.0f, 1.0f};
};

// Blue-noise dither: threshold frac(v) against a precomputed mask value with
// per-frame toroidal (sx, sy) offsets drawn in new_frame().
class BlueDither : public DitherFn {
public:
    explicit BlueDither(cv::Mat m) : mask(std::move(m)) {}
    int operator()(float v, int row, int col) override {
        int lo = (int)v;
        float frac = v - lo;
        uint8_t thr = mask.at<uint8_t>((row + sy) % mask.rows, (col + sx) % mask.cols);
        return frac * 255.0f > thr ? lo + 1 : lo;
    }
    void new_frame() override {
        sx = std::uniform_int_distribution<int>(0, mask.cols - 1)(rng);
        sy = std::uniform_int_distribution<int>(0, mask.rows - 1)(rng);
    }
private:
    cv::Mat mask;
    int sx = 0, sy = 0;
};

static void spatial_none(int /*row*/, int /*col*/, int /*dir*/,
                         cv::Mat& /*spatial_err*/, float /*value*/) { }

// Floyd-Steinberg diffusion, serpentine-aware. On the current row the "next"
// pixel we haven't visited yet is (row, col+dir); the "previous" is col-dir.
static void spatial_floydsteinberg(int row, int col, int dir,
                                   cv::Mat& spatial_err, float value)
{
    const int W = spatial_err.cols;
    const int next_col = col + dir;
    const int prev_col = col - dir;
    if (0 <= next_col && next_col < W)
        spatial_err.at<float>(row, next_col) += value * 7.0f / 16;
    if (row + 1 < spatial_err.rows) {
        if (0 <= prev_col && prev_col < W)
            spatial_err.at<float>(row + 1, prev_col) += value * 3.0f / 16;
        spatial_err.at<float>(row + 1, col) += value * 5.0f / 16;
        if (0 <= next_col && next_col < W)
            spatial_err.at<float>(row + 1, next_col) += value * 1.0f / 16;
    }
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

// One serpentine quantizing scan over a `size` grid, with its own private
// spatial-error channel. Per pixel: `v = value_fn(row, col) + spatial_err`, which
// dither_fn quantizes to a level in [0, max_level]; `spatial` of the residual is
// handed to spatial_fn and the rest withheld. `sink(row, col, level, v)` records
// the result. Used for both the goal pass and each output frame.
template <typename ValueFn, typename Sink>
static void quantize_scan(cv::Size size, DitherFn& dither_fn, SpatialFn spatial_fn,
                          float spatial, int max_level, ValueFn value_fn, Sink sink)
{
    cv::Mat spatial_err = cv::Mat::zeros(size, CV_32F);
    for_serpentine(size, [&](int row, int col, int dir) {
        float v = value_fn(row, col) + spatial_err.at<float>(row, col);
        int level = std::clamp(dither_fn(v, row, col), 0, max_level);
        spatial_fn(row, col, dir, spatial_err, spatial * (v - (float)level));
        sink(row, col, level, v);
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
// reduces to the classical `ideal = target + spatial_err`. `--spatial` is the
// fraction of quantization residual passed to spatial neighbours via
// `spatial_fn` (default 1 = full FS diffusion, 0 = none). The goal pass always
// diffuses in full; only the per-frame passes are scaled.
static std::vector<cv::Mat> ditherPass(const cv::Mat& target,
                          DitherFn& dither_fn, SpatialFn spatial_fn,
                          float spatial, int levels,
                          int frames, int debug_row, int debug_col)
{
    // Per-pixel budget across all frames. Multi-frame dithers it to an integer
    // via the chosen threshold; single-frame leaves target*1 as a float so the
    // main loop's formula collapses to `ideal = target + sp`.
    cv::Mat goals;
    target.convertTo(goals, CV_32F, frames);
    if (frames > 1)
        quantize_scan(goals.size(), dither_fn, spatial_fn, 1.0f, frames * (levels - 1),
                      [&](int row, int col) { return goals.at<float>(row, col); },
                      [&](int row, int col, int goal, float) {
                          goals.at<float>(row, col) = (float)goal;
                      });

    cv::Mat displayed = cv::Mat::zeros(target.size(), CV_32F);
    std::vector<cv::Mat> outputs;
    outputs.reserve(frames);
    for (int f = 0; f < frames; f++) {
        dither_fn.new_frame();
        cv::Mat output(target.size(), CV_8U);
        quantize_scan(target.size(), dither_fn, spatial_fn, spatial, levels - 1,
            [&](int row, int col) {
                return (goals.at<float>(row, col) - displayed.at<float>(row, col))
                       / (float)(frames - f);
            },
            [&](int row, int col, int display, float ideal) {
                float d = displayed.at<float>(row, col) + (float)display;
                displayed.at<float>(row, col) = d;
                output.at<uint8_t>(row, col) = (uint8_t)display;
                if (row == debug_row && col == debug_col)
                    fprintf(stderr, "  frame=%d ideal=%+.4f display=%d displayed=%+.4f\n",
                            f, ideal, display, d);
            });
        outputs.push_back(output);
    }
    return outputs;
}

struct Options {
    std::string inPath;
    std::string threshold = "round";
    std::string outPath = "dither.png";
    bool useSrgb = true;
    int levels = 2;
    int frames = 1;
    bool saveFrames = false;
    double spatial = 1.0;   // fraction of residual diffused spatially (1 = full FS, 0 = none);
                            // resolved in parseOptions, which defaults it to 0 when
                            // --threshold names a threshold explicitly
    uint64_t seed = 42;
    double mask_sigma = 1.8;
    int maskW = 0;
    int maskH = 0;
    std::string levelSpacing = "srgb";    // where the display levels sit: srgb | linear
    std::string maskIn;     // empty = build the mask instead of loading one
    std::string maskOut;    // empty = don't write the mask
    double mask_eps = 1e-4;
    double mask_radius = 6.0;
    int debug_row = -1;
    int debug_col = -1;
};

// cv::imwrite reports an unwritable path by returning false and an unknown
// extension by throwing; neither should reach the user as a core dump.
static bool write_image(const std::string& path, const cv::Mat& img)
{
    try {
        if (cv::imwrite(path, img)) return true;
    } catch (const cv::Exception&) { }
    fprintf(stderr, "Could not write %s\n", path.c_str());
    return false;
}

static void usage();

static bool parseOptions(int argc, char** argv, Options& opt)
{
    opt.inPath = argv[1];
    bool threshold_set = false, spatial_set = false, mask_build_set = false, bad = false;

    for (int index = 2; index < argc && !bad; ) {
        std::string k = argv[index++];
        auto need = [&]() -> std::string {
            if (index >= argc) {
                fprintf(stderr, "Missing value for %s\n", k.c_str());
                bad = true; return "";
            }
            return argv[index++];
        };
        // Numeric arguments are parsed and range-checked here, so a typo names
        // the offending option instead of throwing std::stod out of main. The
        // level ceiling is real: level indices travel in a CV_8U mat.
        auto need_num = [&](double lo, double hi) -> double {
            std::string v = need();
            if (!bad) {
                try {
                    size_t used = 0;
                    double d = std::stod(v, &used);
                    if (used == v.size() && lo <= d && d <= hi) return d;
                } catch (const std::exception&) { }
                fprintf(stderr, "Bad value for %s: '%s' (expected a number in [%g, %g])\n",
                        k.c_str(), v.c_str(), lo, hi);
                bad = true;
            }
            return lo;
        };
        if      (k == "--help" || k == "-h") { usage(); exit(0); }
        else if (k == "--threshold")  { opt.threshold   = need(); threshold_set = true; }
        else if (k == "--spatial")    { opt.spatial     = need_num(0.0, 1.0); spatial_set = true; }
        else if (k == "--linear")      opt.useSrgb     = false;
        else if (k == "--out")         opt.outPath     = need();
        else if (k == "--seed")        opt.seed        = (uint64_t)need_num(0, 9e15);
        else if (k == "--levels")      opt.levels      = (int)need_num(2, 256);
        else if (k == "--level-spacing") opt.levelSpacing = need();
        else if (k == "--frames")      opt.frames      = (int)need_num(1, 100000);
        else if (k == "--save-frames") opt.saveFrames  = true;
        else if (k == "--mask-in")     opt.maskIn      = need();
        else if (k == "--mask-out")    opt.maskOut     = need();
        else if (k == "--mask-sigma")  { opt.mask_sigma  = need_num(0.01, 1000); mask_build_set = true; }
        else if (k == "--mask-eps")    { opt.mask_eps    = need_num(1e-12, 1);   mask_build_set = true; }
        else if (k == "--mask-radius") { opt.mask_radius = need_num(0.1, 100);   mask_build_set = true; }
        else if (k == "--mask-size") {
            std::string v = need();
            if (std::sscanf(v.c_str(), "%dx%d", &opt.maskW, &opt.maskH) != 2 ||
                opt.maskW <= 0 || opt.maskH <= 0) {
                fprintf(stderr, "Bad --mask-size value, use WxH format\n"); return false;
            }
            mask_build_set = true;
        }
        else if (k == "--debug-pixel") {
            std::string v = need();
            if (std::sscanf(v.c_str(), "%d,%d", &opt.debug_row, &opt.debug_col) != 2 ||
                opt.debug_row < 0 || opt.debug_col < 0) {
                fprintf(stderr, "Bad --debug-pixel value, use ROW,COL format\n"); return false;
            }
        }
        else { fprintf(stderr, "Unknown option: %s\n", k.c_str()); return false; }
    }
    if (bad) return false;

    // Naming a threshold opts out of error diffusion; an explicit --spatial wins
    // either way, whichever order the two appear in.
    if (!spatial_set) opt.spatial = threshold_set ? 0.0 : 1.0;

    if (opt.levelSpacing != "linear" && opt.levelSpacing != "srgb") {
        fprintf(stderr, "--level-spacing must be linear or srgb\n"); return false;
    }

    // Mask options are only consulted by the blue threshold, and a supplied mask
    // makes the build knobs moot — say so rather than ignoring them in silence.
    if (opt.threshold != "blue" &&
        (mask_build_set || !opt.maskIn.empty() || !opt.maskOut.empty()))
        fprintf(stderr, "Note: mask options apply only to --threshold blue\n");
    else if (!opt.maskIn.empty() && mask_build_set)
        fprintf(stderr, "Note: --mask-in supplies the mask; mask-building options are ignored\n");

    return true;
}

static void usage() {
    fputs(R"(usage: dither <image> [options]

Treats input as sRGB by default and dithers in linear light. A dither is a
threshold rule (--threshold) plus error diffusion (--spatial); the defaults
give round-to-nearest with full Floyd-Steinberg at 2 levels.

Thresholds (--threshold):
  round      deterministic round to nearest level (default)
  random     stochastic threshold — fast, grainy
  blue       void-and-cluster blue-noise mask; best perceptual quality

Options:
  --threshold <name>  threshold rule to use (default: round)
  --spatial <frac>    fraction of each pixel's residual diffused to its
                      neighbours by Floyd-Steinberg (1 = full FS, 0 = none).
                      Defaults to 1, or to 0 when --threshold is given
  --out <path>        output file path (default: dither.png)
  --linear            treat pixel values as raw linear light (skip the sRGB
                      ↔ linear conversion that normally wraps the dither)
  --levels <n>        number of output tones, 2-256 (default 2)
  --level-spacing <s> where those tones sit: srgb (equal steps in pixel value,
                      default — 3 levels are 0/128/255) or linear (equal steps
                      in linear light — 3 levels are 0/188/255)
  --frames <n>        dither n independent frames and average them (default 1)
  --save-frames       also write each frame as <stem>-N.png
  --seed <n>          random seed (default 42)
  --debug-pixel <r,c> print per-frame ideal/display/displayed for pixel (r, c)
  --help, -h          show this message

blue-only options:
  --mask-sigma <val>  blue-noise kernel radius in pixels (default 1.8)
  --mask-size <WxH>   generate a WxH tile and repeat it over the image
                      (default: the image size, capped at 256 per side)
  --mask-in <path>    use this image as the mask instead of building one;
                      the other mask-building options are then ignored
  --mask-out <path>   write the mask in use to this path
  --mask-eps <val>    Gaussian kernel weight cutoff (default 1e-4)
  --mask-radius <mul> kernel half-width in sigma units (default 6.0). Only
                      bites below ~4.3, where --mask-eps stops binding first
)", stdout);
}

int main(int argc, char** argv)
{
    if (argc < 2) { usage(); return -1; }
    if (std::string(argv[1]) == "--help" || std::string(argv[1]) == "-h") { usage(); return 0; }

    Options opt;
    if (!parseOptions(argc, argv, opt)) return -1;

    std::cout << "threshold=" << opt.threshold << " colorspace=" << (opt.useSrgb ? "sRGB" : "linear")
              << " levels=" << opt.levels << "/" << opt.levelSpacing
              << " frames=" << opt.frames
              << " spatial=" << opt.spatial << " out=" << opt.outPath << "\n";

    rng = std::mt19937(opt.seed);
    std::unique_ptr<DitherFn> dither_fn;
    // With --spatial 0 the residual would be scaled to zero anyway; skip the
    // deposit entirely so no-diffusion runs don't pay for it.
    SpatialFn spatial_fn = (opt.spatial > 0.0) ? spatial_floydsteinberg : spatial_none;
    cv::Mat image = cv::imread(opt.inPath, cv::IMREAD_GRAYSCALE);
    if (image.empty()) { fprintf(stderr, "Could not read %s\n", opt.inPath.c_str()); return -1; }
    if      (opt.threshold == "round")  { dither_fn = std::make_unique<RoundDither>(); }
    else if (opt.threshold == "random") { dither_fn = std::make_unique<RandomDither>(); }
    else if (opt.threshold == "blue")   {
        cv::Mat mask;
        if (!opt.maskIn.empty()) {
            mask = cv::imread(opt.maskIn, cv::IMREAD_GRAYSCALE);
            if (mask.empty()) {
                fprintf(stderr, "Could not read mask %s\n", opt.maskIn.c_str()); return -1;
            }
            std::cerr << "Loaded mask " << mask.cols << "x" << mask.rows
                      << " from " << opt.maskIn << "\n";
        } else {
            constexpr int max_default_mask = 256;
            cv::Size maskSz = (opt.maskW > 0 && opt.maskH > 0)
                            ? cv::Size(opt.maskW, opt.maskH)
                            : cv::Size(std::min(image.cols, max_default_mask),
                                       std::min(image.rows, max_default_mask));
            std::cerr << "Building progressive mask " << maskSz.width << "x" << maskSz.height
                      << " sigma=" << opt.mask_sigma << " eps=" << opt.mask_eps
                      << " radius=" << opt.mask_radius
                      << " srgb=" << opt.useSrgb << " levels=" << opt.levels << " …\n";
            mask = build_blue_noise_mask(maskSz, opt.mask_sigma, opt.mask_eps,
                                         opt.mask_radius, opt.seed);
        }
        if (!opt.maskOut.empty() && !write_image(opt.maskOut, mask)) return -1;
        dither_fn = std::make_unique<BlueDither>(std::move(mask));
    }
    else { fprintf(stderr, "Unknown threshold: %s\n", opt.threshold.c_str()); return -1; }

    // Build target in display units: [0, levels-1] on the chosen level ladder.
    vision::LevelSet ladder = vision::make_levels(opt.levels, opt.useSrgb,
                                                  opt.levelSpacing == "srgb");
    cv::Mat target = vision::decode_display(image, ladder, opt.useSrgb);

    if (opt.debug_row >= 0 && opt.debug_col >= 0) {
        if (opt.debug_row >= target.rows || opt.debug_col >= target.cols) {
            fprintf(stderr, "--debug-pixel %d,%d is outside the %dx%d image\n",
                    opt.debug_row, opt.debug_col, target.cols, target.rows);
            return -1;
        }
        fprintf(stderr, "debug pixel=%d,%d target=%.6f frames=%d spatial=%.2f\n",
                opt.debug_row, opt.debug_col,
                target.at<float>(opt.debug_row, opt.debug_col),
                opt.frames, opt.spatial);
    }

    std::vector<cv::Mat> frames = ditherPass(target, *dither_fn, spatial_fn,
                                             opt.spatial, opt.levels, opt.frames,
                                             opt.debug_row, opt.debug_col);
    for (cv::Mat& f : frames)
        f = vision::encode_levels(f, ladder);

    cv::Mat result = vision::average_gray(frames, opt.useSrgb);
    if (!write_image(opt.outPath, result)) return -1;

    if (opt.saveFrames) {
        auto p = std::filesystem::path(opt.outPath);
        std::string stem = p.stem().string();
        for (size_t k = 0; k < frames.size(); k++)
            if (!write_image((p.parent_path() / (stem + "-" + std::to_string(k) + ".png")).string(),
                             frames[k]))
                return -1;
    }

    return 0;
}
