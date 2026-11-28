// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <iosfwd>
#include <string>
#include "defaults.h"
#include "samples.h"

// The generator table's `calibrate` hook for dft: measure dft.smooth_cells for
// this sample. Unlike the other two calibrations this one usually declines, and
// that is the correct answer rather than a gap — the parameter it searches
// already has a rule with a sample term, so there is only something to find
// where that rule extrapolates. See dftcalib.cpp.
bool calibrate_dft_for(params::Params& P, const std::string& sample_path,
                       uint64_t seed, const std::string& cfg, std::ostream& log,
                       bool measure, const samples::Set* read = nullptr);
