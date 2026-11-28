// SPDX-License-Identifier: GPL-3.0-or-later
// markov.h — threshold masks synthesised from a sample's local statistics.
#pragma once
#include <optional>
#include <vector>
#include <cstdint>
#include <opencv2/core.hpp>
#include "defaults.h"
#include "maskparams.h"
#include "samples.h"
#include <iosfwd>
#include <memory>

// Parameter keys this generator owns. Named rather than spelled out at each use,
// so a typo is one compile error instead of a silent miss.
namespace markov::keys {
inline constexpr auto Rate    = "markov.rate";
inline constexpr auto Bins    = "markov.bins";
inline constexpr auto Radius  = "markov.radius";
inline constexpr auto Weights = "markov.weights";
inline constexpr auto Report  = "markov.report";
inline constexpr auto Pairs = "markov.pairs";
inline constexpr auto Reuse     = "markov.reuse";
inline constexpr auto Temp  = "markov.temp";
inline constexpr auto Iters = "markov.iters";
inline constexpr auto Bands = "markov.bands";
inline constexpr auto Shortlist = "markov.shortlist";
inline constexpr auto MaxSize   = "markov.max_size";
inline constexpr auto MaxRepeats = "markov.max_repeats";
}

// Register this generator's default rules. The rules live here, beside the code
// they configure and beside the measurements that justify them, so the defaults
// mechanism never has to know anything about Markov random fields.
void register_markov_params(params::Params&);

// Work units to build a w*h mask: one Gibbs cell update per pixel, per sweep,
// per pair. `pairs` is passed rather than resolved so this can be called while
// deciding the mask size, which is itself an input to choosing the pair count.
double markov_work(int w, int h, int iters, int pairs);

struct MarkovParams {
    double scale    = 1.0;   // feature size relative to the sample
    double highpass = 0.0;   // drop sample structure coarser than this, in px (0: none)
    int    radius   = 5;     // neighbourhood half-width the pairs are drawn from
    int    pairs    = 64;    // how many neighbour pairs condition each pixel
    int    shortlist = 40;   // offsets pairs may be drawn from; derived from
                             // `pairs`, then clamped to what the neighbourhood
                             // actually offers
    int    bins     = 16;    // quantisation of a conditioning neighbour
    int    iters    = 2;     // Gibbs sweeps; matches markov.iters
    double reuse    = 0.35;  // diversity penalty when the greedy pick reuses an
                             // offset it has already spent; see markov.reuse
    double temp     = 0.4;   // <1 sharpens the combined conditional, >1 softens
                             // it. Derived per run from bins, pairs and iters;
                             // this is the value the rule returns at its anchor
    // How each pair's vote is weighted: "flat" (1/K), "mi" (proportional to
    // mutual information), or "ml" (fitted by maximum likelihood on the sample).
    // Only the direction differs — the sum is always 1/temp. See markov.cpp.
    std::string weights = "flat";
    bool   report   = false; // print table occupancy and a held-out log-loss to
                             // stderr. Diagnostic only — never changes the mask
    int    bands    = 16;    // frequency bands each sweep's spectrum is matched
                             // to the sample's in (maskutil.h); 0 off
};

// Find the temperature whose synthesised field carries the same fraction of the
// sample's neighbour mutual information that the texture optimum does. Fits the
// model once and samples a small probe mask at half a dozen temperatures, which
// is affordable only because fitting dominates the cost and does not depend on
// the temperature. Returns p.temp unchanged if it cannot fit.
// What calibration decides: a temperature, and which way the pairs' votes are
// weighted. The two cannot be chosen separately -- the weighting moves the
// useful temperature range by a factor of three, so picking one at the other's
// default measures neither.
struct Calibration {
    double      temp;
    std::string weights;
    int         radius;
};

// What this sample's texture wants, or nothing if no probe could be drawn or no
// scheme found an answer inside its own search range.
//
// Empty is not the same as the derived default: it used to return that, which
// the caller then stored as a completed measurement, so a run where calibration
// did nothing was indistinguishable from one where it agreed with the rule --
// and the useless answer was cached against the sample for ever.
//
// The pinned flags say the user named that setting, in which case it is held
// and only the rest is searched. A value the user typed is not a default to be
// improved on.
std::optional<Calibration> calibrate_markov(const samples::Set& set, const samples::Set& reference,
                                            const MarkovParams& p,
                                            bool weights_pinned, bool radius_pinned,
                                            uint64_t seed, std::ostream& log);


// Knobs for build_markov_mask. `scale` and `highpass` mean what they mean for
// build_spectral_mask; the rest describe the model.

// Build a CV_8U threshold mask whose local statistics match `sample`'s, by
// Gibbs-sampling a pairwise Markov random field fitted to it. Each pixel is
// resampled from the combined verdict of a set of two-neighbour conditional
// distributions tabulated off the sample; the pairs are chosen by mutual
// information with the centre pixel. The result is toroidal, uniform by
// construction, and usable anywhere a void-and-cluster mask is.
//
// `log` may be null, which means say nothing -- the same contract every other
// generator's entry point honours. It took a caller-owned stream because the
// old signature had none and wrote to std::cerr regardless, so MaskRequest's
// optional log was optional for blue and inpaint and a fiction for markov.
cv::Mat build_markov_mask(cv::Size sz, const samples::Set& set,
                          const MarkovParams& p, uint64_t seed,
                          std::ostream* log,
                          const samples::Set* reference = nullptr);

// Fit and draw, stopping at the ordering. Empty if the fit failed, having said
// why. See maketexture.cpp for what the ordering is for.
std::vector<int> synthesise_markov_order(cv::Size sz, const cv::Mat& sample,
                                         const MarkovParams& p, uint64_t seed,
                                         std::ostream* log);
