// SPDX-License-Identifier: GPL-3.0-or-later
// inpaint.h — threshold masks synthesised by filling each pixel from the sample.
#pragma once
#include <cstdint>
#include <iosfwd>
#include <opencv2/core.hpp>
#include "defaults.h"
#include "maskparams.h"
#include "samples.h"

// Parameter keys this generator owns.
namespace inpaint::keys {
inline constexpr auto Rate       = "inpaint.rate";
inline constexpr auto Radius     = "inpaint.radius";
inline constexpr auto Iters      = "inpaint.iters";
inline constexpr auto Db         = "inpaint.db";
inline constexpr auto Dims       = "inpaint.dims";
inline constexpr auto Checks     = "inpaint.checks";
inline constexpr auto Groups     = "inpaint.groups";
inline constexpr auto Scales     = "inpaint.scales";
inline constexpr auto Cap        = "inpaint.cap";
inline constexpr auto Bands      = "inpaint.bands";
inline constexpr auto MaxSize    = "inpaint.max_size";
inline constexpr auto MaxRepeats = "inpaint.max_repeats";
}

// The defaults, in one place. The struct and the registry both take them from
// here: two independent initialiser lists drifted apart once already, leaving a
// direct caller of the synthesis API with 6 sweeps where the CLI used 3.
namespace inpaint::fallback {
inline constexpr double Scale    = 1.0;
inline constexpr double Highpass = 0.0;    // the generator table's default_highpass
inline constexpr int    Radius   = 3;
inline constexpr int    Iters    = 2;
inline constexpr int    Db       = 250000;
inline constexpr int    Dims     = 12;
inline constexpr int    Checks   = 64;
inline constexpr int    Groups   = 16;
inline constexpr int    Scales   = 2;
inline constexpr double Cap      = 1.25;
inline constexpr int    Bands    = 16;    // maskutil::kDefaultPowerBands
}

struct InpaintParams {
    double scale    = inpaint::fallback::Scale;     // feature size relative to the sample
    double highpass = inpaint::fallback::Highpass;  // drop structure coarser than this, px
    int    radius   = inpaint::fallback::Radius;    // neighbourhood half-width
    int    iters    = inpaint::fallback::Iters;     // passes over the field, per level
    int    db       = inpaint::fallback::Db;        // sample neighbourhoods to match against
    int    dims     = inpaint::fallback::Dims;      // PCA dimensions the match is made in
    int    checks   = inpaint::fallback::Checks;    // kd-tree leaves visited per query
    int    groups   = inpaint::fallback::Groups;    // in-place passes a sweep is split into
    int    scales   = inpaint::fallback::Scales;    // coarse-to-fine resolutions
    double cap      = inpaint::fallback::Cap;       // stop sweeping past this multiple of
                                                    // the sample's directionality; 0 off
    int    bands    = inpaint::fallback::Bands;     // frequency bands each sweep's
                                                    // spectrum is matched in; 0 off
};

// Build a CV_8U threshold mask by repeatedly deciding each pixel from the sample
// neighbourhood that most resembles its own. Where the markov generator pools
// pairwise conditionals, this conditions on the joint neighbourhood and does it
// non-parametrically, which removes markov's white-noise floor. It still worms
// as it sweeps -- each sweep makes neighbourhoods agree a little more -- which
// is why the sweep count is small, the field is held to the sample's band power
// after every sweep, and inpaint.cap stops a directional sample early.
// Toroidal and histogram-flat like every other generator's output.
//
// Returns an empty Mat if the sample is too small to cut neighbourhoods from.
//
// `reference`, when given, is what each sweep is held to at every scale
// (samples::reference_for); otherwise `set` is.
cv::Mat build_inpaint_mask(cv::Size sz, const samples::Set& set,
                           const InpaintParams& p, uint64_t seed,
                           std::ostream* log, const samples::Set* reference = nullptr);

// The synthesis with nothing of the mask about it: hand it a prepared field —
// `maskutil::prepare_texture_sample` output, or a raw CV_32F crop — and it
// returns a `sz` field of the same character, rank-uniform on [0,1].
//
// build_inpaint_mask is this plus one finishing step, and so is `maketexture`;
// they differ only in what they map the final ordering onto. Exposed so that
// tool exercises the code a dither run uses rather than a copy of it.
//
// Empty if the input is too small to cut neighbourhoods from, having said why.
cv::Mat synthesise_inpaint_field(cv::Size sz, const cv::Mat& input,
                                 const InpaintParams& p, uint64_t seed,
                                 std::ostream* log);

// The same over several prepared scans, which is what a film directory gives.
cv::Mat synthesise_inpaint_field(cv::Size sz, const samples::Set& prepared,
                                 const InpaintParams& p, uint64_t seed,
                                 std::ostream* log,
                                 const samples::Set* referencePrepared = nullptr);

// One coarse level's scans: each block-averaged by `downsample`, then the set
// ranked together. Declared rather than kept file-static only so the test can
// assert the second half -- the averaging and the ranking are a pair, and when
// they were one step each scan was re-ranked alone and the cross-scan contrast
// the pooled rank exists to keep was destroyed at every coarse level.
samples::Set downsample_set(const samples::Set& set, int f);

void register_inpaint_params(params::Params&);

// Work units to build a w*h mask. The search is the cost and it is sub-linear in
// the database, so this is queries x dimensions x log2(database), summed over
// scales, plus the index builds.
double inpaint_work(int w, int h, int iters, int db, int dims, int scales, int checks);
