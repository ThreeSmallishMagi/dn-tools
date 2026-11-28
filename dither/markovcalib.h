// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <iosfwd>
#include <string>
#include "defaults.h"
#include "samples.h"

// The generator table's `calibrate` hook for markov: measure markov.temp for
// this sample instead of deriving it from the rule. Takes the sample's path
// rather than the image because its first act is to check whether a
// command-line --markov-temp has already outranked the measurement, which it can
// say without reading anything.
//
// False only when the sample could not be read; a --mask-calibrate with nothing
// to decide is not an error.
bool calibrate_markov_for(params::Params& P, const std::string& sample_path,
                          uint64_t seed, const std::string& cfg, std::ostream& log,
                          bool measure, const samples::Set* read = nullptr);
