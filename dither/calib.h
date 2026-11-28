// SPDX-License-Identifier: GPL-3.0-or-later
// calib.h — what `--mask-calibrate` optimises, shared by the generators that
// have a parameter worth measuring per sample.
//
// Three generators measure something, and they measure *different* things,
// which is the reason this is a module rather than a function. markov searches
// a temperature (with the weighting and radius), whose defect is over- or
// under-clumping; inpaint searches a sweep count, whose defect is ridging; dft
// searches its spectrum smoothing, which only a spectral score can judge. A
// single objective would judge one of them on a quantity it cannot move.
//
// So the pieces are offered separately and each calibrator combines the ones
// that answer its own question. What they share is the *reference* -- the
// film's crop where it has one, turned into the mask it would be if it could be
// used directly -- which is what makes each number sample-relative rather than
// absolute, and `inputs_for`, which reads the sample the way the build does.
#pragma once

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <opencv2/core.hpp>

#include "defaults.h"
#include "samples.h"

namespace calib {

// The densities a threshold mask is actually used at, and the octaves a
// texture is read at. Both match what test/levelfit and test/patchfit use, so
// a calibration can be checked against the tools afterwards.
inline constexpr double kLevels[] = {0.1, 0.25, 0.5, 0.75, 0.9};
inline constexpr int    kOctaves  = 3;

using ClumpProfile = std::array<double, std::size(kLevels)>;
using RidgeProfile = std::array<double, kOctaves>;

// Mean spectral power below 0.1 cycles/px of the pattern shown at density `g`,
// normalised so uncorrelated noise reads 1 — maskstats' `low`, averaged over
// frequency cells rather than radial rings.
//
// Cell averaging rather than ring averaging is measured, not incidental: rings
// weight the sparse lowest-frequency ones as heavily as the dense ones, which
// at 128² is a handful of cells carrying a whole ring, and the resulting
// minimum is noisy enough that a coarse sample walked to the end of its
// bracket. The consequence is that this is a near neighbour of levelfit's score
// rather than the identical number.
double level_set_low(const cv::Mat& mask8, double g);

// How much a mask clumps, at each density it will be used at.
ClumpProfile clump_profile(const cv::Mat& mask8);

// How much a mask *ridges*, per octave: the structure-tensor coherence of each
// band, 0 for a field of blobs and 1 for a field of ridges.
//
// The quantity clumping cannot see, and what a sweep count trades against it.
// Measured per block and then averaged: coherence over the whole field lets
// opposing ridge directions cancel, which reads a ring pattern as isotropic.
// maskutil::field_coherence is that global measure, for the question that wants
// it. Toroidal throughout, since a mask is a torus.
RidgeProfile ridge_profile(const cv::Mat& mask8);

// Radial power in octave-wide bands, from the finest up to `lowCut` cycles/px.
// What test/maskfit compares, and the only one of the three that a generator
// matching a spectrum can be scored on: dft randomises phase, so clumping and
// ridging tell you about its noise and not about its fit.
//
// Bands rather than rings, and log-spaced, because the point is a fit across
// scales rather than a curve: a ring average at 256² puts a handful of cells in
// the lowest ring and the resulting score is dominated by them.
inline constexpr int kBands = 24;
using SpectrumProfile = std::array<double, kBands>;

SpectrumProfile spectrum_profile(const cv::Mat& mask8, double lowCut);

// RMS of the log10 ratio, over the entries where both are measurable. NaN when
// none were: a caller must decide what to do about an unmeasurable candidate
// rather than have it compare as merely bad.
double profile_distance(const double* a, const double* ref, size_t n);

inline double clump_distance(const ClumpProfile& a, const ClumpProfile& ref)
{
    return profile_distance(a.data(), ref.data(), a.size());
}
inline double ridge_distance(const RidgeProfile& a, const RidgeProfile& ref)
{
    return profile_distance(a.data(), ref.data(), a.size());
}
inline double spectrum_distance(const SpectrumProfile& a, const SpectrumProfile& ref)
{
    return profile_distance(a.data(), ref.data(), a.size());
}

// The sample as the mask it would be if it could be used directly: high-passed
// and scaled exactly as the generator was fitted on, centre-cropped to the
// probe size, ranked flat to 0..254. The same construction test/levelfit uses,
// so the number a calibration optimises is the number that scores it.
//
// Empty when the prepared sample is smaller than the probe.
cv::Mat reference_mask(const cv::Mat& sample, double highpass, double scale, cv::Size sz);

// What a calibration works from, for `spec` given to `method`.
//
// `set` is read exactly as the build reads it -- through samples::resolve with
// the generator's own crop preference -- or a calibration is measured on one
// sample and applied to a mask built from another. dft's was, for a while:
// its build took the film's crop and its calibration all of the film's scans.
//
// Two references, because they do two jobs and the build does only one of them.
//
// `reference` is what synthesis is held to -- samples::reference_for, exactly
// the set the build passes its generator: the film's crop when `spec` names a
// film that has one, else every scan. A probe built against anything else is a
// different synthesiser from the one the build runs, and a calibration of it
// answers for nothing; collapsing this set to one image, when a reference was
// only something to score against, did exactly that for any sample of several
// scans with no crop.
//
// `scoring` is the one image a probe is compared with: the reference's largest
// member, the crop itself where there is one. A score needs a single patch of
// real grain, and the crop is a window chosen to hold grain and nothing else.
// The middle of a scan is wherever the photograph put its subject; at markov's
// and inpaint's default of no high-pass, scoring against it ran every
// temperature search to its most clumped end.
struct Inputs {
    samples::Set set;
    samples::Set reference;
    cv::Mat      scoring;
    std::string  described;   // what `scoring` is, for the log
};
//
// `read`, when given, is the sample as the build already read it -- through
// the same resolution -- and is used rather than read again.
//
// `crop_first` is the generator's own answer -- uses_crop(method) -- passed in
// so this module need not reach up into the generator table.
std::optional<Inputs> inputs_for(const std::string& spec, bool crop_first,
                                 const samples::Set* read = nullptr);

// Mix an image's dimensions, type and pixels into an FNV-1a hash, for the
// calibration keys: a reference that changes must change the key.
void mix_image(uint64_t& h, const cv::Mat& m);

// The same over every image of a set, with the count first.
void mix_set(uint64_t& h, const samples::Set& s);

// Is `v` a usable cached answer for `key`? Checks the registry's own range --
// not bounds restated here, which drifted from it -- and integrality where the
// parameter is discrete. A cached result that fails this is a miss: the cache
// is disposable, and letting Params::set throw would stop a run it cannot
// legitimately affect.
bool usable(const params::Params& P, std::string_view key, double v,
            bool wholeNumber);

} // namespace calib
