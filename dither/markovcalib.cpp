// SPDX-License-Identifier: GPL-3.0-or-later
// --mask-calibrate: measure markov.temp for a particular sample instead of
// deriving it from the rule.
//
// The fit sees every scan of the film; the reference it is scored against is a
// crop of the *largest single* scan. That asymmetry is deliberate -- the model
// should learn from all the film there is, while the thing it is compared to
// has to be what a patch of real film looks like rather than a composite.
//
// Split from markov.cpp because it is a different kind of thing from the model
// it tunes -- a search over a scored objective, where markov.cpp is a model and
// a sampler -- and because it was a third of that file. It reaches the model
// through markovmodel.h, which is exactly fit-and-draw; it never looks inside.
//
// The clumping metric it scores against is calib.cpp's, shared with inpaint's
// calibrator; its divergence from test/levelfit is deliberate and measured --
// see calib::level_set_low.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numeric>
#include <ostream>
#include <vector>
#include <opencv2/opencv.hpp>
#include "markov.h"
#include "markovmodel.h"
#include "maskutil.h"
#include "calib.h"
#include "samples.h"
#include "markovcalib.h"
#include "maskparams.h"
#include "generators.h"

namespace {

// --- calibration -----------------------------------------------------------
//
// The temperature that produces the best texture cannot be found by fitting the
// sample: likelihood peaks near 0.21 while the texture optimum is 0.41, because
// the pool over-sharpens once the Gibbs chain feeds on its own samples.
//
// Nor can it be found by matching a fixed share of the sample's neighbour mutual
// information, which was the first design here. That share is a property of the
// texture, not a constant: at their own optima a film scan reads 0.63 and a
// coarser sample 0.79, so a single target misses badly on anything it was not
// derived from. See DEFAULTS.md.
//
// What does work is comparing the thing actually being judged. A threshold mask
// is used one level set at a time, so the question is whether the pattern shown
// at each density clumps like the sample's does — which is what `levelfit`
// measures, and what this reproduces in process.

constexpr int kCalibProbe = 128;                          // probe mask side

// The objective, and the reference it is measured against, are calib.cpp's --
// shared with inpaint's calibrator, which searches a different parameter and
// therefore scores a different quantity. markov keeps clumping alone: the
// temperature's defect *is* over- or under-clumping, and this search was
// validated against hand sweeps on that measure. Adding the ridge term here
// would move every markov mask for no measured reason.
using calib::ClumpProfile;
using calib::clump_profile;
using calib::clump_distance;


// Everything the sampler needs, and nothing that depends on the temperature or
// the mask size. Fitting scans the sample and costs ~1.25s where sampling a 64^2
// field costs 60ms, so keeping the two apart is what makes calibration — fit
// once, sample at half a dozen temperatures — affordable.

} // namespace

// A weighting scheme and the range of temperatures worth trying it over.
//
// The ranges differ because the schemes do, by a factor of three, and one
// bracket cannot serve both: `flat` measures its optimum between 0.28 and 0.43
// across the films here, `ml` between 0.06 and 0.16. Searching ml over flat's
// range finds flat's floor and calls it an answer, which is how ml came to look
// like a small improvement for as long as it did -- every comparison judged it
// at two to four times its own temperature.
//
// Both ends are measured rather than chosen: each range is the observed span
// with a rung of room either side, and a winner that still lands on an end is
// refused below rather than reported.
struct Scheme {
    const char* name;
    double      lo, hi;
    int         probes;
};
constexpr Scheme kSchemes[] = {
    {"flat", 0.10, 2.00, 10},
    {"mi",   0.05, 2.00, 10},
    {"ml",   0.03, 2.00, 10},
};

// Neighbourhood half-widths worth refitting for.
//
// Unlike the weighting and the temperature, this one changes the *model*: it
// decides which offsets the pairs may be drawn from, so each value needs its own
// fit rather than its own draw. Fits are cheap -- a radius-8 fit measures within
// 10% of a radius-3 one -- which is what makes searching it affordable at all.
//
// Three values, not a range. The parameter study found 3 the best choice for one
// film and 8 for another with 5 the general default, and nothing between them
// separated; a ladder with rungs where the evidence is beats a finer one with
// rungs nowhere.
constexpr int kRadii[] = {3, 5, 8};

std::optional<Calibration> calibrate_markov(const samples::Set& set, const samples::Set& reference,
                                            const MarkovParams& p,
                                            bool weights_pinned, bool radius_pinned,
                                            uint64_t seed, std::ostream& log)
{
    const cv::Size probe(kCalibProbe, kCalibProbe);
    // Scored against one image of the reference; synthesised against all of it.
    const cv::Mat ref = calib::reference_mask(reference.primary(), p.highpass, p.scale, probe);
    // Both of these are "could not measure", not "measured and agreed with the
    // rule". Returning p.temp made them indistinguishable from a result, so the
    // caller stored a non-answer against this sample and every later run
    // reported it as `from a previous calibration`.
    if (ref.empty()) {
        log << "  sample too small to calibrate against; keeping markov.temp "
            << p.temp << "\n";
        return std::nullopt;
    }
    // Once, before the draws: it does not change across them.
    const ClumpProfile refProfile = clump_profile(ref);

    // Draws per temperature, and it has to grow with the search.
    //
    // Each distance is an average over stochastic draws, so it carries noise;
    // taking the minimum over many candidates therefore picks partly on noise,
    // and the bias grows with the number of candidates. At three draws the
    // two-way search was sound and the nine-way one was not -- adding the radius
    // ladder made Kodak-4X-2 22% worse and AutoRex 13% worse on patchfit, which
    // looked like radius genuinely not helping them. It was not: at six draws
    // both come back better than they were, and every film measured improves.
    // The lesson is that a wider search needs a quieter measurement, not that
    // the extra axis was a bad idea.
    //
    // Set from the search actually being run, so pinning a weighting or a radius
    // buys back the time rather than paying for a precision it no longer needs.

    // Whichever schemes are in play. A weighting the user named is not a default
    // to improve on, so only its temperature is searched.
    std::vector<Scheme> schemes;
    for (const Scheme& sc : kSchemes)
        if (!weights_pinned || p.weights == sc.name) schemes.push_back(sc);
    if (schemes.empty()) schemes.push_back(kSchemes[0]);

    // And whichever radii. A radius the user named is held for the same reason a
    // weighting is.
    std::vector<int> radii;
    for (int r : kRadii)
        if (!radius_pinned || p.radius == r) radii.push_back(r);
    if (radii.empty()) radii.push_back(p.radius);   // a pinned value off the ladder

    const int kRealisations = (int)(schemes.size() * radii.size()) > 3 ? 6 : 3;

    struct Scored { const char* scheme; int radius; double temp, dist; };
    std::vector<Scored> winners;            // one per (radius, scheme) that has one

    for (int radius : radii) {
    // One model serves every scheme at this radius. The weights are a property
    // of the draw, not of the fit -- which is what makes searching them cost
    // draws rather than fits, and is why they had to stop living on the model
    // before any of this was possible. The radius is the opposite: it is part of
    // the fit, so it is the one axis here that pays for a refit per value.
    MarkovParams fp = p;
    fp.radius = radius;
    auto model = fit_markov_model(set, fp, probe, &reference);
    if (!model) {
        log << "  radius " << radius << ": no model could be fitted\n";
        continue;
    }

    for (const Scheme& sc : schemes) {
        // Spaced evenly in log temperature: the ranges span 20 to 33 times,
        // and an even step in temperature is coarse at the bottom of the range
        // and wasted at the top.
        const auto temp_at = [&sc](int i) {
            return sc.lo * std::pow(sc.hi / sc.lo, (double)i / (sc.probes - 1));
        };

        // The probes are independent: each draws from the same fitted model,
        // which nothing writes to, at its own temperature and its own seed.
        std::vector<double> draw((size_t)sc.probes * kRealisations, -1.0);
        cv::parallel_for_(cv::Range(0, sc.probes * kRealisations), [&](const cv::Range& range) {
            for (int task = range.start; task < range.end; task++) {
                const int i = task / kRealisations, r = task % kRealisations;
                MarkovParams pp = fp;      // a copy per task: the settings differ
                pp.temp = temp_at(i);
                pp.weights = sc.name;
                pp.report = false;         // the report describes the fit, not the probe
                const cv::Mat probeMask = sample_markov_mask(probe, *model, pp, seed + r, nullptr);
                if (!probeMask.empty())
                    draw[task] = clump_distance(clump_profile(probeMask), refProfile);
            }
        });

        // Averaged over the realisations. One draw is a single sample of a
        // stochastic field and the distance curve is steep near its minimum, so
        // a lone one puts the minimum in the wrong place often enough to matter.
        std::vector<Scored> scored;
        size_t bestAt = 0;
        for (int i = 0; i < sc.probes; i++) {
            double d = 0;
            int drawn = 0;
            for (int r = 0; r < kRealisations; r++)
                if (draw[(size_t)i * kRealisations + r] >= 0) {
                    d += draw[(size_t)i * kRealisations + r]; drawn++;
                }
            if (!drawn) continue;
            d /= drawn;
            // Strictly less than, so an exact tie keeps the lower temperature.
            if (scored.empty() || d < scored[bestAt].dist) bestAt = scored.size();
            scored.push_back({sc.name, radius, temp_at(i), d});
        }
        if (scored.empty()) continue;

        // A winner at either end is not a measurement: the objective was still
        // falling when the search ran out of room, so what comes back is where
        // the looking stopped. Such a scheme is dropped rather than compared --
        // its score is a lower bound on a curve that never turned over, and
        // letting it into the comparison would let it win on that.
        if (scored.size() > 1 && (bestAt == 0 || bestAt + 1 == scored.size())) {
            log << "  radius " << radius << ", " << sc.name
                << " weighting: best at the end of its search range ("
                << scored[bestAt].temp << " in [" << sc.lo << ", " << sc.hi
                << "]), so not measured\n";
            continue;
        }

        // Parabolic interpolation through the best point and its neighbours,
        // which costs nothing and takes the answer off the ladder's grid. Only
        // where the two neighbours are genuinely one step either side: a failed
        // draw removing a temperature would otherwise leave a parabola fitted
        // through points it does not describe.
        // In log temperature, where the ladder is even.
        Scored win = scored[bestAt];
        if (bestAt > 0 && bestAt + 1 < scored.size()) {
            const Scored& lo = scored[bestAt - 1];
            const Scored& hi = scored[bestAt + 1];
            const double denom = lo.dist - 2 * win.dist + hi.dist;
            const double stepLo = std::log(win.temp / lo.temp), stepHi = std::log(hi.temp / win.temp);
            const bool even = std::abs(stepLo - stepHi) <= 1e-9 * std::max(1.0, stepLo);
            if (even && std::abs(denom) > 1e-12) {
                const double shift = 0.5 * (lo.dist - hi.dist) / denom;
                if (std::abs(shift) <= 1.0) win.temp *= std::exp(shift * stepLo);
            }
        }
        winners.push_back(win);
        log << "  radius " << radius << ", " << sc.name << " weighting: best at "
            << win.temp << " (level-set distance " << win.dist << ")\n";
    }
    }

    if (winners.empty()) {
        log << "  no combination found an optimum inside its search range; keeping "
               "the derived markov.temp of " << p.temp << "\n";
        return std::nullopt;
    }

    // Comparable across schemes: same objective, same reference, same probe size.
    const Scored* best = &winners[0];
    for (const Scored& w : winners) if (w.dist < best->dist) best = &w;

    log << "  calibrated markov.temp to " << best->temp << " with "
        << best->scheme << " weighting and radius " << best->radius
        << " (level-set distance " << best->dist << ", best of " << winners.size()
        << " combination" << (winners.size() == 1 ? "" : "s") << " x "
        << kRealisations << " draws at " << kCalibProbe << "x" << kCalibProbe << ")\n";
    return Calibration{best->temp, best->scheme, best->radius};
}

// ---------------------------------------------------------------------------
// The calibration cache. This file registers no default rules -- markov.cpp
// does, and this banner used to say otherwise directly above a hash function.
// ---------------------------------------------------------------------------

// What this calibration is *of*: the sample, and every setting the fit or the
// sampler will read.
//
// Built from the resolved request rather than from the registry, so that the
// thing keyed and the thing executed are the same object. Assembling it
// separately is how it came to omit seed, shortlist and reuse -- all three are
// read by the pair pick or the Gibbs draw, so a run that changed one got
// another sample's answer.
//
// Dimensions are hashed with the pixels because bytes alone do not identify an
// image: the same sequence arranged 16x32 and 32x16 has entirely different
// neighbourhoods and was getting one key.
//
// mask.size is deliberately *not* here, and that exclusion is the point of the
// cache: the optimum was measured to transfer across mask size -- 0.41 at 64,
// 128 and 256 square -- so a probe on a cheap small mask answers for a large
// one. markov.temp is absent because it is the answer, and markov.report
// because it is documented inert.
static std::string calibration_key(const samples::Set& set, const samples::Set& reference,
                                   const MarkovParams& p,
                                   uint64_t seed, bool weightsPinned, bool radiusPinned)
{
    uint64_t h = 1469598103934665603ull;                 // FNV-1a
    auto mix = [&h](const void* data, size_t n) {
        const auto* b = static_cast<const unsigned char*>(data);
        for (size_t i = 0; i < n; i++) { h ^= b[i]; h *= 1099511628211ull; }
    };
    auto mix_i = [&mix](long long v) { mix(&v, sizeof v); };
    auto mix_d = [&mix](double v)    { mix(&v, sizeof v); };

    // Version first. The inputs below changed when the omissions were fixed,
    // so an entry written by the old scheme describes a different question and
    // must never answer this one.
    // Bumped whenever the objective, the search or the result schema changes:
    // an older entry answers a different question. calib/3 adds the pinned axes,
    // without which a search free to move the weighting shares a key with one
    // forbidden to.
    // calib/4: the fit sees every scan whole rather than a crop of a stack of
    // them, and the reference is a crop of one real scan, so an older entry
    // answers a different question.
    // calib/5: scored against the film's crop where it has one, and on the
    // level sets the dither shows rather than a cut through tied values.
    // calib/6: markov.bands, the spectrum matching's band count, joins the inputs.
    // calib/7: the whole reference set, which synthesis is held to, not only
    // its largest image.
    const char* kVersion = "calib/7";
    mix(kVersion, std::strlen(kVersion));
    calib::mix_set(h, reference);

    // Every scan, with the count mixed in -- content alone cannot tell one
    // grouping of the same bytes from another, which is the mask cache's
    // argument and holds here for the same reason.
    mix_i((long long)set.size());
    for (const cv::Mat& s : set) {
        mix_i(s.rows); mix_i(s.cols); mix_i(s.type());
        const cv::Mat flat = s.isContinuous() ? s : s.clone();
        mix(flat.data, flat.total() * flat.elemSize());
    }

    mix_i(p.bins); mix_i(p.pairs); mix_i(p.shortlist); mix_i(p.radius);
    mix_i(p.iters);
    mix_d(p.scale); mix_d(p.highpass); mix_d(p.reuse);
    mix(p.weights.data(), p.weights.size());
    mix_i((long long)seed);
    mix_i(weightsPinned); mix_i(radiusPinned);
    mix_i(p.bands);

    char buf[17];
    std::snprintf(buf, sizeof buf, "%016llx", (unsigned long long)h);
    return buf;
}

// Writes the result in as Origin::Measured, which is what a later run's
// --explain reports it as once the cache supplies it; this run's --explain has
// already returned. Cached against everything that changes the answer — but not the
// mask size, since the optimum was measured to transfer across it, which is
// what makes probing on a small mask legitimate.
// A cached answer is three values or it is nothing. Returning two of them and
// leaving the third at whatever the rule said would silently mix a temperature
// fitted for one weighting with another, so an incomplete or out-of-range
// entry is a miss -- the cache is disposable and a rebuild is always correct.
static std::optional<Calibration> cached_calibration(
    params::Params& P, const std::string& key, const std::string& wkey,
    const std::string& rkey, const MarkovParams& mp,
    bool weightsPinned, bool radiusPinned)
{
    const auto t = P.cached(key);
    if (!t || !calib::usable(P, markov::keys::Temp, *t, false)) return std::nullopt;

    Calibration c;
    c.temp = *t;

    // A pinned axis was not searched, so there is nothing cached for it and the
    // pinned value is the answer.
    if (weightsPinned) c.weights = mp.weights;
    else {
        const auto wi = P.cached(wkey);
        if (!wi || !std::isfinite(*wi) || std::abs(*wi - std::round(*wi)) > 1e-9)
            return std::nullopt;
        const long long i = (long long)std::round(*wi);
        if (i < 0 || i >= (long long)std::size(kSchemes)) return std::nullopt;
        c.weights = kSchemes[i].name;
    }

    if (radiusPinned) c.radius = mp.radius;
    else {
        // Membership of the searched ladder, not merely its span: a radius the
        // search never offers cannot be something the search returned.
        const auto ri = P.cached(rkey);
        if (!ri || !calib::usable(P, markov::keys::Radius, *ri, true)) return std::nullopt;
        const long long r = (long long)std::round(*ri);
        if (std::find(std::begin(kRadii), std::end(kRadii), (int)r) == std::end(kRadii))
            return std::nullopt;
        c.radius = (int)r;
    }
    return c;
}

// Pinned axes are never written: the user's value outranks a measurement, and
// the search was told not to move them.
static void apply(params::Params& P, const Calibration& c,
                  bool weightsPinned, bool radiusPinned)
{
    P.set(markov::keys::Temp, c.temp, params::Origin::Measured);
    if (!weightsPinned) P.set(markov::keys::Weights, c.weights, params::Origin::Measured);
    if (!radiusPinned)  P.set(markov::keys::Radius, (long long)c.radius,
                              params::Origin::Measured);
}

bool calibrate_markov_for(params::Params& P, const std::string& sample_path,
                          uint64_t seed, const std::string& cfg, std::ostream& log,
                          bool measure, const samples::Set* read)
{
    if (P.user_set(markov::keys::Temp)) {
        if (!measure) return true;
        // A command-line --markov-temp outranks a measurement. Said out loud
        // rather than skipped quietly, because a flag that does nothing should
        // not look like a flag that worked.
        // Still measures the weighting if that was left open: pinning one of
        // the two is not a reason to stop deciding the other.
        // The search scores a temperature against the sample's clumping, so a
        // pinned temperature leaves it nothing to score and the weighting and
        // radius are not searched either. Say that, rather than promising work
        // that does not happen.
        fprintf(stderr, "Note: --markov-temp %g was given, so --mask-calibrate is skipped "
                        "entirely -- the search scores temperatures, and the weighting and "
                        "radius are chosen alongside one. Drop it to measure all three\n",
                P.num(markov::keys::Temp));
        return true;
    }
    const auto in = calib::inputs_for(sample_path, uses_crop("markov"), read);
    if (!in) {
        if (!measure) return true;           // the build reports it, with the path
        fprintf(stderr, "Could not read sample %s\n", sample_path.c_str());
        return false;
    }
    const samples::Set& set = in->set;
    // Resolved once, then both keyed and executed -- the two cannot describe
    // different requests if there is only one request.
    const MarkovParams mp = markov_params_from(P);
    const bool weightsPinned = P.user_set(markov::keys::Weights);
    const bool radiusPinned  = P.user_set(markov::keys::Radius);
    const std::string key = "calib."
        + calibration_key(set, in->reference, mp, seed, weightsPinned, radiusPinned);

    // Two entries, because the answer is two values and the store holds numbers.
    // The scheme is kept by index into kSchemes rather than by name; a name
    // would need the store to hold strings for this one use.
    const std::string wkey = key + ".w", rkey = key + ".r";
    if (auto c = cached_calibration(P, key, wkey, rkey, mp, weightsPinned, radiusPinned)) {
        apply(P, *c, weightsPinned, radiusPinned);
        log << "  markov.temp " << c->temp << " with " << c->weights
            << " weighting and radius " << c->radius
            << ", from a previous calibration\n";
        return true;
    }

    if (!measure) return true;
    log << "  calibrating against " << in->described << "\n";
    const std::optional<Calibration> c =
        calibrate_markov(set, in->reference, mp, weightsPinned, radiusPinned, seed, log);
    if (!c) {
        log << "  keeping the derived markov.temp of " << P.num(markov::keys::Temp)
            << ", " << mp.weights << " weighting and radius " << mp.radius << "\n";
        return true;       // the rule's answer stands, and is not cached as measured
    }
    apply(P, *c, weightsPinned, radiusPinned);
    P.remember(key, c->temp);
    if (!weightsPinned) {
        for (size_t i = 0; i < std::size(kSchemes); i++)
            if (c->weights == kSchemes[i].name) P.remember(wkey, (double)i);
    }
    if (!radiusPinned) {
        P.remember(rkey, (double)c->radius);
    }
    if (!cfg.empty() && !P.save_config(cfg, P.keys_ending(".rate")))
        log << "Note: could not save the calibration to " << cfg
            << "; the next run on this sample will measure it again\n";
    return true;
}
