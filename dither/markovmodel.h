// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <memory>
#include <vector>
#include <cstdint>
#include <iosfwd>
#include <opencv2/core.hpp>
#include "markov.h"
#include "samples.h"

// markov.cpp's interior, shared with markovcalib.cpp and with nothing else.
//
// The calibrator has to fit a model and draw from it at a range of
// temperatures, which is exactly two operations; it never looks inside the
// model. This header is that pair plus the types they are spelled in, and is
// deliberately not part of markov.h — no caller outside these two files should
// know that a fitted model is a thing you can hold.

// A neighbour offset from the centre pixel.
struct Offset {
    int dy, dx;
    int band() const { return std::max(std::abs(dy), std::abs(dx)); }
};

// One pair's conditional table. Weights are not stored here: they are a
// property of the call, which is what lets calibration probe several
// temperatures against one model in parallel.
struct Expert {
    Offset d1, d2;
    double mi = 0;
    std::vector<float> logp;   // [(b1 * Qc + b2) * Qv + c]
};

// The scans as the model sees them: each quantised twice, once at the
// conditioning resolution and once at the finer centre resolution.
//
// The rank is pooled over every scan rather than taken within each, so a bin
// means the same grain level everywhere and the model's marginal is the flat
// histogram a threshold mask is required to have. Ranking per scan would
// normalise away the contrast differences between exposures, which are real
// grain behaviour and the thing the model should be learning.
struct Quantised {
    std::vector<cv::Mat> qn, qv;
    std::size_t size() const { return qn.size(); }
};

// Which columns of each scan a pass visits. The held-out report fits on one
// half and scores on the other, and with several scans of different widths
// there is no single column to split at -- so the split is per scan, which is
// the same claim and the same `radius` gap.
enum class Columns { All, Left, Right };

struct MarkovModel {
    std::vector<Expert> experts;
    std::vector<double> occupancy;
    Quantised quant;                    // the quantised scans, kept for --markov-report
    int Qv = 0, Qc = 0, cells = 0, radius = 0, step = 0;
    std::vector<double> bands;          // what spectrum matching aims at, for a field
                                        // of `size`; empty when markov.bands is 0
    cv::Size size;
    size_t shortlistUsed = 0, offsetCount = 0;
};

// Fit the pairwise tables. Null if the sample is too small to fit from.
std::unique_ptr<MarkovModel> fit_markov_model(const samples::Set& set,
                                              const MarkovParams& p, cv::Size sz,
                                              const samples::Set* reference = nullptr);

// Gibbs-sample a mask from a fitted model. Nothing writes to `m`, so draws at
// different temperatures may run concurrently.
// The draw, stopping at the ordering rather than at the mask built from it.
// `sample_markov_mask` is this plus `threshold_mask`; maketexture maps the same
// ordering onto the sample's own values instead.
std::vector<int> sample_markov_order(cv::Size sz, const MarkovModel& m,
                                     const MarkovParams& p, uint64_t seed,
                                     std::ostream* log);

cv::Mat sample_markov_mask(cv::Size sz, const MarkovModel& m, const MarkovParams& p,
                           uint64_t seed, std::ostream* log);
