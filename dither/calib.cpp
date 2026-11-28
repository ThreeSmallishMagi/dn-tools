// SPDX-License-Identifier: GPL-3.0-or-later
// calib.cpp — see calib.h.
//
// `level_set_low`, `clump_profile` and `reference_mask` were markovcalib.cpp's
// and are unchanged here, which is deliberate: markov's calibration has been
// validated against hand sweeps and against test/levelfit, and moving it must
// not move its answers. `ridge_profile` is new and only inpaint uses it.
#include "calib.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <vector>
#include <opencv2/opencv.hpp>

#include "maskutil.h"

namespace calib {

// test/patchfit's block size, so the two measure the same thing.
constexpr int kRidgeBlock = 32;

double level_set_low(const cv::Mat& mask8, double g)
{
    const int H = mask8.rows, W = mask8.cols, N = H * W;

    // The cells the dither lights at density g -- MaskDither fires where
    // g*254 > t -- rather than the lowest round(g*N) cells. A mask holds 255
    // values, so the second cuts through a tie group, and partial_sort cut it
    // wherever it happened to: the measure moved when the same mask was merely
    // reflected, and a calibration moved with it.
    cv::Mat b(H, W, CV_32F);
    const uint8_t* v = mask8.ptr<uint8_t>();
    long long on = 0;
    for (int i = 0; i < N; i++) {
        const bool lit = g * 254.0 > v[i];
        b.ptr<float>()[i] = lit ? 1.0f : 0.0f;
        on += lit;
    }
    if (on == 0 || on == N) return std::numeric_limits<double>::quiet_NaN();
    g = (double)on / N;                                 // the density it really is
    b -= cv::mean(b)[0];

    cv::Mat planes[] = {b, cv::Mat::zeros(b.size(), CV_32F)}, cplx;
    cv::merge(planes, 2, cplx);
    cv::dft(cplx, cplx, cv::DFT_COMPLEX_OUTPUT);
    cv::split(cplx, planes);

    const double norm = (double)N * g * (1.0 - g);
    double sum = 0;
    int count = 0;
    for (int y = 0; y < H; y++) {
        const double fy = maskutil::bin_freq(y, H);
        for (int x = 0; x < W; x++) {
            if (!y && !x) continue;                       // DC carries the mean
            const double fx = maskutil::bin_freq(x, W);
            if (fy * fy + fx * fx >= 0.01) continue;      // below 0.1 cycles/px
            const double re = planes[0].at<float>(y, x), im = planes[1].at<float>(y, x);
            sum += (re * re + im * im) / norm;
            count++;
        }
    }
    return count ? sum / count : 0.0;
}

ClumpProfile clump_profile(const cv::Mat& mask8)
{
    ClumpProfile p{};
    for (size_t i = 0; i < p.size(); i++) p[i] = level_set_low(mask8, kLevels[i]);
    return p;
}

namespace {

// Gaussian blur that treats the field as a torus. cv::GaussianBlur will not
// take BORDER_WRAP, so the field is wrap-padded and the padding cut off again.
cv::Mat blur_toroidal(const cv::Mat& f, double sigma)
{
    const int pad = std::max(1, (int)std::ceil(3.0 * sigma));
    cv::Mat big;
    cv::copyMakeBorder(f, big, pad, pad, pad, pad, cv::BORDER_WRAP);
    cv::GaussianBlur(big, big, cv::Size(), sigma, sigma, cv::BORDER_REPLICATE);
    return big(cv::Rect(pad, pad, f.cols, f.rows)).clone();
}

} // namespace

RidgeProfile ridge_profile(const cv::Mat& mask8)
{
    cv::Mat f;
    mask8.convertTo(f, CV_32F, 1.0 / 254.0);
    f -= cv::mean(f)[0];

    RidgeProfile out{};
    cv::Mat prev = f;
    for (int s = 0; s < kOctaves; s++) {
        // The same octave ladder test/patchfit uses: successive Gaussians a
        // factor of two apart, each band the difference between neighbours.
        const cv::Mat smooth = blur_toroidal(f, 1.4 * (1 << s));
        const cv::Mat band = prev - smooth;
        prev = smooth;

        // **Per block, then averaged** -- coherence over the whole field is a
        // different quantity. Summing the tensor first lets ridges at opposing
        // angles cancel, so a field of concentric rings reads as isotropic as
        // white noise (1e-8 against a local 0.95). Worming has no single
        // direction, so it is only visible locally. test/patchfit uses the same
        // block size for the same reason; maskutil::field_coherence is the
        // deliberately global measure and answers a different question.
        const int H = band.rows, W = band.cols;
        double acc = 0;
        int blocks = 0;
        for (int by = 0; by + kRidgeBlock <= H; by += kRidgeBlock)
            for (int bx = 0; bx + kRidgeBlock <= W; bx += kRidgeBlock) {
                double Jxx = 0, Jyy = 0, Jxy = 0;
                for (int y = by; y < by + kRidgeBlock; y++) {
                    const float* up   = band.ptr<float>(y ? y - 1 : H - 1);
                    const float* row  = band.ptr<float>(y);
                    const float* down = band.ptr<float>(y + 1 < H ? y + 1 : 0);
                    for (int x = bx; x < bx + kRidgeBlock; x++) {
                        const double gx = 0.5 * (row[x + 1 < W ? x + 1 : 0]
                                               - row[x ? x - 1 : W - 1]);
                        const double gy = 0.5 * (down[x] - up[x]);
                        Jxx += gx * gx; Jyy += gy * gy; Jxy += gx * gy;
                    }
                }
                const double tr = Jxx + Jyy;
                if (tr > 0) { acc += std::hypot(Jxx - Jyy, 2.0 * Jxy) / tr; blocks++; }
            }
        out[s] = blocks ? acc / blocks : 0.0;
    }
    return out;
}

SpectrumProfile spectrum_profile(const cv::Mat& mask8, double lowCut)
{
    cv::Mat f;
    mask8.convertTo(f, CV_32F, 1.0 / 254.0);
    f -= cv::mean(f)[0];

    cv::Mat planes[] = {f, cv::Mat::zeros(f.size(), CV_32F)}, cplx;
    cv::merge(planes, 2, cplx);
    cv::dft(cplx, cplx, cv::DFT_COMPLEX_OUTPUT);
    cv::split(cplx, planes);

    // Octave-wide bands from Nyquist down, stopping at the high-pass cutoff:
    // below it the sample was deliberately emptied, so a mask has nothing to
    // match and the ratio would be two near-zeroes.
    // An empty or inverted band has nothing to measure. lowCut comes from the
    // high-pass, so --mask-highpass 2 puts it exactly at Nyquist: log(hi/lo)
    // is then zero and the ratio below is 0/0, which reached a cast to int.
    const double hi = 0.5;
    const double lo = std::max(lowCut, hi / std::pow(2.0, kBands));
    SpectrumProfile p{};
    if (!(lo > 0.0) || !(lo < hi)) return p;
    std::array<double, kBands> sum{}, count{};
    const int H = mask8.rows, W = mask8.cols;
    for (int y = 0; y < H; y++) {
        const double fy = maskutil::bin_freq(y, H);
        for (int x = 0; x < W; x++) {
            if (!y && !x) continue;
            const double fx = maskutil::bin_freq(x, W);
            const double r = std::sqrt(fy * fy + fx * fx);
            if (r < lo || r > hi) continue;
            const double t = std::log(r / lo) / std::log(hi / lo);
            const int b = std::clamp((int)(t * kBands), 0, kBands - 1);
            const double re = planes[0].at<float>(y, x), im = planes[1].at<float>(y, x);
            sum[b] += re * re + im * im;
            count[b] += 1;
        }
    }
    for (int i = 0; i < kBands; i++) p[i] = count[i] ? sum[i] / count[i] : 0.0;
    return p;
}

double profile_distance(const double* a, const double* ref, size_t n)
{
    double acc = 0;
    int used = 0;
    for (size_t i = 0; i < n; i++) {
        const double la = a[i], lr = ref[i];
        if (!(la > 0 && lr > 0)) continue;
        const double d = std::log10(la / lr);
        acc += d * d; used++;
    }
    // NaN rather than a huge number: nothing was comparable, which is not the
    // same as being very different, and a sentinel that sorts like a score gets
    // compared like one.
    return used ? std::sqrt(acc / used)
                : std::numeric_limits<double>::quiet_NaN();
}

cv::Mat reference_mask(const cv::Mat& sample, double highpass, double scale, cv::Size sz)
{
    cv::Mat f = maskutil::prepare_texture_sample(sample, highpass, scale);
    if (f.cols < sz.width || f.rows < sz.height) return {};
    const cv::Rect roi((f.cols - sz.width) / 2, (f.rows - sz.height) / 2,
                       sz.width, sz.height);
    cv::Mat crop = f(roi).clone();
    return maskutil::threshold_mask(maskutil::rank_order(crop), crop.size());
}

bool usable(const params::Params& P, std::string_view key, double v, bool wholeNumber)
{
    if (!std::isfinite(v)) return false;
    if (wholeNumber && std::abs(v - std::round(v)) > 1e-9) return false;
    const auto r = P.range_of(key);
    return !r || (v >= r->first && v <= r->second);
}

std::optional<Inputs> inputs_for(const std::string& spec, bool crop_first,
                                 const samples::Set* read)
{
    Inputs in;
    in.set = read && !read->empty()
           ? *read
           : samples::Set::read(samples::resolve(spec, crop_first), nullptr);
    if (in.set.empty()) return std::nullopt;
    in.reference = samples::reference_for(spec, in.set);
    in.scoring = in.reference.primary();
    const std::string crop = samples::is_path(spec) ? std::string() : samples::crop_for(spec);
    in.described = !crop.empty() ? "the film's crop, " + crop
                 : in.set.size() == 1 ? "the sample" : "the largest of its scans";
    return in;
}

void mix_set(uint64_t& h, const samples::Set& s)
{
    const long long n = (long long)s.size();
    const auto* b = reinterpret_cast<const unsigned char*>(&n);
    for (size_t i = 0; i < sizeof n; i++) { h ^= b[i]; h *= 1099511628211ull; }
    for (const cv::Mat& m : s) mix_image(h, m);
}

void mix_image(uint64_t& h, const cv::Mat& m)
{
    auto mix = [&h](const void* data, size_t n) {
        const auto* b = static_cast<const unsigned char*>(data);
        for (size_t i = 0; i < n; i++) { h ^= b[i]; h *= 1099511628211ull; }
    };
    const long long dims[] = {m.rows, m.cols, m.type()};
    mix(dims, sizeof dims);
    const cv::Mat flat = m.isContinuous() ? m : m.clone();
    mix(flat.data, flat.total() * flat.elemSize());
}

} // namespace calib
