// SPDX-License-Identifier: GPL-3.0-or-later
// --mask-calibrate for dft: measure `dft.smooth_cells` for a particular sample.
//
// **On a full scan this finds nothing, and that is the correct answer rather
// than a gap.** `dft.smooth_cells` is not the smoothing: it is a *coefficient*,
// and `smooth_power` divides it by the square root of the sample-to-mask area
// ratio and floors the result at one mask cell. So on a 2.9-megapixel sample
// against a 256² mask the divisor is about 6.6, and coefficients 1, 2 and 4 all
// land on the floor and produce **the same mask** — three of this ladder's five
// rungs are one point. The search duly reads 0.0473 / 0.0473 / 0.0473 / 0.0472 /
// 0.0468 on Kodak-4X-1 and declines, saying so.
//
// It earns its place on the other end of that rule. The area term is a
// measurement extrapolated, not a law: it was fitted on samples from 1/4 to
// 326 times the mask's area, and a sample near the mask's own size is where
// the periodogram is noisiest and imposing it copies one realisation's speckle.
// There the divisor is near 1, the rungs separate, and the search finds a real
// interior optimum: on a 128² crop of Kodak-4X-1 it reads 0.1174 / **0.0769** /
// 0.0943 / 0.1386 / 0.1991 and takes 2, 18% better than the 4 the rule would
// have used.
//
// Which is why the probe size follows the sample rather than being fixed at
// 256: insisting on a large probe would refuse exactly the samples this can
// help.
//
// **The objective is spectral and nothing else.** dft randomises phase, so a
// clumping or ridging score of a dft mask measures its noise rather than its
// fit — which is the same reason CLAUDE.md gives for dft scoring 0.385 on
// level-set fit while owning the spectrum. The two other terms calib.h offers
// would be pure noise here.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ostream>
#include <opencv2/opencv.hpp>

#include "calib.h"
#include "samples.h"
#include "generators.h"
#include "maskparams.h"
#include "spectralmask.h"

namespace {

// Largest probe worth building, but the sample decides: this calibration can
// only find anything on a *small* sample, so insisting on 256 would refuse
// exactly the case it exists for. The reference is a centre crop, so the probe
// cannot exceed what the prepared sample can supply.
constexpr int kMaxProbe = 256;
constexpr int kMinProbe = 64;

// The ladder is in *effective* smoothing -- mask cells, after the area rule --
// and turned into the coefficient that produces it on this sample at this
// probe. It used to be a ladder of coefficients, {1, 2, 4, 8, 16}, which the
// rule divides by sqrt(sample area / mask area) and floors at one cell: on a
// 1024 crop against a 256 probe that put 1, 2 and 4 all on the floor, three
// rungs of five measuring one point, and on a whole scan all five. Stepping in
// the quantity the build actually uses makes every rung a different mask.
constexpr double kEffective[] = {1.0, 1.5, 2.0, 3.0, 4.0, 6.0, 8.0};

} // namespace

static std::string calibration_key(const samples::Set& set, const samples::Set& reference,
                                   double scale, double highpass, int iters, uint64_t seed)
{
    uint64_t h = 1469598103934665603ull;                 // FNV-1a
    auto mix = [&h](const void* data, size_t n) {
        const auto* b = static_cast<const unsigned char*>(data);
        for (size_t i = 0; i < n; i++) { h ^= b[i]; h *= 1099511628211ull; }
    };
    auto mix_i = [&mix](long long v) { mix(&v, sizeof v); };
    auto mix_d = [&mix](double v)    { mix(&v, sizeof v); };

    // Its own version prefix, distinct from markov's "calib/" and inpaint's
    // "icalib/": three calibrations answer three different questions about one
    // sample and must not be able to collide in one store.
    // /4: the film's crop, as the build takes it, and the dither's level sets.
    // /5: hashed as the whole reference set, as the other two now are.
    const char* kVersion = "dcalib/5";
    mix(kVersion, std::strlen(kVersion));
    calib::mix_set(h, reference);

    // Every scan, with the count mixed in -- content alone cannot tell one
    // grouping of the same bytes from another.
    mix_i((long long)set.size());
    for (const cv::Mat& s : set) {
        mix_i(s.rows); mix_i(s.cols); mix_i(s.type());
        const cv::Mat flat = s.isContinuous() ? s : s.clone();
        mix(flat.data, flat.total() * flat.elemSize());
    }

    mix_d(scale); mix_d(highpass); mix_i(iters); mix_i((long long)seed);

    char buf[17];
    std::snprintf(buf, sizeof buf, "%016llx", (unsigned long long)h);
    return buf;
}

bool calibrate_dft_for(params::Params& P, const std::string& sample_path,
                       uint64_t seed, const std::string& cfg, std::ostream& log,
                       bool measure, const samples::Set* read)
{
    using namespace spectralmask::keys;

    if (P.user_set(SmoothCells)) {
        if (!measure) return true;
        std::fprintf(stderr, "Note: --dft-smooth-cells %g was given, so "
                             "--mask-calibrate has nothing to decide for this "
                             "generator\n", P.num(SmoothCells));
        return true;
    }
    const auto in = calib::inputs_for(sample_path, uses_crop("dft"), read);
    if (!in) {
        if (!measure) return true;           // the build reports it, with the path
        std::fprintf(stderr, "Could not read sample %s\n", sample_path.c_str());
        return false;
    }

    const samples::Set& set = in->set;
    const double scale    = P.num(mask::keys::Scale);
    const double highpass = P.num(mask::keys::Highpass);
    const int    iters    = (int)P.integer(Iters);
    const double derived  = P.num(SmoothCells);

    const std::string key = "dcalib." + calibration_key(set, in->reference, scale,
                                                        highpass, iters, seed);
    // A corrupt entry is a miss; see inpaintcalib for why that beats throwing.
    if (auto hit = P.cached(key);
        hit && calib::usable(P, SmoothCells, *hit, false)
        ) {
        P.set(SmoothCells, *hit, params::Origin::Measured);
        log << "  dft.smooth_cells " << *hit << ", from a previous calibration\n";
        return true;
    }
    if (!measure) return true;
    log << "  calibrating against " << in->described << "\n";

    int side = kMaxProbe;
    cv::Mat ref;
    while (side >= kMinProbe) {
        ref = calib::reference_mask(in->scoring, highpass, scale, cv::Size(side, side));
        if (!ref.empty()) break;
        side /= 2;
    }
    if (ref.empty()) {
        log << "  sample is smaller than the " << kMinProbe << "x" << kMinProbe
            << " probe, so there is nothing to compare against; keeping "
               "dft.smooth_cells " << derived << "\n";
        return true;
    }
    const cv::Size probe(side, side);
    // Below the high-pass cutoff the sample was emptied, so there is nothing
    // there for either field to match. Two things have to match preprocessing:
    // prepare_texture_sample only filters above 1 px, and it resizes by scale
    // afterwards -- so the cutoff is in mask pixels, not sample pixels.
    const double lowCut = highpass > 1.0 ? 1.0 / (highpass * scale) : 0.0;
    const calib::SpectrumProfile refSpec = calib::spectrum_profile(ref, lowCut);

    // The coefficient each effective width needs here. smooth_power divides by
    // sqrt(r) for the sample's area over the mask's, per scan; the largest scan
    // stands for the set, which for a crop is the only one.
    const cv::Mat& big = set.primary();
    const double r = std::max(1.0, (double)big.rows * big.cols * scale * scale
                                   / ((double)side * side));
    constexpr size_t n = std::size(kEffective);
    std::array<double, n> kLadder{};
    for (size_t i = 0; i < n; i++) kLadder[i] = kEffective[i] * std::sqrt(r);
    std::array<double, n> score{};
    std::array<bool, n> ok{};
    int best = -1, fallback = -1;
    double nearest = 0;
    for (size_t i = 0; i < n; i++) {
        const cv::Mat m = build_spectral_mask(probe, set, scale, highpass,
                                              iters, kLadder[i], seed);
        if (m.empty()) continue;
        score[i] = calib::spectrum_distance(calib::spectrum_profile(m, lowCut), refSpec);
        if (!std::isfinite(score[i])) continue;      // nothing in the band to compare
        ok[i] = true;
        if (best < 0 || score[i] < score[best]) best = (int)i;
        // The derived value is a continuous rule and the ladder is discrete, so
        // the rung to beat is the closest one rather than an exact match.
        if (fallback < 0 || std::abs(kLadder[i] - derived) < nearest) {
            fallback = (int)i; nearest = std::abs(kLadder[i] - derived);
        }
    }
    if (best < 0) {
        log << "  dft.smooth_cells stays at " << derived
            << "; nothing above the high-pass cutoff to compare"
               " (--mask-highpass " << highpass << " leaves no band)\n";
        return true;
    }

    // The same 10% margin inpaint's search uses, and for a sharper reason here:
    // the rule this would override already has a sample term, so a search that
    // moves it on noise is strictly worse than leaving it alone.
    constexpr double kMargin = 0.10;
    const bool decisive = fallback >= 0 &&
                          score[best] <= score[fallback] * (1.0 - kMargin);

    auto trace = [&](std::ostream& o) {
        for (size_t i = 0; i < n; i++)
            if (ok[i]) o << (i ? " " : "") << kEffective[i] << ":"
                         << cv::format("%.4f", score[i]);
    };

    if (!decisive) {
        // Cached as the answer, as inpaint's is: it was measured.
        log << "  dft.smooth_cells stays at " << derived
            << "; the sample's spectrum is estimated well enough that smoothing "
               "it differently changes nothing (";
        trace(log);
        log << ")\n";
        P.remember(key, derived);
        if (!cfg.empty() && !P.save_config(cfg, P.keys_ending(".rate")))
            log << "Note: could not save the calibration to " << cfg << "\n";
        return true;
    }

    P.set(SmoothCells, kLadder[best], params::Origin::Measured);
    P.remember(key, kLadder[best]);
    log << "  dft.smooth_cells " << kLadder[best] << " (" << kEffective[best]
        << " cells at the probe), measured for this sample (";
    trace(log);
    log << ")\n";

    if (!cfg.empty() && !P.save_config(cfg, P.keys_ending(".rate")))
        log << "Note: could not save the calibration to " << cfg
            << "; the next run on this sample will measure it again\n";
    return true;
}
