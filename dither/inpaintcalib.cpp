// SPDX-License-Identifier: GPL-3.0-or-later
// --mask-calibrate for inpaint: measure `inpaint.iters` for a particular sample
// instead of taking the default.
//
// **Why this parameter and no other.** Sweeping worms — see inpaint.cpp's
// header for the measurement — so the sweep count buys clumping and pays in
// ridging, and the exchange rate is a property of the film. Across seven scans
// the best value is 2, 3 or 6 depending on the sample, and where fewer sweeps
// win they win by 31–68% where more win by 6–31%. Nothing derives it: unlike
// `markov.temp` there is not even a rule with the wrong terms in it, because
// the parameter was believed to be a convergence count with a single right
// answer.
//
// **Why the objective is not markov's.** markovcalib scores clumping alone,
// which is right for a temperature — over- or under-clumping *is* the
// temperature's defect. It is wrong here. CLAUDE.md says of test/levelfit that
// it "asks how much each level set clumps and not whether the clumps are round
// or stringy", and stringy is exactly what this parameter controls. Scored on
// clumping alone the search picks 6 sweeps on AutoRex where the texture wants
// 2 — patchfit 13.36 against 9.26, the single worst call available on that film.
//
// So the objective is clumping *and* ridging, and the two are combined after
// normalising each by its own mean over the ladder. That is not fastidiousness:
// the raw numbers are log-ratios of quantities with different spreads, and on
// AutoRex clumping ranges over 0.39–0.44 while ridging ranges over 0.09–0.15,
// so an unweighted sum is a clumping-only search wearing a second term. Against
// test/patchfit over five films the combined objective agrees exactly on four
// and is one rung out on the fifth; clumping alone agrees on four and is three
// rungs out on AutoRex; ridging alone agrees on three.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ostream>
#include <vector>
#include <opencv2/opencv.hpp>

#include "calib.h"
#include "samples.h"
#include "generators.h"
#include "inpaint.h"
#include "maskparams.h"

namespace {

// Whether the answer transfers from this probe to a larger requested mask is
// measured for markov (0.41 at 64, 128 and 256 square) and assumed here. What
// makes the assumption tolerable is that the answer is one rung of a four-rung
// ladder, so a small drift does not change it.
constexpr int kProbe = 256;

// Where the evidence is. 1 was measured worse than 2 on every film in the
// parameter study (its screening mean reads 1.215 against 2's 1.087), and above
// 6 every film measured worse than at 6, so the ladder spans the useful range
// with a rung at each value that actually won on something.
constexpr int kLadder[] = {2, 3, 4, 6};

} // namespace

// What this calibration is *of*: the sample, and every setting the build will
// read except the one being answered.
//
// `iters` is absent because it is the answer. The mask size is absent for the
// same reason as markov's — the probe is small and the answer is applied to
// whatever size is asked for — and unlike markov's that is an assumption rather
// than a measurement, recorded above.
static std::string calibration_key(const samples::Set& set, const samples::Set& reference,
                                   const InpaintParams& p, uint64_t seed)
{
    uint64_t h = 1469598103934665603ull;                 // FNV-1a
    auto mix = [&h](const void* data, size_t n) {
        const auto* b = static_cast<const unsigned char*>(data);
        for (size_t i = 0; i < n; i++) { h ^= b[i]; h *= 1099511628211ull; }
    };
    auto mix_i = [&mix](long long v) { mix(&v, sizeof v); };
    auto mix_d = [&mix](double v)    { mix(&v, sizeof v); };

    // Its own version prefix, distinct from markov's "calib/" and dft's
    // "dcalib/". The calibrations answer different questions about the same
    // sample and must not be able to collide in one store.
    // Moves whenever the question does: /2 added inpaint.cap, which decides
    // when a candidate stops sweeping; /3 is the local ridging objective.
    // /5: scored against the film's crop where it has one, and on the level
    // sets the dither shows.
    // /6: inpaint.bands, the spectrum matching's band count, joins the inputs.
    // /7: the whole reference set, which synthesis is held to.
    const char* kVersion = "icalib/7";
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

    mix_i(p.radius); mix_i(p.db); mix_i(p.dims); mix_i(p.checks);
    mix_i(p.groups); mix_i(p.scales);
    mix_d(p.scale); mix_d(p.highpass); mix_d(p.cap);
    mix_i(p.bands);
    mix_i((long long)seed);

    char buf[17];
    std::snprintf(buf, sizeof buf, "%016llx", (unsigned long long)h);
    return buf;
}

bool calibrate_inpaint_for(params::Params& P, const std::string& sample_path,
                           uint64_t seed, const std::string& cfg, std::ostream& log,
                           bool measure, const samples::Set* read)
{
    if (P.user_set(inpaint::keys::Iters)) {
        if (!measure) return true;
        // A typed value outranks a measurement, and says so: a flag that does
        // nothing must not look like a flag that worked. Same reasoning as
        // markov's, and the same trap — the test has to be `user_set` and not
        // `measured`, since a cached calibration loads as Origin::Measured and
        // would otherwise pin itself for ever.
        std::fprintf(stderr, "Note: --inpaint-iters %lld was given, so --mask-calibrate "
                             "has nothing to decide for this generator\n",
                     P.integer(inpaint::keys::Iters));
        return true;
    }
    const auto in = calib::inputs_for(sample_path, uses_crop("inpaint"), read);
    if (!in) {
        if (!measure) return true;           // the build reports it, with the path
        std::fprintf(stderr, "Could not read sample %s\n", sample_path.c_str());
        return false;
    }

    const samples::Set& set = in->set;
    InpaintParams ip = inpaint_params_from(P);
    const std::string key = "icalib." + calibration_key(set, in->reference, ip, seed);
    // Out of range or not a whole number means a corrupt entry, and the cache
    // is disposable: treat it as a miss rather than letting P.set throw and
    // take the run down with it.
    if (auto hit = P.cached(key);
        hit && calib::usable(P, inpaint::keys::Iters, *hit, true)) {
        const long long v = (long long)std::round(*hit);
        if (std::find(std::begin(kLadder), std::end(kLadder), (int)v) != std::end(kLadder)) {
            P.set(inpaint::keys::Iters, v, params::Origin::Measured);
            log << "  inpaint.iters " << v << ", from a previous calibration\n";
            return true;
        }
    }
    if (!measure) return true;
    log << "  calibrating against " << in->described << "\n";

    const cv::Size probe(kProbe, kProbe);
    const cv::Mat ref = calib::reference_mask(in->scoring, ip.highpass, ip.scale, probe);
    if (ref.empty()) {
        log << "  sample is smaller than the " << kProbe << "x" << kProbe
            << " probe, so there is nothing to compare against; keeping "
               "inpaint.iters " << ip.iters << "\n";
        return true;            // not cached: no answer was reached
    }
    const calib::ClumpProfile refClump = calib::clump_profile(ref);
    const calib::RidgeProfile refRidge = calib::ridge_profile(ref);

    // One draw per rung. Each is independent of the others -- a different
    // `iters` is a different build from the same seed, not a continuation --
    // so they could be run in parallel; they are not, because the search is
    // four builds and each is already internally threaded, and nesting
    // parallel_for_ around the k-d tree search would fight the concurrency cap
    // that search deliberately imposes on itself.
    constexpr size_t n = std::size(kLadder);
    std::array<double, n> clump{}, ridge{};
    std::array<bool, n> ok{};
    for (size_t i = 0; i < n; i++) {
        InpaintParams probe_p = ip;
        probe_p.iters = kLadder[i];
        // Held to the same reference set the build holds its mask to.
        const cv::Mat m = build_inpaint_mask(probe, set, probe_p, seed, nullptr, &in->reference);
        if (m.empty()) continue;
        clump[i] = calib::clump_distance(calib::clump_profile(m), refClump);
        ridge[i] = calib::ridge_distance(calib::ridge_profile(m), refRidge);
        if (!std::isfinite(clump[i]) || !std::isfinite(ridge[i])) continue;
        ok[i] = true;
    }

    // Normalise each term by its own mean over the rungs, then add. See the
    // header: the two are log-ratios of quantities with different spreads, so
    // an unweighted sum silently becomes a clumping-only search.
    double cs = 0, rs = 0;
    int used = 0;
    for (size_t i = 0; i < n; i++)
        if (ok[i]) { cs += clump[i]; rs += ridge[i]; used++; }
    if (!used) {
        log << "  no probe mask could be built, so inpaint.iters stays at "
            << ip.iters << "\n";
        return true;
    }
    const double cm = cs / used, rm = rs / used;

    std::array<double, n> score{};
    int best = -1, fallback = -1;
    for (size_t i = 0; i < n; i++) {
        if (!ok[i]) continue;
        score[i] = (cm > 0 ? clump[i] / cm : 0) + (rm > 0 ? ridge[i] / rm : 0);
        if (best < 0 || score[i] < score[best]) best = (int)i;
        if (kLadder[i] == ip.iters) fallback = (int)i;
    }

    // **A calibration that can make a sample worse than not calibrating is not
    // worth having at any mean**, so the default stands unless the search beats
    // it by a margin. The objective is two terms where test/patchfit, which it
    // stands in for, is eleven; it ranks well but is not trustworthy at close
    // range, and the margin is what keeps a near-tie from moving anything.
    //
    // The figures that chose 10% were measured under the previous, whole-field
    // ridging objective and are not evidence for this one -- the margin is kept
    // as a guard whose value wants re-measuring, not as a fitted constant.
    constexpr double kMargin = 0.10;
    const bool decisive = best >= 0 && fallback >= 0 &&
                          score[best] <= score[fallback] * (1.0 - kMargin);

    auto trace = [&](std::ostream& o) {
        for (size_t i = 0; i < n; i++)
            if (ok[i]) o << (i ? " " : "") << kLadder[i] << ":"
                         << cv::format("%.3f", score[i]);
    };

    if (!decisive) {
        // Cached all the same: "nothing beats the default by the margin" is a
        // measured answer, and storing it is what lets a later run -- or
        // install_samples' -- know this film has been calibrated rather than
        // measure it again. A better objective moves the key's version, so
        // this cannot outlive the question it answers.
        log << "  inpaint.iters stays at " << ip.iters
            << "; no sweep count was better by the 10% this needs to overrule the "
               "default (";
        trace(log);
        log << ")\n";
        P.remember(key, (double)ip.iters);
        if (!cfg.empty() && !P.save_config(cfg, P.keys_ending(".rate")))
            log << "Note: could not save the calibration to " << cfg << "\n";
        return true;
    }

    // A winner at an end of the ladder is accepted, unlike markov's temperature,
    // and the difference is that this ladder has measured walls rather than
    // chosen ones: 1 sweep is worse than 2 on every film tried, and more than 6
    // is worse than 6 on every film tried. An end here is an optimum, not a
    // search that ran out of room.
    P.set(inpaint::keys::Iters, (long long)kLadder[best], params::Origin::Measured);
    P.remember(key, (double)kLadder[best]);
    log << "  inpaint.iters " << kLadder[best] << ", measured for this sample (";
    trace(log);
    log << ")\n";

    if (!cfg.empty() && !P.save_config(cfg, P.keys_ending(".rate")))
        log << "Note: could not save the calibration to " << cfg
            << "; the next run on this sample will measure it again\n";
    return true;
}
