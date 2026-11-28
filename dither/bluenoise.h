// SPDX-License-Identifier: GPL-3.0-or-later
// bluenoise.h — void-and-cluster blue-noise mask construction.
#pragma once
#include <iosfwd>
#include <cstdint>
#include <opencv2/core.hpp>
#include "defaults.h"
#include "maskparams.h"

// Build a CV_8U threshold mask via progressive void-and-cluster placement.
// `sigma` controls the Gaussian repulsion kernel; `eps`/`mul` control the
// sparse kernel cutoff and half-width; `seed` picks the starting pixel.
cv::Mat build_blue_noise_mask(cv::Size sz, double sigma, double eps, double mul, uint64_t seed, std::ostream* log);

// Parameter keys this generator owns. Both are internal: neither has a best
// value the user could know better than the code does.
namespace bluenoise::keys {
inline constexpr auto Rate    = "blue.rate";
inline constexpr auto Sigma   = "blue.sigma";
inline constexpr auto Eps     = "blue.eps";
inline constexpr auto MaxSize    = "blue.max_size";
inline constexpr auto MaxRepeats = "blue.max_repeats";
inline constexpr auto Radius = "blue.radius";
}

void register_bluenoise_params(params::Params&);

// Work units to build a w*h mask: machine-independent, divided by the measured
// rate to predict seconds. Void-and-cluster places N pixels, each a heap
// operation over a kernel whose tap count grows with sigma^2.
double blue_work(int w, int h, double sigma);
