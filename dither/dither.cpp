// SPDX-License-Identifier: GPL-3.0-or-later
// The pipeline, and nothing else: decode the input onto the level ladder, hand
// it to the engine, encode what comes back. Everything it calls lives in
// engine.cpp (the dither), cli.cpp (the command line), mask.cpp (mask policy)
// or generators.cpp (the generators themselves).
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include <opencv2/opencv.hpp>
#include "app.h"
#include "cli.h"
#include "engine.h"
#include "generators.h"
#include "mask.h"
#include "samples.h"
#include "maskparams.h"
#include "generators.h"
#include "options.h"
#include "srgb.h"

// The input in display units: [0, levels-1] on the chosen level ladder, and
// enlarged first if --supersample asked for it.
static cv::Mat prepare_target(const cv::Mat& image, const vision::LevelSet& ladder,
                              const Options& opt)
{
    cv::Mat target = vision::decode_display(image, ladder, opt.useSrgb);
    if (opt.supersample > 1)
        // Enlarged in display units rather than in pixels: interpolating decoded
        // values keeps the tone correct, and the dither then runs on the fine
        // grid where the mask's grain can sit below the picture detail.
        cv::resize(target, target, cv::Size(target.cols * opt.supersample,
                                            target.rows * opt.supersample),
                   0, 0, cv::INTER_CUBIC);
    return target;
}

// False if --debug-pixel names a pixel the run does not have. Checked against
// the *target*, which --supersample has already enlarged.
static bool report_debug_pixel(const cv::Mat& target, const Options& opt)
{
    if (opt.debug_row < 0 || opt.debug_col < 0) return true;
    if (opt.debug_row >= target.rows || opt.debug_col >= target.cols) {
        fprintf(stderr, "--debug-pixel %d,%d is outside the %dx%d image\n",
                opt.debug_row, opt.debug_col, target.cols, target.rows);
        return false;
    }
    fprintf(stderr, "debug pixel=%d,%d target=%.6f frames=%d spatial=%.2f\n",
            opt.debug_row, opt.debug_col,
            target.at<float>(opt.debug_row, opt.debug_col),
            opt.frames, opt.spatial);
    return true;
}

// Level indices to pixel values, then to the output's size, in linear light.
//
// **One resample, not two.** With --supersample the dither grid is the only
// place the sub-pixel information exists, so --out-size is reached from there
// directly rather than via the input's size. Resolving to the input first
// quantises to 8 bits in between, and the second resample cannot recover what
// that threw away: shrinking a 4096 grid to 512 reads 21.1/1000 linear against
// the truth in one step and 36.4 through 1024. (It comes back with *more*
// distinct tones that way, 206 against 64, which is interpolation inventing
// values rather than detail surviving.)
//
// At exactly input x supersample the target *is* the dither grid, so nothing is
// averaged and the raw dither comes out whole -- supersample without reducing,
// which is the main thing --out-size is for. Past that the resample resumes, and
// enlarging further costs tone: the resolution here is how many dithered pixels
// each output pixel averages, so a bigger target averages fewer of them.
static void encode_and_resize(std::vector<cv::Mat>& frames,
                              const vision::LevelSet& ladder, const Options& opt)
{
    for (cv::Mat& f : frames) {
        f = vision::encode_levels(f, ladder);
        if (opt.outSize <= 0) {
            f = vision::downsample_gray(f, opt.supersample, opt.useSrgb);
            continue;
        }
        const int ss = std::max(opt.supersample, 1);
        const int w = f.cols / ss, h = f.rows / ss;       // the input's size
        const double k = (double)opt.outSize / std::max(w, h);
        f = vision::resize_gray(f, cv::Size(std::max(1, (int)std::lround(w * k)),
                                            std::max(1, (int)std::lround(h * k))),
                                opt.useSrgb);
    }
}

// The linear-light average, and the individual frames if --save-frames asked.
static bool write_outputs(const std::vector<cv::Mat>& frames, const Options& opt)
{
    if (!write_image(opt.outPath, vision::average_gray(frames, opt.useSrgb))) return false;
    if (!opt.saveFrames) {
        // The average is a preview, re-encoded to 8 bits, and it is easy to
        // hand on as though it were the frames.
        if (frames.size() > 1)
            std::cerr << "Note: " << opt.outPath << " is the average of " << frames.size()
                      << " frames, a preview; --save-frames also writes each frame, which"
                         " is what to display or expose\n";
        return true;
    }
    const auto path = std::filesystem::path(opt.outPath);
    const std::string stem = path.stem().string();
    for (size_t k = 0; k < frames.size(); k++)
        if (!write_image((path.parent_path() /
                          (stem + "-" + std::to_string(k) + ".png")).string(), frames[k]))
            return false;
    return true;
}

int dither_main(int argc, char** argv)
{
    if (argc < 2) { usage_brief(); return -1; }
    if (std::string(argv[1]) == "--help" || std::string(argv[1]) == "-h") { usage(); return 0; }

    Options opt;
    params::Params P;
    for (const Generator& g : generators()) g.register_params(P);
    register_mask_params(P);            // and the mask geometry that spans them

    // Loaded first so anything on the command line still outranks it: cached
    // values arrive as Origin::Measured, a command-line one as Origin::User.
    // Parsing --no-config/--config needs a peek, since loading precedes parsing.
    //
    // It walks the command line the way the parser will -- same starting index,
    // same idea of which tokens consume the next one -- because a pre-scan that
    // disagrees with the parser is a pre-scan that acts on tokens the parser
    // reads as something else. It did: it began at argv[2], so `dither
    // --no-config …` loaded the config anyway, and it matched any argument
    // equal to the string, so `--out --no-config` turned configuration off and
    // then wrote to a file of that name. Last one wins, as in the parser.
    std::string cfg = config_path();
    for (int i = first_option_index(argc, argv); i < argc; i++) {
        const std::string a = argv[i];
        if (a == "--no-config") cfg.clear();
        else if (a == "--config") { if (i + 1 < argc) cfg = argv[i + 1]; i++; }
        else if (option_takes_value(P, a)) i++;      // its value is not an option
    }
    if (!cfg.empty()) P.load_config(cfg);

    if (!parseOptions(argc, argv, opt, P)) return -1;

    // Where the results go is checked before any of the work that makes them:
    // a mask build can take a minute, and finding afterwards that the
    // directory is missing or the extension has no encoder throws that away.
    // write_image still reports what goes wrong at the write itself.
    if (!opt.explain) {
        for (const std::string& out : {opt.outPath, opt.maskOut}) {
            if (out.empty()) continue;
            const auto dir = std::filesystem::path(out).parent_path();
            std::error_code ec;
            if (!dir.empty() && !std::filesystem::is_directory(dir, ec)) {
                fprintf(stderr, "Could not write %s: %s is not a directory\n",
                        out.c_str(), dir.string().c_str());
                return -1;
            }
            if (!cv::haveImageWriter(out)) {
                fprintf(stderr, "Could not write %s: no image format has that extension"
                                " (use .png)\n", out.c_str());
                return -1;
            }
        }
    }


    if (!opt.explain) {
      std::cout << "threshold=" << opt.threshold;
      // Which mask, and where it came from. Only under --threshold mask, where
      // they are the settings that decide what the output looks like; under the
      // others they are resolved but unused, and printing them would suggest
      // otherwise. The generator prints its own line, but only when it builds:
      // --mask-in says nothing at all without this.
      if (opt.threshold == "mask") {
          if (!opt.maskIn.empty()) std::cout << " mask=" << opt.maskIn;
          else {
              std::cout << " mask-method=" << opt.maskMethod;
              if (!opt.maskSample.empty()) std::cout << " mask-sample=" << opt.maskSample;
          }
      }
      std::cout << " colorspace=" << (opt.useSrgb ? "sRGB" : "linear")
              << " levels=" << opt.levels << "/" << opt.levelSpacing
              << " frames=" << opt.frames
              << " spatial=" << opt.spatial << " out=" << opt.outPath << "\n";
    }

    rng = std::mt19937(opt.seed);
    std::unique_ptr<DitherFn> dither_fn;
    // With --spatial 0 the residual would be scaled to zero anyway; skip the
    // deposit entirely so no-diffusion runs don't pay for it.
    SpatialFn spatial_fn = (opt.spatial > 0.0) ? spatial_floydsteinberg : spatial_none;
    cv::Mat image;
    if (!opt.inPath.empty()) {
        // ANYDEPTH is what lets a 16-bit file through; without it imread silently
        // hands back 8 bits and the extra precision is gone before anything sees
        // it. Verified to give single-channel native depth for 16-bit gray *and*
        // 16-bit colour, so to_gray only has channels to reduce for odd inputs.
        image = cv::imread(opt.inPath, cv::IMREAD_GRAYSCALE | cv::IMREAD_ANYDEPTH);
        if (image.empty()) { fprintf(stderr, "Could not read %s\n", opt.inPath.c_str()); return -1; }
        // Facts read off the input rather than chosen; the mask size needs them.
        P.set(mask::keys::ImageW, (long long)image.cols, params::Origin::Measured);
        P.set(mask::keys::ImageH, (long long)image.rows, params::Origin::Measured);
    } else if (!opt.explain) {
        fprintf(stderr, "No input image given\n"); usage_brief(); return -1;
    }

    // The sample, read here for the same reason the image is: the mask size is
    // derived from both. A generator whose tile is a piece of its sample cannot
    // be asked for one bigger than the sample has, so the size rule needs the
    // sample's dimensions before it can answer -- and reading it now rather
    // than at build time is also what keeps --explain honest about the size it
    // reports. Decoded once and handed on.
    samples::Set set;
    if (!opt.maskSample.empty() && opt.maskIn.empty()) {
        // One resolution for the whole run. --mask-sample may name a file, a
        // directory of scans of one film, or a film in the library; reading
        // happens here so that `mask.sample_w`/`sample_h` -- which the size
        // rule reads, and which direct's limit is computed from -- are known
        // before the size is resolved.
        const bool crop_first = uses_crop(opt.maskMethod);
        const std::vector<std::string> paths = samples::resolve(opt.maskSample, crop_first);
        if (crop_first && !paths.empty() && !samples::is_path(opt.maskSample)) {
            // Said either way, since which file a film name reached is not
            // otherwise visible: the crop, or scans that were never chosen for
            // a tile.
            if (const std::string crop = samples::crop_for(opt.maskSample); !crop.empty())
                std::cerr << "  " << opt.maskMethod << " uses the film's crop, " << crop << "\n";
            else {
                const Generator* g = find_generator(opt.maskMethod);
                std::cerr << "Note: no crop for " << opt.maskSample << " in "
                          << samples::crops_root() << ", so " << opt.maskMethod
                          << (g && g->combines ? " takes the film's whole scans, picture and all. "
                                               : " cuts its tile from the middle of a whole scan. ")
                          << "Save a square crop of clean grain there as "
                          << opt.maskSample << ".png to choose it.\n";
            }
        }
        set = samples::Set::read(paths, &std::cerr);
        if (!set.empty()) {
            // The primary scan's size, not the set's total: the only thing that
            // reads these is `direct`'s sample_limit, and `direct` builds its
            // tile out of that one scan.
            const cv::Mat& one = set.primary();
            P.set(mask::keys::SampleW, (long long)one.cols, params::Origin::Measured);
            P.set(mask::keys::SampleH, (long long)one.rows, params::Origin::Measured);
        }
        // An unreadable sample is not reported here. --explain should still
        // answer, and the build says so properly a moment later with the path
        // in the message.
    }
    if (P.num(mask::keys::Budget) > 0 && !P.measured(P.str(mask::keys::Method) + ".rate"))
        fprintf(stderr, "Note: no measured speed for this machine yet, so --budget is working "
                        "from a compiled-in guess and this run may overshoot. It is recorded "
                        "afterwards, so the next one will be closer.\n");
    // A calibration for this sample: taken from the config when there is one,
    // and measured as well under --mask-calibrate. Before --explain, so it
    // reports what the build will use. A command-line value still outranks it.
    if (!maybe_calibrate(opt, P, cfg, &set)) return -1;
    if (opt.explain) { P.explain(std::cout); return 0; }

    if      (opt.threshold == "round")  { dither_fn = std::make_unique<RoundDither>(); }
    else if (opt.threshold == "random") { dither_fn = std::make_unique<RandomDither>(); }
    else if (opt.threshold == "mask")   {
        cv::Mat mask = obtain_mask(opt, set, P, cfg);
        if (mask.empty()) return -1;
        if (!opt.maskOut.empty() && !write_image(opt.maskOut, mask)) return -1;
        dither_fn = std::make_unique<MaskDither>(std::move(mask));
    }
    else { fprintf(stderr, "Unknown threshold: %s\n", opt.threshold.c_str()); return -1; }

    const vision::Spacing spacing = opt.levelSpacing == "linear" ? vision::Spacing::Linear
                                 : opt.levelSpacing == "device" ? vision::Spacing::Device
                                                                : vision::Spacing::Srgb;
    const vision::LevelSet ladder = vision::make_levels(opt.levels, opt.useSrgb, spacing);
    const cv::Mat target = prepare_target(image, ladder, opt);
    if (!report_debug_pixel(target, opt)) return -1;

    std::vector<cv::Mat> frames = ditherPass(target, ladder, *dither_fn, spatial_fn,
                                             opt.spatial, opt.levels, opt.frames,
                                             opt.debug_row, opt.debug_col);
    encode_and_resize(frames, ladder, opt);
    return write_outputs(frames, opt) ? 0 : -1;
}
