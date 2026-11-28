// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <iosfwd>
#include <opencv2/core.hpp>
#include "defaults.h"
#include "maskparams.h"

// The sample used as the mask, rather than imitated.
//
// dft, markov and inpaint all fit a model to the sample and synthesise from it.
// This one skips the model: equalise the sample, take a tile of it, rank that
// flat. Everything true of the grain is true of the mask, including whatever no
// model has captured -- so it is the upper bound the other three are measured
// against, and on a sample large enough to supply the tile it is also simply the
// best mask available.
//
// What it cannot do is invent. The tile has to come out of the sample, so the
// sample must be at least mask-sized after --mask-scale, and a second tile from
// the same picture is the same grain rather than another draw of it.
//
// The one thing a crop is not is toroidal: its left edge has never met its
// right. See directmask.cpp for what is done about that -- and note that the
// answer is chosen to keep the paragraph above literally true, which rules out
// anything that averages two pieces of sample together.
// `equalise` above 0 is the sigma, in pixels, of the neighbourhood each pixel is
// ranked within instead of high-passing the sample, and is the default; 0
// restores the high-pass. See maskutil::local_quantile.
cv::Mat build_direct_mask(cv::Size sz, const cv::Mat& sample,
                          double scale, double highpass, double equalise, int overlap,
                          double quantStep, uint64_t seed, std::ostream* log);

void register_direct_params(params::Params&);

// Work units, for --budget. A sort of the tile plus the two boundary cuts.
double direct_work(int w, int h, int overlap);

namespace direct::keys {
inline constexpr auto Rate        = "direct.rate";
inline constexpr auto MaxSize     = "direct.max_size";
inline constexpr auto MaxRepeats  = "direct.max_repeats";
inline constexpr auto Overlap     = "direct.overlap";
inline constexpr auto Equalise    = "direct.equalise";
inline constexpr auto Quantise    = "direct.quantise";
}
