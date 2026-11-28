// SPDX-License-Identifier: GPL-3.0-or-later
// maskutil.cpp — see maskutil.h.

#include "maskutil.h"

#include <execution>
#include <limits>
#include <numeric>
#include <random>
#include <opencv2/imgproc.hpp>

namespace maskutil {

std::vector<int> stable_order(const float* v, std::size_t n)
{
    std::vector<std::pair<float, int>> kv(n);
    cv::parallel_for_(cv::Range(0, (int)n), [&](const cv::Range& r) {
        for (int i = r.start; i < r.end; i++) kv[i] = {v[i], i};
    });
    std::sort(std::execution::par, kv.begin(), kv.end());
    std::vector<int> order(n);
    cv::parallel_for_(cv::Range(0, (int)n), [&](const cv::Range& r) {
        for (int i = r.start; i < r.end; i++) order[i] = kv[i].second;
    });
    return order;
}

std::vector<int> rank_order(const cv::Mat& f)
{
    CV_Assert(f.type() == CV_32F && f.isContinuous());
    return stable_order(f.ptr<float>(), f.total());
}

void quantise(cv::Mat& f, double step)
{
    if (step <= 0.0) return;
    CV_Assert(f.type() == CV_32F && f.isContinuous());
    float* v = f.ptr<float>();
    const int n = f.rows * f.cols;
    // Round to nearest and keep the sign. Truncating would quantise either side
    // of zero onto a different grid, and these fields straddle zero because a
    // high-pass has removed their mean.
    for (int i = 0; i < n; i++)
        v[i] = (float)(std::round((double)v[i] / step) * step);
}

double tie_share(const cv::Mat& f)
{
    CV_Assert(f.type() == CV_32F && f.isContinuous());
    const std::vector<int> order = rank_order(f);
    const float* v = f.ptr<float>();
    const double n = (double)order.size();
    if (n < 2) return 1.0;
    double sum = 0.0;
    size_t i = 0;
    while (i < order.size()) {
        size_t j = i + 1;
        while (j < order.size() && v[order[j]] == v[order[i]]) j++;
        const double p = (double)(j - i) / n;
        sum += p * p;
        i = j;
    }
    return sum;
}

std::vector<int> rank_order_random(const cv::Mat& f, uint64_t seed)
{
    CV_Assert(f.type() == CV_32F && f.isContinuous());
    const int n = f.rows * f.cols;
    std::vector<int> order(n);
    std::iota(order.begin(), order.end(), 0);

    // One key per pixel, so the shuffle is a property of the position rather
    // than of where the sort happens to visit it -- which keeps the result the
    // same whatever the sort does internally.
    std::mt19937_64 rng(seed);
    std::vector<uint64_t> key(n);
    for (int i = 0; i < n; i++) key[i] = rng();

    const float* v = f.ptr<float>();
    std::sort(std::execution::par, order.begin(), order.end(), [v, &key](int a, int b) {
        if (v[a] != v[b]) return v[a] < v[b];
        // Two pixels can draw the same key; falling through to the index keeps
        // the order total, which std::sort requires.
        return key[a] != key[b] ? key[a] < key[b] : a < b;
    });
    return order;
}

void rank_uniform_unit(cv::Mat& f, std::vector<int>& order)
{
    order = rank_order(f);
    const int n = (int)order.size();
    float* v = f.ptr<float>();
    for (int r = 0; r < n; r++) v[order[r]] = (r + 0.5f) / n;
}

void rank_uniform_signed(cv::Mat& f, std::vector<int>& order)
{
    order = rank_order(f);
    const int n = (int)order.size();
    float* v = f.ptr<float>();
    for (int r = 0; r < n; r++)
        v[order[r]] = (n > 1) ? (2.0f * r / (n - 1) - 1.0f) : 0.0f;
}

cv::Mat threshold_mask(const std::vector<int>& order, cv::Size sz)
{
    // The boundary where a generator's bug stops being a wrong number and
    // becomes a corrupt image, so it is the place to catch a ranking that does
    // not cover the mask.
    CV_Assert((int)order.size() == sz.area());
    cv::Mat mask(sz, CV_8U);
    const long long n = (long long)order.size();
    for (long long r = 0; r < n; r++)
        mask.ptr<uint8_t>()[order[(size_t)r]] = threshold_byte(n - 1 - r, n);
    return mask;
}

// The boundary mismatch, as the right-hand side of a Poisson problem: zero
// everywhere except the four edges, where it holds how badly each edge fails to
// meet the one it would wrap onto. Corners take both contributions, which is why
// this accumulates rather than assigns.
static cv::Mat boundary_jump(const cv::Mat& u)
{
    const int H = u.rows, W = u.cols;
    cv::Mat v = cv::Mat::zeros(H, W, CV_32F);
    for (int x = 0; x < W; x++) {
        const float d = u.at<float>(H - 1, x) - u.at<float>(0, x);
        v.at<float>(0, x)     += d;
        v.at<float>(H - 1, x) -= d;
    }
    for (int y = 0; y < H; y++) {
        const float d = u.at<float>(y, W - 1) - u.at<float>(y, 0);
        v.at<float>(y, 0)     += d;
        v.at<float>(y, W - 1) -= d;
    }
    return v;
}

// The periodic component of `u`: the part of it that tiles.
cv::Mat periodic_component(const cv::Mat& u)
{
    const int H = u.rows, W = u.cols;
    cv::Mat planes[] = {boundary_jump(u), cv::Mat::zeros(H, W, CV_32F)}, cplx;
    cv::merge(planes, 2, cplx);
    cv::dft(cplx, cplx, cv::DFT_COMPLEX_OUTPUT);

    // Divide by the discrete Laplacian's eigenvalues to solve the Poisson
    // equation. The DC term's eigenvalue is zero -- a constant offset is
    // unconstrained, which is right: it is the one part of `s` the boundary
    // cannot pin down, and the rank-flattening that follows would remove it in
    // any case.
    for (int y = 0; y < H; y++) {
        cv::Vec2f* row = cplx.ptr<cv::Vec2f>(y);
        const double cy = 2.0 * std::cos(2.0 * CV_PI * y / H);
        for (int x = 0; x < W; x++) {
            const double eigen = cy + 2.0 * std::cos(2.0 * CV_PI * x / W) - 4.0;
            if (y == 0 && x == 0) { row[x] = cv::Vec2f(0.0f, 0.0f); continue; }
            const float s = (float)(1.0 / eigen);
            row[x] *= s;
        }
    }
    cv::Mat smooth;
    cv::idft(cplx, smooth, cv::DFT_SCALE | cv::DFT_REAL_OUTPUT);
    return u - smooth;
}

std::vector<int> mincut_path(const cv::Mat& cost)
{
    CV_Assert(cost.type() == CV_64F && cost.rows > 0 && cost.cols > 0);
    const int L = cost.rows, k = cost.cols;
    if (k == 1) return std::vector<int>(L, 0);

    constexpr double kInf = std::numeric_limits<double>::infinity();
    // best[s * k + i] -- the cheapest path that began at column s and has
    // reached column i on the current row. Running all k starts together is
    // what makes pinning both ends cost nothing.
    std::vector<double> best((size_t)k * k, kInf), next((size_t)k * k);
    for (int s = 0; s < k; s++) best[(size_t)s * k + s] = cost.at<double>(0, s);

    // step[y][s * k + i] -- which of {i-1, i, i+1} the best path into (s, i) at
    // row y came from, as -1, 0 or +1. One byte per cell: at the sizes this
    // runs on (a 1024 tile and an overlap of tens) that is a few megabytes, and
    // recomputing instead would mean running the whole search again per row.
    std::vector<std::vector<int8_t>> step(L, std::vector<int8_t>((size_t)k * k, 0));

    for (int y = 1; y < L; y++) {
        const double* row = cost.ptr<double>(y);
        for (int s = 0; s < k; s++) {
            const double* prev = &best[(size_t)s * k];
            double* out = &next[(size_t)s * k];
            int8_t* back = &step[y][(size_t)s * k];
            for (int i = 0; i < k; i++) {
                // Ties go to the smallest offset and then to the left, which is
                // arbitrary but must be fixed: an unspecified tie-break would
                // make the mask depend on the comparison order.
                double bestPrev = prev[i];
                int8_t from = 0;
                if (i > 0 && prev[i - 1] < bestPrev)     { bestPrev = prev[i - 1]; from = -1; }
                if (i + 1 < k && prev[i + 1] < bestPrev) { bestPrev = prev[i + 1]; from = +1; }
                out[i] = row[i] + bestPrev;
                back[i] = from;
            }
        }
        best.swap(next);
    }

    int start = 0;
    double total = kInf;
    for (int s = 0; s < k; s++) {
        const double end = best[(size_t)s * k + s];   // pinned: ends where it began
        if (end < total) { total = end; start = s; }
    }

    std::vector<int> path(L);
    path[L - 1] = start;
    for (int y = L - 1; y > 0; y--)
        path[y - 1] = path[y] + step[y][(size_t)start * k + path[y]];
    return path;
}

cv::Mat quilt_axis(const cv::Mat& c, int keep, int overlap, int axis)
{
    CV_Assert(c.type() == CV_32F && (axis == 0 || axis == 1));
    CV_Assert(overlap >= 0 && keep > overlap);
    // Work in rows-along-`axis` orientation and transpose back at the end,
    // rather than writing the whole thing twice with the indices swapped.
    cv::Mat a = (axis == 1) ? c : c.t();
    CV_Assert(a.cols == keep + overlap);
    if (overlap == 0) return (axis == 1) ? a(cv::Rect(0, 0, keep, a.rows)).clone()
                                         : a(cv::Rect(0, 0, keep, a.rows)).t();

    cv::Mat cost(a.rows, overlap, CV_64F);
    for (int y = 0; y < a.rows; y++) {
        const float* src = a.ptr<float>(y);
        double* dst = cost.ptr<double>(y);
        for (int i = 0; i < overlap; i++) {
            const double d = (double)src[i] - (double)src[keep + i];
            dst[i] = d * d;
        }
    }
    const std::vector<int> path = mincut_path(cost);

    cv::Mat out = a(cv::Rect(0, 0, keep, a.rows)).clone();
    for (int y = 0; y < a.rows; y++) {
        const float* src = a.ptr<float>(y);
        float* dst = out.ptr<float>(y);
        // Left of the cut take the continuation, right of it the original. At
        // the wrap that puts column keep-1 next to what genuinely followed it in
        // the sample; at the inner edge it puts the original's own neighbours
        // back together.
        for (int i = 0; i < path[y]; i++) dst[i] = src[keep + i];
    }
    return (axis == 1) ? out : out.t();
}

Tensor structure_tensor(const cv::Mat& f)
{
    CV_Assert(f.type() == CV_32F);
    const int H = f.rows, W = f.cols;
    if (H < 3 || W < 3) return {};

    double Jxx = 0, Jyy = 0, Jxy = 0;
    for (int y = 0; y < H; y++) {
        const float* up   = f.ptr<float>(y ? y - 1 : H - 1);
        const float* row  = f.ptr<float>(y);
        const float* down = f.ptr<float>(y + 1 < H ? y + 1 : 0);
        for (int x = 0; x < W; x++) {
            const double gx = 0.5 * (row[x + 1 < W ? x + 1 : 0] - row[x ? x - 1 : W - 1]);
            const double gy = 0.5 * (down[x] - up[x]);
            Jxx += gx * gx; Jyy += gy * gy; Jxy += gx * gy;
        }
    }
    return {Jxx, Jyy, Jxy};
}

double coherence_of(const Tensor& t)
{
    const double tr = t.xx + t.yy;
    return tr > 0 ? std::hypot(t.xx - t.yy, 2.0 * t.xy) / tr : 0.0;
}

void highpass_toroidal(cv::Mat& field, double px)
{
    if (px <= 1.0) return;
    const double sigma = kHighpassSigmaPerPixel * px;
    // 4 sigma, not 3. The padding is what makes this toroidal, and 3 sigma is
    // not enough to make it so: measured against an exactly toroidal blur (the
    // Gaussian applied as a transfer function in the frequency domain), 3 sigma
    // leaves an error of 0.0050 at the border where 4 leaves 0.0001. That
    // sounds negligible and is not, because the error is *localised at the
    // border* -- exactly where a seam is judged -- and because ranking
    // afterwards has no notion of how small a difference is. On a two-level
    // sample the whole field sorts into two clusters of spread 0.01, so a
    // border error of 0.005 reorders half of it.
    const int pad = std::min({(int)std::ceil(4 * sigma), field.rows, field.cols});
    cv::Mat padded, coarse;
    cv::copyMakeBorder(field, padded, pad, pad, pad, pad, cv::BORDER_WRAP);
    cv::GaussianBlur(padded, coarse, cv::Size(), sigma, sigma, cv::BORDER_REFLECT);
    field -= coarse(cv::Rect(pad, pad, field.cols, field.rows));
}

cv::Mat prepare_texture_sample(const cv::Mat& sample, double highpass, double scale)
{
    cv::Mat f;
    sample.convertTo(f, CV_32F);
    if (highpass > 1.0) {
        cv::Mat coarse;
        const double sigma = kHighpassSigmaPerPixel * highpass;
        cv::GaussianBlur(f, coarse, cv::Size(), sigma, sigma, cv::BORDER_REFLECT);
        f -= coarse;
    }
    if (scale != 1.0)
        cv::resize(f, f, cv::Size(), scale, scale,
                   scale > 1.0 ? cv::INTER_CUBIC : cv::INTER_AREA);
    return f;
}

cv::Mat local_quantile(const cv::Mat& f, double sigma, uint64_t seed)
{
    CV_Assert(f.type() == CV_32F && f.isContinuous() && sigma > 0.0);
    const int H = f.rows, W = f.cols;
    const long long N = (long long)H * W;

    // One total order: by value, ties shuffled from the seed. Every later step
    // reads positions in it, so a tie group is broken once and consistently.
    const std::vector<int> order = rank_order_random(f, seed);
    const float* v = f.ptr<float>();

    // Levels: each distinct value its own, which is exact. Past 256 of them the
    // count is what costs, so pool into equal-count groups of the same order --
    // cutting a strict order anywhere is still a strict order.
    constexpr int kMaxLevels = 256;
    std::vector<long long> start{0};
    for (long long r = 1; r < N; r++)
        if (v[order[(size_t)r]] != v[order[(size_t)r - 1]]) start.push_back(r);
    if ((int)start.size() > kMaxLevels) {
        start.clear();
        for (int b = 0; b < kMaxLevels; b++) start.push_back(N * b / kMaxLevels);
    }
    start.push_back(N);

    // The coarse grid. Cells of sigma/3 keep the weight smooth across a cell, and
    // a pixel's share is read back bilinearly from the four cells round it.
    const int F = std::max(1, (int)std::floor(sigma / 3.0));
    const int Hc = (H + F - 1) / F, Wc = (W + F - 1) / F;
    const double sc = sigma / F;
    auto weigh = [&](cv::Mat& m) {
        // Zero outside the sample, and normalised by the weight of what is
        // inside, so the edge is a truncated neighbourhood rather than a
        // mirrored one: reflecting counts the pixels nearest the edge twice,
        // which is exactly where a seam is judged.
        cv::GaussianBlur(m, m, cv::Size(), sc, sc, cv::BORDER_CONSTANT);
    };
    cv::Mat norm(Hc, Wc, CV_32F, cv::Scalar(0));
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) norm.at<float>(y / F, x / F) += 1.0f;
    weigh(norm);

    // Bilinear lookup of a coarse map at pixel p, by cell centres.
    std::vector<int> y0(H), y1(H), x0(W), x1(W);
    std::vector<float> ty(H), tx(W);
    auto axis = [F](int n, int cells, std::vector<int>& i0, std::vector<int>& i1,
                    std::vector<float>& t) {
        for (int p = 0; p < n; p++) {
            const double c = (p + 0.5) / F - 0.5;
            const int a = std::clamp((int)std::floor(c), 0, cells - 1);
            i0[p] = a;
            i1[p] = std::min(a + 1, cells - 1);
            t[p] = (float)std::clamp(c - a, 0.0, 1.0);
        }
    };
    axis(H, Hc, y0, y1, ty);
    axis(W, Wc, x0, x1, tx);
    auto at = [&](const cv::Mat& m, int p) {
        const int y = p / W, x = p % W;
        const float* r0 = m.ptr<float>(y0[y]);
        const float* r1 = m.ptr<float>(y1[y]);
        const float top = r0[x0[x]] + tx[x] * (r0[x1[x]] - r0[x0[x]]);
        const float bot = r1[x0[x]] + tx[x] * (r1[x1[x]] - r1[x0[x]]);
        return top + ty[y] * (bot - top);
    };

    // Rising through the levels, `below` holds the weighted share of each
    // neighbourhood already passed. A pixel's quantile is that, plus its own
    // level's share scaled by where it sits in the level -- which, for a tie
    // group, is a uniform draw, and makes this the randomised transform.
    cv::Mat below(Hc, Wc, CV_32F, cv::Scalar(0)), level(Hc, Wc, CV_32F);
    cv::Mat inv;
    cv::divide(1.0, cv::max(norm, 1e-12f), inv);
    cv::Mat u(H, W, CV_32F);
    float* out = u.ptr<float>();
    for (size_t b = 0; b + 1 < start.size(); b++) {
        const long long lo = start[b], hi = start[b + 1];
        level.setTo(0);
        for (long long r = lo; r < hi; r++) {
            const int p = order[(size_t)r];
            level.at<float>((p / W) / F, (p % W) / F) += 1.0f;
        }
        weigh(level);
        level = level.mul(inv);
        const double n = (double)(hi - lo);
        for (long long r = lo; r < hi; r++) {
            const int p = order[(size_t)r];
            out[p] = at(below, p) + (float)((r - lo + 0.5) / n) * at(level, p);
        }
        below += level;
    }
    return u;
}

// --- band power ---------------------------------------------------------------

namespace {
// The band a frequency cell falls in, or -1 below the range. Out to the corner
// of the frequency plane, sqrt(1/2), not 1/2: a band set that stopped at the
// axis Nyquist left the corners uncorrected, and once the bands inside them
// were scaled down the re-rank handed the corners the difference -- 10 to 40x
// the sample's power there, a fine speckle on every mask.
int band_of(int y, int x, int H, int W, double lo, int n)
{
    constexpr double hi = 0.70710678118654752;
    const double r = std::hypot(bin_freq(y, H), bin_freq(x, W));
    if (r < lo || r > hi) return -1;
    return std::clamp((int)(n * std::log(r / lo) / std::log(hi / lo)), 0, n - 1);
}
cv::Mat spectrum_of(const cv::Mat& f)
{
    cv::Mat z;
    f.convertTo(z, CV_32F);
    z -= cv::mean(z)[0];
    cv::Mat c;
    cv::dft(z, c, cv::DFT_COMPLEX_OUTPUT);
    return c;
}
} // namespace

std::vector<double> band_power(const cv::Mat& f, cv::Size span, int n)
{
    const double lo = 1.0 / std::max(span.width, span.height);
    const cv::Mat c = spectrum_of(f);
    std::vector<double> sum(n, 0.0), cells(n, 0.0);
    for (int y = 0; y < f.rows; y++)
        for (int x = 0; x < f.cols; x++) {
            if (!y && !x) continue;
            const int b = band_of(y, x, f.rows, f.cols, lo, n);
            if (b < 0) continue;
            const cv::Vec2f v = c.at<cv::Vec2f>(y, x);
            sum[b] += (double)v[0] * v[0] + (double)v[1] * v[1];
            cells[b] += 1;
        }
    const double N = (double)f.total();
    for (int b = 0; b < n; b++) sum[b] = cells[b] ? sum[b] / cells[b] / N : 0.0;
    return sum;
}

std::vector<double> band_reference(const std::vector<cv::Mat>& scans, cv::Size field,
                                   int n, double equalise)
{
    if (n < 1) return {};
    // Sixteen tiles, shared out so the estimate describes every scan and every
    // part of each. A raster that stopped at sixteen took them all from the top
    // of the first scan big enough, so the other scans counted for nothing and
    // the order the files came in decided the target.
    //
    // Policy: each scan that can offer a tile gets at least one, the rest go in
    // proportion to area (largest remainder, ties to the earlier scan); past
    // sixteen such scans, the sixteen largest get one each. Within a scan the
    // tiles sit on an even grid spanning it. Every tile weighs the same.
    constexpr int kTiles = 16;
    struct Usable { size_t i; int tw, th; double area; int n = 0; };
    std::vector<Usable> use;
    for (size_t i = 0; i < scans.size(); i++) {
        const cv::Mat& s = scans[i];
        const int tw = std::min(field.width, s.cols), th = std::min(field.height, s.rows);
        if (tw >= 16 && th >= 16) use.push_back({i, tw, th, (double)s.total()});
    }
    if (use.empty()) return {};
    if ((int)use.size() > kTiles) {
        std::stable_sort(use.begin(), use.end(),
                         [](const Usable& a, const Usable& b) { return a.area > b.area; });
        use.resize(kTiles);
        for (Usable& u : use) u.n = 1;
    } else {
        double total = 0;
        for (const Usable& u : use) total += u.area;
        const int spare = kTiles - (int)use.size();
        std::vector<std::pair<double, size_t>> rem;
        int given = 0;
        for (size_t k = 0; k < use.size(); k++) {
            const double q = spare * use[k].area / total;
            use[k].n = 1 + (int)q;
            given += (int)q;
            rem.push_back({q - (int)q, k});
        }
        std::stable_sort(rem.begin(), rem.end(),
                         [](const auto& a, const auto& b) { return a.first > b.first; });
        for (size_t r = 0; given < spare && r < rem.size(); r++, given++) use[rem[r].second].n++;
    }

    std::vector<double> acc(n, 0.0);
    int tiles = 0;
    for (const Usable& u : use) {
        const cv::Mat& s = scans[u.i];
        const int X = s.cols - u.tw, Y = s.rows - u.th;
        const int cols = std::clamp((int)std::lround(std::sqrt(u.n * (double)s.cols / s.rows)),
                                    1, u.n);
        const int rows = (u.n + cols - 1) / cols;
        for (int k = 0; k < u.n; k++) {
            const int x = (int)std::lround(X * ((k % cols) + 0.5) / cols);
            const int y = (int)std::lround(Y * ((k / cols) + 0.5) / rows);
            cv::Mat t;
            s(cv::Rect(x, y, u.tw, u.th)).convertTo(t, CV_32F);
            if (equalise > 0) t = local_quantile(t, equalise, 1);
            std::vector<int> order;
            rank_uniform_unit(t, order);
            const std::vector<double> p = band_power(t, field, n);
            for (int b = 0; b < n; b++) acc[b] += p[b];
            tiles++;
        }
    }
    for (double& v : acc) v /= tiles;
    return acc;
}

void impose_band_power(cv::Mat& field, const std::vector<double>& ref)
{
    const int n = (int)ref.size();
    if (n < 1) return;
    const std::vector<double> cur = band_power(field, field.size(), n);
    const double lo = 1.0 / std::max(field.cols, field.rows);
    cv::Mat c = spectrum_of(field);
    for (int y = 0; y < field.rows; y++)
        for (int x = 0; x < field.cols; x++) {
            if (!y && !x) continue;
            const int b = band_of(y, x, field.rows, field.cols, lo, n);
            if (b < 0 || !(cur[b] > 0) || !(ref[b] > 0)) continue;
            c.at<cv::Vec2f>(y, x) *= (float)std::sqrt(ref[b] / cur[b]);
        }
    cv::dft(c, field, cv::DFT_INVERSE | cv::DFT_SCALE | cv::DFT_REAL_OUTPUT);
}

} // namespace maskutil
