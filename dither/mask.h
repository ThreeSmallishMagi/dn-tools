// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "samples.h"
#include <string>
#include <opencv2/core.hpp>
#include "defaults.h"
#include "options.h"

// Mask policy: how big a tile to build, what it costs, and getting one built or
// loaded. Which generator does the building is generators.cpp's business; this
// file names none of them.

// The geometry rules that span every generator -- mask.size.w/h, mask.scale,
// mask.highpass, mask.sample, mask.in, mask.method, supersample, budget.
void register_mask_params(params::Params& P);

// The mask this run will threshold against: loaded if --mask-in gave one, built
// otherwise, and timed if it was built so the next --budget run is calibrated
// to this machine. Empty on failure, having already said why.
// `sample` is the already-decoded --mask-sample, or empty when none was named:
// the mask size depends on it for a generator whose tile is a piece of it, so
// it has to be read before the size is resolved rather than at build time.
cv::Mat obtain_mask(const Options& opt, const samples::Set& set,
                    params::Params& P, const std::string& cfg);

// Measure a parameter for this sample rather than deriving it, if
// --mask-calibrate asked and the chosen generator has one to measure. False
// stops the run.
bool maybe_calibrate(const Options& opt, params::Params& P, const std::string& cfg,
                     const samples::Set* read = nullptr);
