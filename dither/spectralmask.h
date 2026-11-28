// SPDX-License-Identifier: GPL-3.0-or-later
// spectralmask.h — threshold masks synthesised to match a sample's spectrum.
#pragma once
#include <cstdint>
#include <vector>
#include <opencv2/core.hpp>
#include "defaults.h"
#include "maskparams.h"
#include "samples.h"

// Build a CV_8U threshold mask whose amplitude spectrum matches `sample`'s, by
// alternately imposing that spectrum and re-ranking to a uniform histogram. The
// whole 2D spectrum is used, so an oriented sample keeps its direction. The
// result is toroidal, uniform by construction, and usable anywhere a
// void-and-cluster mask is.
//
// `scale` multiplies feature size: 1 keeps the sample's grain in pixels, 2 makes
// it twice as coarse, 0.5 twice as fine. `highpass` drops structure in the
// sample coarser than that many pixels, so a whole photograph can be handed in
// without its subject becoming the texture. `iters` is the number of projection
// rounds (the fixed point is reached in well under 20).
// `smooth` is how many of the frequency cells one mask bin covers the sample's
// periodogram is averaged over before it is imposed; see dft.smooth_cells.
//
// Several scans are averaged in *power* -- Bartlett's method. Each scan's
// periodogram is a noisy estimate of the same spectrum, so averaging N of them
// cuts that noise, which is the same thing dft.smooth_cells buys by smoothing
// one of them. Note that dft.smooth_cells was fitted on single scans, so a
// multi-scan film gets more variance reduction than it was measured for.
cv::Mat build_spectral_mask(cv::Size sz, const samples::Set& set,
                            double scale, double highpass, int iters, double smooth,
                            uint64_t seed);

// The same synthesis, stopping at the ordering it settles on rather than at the
// mask built from it. `maketexture` maps that ordering onto the sample's own
// values instead of onto a flat 0..254 scale; nothing else differs.
std::vector<int> synthesise_spectral_order(cv::Size sz, const cv::Mat& sample,
                                           double scale, double highpass, int iters,
                                           double smooth, uint64_t seed);

// Parameter keys this generator owns. `iters` used to be --mask-iters, one
// option shared with markov although the two count different things.
namespace spectralmask::keys {
inline constexpr auto Rate     = "dft.rate";
inline constexpr auto SmoothCells = "dft.smooth_cells";
inline constexpr auto Iters   = "dft.iters";
inline constexpr auto MaxSize    = "dft.max_size";
inline constexpr auto MaxRepeats = "dft.max_repeats";
}

void register_spectral_params(params::Params&);

// Work units to build a w*h mask: one forward and one inverse FFT per round.
double dft_work(int w, int h, int iters);
