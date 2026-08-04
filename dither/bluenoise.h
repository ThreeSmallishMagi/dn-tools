// SPDX-License-Identifier: GPL-3.0-or-later
// bluenoise.h — void-and-cluster blue-noise mask construction.
#pragma once
#include <cstdint>
#include <opencv2/core.hpp>

// Build a CV_8U threshold mask via progressive void-and-cluster placement.
// `sigma` controls the Gaussian repulsion kernel; `eps`/`mul` control the
// sparse kernel cutoff and half-width; `seed` picks the starting pixel.
cv::Mat build_blue_noise_mask(cv::Size sz, double sigma, double eps, double mul, uint64_t seed);
