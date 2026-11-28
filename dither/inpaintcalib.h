// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <iosfwd>
#include <string>
#include "defaults.h"
#include "samples.h"

// The generator table's `calibrate` hook for inpaint: measure inpaint.iters for
// this sample instead of taking the default. The sweep count trades clumping
// against ridging at a rate that is a property of the film, and — unlike
// markov.temp — no rule derives it, because it was believed to be a convergence
// count with one right answer. See inpaintcalib.cpp.
//
// Takes the sample's path rather than the image for the same reason markov's
// does: its first act is to check whether --inpaint-iters has already outranked
// the measurement, which it can say without reading anything.
//
// False only when the sample could not be read; a --mask-calibrate with nothing
// to decide is not an error.
bool calibrate_inpaint_for(params::Params& P, const std::string& sample_path,
                           uint64_t seed, const std::string& cfg, std::ostream& log,
                           bool measure, const samples::Set* read = nullptr);
