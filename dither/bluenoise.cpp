// bluenoise.cpp — Void-and-cluster blue-noise mask construction
// - Progressive (one-pixel-per-rank) placement, lowest-energy-first
// - Toroidal Gaussian energy with incremental updates
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "bluenoise.h"

#include <opencv2/opencv.hpp>
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <queue>
#include <random>
#include <vector>


struct Tap { int dy, dx; float w; };

// Make a sparse Gaussian kernel with the given sigma.
static std::vector<Tap> makeSparseGaussian(double sigma, double eps, double mul) {
    int R = std::max(1, int(std::ceil(mul*sigma)));
    std::vector<Tap> taps;
    const double denom = 2.0*sigma*sigma;
    for (int dy=-R; dy<=R; ++dy)
        for (int dx=-R; dx<=R; ++dx) {
            double r2 = double(dx*dx + dy*dy);
            double w = std::exp(-r2/denom);
            if (w >= eps) taps.push_back(Tap{dy,dx,(float)w});
        }
    double sum = 0.0; for (auto& t: taps) sum += t.w;
    for (auto& t: taps) t.w = (float)(t.w / std::max(sum, 1e-12));
    return taps;
}

static inline int wrapCoord(int v, int n) { v %= n; return v < 0 ? v + n : v; }

// Incremental energy update: E += kernel shifted to (y,x) with wrap.
static inline void addKernelWrapped(cv::Mat& E, const std::vector<Tap>& K, int y, int x) {
    const int H = E.rows, W = E.cols;
    for (const auto& t : K)
        E.at<float>(wrapCoord(y + t.dy, H), wrapCoord(x + t.dx, W)) += t.w;
}

// Build progressive blue-noise threshold order: one pixel per rank.
// Start empty, place a random seed, then repeatedly place the next pixel at
// the **lowest energy** empty location; update E incrementally.
// This avoids batch artifacts and produces very uniform dot spacing.
cv::Mat build_blue_noise_mask(cv::Size sz,
                              double sigma, double eps, double mul,
                              uint64_t seed)
{
    const int H = sz.height, W = sz.width;
    const int N = H*W;

    cv::Mat rank(H, W, CV_32S, cv::Scalar(-1));
    cv::Mat occupied = cv::Mat::zeros(H, W, CV_8U);
    cv::Mat E = cv::Mat::zeros(H, W, CV_32F); // energy map

    auto K = makeSparseGaussian(sigma, eps, mul);

    std::mt19937_64 rng(seed);
    std::uniform_int_distribution<int> Uy(0, H-1), Ux(0, W-1);

    // Tiny random jitter for tie-breaks
    cv::Mat jitter(H, W, CV_32F);
    {
        std::uniform_real_distribution<float> U(0.f, 1e-6f);
        for (int y=0;y<H;++y) {
            float* p = jitter.ptr<float>(y);
            for (int x=0;x<W;++x) p[x] = U(rng);
        }
    }

    // Seed a random pixel
    int sy = Uy(rng), sx = Ux(rng);
    rank.at<int>(sy, sx) = 0;
    occupied.at<uint8_t>(sy, sx) = 1;
    addKernelWrapped(E, K, sy, sx);

    // Min-heap: (energy+jitter, linearized index y*W+x).
    // Lazy deletion: stale entries (energy increased since push) are discarded on pop.
    // Energy is monotonically non-decreasing, so ev < curE means the entry is stale.
    using Entry = std::pair<float, int>;
    std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> pq;

    for (int y=0; y<H; ++y) {
        const float* e = E.ptr<float>(y);
        const float* j = jitter.ptr<float>(y);
        const uint8_t* occ = occupied.ptr<uint8_t>(y);
        for (int x=0; x<W; ++x)
            if (!occ[x]) pq.push({e[x]+j[x], y*W+x});
    }

    int reportStep = std::max(1, N/100);
    for (int r=1; r<N; ++r) {
        if (r % reportStep == 0)
            std::cerr << "\r" << (r*100/N) << "%" << std::flush;

        // Pop until we find an unoccupied, non-stale entry
        int by, bx;
        for (;;) {
            auto [ev, idx] = pq.top(); pq.pop();
            int y = idx/W, x = idx%W;
            if (occupied.at<uint8_t>(y, x)) continue;
            if (ev < E.at<float>(y,x) + jitter.at<float>(y,x)) continue; // stale
            by = y; bx = x;
            break;
        }

        rank.at<int>(by, bx) = r;
        occupied.at<uint8_t>(by, bx) = 1;
        addKernelWrapped(E, K, by, bx);

        // Push updated entries for affected unoccupied neighbors
        for (const auto& t : K) {
            int yy = wrapCoord(by + t.dy, H);
            int xx = wrapCoord(bx + t.dx, W);
            if (!occupied.at<uint8_t>(yy, xx))
                pq.push({E.at<float>(yy,xx)+jitter.at<float>(yy,xx), yy*W+xx});
        }
    }
    std::cerr << "\r100%\n";

    // Map rank 0..N-1 to threshold 0..254. Never 255 so pure-white input always passes.
    cv::Mat T8(H, W, CV_8U);
    for (int y=0;y<H;++y) {
        const int* rr = rank.ptr<int>(y);
        uint8_t* dd = T8.ptr<uint8_t>(y);
        for (int x=0;x<W;++x) {
            int v = (int)std::round((double)rr[x] * 254.0 / std::max(1, N - 1));
            dd[x] = (uint8_t)std::clamp(v, 0, 254);
        }
    }
    return T8;
}

