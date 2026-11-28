// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <string>
#include <opencv2/core.hpp>
#include "defaults.h"
#include "options.h"

// Apply the command line to `P`, reconcile the defaults around it, refuse what
// cannot be honoured, and fill `opt` from the result. False means it has
// already said why.
bool parseOptions(int argc, char** argv, Options& opt, params::Params& P);

void usage();        // the full reference, for --help
void usage_brief();  // enough to get started, for a bare invocation

// Where the rate cache and any saved parameters live. Empty disables both.
std::string config_path();

// Shared with dither_main's config pre-scan, which must agree with the parser
// about which tokens are options and which are their values. See cli.cpp.
bool option_takes_value(const params::Params& P, const std::string& flag);
int  first_option_index(int argc, char** argv);

// cv::imwrite reports an unwritable path by returning false and an unknown
// extension by throwing; neither should reach the user as a core dump.
bool write_image(const std::string& path, const cv::Mat& img);
