// bluenoise.cpp — Void-and-cluster blue-noise mask construction
// - Progressive (one-pixel-per-rank) placement, lowest-energy-first
// - Toroidal Gaussian energy with incremental updates
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "bluenoise.h"
#include "maskutil.h"

#include <opencv2/opencv.hpp>
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <queue>
#include <random>
#include <vector>
#include <cstdio>
#include <unistd.h>


namespace {

struct Tap { int dy, dx; float w; };

// A Gaussian kernel with every tap below `eps` dropped, normalised to sum 1.
// The box scanned is `mul` sigmas wide, and `mul` is derived rather than chosen:
// the only half-width that neither truncates the kernel nor scans past it is
// where the Gaussian reaches eps, sqrt(-2 ln eps). See register_bluenoise_params.
std::vector<Tap> makeSparseGaussian(double sigma, double eps, double mul) {
    const int radius = std::max(1, int(std::ceil(mul*sigma)));
    const double denom = 2.0*sigma*sigma;
    std::vector<Tap> kernel;
    kernel.reserve((size_t)(2*radius + 1) * (2*radius + 1));
    for (int dy=-radius; dy<=radius; ++dy)
        for (int dx=-radius; dx<=radius; ++dx) {
            const double w = std::exp(-double(dx*dx + dy*dy)/denom);
            if (w >= eps) kernel.push_back(Tap{dy,dx,(float)w});
        }
    double sum = 0.0; for (auto& t: kernel) sum += t.w;
    for (auto& t: kernel) t.w = (float)(t.w / std::max(sum, 1e-12));
    return kernel;
}


// Incremental energy update: E += kernel shifted to (y,x) with wrap.
inline void addKernelWrapped(cv::Mat& E, const std::vector<Tap>& K, int y, int x) {
    const int H = E.rows, W = E.cols;
    for (const auto& t : K)
        E.at<float>(maskutil::wrap_any(y + t.dy, H), maskutil::wrap_any(x + t.dx, W)) += t.w;
}

// The lowest-energy empty pixel. Entries are never removed from the heap when
// their energy rises, only re-pushed, so a pop can turn up one that has since
// been filled or superseded; both are discarded here. Energy only ever
// increases, which is what makes "pushed below what it is now" mean stale.
using Heap = std::priority_queue<std::pair<float, int>,
                                 std::vector<std::pair<float, int>>,
                                 std::greater<std::pair<float, int>>>;

std::pair<int, int> pop_lowest_empty(Heap& pq, const cv::Mat& E,
                                            const cv::Mat& jitter,
                                            const cv::Mat& occupied, int W)
{
    for (;;) {
        const auto [energy, idx] = pq.top(); pq.pop();
        const int y = idx/W, x = idx%W;
        if (occupied.at<uint8_t>(y, x)) continue;
        if (energy < E.at<float>(y,x) + jitter.at<float>(y,x)) continue;
        return {y, x};
    }
}

} // namespace

// Build progressive blue-noise threshold order: one pixel per rank.
// Start empty, place a random seed, then repeatedly place the next pixel at
// the **lowest energy** empty location; update E incrementally.
// This avoids batch artifacts and produces very uniform dot spacing.
cv::Mat build_blue_noise_mask(cv::Size sz,
                              double sigma, double eps, double mul,
                              uint64_t seed, std::ostream* log)
{
    const int H = sz.height, W = sz.width;
    const int N = H*W;

    // order[r] is the pixel chosen for rank r, which is what the algorithm
    // produces and what a threshold mask is made of. Keeping it as a rank per
    // pixel instead meant a second pass to invert it at the end.
    std::vector<int> order;
    order.reserve(N);
    cv::Mat occupied = cv::Mat::zeros(H, W, CV_8U);
    cv::Mat E = cv::Mat::zeros(H, W, CV_32F); // energy map

    auto K = makeSparseGaussian(sigma, eps, mul);

    std::mt19937_64 rng(seed);
    std::uniform_int_distribution<int> Uy(0, H-1), Ux(0, W-1);

    // Tie-breaks. Without it the first placements sit on a flat energy map and
    // the heap breaks ties by index, which lines the early pixels up in rows.
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
    order.push_back(sy * W + sx);
    occupied.at<uint8_t>(sy, sx) = 1;
    addKernelWrapped(E, K, sy, sx);

    // Min-heap: (energy+jitter, linearized index y*W+x).
    // Lazy deletion: stale entries (energy increased since push) are discarded on pop.
    // Energy is monotonically non-decreasing, so ev < curE means the entry is stale.
    Heap pq;

    for (int y=0; y<H; ++y) {
        const float* e = E.ptr<float>(y);
        const float* j = jitter.ptr<float>(y);
        const uint8_t* occ = occupied.ptr<uint8_t>(y);
        for (int x=0; x<W; ++x)
            if (!occ[x]) pq.push({e[x]+j[x], y*W+x});
    }

    // A redraw is for a terminal. Redirected to a file, a hundred carriage
    // returns are one unreadable line, and the line announcing the build has
    // already said what is happening.
    const bool redraw = log && (log != &std::cerr || isatty(fileno(stderr)));
    int reportStep = std::max(1, N/100);
    for (int r=1; r<N; ++r) {
        if (redraw && r % reportStep == 0)
            *log << "\r" << (r*100/N) << "%" << std::flush;

        const auto [by, bx] = pop_lowest_empty(pq, E, jitter, occupied, W);

        order.push_back(by * W + bx);
        occupied.at<uint8_t>(by, bx) = 1;
        addKernelWrapped(E, K, by, bx);

        // Push updated entries for affected unoccupied neighbors
        for (const auto& t : K) {
            int yy = maskutil::wrap_any(by + t.dy, H);
            int xx = maskutil::wrap_any(bx + t.dx, W);
            if (!occupied.at<uint8_t>(yy, xx))
                pq.push({E.at<float>(yy,xx)+jitter.at<float>(yy,xx), yy*W+xx});
        }
    }
    if (redraw) *log << "\r100%\n";

    // `order` is the order the dots were placed in, which is the order they
    // should light in. threshold_mask takes a field's ascending order and
    // lights its top first, so the first-placed dot goes last.
    std::reverse(order.begin(), order.end());
    return maskutil::threshold_mask(order, sz);
}


// ---------------------------------------------------------------------------
// Default rules. See DEFAULTS.md.
// ---------------------------------------------------------------------------

double blue_work(int w, int h, double sigma)
{
    const double n = (double)w * h;
    return n * std::log2(std::max(n, 2.0)) * sigma * sigma;
}

void register_bluenoise_params(params::Params& P)
{
    // Named once. inpaint has always done this; the others repeated the
    // expression at every rule, which buries what each rule *says* under
    // what they all have in common.
    const auto mine = mask::generator("blue");

    // Work units per second, measured from this machine's own builds and cached
    // in the config file. The compiled-in value is only a first guess; the first
    // --budget run says so, and every build refines it.
    params::add_const(P, bluenoise::keys::Rate, 3.4e6,
                      mine, {{1.0, 1e15}},
                      "measured build throughput, work units per second",
                                        true);   // an intermediate: reaches the mask only via --budget

    // DEFAULTS.md proposes scaling this with --supersample so the grain lands at
    // a fixed *output* size. That is reasoned, not measured, and would change
    // every supersampled blue mask, so it stays a constant until someone checks.
    params::add_const(P, bluenoise::keys::Sigma, 1.8,
                      mine, {{0.01, 1000.0}},
                      "repulsion kernel radius in pixels: how far dots push each other apart");

    params::add_const(P, bluenoise::keys::MaxRepeats, (long long)64,
                      mine, {{1, 1024}},
                      "tile repeats the grid may show before it looks regular");

    params::add_const(P, bluenoise::keys::MaxSize, (long long)256,
                      mine, {{16, 8192}},
                      "largest tile worth building, absent a --budget");

    // A quality-for-time trade with no best value in the abstract. Fixed at
    // today's figure until --budget exists to spend on it.
    params::add_const(P, bluenoise::keys::Eps, 1e-4,
                      mine, {{1e-12, 1.0}},
                      "sparse-kernel cutoff: lighter taps are dropped");

    // Not a free choice at all. makeSparseGaussian scans a box of
    // ceil(radius*sigma) and then drops every tap under `eps`, so any radius
    // beyond where the Gaussian reaches eps only visits taps that eps rejects,
    // and any radius short of it silently truncates the kernel. That crossing is
    // at sigma*sqrt(-2 ln eps) pixels, i.e. sqrt(-2 ln eps) in sigma units —
    // 4.29 at the default eps, which is why the old fixed 6.0 was documented as
    // inert. Deriving it makes the kernel identical and the parameter honest.
    P.add({ bluenoise::keys::Radius,
            [](const params::Resolver& r) -> params::Value {
                return std::sqrt(-2.0 * std::log(r.num(bluenoise::keys::Eps)));
            },
            mine,
            {{0.0, 100.0}},
            "kernel half-width in sigma units; where the Gaussian reaches blue.eps" });

    // Measured from this machine's own builds, never chosen, so it is
    // reachable by --set and is not offered as an option.
    P.hide_option(bluenoise::keys::Rate);
}
