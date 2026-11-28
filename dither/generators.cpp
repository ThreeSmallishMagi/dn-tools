// SPDX-License-Identifier: GPL-3.0-or-later
// The generator table. This is the only file that knows all five generators
// exist; each of them knows about none of the others, and dither.cpp now names
// none of them.
#include <cstdio>
#include <ostream>
#include "generators.h"
#include "bluenoise.h"
#include "spectralmask.h"
#include "markov.h"
#include "inpaint.h"
#include "inpaintcalib.h"
#include "dftcalib.h"
#include "directmask.h"
#include "maskparams.h"
#include "markovcalib.h"

namespace {

// --- blue ------------------------------------------------------------------

double blue_work_of(const params::Resolver& r, int w, int h, int)
{
    return blue_work(w, h, r.num(bluenoise::keys::Sigma));
}

cv::Mat blue_build(params::Params& P, const MaskRequest& q)
{
    const double sigma  = P.num(bluenoise::keys::Sigma);
    const double eps    = P.num(bluenoise::keys::Eps);
    const double radius = P.num(bluenoise::keys::Radius);
    // Blue announces itself: its line names its own parameters, where the three
    // sample generators share one line that the caller prints before dispatch.
    //
    // Guarded, because a null log means "say nothing" and this one used to
    // dereference it -- which made blue the only generator a test could not
    // call without a stream to throw away. Found by doing exactly that.
    //
    // The comment here used to claim every other row already honoured that
    // contract. It did not: markov_build dropped q.log and build_markov_mask
    // wrote to std::cerr whatever the caller asked. It threads the stream now,
    // so the contract is one the table actually keeps.
    if (q.log)
        *q.log << "Building progressive mask " << q.size.width << "x" << q.size.height
               << " sigma=" << sigma << " eps=" << eps
               << " radius=" << radius
               << " srgb=" << q.srgb << " levels=" << q.levels << " …\n";
    return build_blue_noise_mask(q.size, sigma, eps, radius, q.seed, q.log);
}

// --- dft -------------------------------------------------------------------

double dft_work_of(const params::Resolver& r, int w, int h, int)
{
    return dft_work(w, h, (int)r.integer(spectralmask::keys::Iters));
}

cv::Mat dft_build(params::Params& P, const MaskRequest& q)
{
    return build_spectral_mask(q.size, q.samples,
                               P.num(mask::keys::Scale),
                               P.num(mask::keys::Highpass),
                               (int)P.integer(spectralmask::keys::Iters),
                               P.num(spectralmask::keys::SmoothCells),
                               q.seed);
}

// --- markov ----------------------------------------------------------------

double markov_work_of(const params::Resolver& r, int w, int h, int pairs)
{
    // 0 means the caller has no reference count in mind and the resolved value
    // is the right one — which is the case once a build has finished.
    if (pairs == 0) pairs = (int)r.integer(markov::keys::Pairs);
    return markov_work(w, h, (int)r.integer(markov::keys::Iters), pairs);
}


cv::Mat markov_build(params::Params& P, const MaskRequest& q)
{
    // Resolved rather than assumed: unset values derive from whatever the user
    // did supply. See doc/DEFAULTS.md.
    return build_markov_mask(q.size, q.samples, markov_params_from(P), q.seed, q.log,
                             &q.reference);
}

// --- inpaint ---------------------------------------------------------------

double inpaint_work_of(const params::Resolver& r, int w, int h, int)
{
    return inpaint_work(w, h,
                        (int)r.integer(inpaint::keys::Iters),
                        (int)r.integer(inpaint::keys::Db),
                        (int)r.integer(inpaint::keys::Dims),
                        (int)r.integer(inpaint::keys::Scales),
                        (int)r.integer(inpaint::keys::Checks));
}

cv::Mat inpaint_build(params::Params& P, const MaskRequest& q)
{
    // Empty here means the builder already said which level and why.
    return build_inpaint_mask(q.size, q.samples, inpaint_params_from(P), q.seed, q.log,
                              &q.reference);
}

bool markov_validate(params::Params& P)
{
    const std::string w = P.str(markov::keys::Weights);
    if (w == "flat" || w == "mi" || w == "ml") return true;
    fprintf(stderr, "markov.weights must be flat, mi or ml\n");
    return false;
}

// --- direct ----------------------------------------------------------------

double direct_work_of(const params::Resolver& r, int w, int h, int)
{
    return direct_work(w, h, (int)r.integer(direct::keys::Overlap));
}

// The tile comes out of the sample, so the sample bounds it -- after
// --mask-scale, which resizes it, and less the overlap the boundary cut needs
// on each axis. Taking the whole sample instead would leave the cut nowhere to
// go, and a tile that does not join is the defect this generator works hardest
// to avoid; a few pixels of mask are the cheaper thing to give up.
long long direct_sample_limit(const params::Resolver& r, long long sampleSide)
{
    const double usable = (double)sampleSide * r.num(mask::keys::Scale);
    return (long long)usable - r.integer(direct::keys::Overlap);
}

cv::Mat direct_build(params::Params& P, const MaskRequest& q)
{
    // Empty here means the builder already said why -- its only failure is a
    // sample too small to cut the tile from.
    // Its tile *is* the sample, so it takes one whole scan rather than the set.
    return build_direct_mask(q.size, q.samples.primary(),
                             P.num(mask::keys::Scale),
                             P.num(mask::keys::Highpass),
                             P.num(direct::keys::Equalise),
                             (int)P.integer(direct::keys::Overlap),
                             // In the source's codes, which is what the option
                             // documents: one code of a 16-bit scan is 1/257 of
                             // one of an 8-bit scan's, and quantising at the
                             // wider step would throw away the detail the extra
                             // depth is there to keep.
                             P.num(direct::keys::Quantise) * q.samples.code_step(),
                             q.seed, q.log);
}

} // namespace

// Every markov setting, resolved. The public definition, used by the build and
// by calibration alike so the two cannot drift -- calibrating with different
// settings than the build uses would measure the wrong thing. It was a private
// impl plus a one-line public forwarder, which is two names for one function.
MarkovParams markov_params_from(params::Params& P)
{
    MarkovParams mp;
    mp.bins      = (int)P.integer(markov::keys::Bins);
    mp.pairs     = (int)P.integer(markov::keys::Pairs);
    mp.shortlist = (int)P.integer(markov::keys::Shortlist);
    mp.radius    = (int)P.integer(markov::keys::Radius);
    mp.iters     = (int)P.integer(markov::keys::Iters);
    mp.weights   = P.str(markov::keys::Weights);
    mp.report    = P.integer(markov::keys::Report) != 0;
    mp.temp      = P.num(markov::keys::Temp);
    mp.scale     = P.num(mask::keys::Scale);
    mp.highpass  = P.num(mask::keys::Highpass);
    mp.reuse     = P.num(markov::keys::Reuse);
    mp.bands     = (int)P.integer(markov::keys::Bands);
    return mp;
}

// Every inpaint setting, resolved -- the same arrangement and for the same
// reason as markov's above: --mask-calibrate has to probe the block the build
// will use, or it measures a model nobody is going to draw from.
InpaintParams inpaint_params_from(params::Params& P)
{
    InpaintParams ip;
    ip.scale    = P.num(mask::keys::Scale);
    ip.highpass = P.num(mask::keys::Highpass);
    ip.radius   = (int)P.integer(inpaint::keys::Radius);
    ip.iters    = (int)P.integer(inpaint::keys::Iters);
    ip.db       = (int)P.integer(inpaint::keys::Db);
    ip.dims     = (int)P.integer(inpaint::keys::Dims);
    ip.groups   = (int)P.integer(inpaint::keys::Groups);
    ip.checks   = (int)P.integer(inpaint::keys::Checks);
    ip.scales   = (int)P.integer(inpaint::keys::Scales);
    ip.cap      = P.num(inpaint::keys::Cap);
    ip.bands    = (int)P.integer(inpaint::keys::Bands);
    return ip;
}

// See maskparams.h. Declared there and defined here, because the answer is a
// property of the table below.
params::Applicability mask::calibratable()
{
    return { [](const params::Resolver& r) {
                 if (!mask::building(r)) return false;
                 const Generator* g = find_generator(r.str(mask::keys::Method));
                 return g && g->calibrate != nullptr;
             },
             "building a mask with a generator that can measure its own settings" };
}

const std::vector<Generator>& generators()
{
    static const std::vector<Generator> table = {
        { "blue", false, false, false, false, 32.0, register_bluenoise_params, blue_work_of, blue_build,
          nullptr, nullptr, nullptr },

        { "dft", true, true, true, true, 32.0, register_spectral_params, dft_work_of, dft_build,
          nullptr, nullptr, calibrate_dft_for },

        { "markov", true, true, true, false, 0.0, register_markov_params, markov_work_of, markov_build,
          markov_validate, nullptr, calibrate_markov_for },

        { "inpaint", true, true, true, false, 0.0, register_inpaint_params, inpaint_work_of, inpaint_build,
          nullptr, nullptr, calibrate_inpaint_for },

        { "direct", true, true, false, true, 32.0, register_direct_params, direct_work_of, direct_build,
          nullptr, direct_sample_limit, nullptr },
    };
    return table;
}

std::string generator_list(bool (*pick)(const Generator&))
{
    std::vector<std::string> names;
    for (const Generator& g : generators())
        if (pick(g)) names.push_back(g.name);
    std::string out;
    for (size_t i = 0; i < names.size(); i++)
        out += (i == 0 ? "" : i + 1 == names.size() ? " or " : ", ") + names[i];
    return out;
}

std::vector<params::Value> reconcile_candidates()
{
    std::vector<params::Value> out;
    for (const Generator& g : generators())
        if (g.offer_in_reconcile) out.push_back(std::string(g.name));
    return out;
}

const Generator* find_generator(const std::string& name)
{
    for (const Generator& g : generators())
        if (name == g.name) return &g;
    return nullptr;
}
