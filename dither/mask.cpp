// SPDX-License-Identifier: GPL-3.0-or-later
// Mask policy: size, cost and acquisition. The generator table does the rest --
// including calibration, which is per-generator and which this file used to
// name markov to reach.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <iostream>
#include <string>
#include <opencv2/opencv.hpp>
#include "mask.h"
#include "cli.h"
#include "generators.h"
#include "maskcache.h"
#include "samples.h"
#include "maskparams.h"

// The pair count the mask-size allocation assumes. Size is chosen first and pair
// count second, so the size estimate has to use a fixed reference rather than
// the value that will later be chosen against the size.
constexpr int kBudgetRefPairs = 64;

// Work units to build a w*h mask with `method`. Every generator's formula, in
// the one place, because two callers need it and they must agree: --budget
// divides this by the measured rate to choose a size, and the post-build timing
// multiplies the rate back out of it. Two copies would calibrate the rate
// against a different quantity than the budget then spends it on.
//
// `pairs` is passed in rather than resolved because choosing the mask size
// happens before choosing the pair count, and resolving it here would be a cycle.
//
// It was a template once, so that a caller holding a Resolver mid-rule and one
// holding a Params could share it. Params converts to a Resolver, so it takes
// one and the template is gone -- but the description of why it was templated
// outlived it.

// Predicted seconds, from that work and the throughput measured on this machine.
// 4^4, 4^5, 2^11. See mask_side.
inline constexpr double kMaskSides[] = {256.0, 1024.0, 2048.0, 4096.0};

static double mask_seconds(const params::Resolver& r, const std::string& method,
                           double w, double h, int pairs)
{
    // parseOptions validated the name against the same table, so this is not
    // null. Work formula and throughput both live under the generator's own
    // name, so neither is a branch.
    return find_generator(method)->work(r, (int)w, (int)h, pairs)
         / r.num(method + ".rate");
}

// One side of the default mask. Three floors and a ceiling, and the floors win:
//
//   grid  = image x supersample, the grid the dither actually runs on. A mask
//           bigger than that only ever exposes a window of itself, and a window
//           of a clustered mask is not histogram-flat.
//   128   = threshold resolution. A mask spreads 255 threshold values over its
//           pixels; 128^2 gives 64 per value and works, 32^2 gives 4 and
//           measurably fails.
//   4*highpass*scale = the mask must be able to contain the coarsest structure
//           it is allowed to have. --mask-highpass is already the claim that
//           nothing coarser exists, and --mask-scale multiplies it, so asking
//           for coarser grain enlarges the tile without anyone measuring it.
//   grid/max_repeats = repeat visibility, and how much repetition shows depends
//           entirely on the generator, so each one declares its own limit. A
//           markov tile repeated 16 times reads as regular banding in flat
//           areas; at 4 it does not. Blue noise is locally featureless, so a
//           repeat of it is still featureless — measured indistinguishable at
//           16 repeats, and forcing it to a bigger tile would have cost 30s a
//           run for nothing.
//
// The ceiling is what the chosen generator says it can afford. Floors override
// it, because repetition is a visible defect while the ceiling is only a cost
// preference — which is the honest stand-in until --budget can price both.
// The mask is square, and its side is a power of 2: 256, 1024, 2048 or 4096.
//
// It used to be derived per axis from the image, which made it a different size
// for almost every picture -- and a mask is cached under its own parameters, so
// a size that tracks the image means a rebuild for each new one and a cache that
// never hits. Two images of different shapes wanting the same texture want the
// same mask; nothing about a tile of grain follows from the aspect ratio of what
// it will be laid over.
//
// The ladder is coarse on purpose. Snapping to powers of four means a handful of
// distinct sizes across every image anyone dithers, so the cache is reused
// rather than merely present. Between rungs the step is 4x in side and 16x in
// build time, which is also why there is no point in finer gradations: the cost
// of the next rung up is not a small increase.
//
// It is capped, which it was not. The anti-repetition floor is grid/max_repeats,
// and grid is the image times --supersample, so a 3877x2869 scan at
// --supersample 10 asked for 9693x7173 -- seventy megapixels, from a generator
// whose max_size says 256, because the floor overrides the cap. That is not a
// large mask, it is a mask that never finishes. 4096 is the top of the ladder
// and the floor no longer beats it: a tile repeated more often than its
// generator would like is a visible defect, and a build that does not terminate
// is not an alternative to it.
static long long mask_side(const params::Resolver& r)
{
    const double ss = (double)r.integer(mask::keys::Supersample);
    // The longer axis, so neither axis repeats more often than the generator
    // says it may.
    const double grid = ss * (double)std::max(r.integer(mask::keys::ImageW),
                                              r.integer(mask::keys::ImageH));
    const std::string method = r.str(mask::keys::Method);
    const double repeats = (double)r.integer(method + ".max_repeats");
    const double floor = std::max({128.0,
                                   4.0 * r.num(mask::keys::Highpass) * r.num(mask::keys::Scale),
                                   grid / repeats});
    // Unset, the ceiling is the fixed figure each generator declares it can
    // afford. Given a budget, it becomes the largest size that fits in the time,
    // which is the whole point: a fixed pixel cap is a guess at a cost limit.
    // The floors still win — repetition is a defect, the budget is a preference.
    double cap = (double)r.integer(method + ".max_size");
    const double budget = r.num(mask::keys::Budget);
    if (budget > 0) {
        // The rungs themselves, so the size that is priced is the size that is
        // chosen. Pricing a halving sequence from the grid instead priced
        // sides the ladder does not have, and the snap below then rounded the
        // winner *up* to one that had never been costed: a 512 grid at
        // 1e8 work/s and --budget 1 priced 512^2 at 0.94s, took it, and
        // selected the 1024^2 rung the same formula prices at 4.19s.
        cap = kMaskSides[0];
        for (double rung : kMaskSides) {
            if (rung > grid) break;
            if (mask_seconds(r, method, rung, rung, kBudgetRefPairs) <= budget) cap = rung;
            else break;
        }
    }
    const double want = std::max(floor, std::min(grid, cap));

    // Up to the next rung, because the floors below it are minimums: rounding
    // down to the nearest would put the tile under the repetition limit that
    // asked for it.
    double side = kMaskSides[0];
    for (double rung : kMaskSides) { side = rung; if (rung >= want) break; }

    // And then whatever the sample can actually supply, which is a limit of a
    // different kind from the rest: max_size is a cost preference and the floor
    // may override it, but a generator whose tile *is* its sample cannot exceed
    // one however much repetition that costs. So this clamp comes last and beats
    // everything, including the ladder -- a size off the rungs is better than a
    // size that does not exist.
    //
    // Only when the size is being derived. A pinned --mask-size never reaches
    // this rule at all, so asking for a tile the sample has not got is still
    // refused rather than quietly shrunk: that is a statement about what the
    // user typed, and the build says so plainly.
    const Generator* gen = find_generator(method);
    if (gen && gen->sample_limit) {
        // Both axes, since one number now serves both and the smaller governs.
        const long long w = r.integer(mask::keys::SampleW);
        const long long h = r.integer(mask::keys::SampleH);
        if (w > 0) side = std::min(side, (double)gen->sample_limit(r, w));
        if (h > 0) side = std::min(side, (double)gen->sample_limit(r, h));

        // Deliberately *not* snapped back onto a rung. Rounding 938 down to 256
        // would throw away most of a tile the sample can perfectly well supply,
        // and buys nothing: the ladder exists so the size stops tracking the
        // *image*, and this limit tracks the sample. One sample gives one size
        // however many pictures it is used on, which is what the cache needs.
    }
    return (long long)std::llround(std::max(16.0, side));
}

void register_mask_params(params::Params& P)
{
    P.add({ mask::keys::SizeW,
            [](const params::Resolver& r) -> params::Value { return mask_side(r); },
            mask::any_generator(), {{8, 16384}}, "mask width" });
    P.add({ mask::keys::SizeH,
            [](const params::Resolver& r) -> params::Value { return mask_side(r); },
            mask::any_generator(), {{8, 16384}}, "mask height" });
    params::add_const(P, mask::keys::Scale, 1.0,
                      mask::from_sample(), {{0.05, 20.0}},
                      "feature size relative to the sample");
    // The generator's own default, from the table: see default_highpass.
    P.add({ mask::keys::Highpass,
            [](const params::Resolver& r) -> params::Value {
                const Generator* g = find_generator(r.str(mask::keys::Method));
                return g ? g->default_highpass : 32.0;
            },
            mask::from_sample(), {{0.0, 100000.0}},
            "ignore sample structure coarser than this, in pixels" });
    params::add_const(P, mask::keys::Sample, std::string(),
                      mask::from_sample(), {}, "the texture to imitate");
    // `mask` is a candidate rather than part of the rule: every mask parameter
    // already declares that it needs a mask being built, so naming one is enough
    // for reconcile() to work out that the threshold has to move. Listing them
    // here instead would restate those conditions in a second place, which is
    // the coupling the registry exists to remove. `random` is not a candidate —
    // nothing implies it, because nothing is configured by it.
    P.add({ mask::keys::Threshold,
            [](const params::Resolver&) -> params::Value { return std::string("round"); },
            {}, {}, "how each pixel picks its level",
            { std::string("mask") } });
    params::add_const(P, mask::keys::In, std::string(),
                      mask::thresholding(), {},
                      "use this image as the mask instead of building one");

    // In the registry rather than beside it in Options, because asking for the
    // mask file is asking for a mask. Outside, it implied nothing and conflicted
    // with nothing, so `--mask-out m.png` under the default round threshold
    // exited 0 having written no mask -- an option silently dropped, which is
    // the shape conflicts() exists to make impossible.
    //
    // Left out of the cache key, as intermediates are: it says where the mask
    // goes, not what it is. In the key, every run that saved its mask missed,
    // and samplecheck's cache cases -- each saving to a new name -- passed
    // without the cache ever answering.
    params::add_const(P, mask::keys::Out, std::string(),
                      mask::thresholding(), {},
                      "write the mask that was used to this file", true);

    // The deferred default that used to be written out in parseOptions. Naming a
    // sample means imitating it, and dft is the cheaper way to do that; naming
    // nothing means there is nothing to imitate, so build noise from scratch.
    // The candidates are every generator that takes a sample, from the table.
    // Setting a markov-only parameter — say `--set markov.temp=0.3` — is
    // asking for markov, and the
    // search finds that jointly with the threshold move it needs to be useful.
    // blue is not among them: it configures nothing the others do not,
    // so nothing can imply it.
    P.add({ mask::keys::Method,
            [](const params::Resolver& r) -> params::Value {
                return r.str(mask::keys::Sample).empty()
                           ? std::string(kDefaultGenerator)
                           : std::string(kDefaultSampleGenerator);
            },
            mask::any_generator(), {}, "which generator builds the mask",
            reconcile_candidates() });

    // Nominal, so `dither --explain` answers without an image; a real one
    // overrides these as Origin::Measured, which --explain then shows.
    // These four are intermediates: each shapes the mask only by deciding some
    // other parameter, and nothing reads them once that other parameter has an
    // answer. The picture's size and --supersample settle mask.size and have no
    // further say; --budget settles mask.size and markov.pairs and likewise.
    // Marking them is what stops the mask cache treating two images of
    // different sizes as needing two identical masks.
    params::add_const(P, mask::keys::ImageW, (long long)512,
                      {}, {{1, 1000000}}, "input width (nominal until an image is read)",
                      true);
    params::add_const(P, mask::keys::ImageH, (long long)512,
                      {}, {{1, 1000000}}, "input height (nominal until an image is read)",
                      true);
    params::add_const(P, mask::keys::Budget, 0.0,
                      {}, {{0.0, 86400.0}},
                      "seconds to spend building the mask; 0 uses fixed ceilings",
                      true);
    params::add_const(P, mask::keys::Supersample, (long long)1,
                      {}, {{1, 16}}, "dither at this multiple of the output resolution",
                      true);

    // 0 means "the input's size", which is what every run did before this
    // existed. Otherwise the longer edge becomes this, and with --supersample
    // the resample happens once, from the supersampled field.
    //
    // Intermediate: it scales the finished frames and reaches the mask by no
    // route at all, so it has no business in the cache key. Left in, changing
    // only --out-size rebuilt a byte-identical mask and stored a second copy.
    params::add_const(P, mask::keys::OutSize, (long long)0,
                      {}, {{0, 65536}},
                      "fit the output inside a square of this side; 0 keeps the input's size",
                      true);

    // The sample's own size, read off the file like image.width is read off the
    // picture, and 0 until it has been. Only `direct` is bounded by it, but the
    // keys are generic because being bounded by your sample is a property a
    // future generator could share. Intermediates: they reach the mask through
    // mask.size and nowhere else, and the sample's contents are hashed into the
    // cache key in any case, which covers its dimensions.
    params::add_const(P, mask::keys::SampleW, (long long)0,
                      mask::from_sample(), {{0, 1000000}},
                      "sample width (0 until the sample is read)", true);
    params::add_const(P, mask::keys::SampleH, (long long)0,
                      mask::from_sample(), {{0, 1000000}},
                      "sample height (0 until the sample is read)", true);

    // These four are read off the files, not chosen, so they get no flag --
    // a `--image-width` would be a lie, since the picture decides it and says
    // so a moment later. Everything else namespaced is an option by the one
    // rule, which is what makes the registry the single statement of what the
    // command line accepts.
    for (const char* k : {mask::keys::ImageW, mask::keys::ImageH,
                          mask::keys::SampleW, mask::keys::SampleH})
        P.hide_option(k);

    // Registered even though Options carries it and every builder is handed it
    // directly. Two things want it in the registry rather than beside it: it
    // shows in --explain alongside everything else that decides the output, and
    // the mask cache builds its key by walking the registry, so a value that is
    // not in there is a value the cache cannot see. It was the only input to a
    // mask that lived outside, which made it the one thing a registry-derived
    // key would silently miss.
    params::add_const(P, mask::keys::Seed, (long long)42,
                      {}, {{0, 9000000000000000LL}},
                      "random seed for masks and stochastic thresholds");
}

// Load the mask, or build it with whichever generator was chosen. Returns an
// empty Mat on failure, having already said why. Split out of run() because it
// is the whole of the generator dispatch and reads as one job: get me a mask.

// Build one mask with the chosen generator. Empty on failure, having said why.
// No branch on the method: which generator, whether it takes a sample and how
// it announces itself are all rows in the table.
// What the announcement says the sample is. One scan has a shape; several have
// a count and a size, since the set is no longer one image. A generator that
// cannot combine is described by the one scan it will actually use, because
// saying "4 scans, 26.8 Mpx" of a build that reads one of them is a lie.
static std::string describe(const samples::Set& set, bool combines)
{
    if (set.size() == 1 || !combines) {
        const cv::Mat& one = set.size() == 1 ? set[0] : set.primary();
        return cv::format("%dx%d", one.cols, one.rows);
    }
    return cv::format("%zu scans, %.1f Mpx", set.size(), (double)set.pixels() / 1e6);
}

static cv::Mat build_with_method(cv::Size maskSz, const samples::Set& set,
                                 const Options& opt, params::Params& P)
{
    const Generator* gen = find_generator(opt.maskMethod);

    // Validate and announce, then dispatch once. Two requests differing in one
    // field meant a second place to update whenever MaskRequest gained one.
    if (gen->needs_sample) {
        // Read by the caller, because the mask size is derived from it for any
        // generator whose tile comes out of the sample, and the size has to be
        // settled before anything is built. Reading it here as well would
        // decode a ten-megabyte scan twice for nothing.
        if (set.empty()) {
            // Say what is available rather than only what failed: the usual
            // cause is a film name that is nearly right, and the library knows
            // the spelling.
            fprintf(stderr, "Could not read sample %s\n", opt.maskSample.c_str());
            const std::vector<std::string> have = samples::library_names();
            if (!have.empty()) {
                fprintf(stderr, "  the sample library offers:");
                for (const std::string& n : have) fprintf(stderr, " %s", n.c_str());
                fprintf(stderr, "\n  (%s)\n", samples::library_root().c_str());
            } else if (!samples::library_root().empty()) {
                fprintf(stderr, "  a path, or a directory of scans of one film. "
                                "Put films in %s to name them directly.\n",
                        samples::library_root().c_str());
            }
            return {};
        }
        // Said out loud, because a run that quietly used a quarter of what it
        // was given is the shape of a silently dropped option. `combines` is
        // the generator's own declaration, so this needs no name here.
        if (!gen->combines && set.size() > 1)
            std::cerr << "  " << set.size() << " scans available; this generator takes one "
                         "tile from the sample rather than fitting a model to it, so "
                         "using the largest\n";

        // One announcement for every sample generator, since it says the same
        // thing about all of them.
        std::cerr << "Synthesising " << maskSz.width << "x" << maskSz.height
                  << " " << opt.maskMethod << " mask from " << opt.maskSample
                  << " (" << describe(set, gen->combines) << ")"
                  << " scale=" << opt.mask_scale << " highpass=" << opt.mask_highpass
                  << " …\n";
    }

    // Empty for a generator that takes none, which is the contract MaskRequest
    // states: the set is empty exactly when the generator needs none.
    static const samples::Set none;
    const samples::Set reference = gen->needs_sample
                                 ? samples::reference_for(opt.maskSample, set) : none;
    const MaskRequest q{maskSz, gen->needs_sample ? set : none, reference,
                        opt.seed, &std::cerr, opt.useSrgb, opt.levels};
    return gen->build(P, q);
}

// What the build cost, so the next --budget run is calibrated to this machine
// rather than to a compiled-in guess. Recorded even when no budget was asked
// for: the measurement is free, and it means the first run that does ask has a
// real number to work from. Too-short builds are ignored — the clock has nothing
// useful to say about them.
static void record_build_rate(const Options& opt, params::Params& P,
                              const cv::Mat& mask, double secs, const std::string& cfg)
{
    if (secs <= 0.25) return;
    // 0 asks each work formula to resolve its own pair count. Only markov has
    // one, so no other generator pulls that parameter into the graph — which
    // used to need a special case here naming markov.
    const params::Resolver r(P);
    const double work = find_generator(opt.maskMethod)->work(r, mask.cols, mask.rows, 0);
    P.set(opt.maskMethod + ".rate", work / secs, params::Origin::Measured);
    // Reported rather than discarded. Failing to record a measurement is not
    // worth stopping a finished run, but a --budget that keeps announcing it is
    // working from a guess, on a machine that has built a hundred masks, has a
    // cause the user cannot see otherwise.
    if (!cfg.empty() && !P.save_config(cfg, P.keys_ending(".rate")))
        fprintf(stderr, "Note: could not record this machine's build speed in %s; "
                        "--budget will keep working from a compiled-in guess\n",
                cfg.c_str());
}

// The mask this run will threshold against: loaded if --mask-in gave one, built
// otherwise, and timed if it was built.
cv::Mat obtain_mask(const Options& opt, const samples::Set& set,
                    params::Params& P, const std::string& cfg)
{
    if (!opt.maskIn.empty()) {
        cv::Mat mask = cv::imread(opt.maskIn, cv::IMREAD_GRAYSCALE);
        if (mask.empty()) {
            fprintf(stderr, "Could not read mask %s\n", opt.maskIn.c_str());
            return {};
        }
        std::cerr << "Loaded mask " << mask.cols << "x" << mask.rows
                  << " from " << opt.maskIn << "\n";
        return mask;
    }

    // Derived by mask_side(), or pinned by --mask-size.
    const cv::Size maskSz((int)P.integer(mask::keys::SizeW),
                          (int)P.integer(mask::keys::SizeH));

    // After maybe_calibrate(), which is why the cache lives here and not in
    // build_with_method: --mask-calibrate writes markov.temp into P, so a key
    // taken before it would be the key of a different mask.
    const std::string cacheDir = maskcache::dir_for(cfg);
    // Not built when there is nowhere to put it. key_for reads and hashes the
    // whole sample file and stats the executable, and under --no-config every
    // byte of that was for a value neither load nor store would look at.
    const maskcache::Key key = cacheDir.empty() ? maskcache::Key{}
                                                : maskcache::key_for(P, opt.maskSample,
                                                                    uses_crop(opt.maskMethod));
    if (cv::Mat hit = maskcache::load(cacheDir, key, &std::cerr); !hit.empty()) {
        // No rate is recorded from a hit. The clock measured a file read, and
        // writing that into <generator>.rate would tell the next --budget run
        // that this machine builds masks a thousand times faster than it does.
        if (hit.size() != maskSz) {
            fprintf(stderr, "Cached mask is %dx%d, expected %dx%d; rebuilding\n",
                    hit.cols, hit.rows, maskSz.width, maskSz.height);
        } else {
            return hit;
        }
    }

    const auto started = std::chrono::steady_clock::now();
    const cv::Mat mask = build_with_method(maskSz, set, opt, P);
    if (mask.empty()) return {};
    record_build_rate(opt, P, mask,
                      std::chrono::duration<double>(
                          std::chrono::steady_clock::now() - started).count(), cfg);
    maskcache::store(cacheDir, key, mask);
    return mask;
}



// A calibration for this sample, if the chosen generator has something to
// calibrate. Every build from a sample takes one already in the config --
// install_samples makes them for the shipped films -- and --mask-calibrate also
// measures one when there is none. Before --explain, so it reports the values
// the build will use, and before the build so the build uses them; never
// measures under --explain, which must stay fast. --no-config reads no config,
// so it is also how to get the rules' values regardless.
//
// One call, not a lookup and then a search: a cached answer can move a
// parameter that is itself in the calibration's key (markov.radius), so a
// second look would miss and measure again.
//
// Which parameter, and whether a sample even has to be read to decide, is the
// generator's business: this only routes. False stops the run.
bool maybe_calibrate(const Options& opt, params::Params& P, const std::string& cfg,
                     const samples::Set* read)
{
    if (opt.threshold != "mask" || !opt.maskIn.empty() || opt.maskSample.empty())
        return true;
    const Generator* gen = find_generator(opt.maskMethod);
    if (!gen || !gen->calibrate) return true;
    return gen->calibrate(P, opt.maskSample, opt.seed, cfg, std::cerr,
                          opt.calibrate && !opt.explain, read);
}
