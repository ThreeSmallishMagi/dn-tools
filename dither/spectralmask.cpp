// SPDX-License-Identifier: GPL-3.0-or-later
// spectralmask.cpp — synthesise a threshold mask matching a sample's spectrum.
//
// Two constraints are alternately imposed on a random field:
//   - the sample's amplitude spectrum (the arrangement), and
//   - a flat value histogram (what a threshold mask must have to preserve tone).
// Neither projection is undone by the other in the limit, and the pair converges
// in distribution -- not to a fixed point. 30 rounds still differ from 20 on
// 1.6% of pixels by one threshold level, so there is nothing for a convergence
// test to detect; the iteration count is where quality plateaus. A greedy void-and-cluster loop can't
// reach a broadband target this way: with no temperature term its kernel shape
// alone decides the outcome, so it only ever produces one characteristic scale.
//
// The whole 2D spectrum is imposed, not a radial average, so an oriented sample
// (a weave, a directional grain) keeps its direction. What phase randomisation
// still discards is everything higher-order — edges, motifs, anything whose
// character lives in how the frequencies line up rather than in how much of each
// there is. A woven texture comes back as woven-looking noise, not as weave.

#include "spectralmask.h"
#include "maskutil.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <random>
#include <vector>
#include <opencv2/imgproc.hpp>

namespace {

// Smooth the power spectrum over several of the frequency cells one mask bin
// covers. A periodogram is one noisy realisation; imposing it directly copies
// the sample's speckle rather than its spectrum. Broad directional lobes — a
// weave — survive this, so it buys fidelity on isotropic samples without
// costing orientation. Smoothing much harder converges on a radial average,
// which fits isotropic grain slightly better but erases direction entirely.
void smooth_power(cv::Mat& power, cv::Size sz, double cells) {
    if (cells <= 0.0) return;
    // `cells` is quoted for a sample the same area as the mask. A bigger sample
    // resolves the spectrum finer, so a window of the same width in mask cells
    // averages more of its bins and the estimate is already quieter -- smoothing
    // it as hard then costs bias for variance that is no longer there. The
    // periodogram's variance does not fall with sample size, but the count of
    // sample bins under one mask cell rises with the area, so the noise in the
    // averaged estimate falls as the square root of it: hence sqrt.
    //
    // Measured on three films with the sample area swept over 64x and the mask
    // held at 256^2, the best `cells` tracks r^-0.5 across the whole range, and
    // a free fit lands at r^-0.35 with a coefficient within 3% of this one. The
    // floor is where smoothing stops meaning anything: below one mask cell the
    // estimate is already sharper than the mask can represent, and asking for
    // less buys nothing. It is never raised above what the caller asked for.
    const double r = ((double)power.rows * power.cols)
                   / ((double)sz.height * sz.width);
    cells = std::max(std::min(cells, 1.0), cells / std::sqrt(r));
    double sy = cells * power.rows / (2.0 * sz.height);
    double sx = cells * power.cols / (2.0 * sz.width);
    if (sy <= 0.5 && sx <= 0.5) return;
    // A spectrum wraps, so pad it that way rather than reflecting at DC.
    int pad = (int)std::ceil(3 * std::max(sx, sy));
    cv::Mat padded;
    cv::copyMakeBorder(power, padded, pad, pad, pad, pad, cv::BORDER_WRAP);
    cv::GaussianBlur(padded, padded, cv::Size(), sx, sy, cv::BORDER_REFLECT);
    padded(cv::Rect(pad, pad, power.cols, power.rows)).copyTo(power);
}

// |DFT| of the sample, after removing structure coarser than `highpass` pixels.
// Subtracting a Gaussian blur is exactly a (1 - G) high-pass; the half-power
// point sits at 1/highpass cycles/px, the same convention maskstats uses.
cv::Mat sample_amplitude(const cv::Mat& sample, double highpass, cv::Size sz,
                         double smooth) {
    cv::Mat f;
    sample.convertTo(f, CV_32F);
    if (highpass > 1.0) {
        cv::Mat coarse;
        double sigma = maskutil::kHighpassSigmaPerPixel * highpass;
        cv::GaussianBlur(f, coarse, cv::Size(), sigma, sigma, cv::BORDER_REFLECT);
        f -= coarse;
    } else {
        f -= cv::mean(f)[0];
    }

    cv::Mat planes[] = {f, cv::Mat::zeros(f.size(), CV_32F)}, cplx, amp;
    cv::merge(planes, 2, cplx);
    cv::dft(cplx, cplx, cv::DFT_COMPLEX_OUTPUT);
    cv::split(cplx, planes);
    cv::magnitude(planes[0], planes[1], amp);
    cv::Mat power = amp.mul(amp);
    smooth_power(power, sz, smooth);
    cv::sqrt(power, amp);
    return amp;
}

inline float wrapped(const cv::Mat& m, int y, int x) {
    return m.at<float>(maskutil::wrap_any(y, m.rows), maskutil::wrap_any(x, m.cols));
}

// Bilinear read from the sample's spectrum, at a position given in bins.
// `wrapped` handles the negative frequencies that the upper half of a DFT row
// holds; the caller is responsible for not asking past the sample's Nyquist,
// where wrapping would fold rather than run out.
float sample_amplitude_at(const cv::Mat& sample_amp, double fy, double fx) {
    const int y0 = (int)std::floor(fy), x0 = (int)std::floor(fx);
    const double wy = fy - y0, wx = fx - x0;
    return (float)((1 - wy) * (1 - wx) * wrapped(sample_amp, y0,     x0) +
                   (    wy) * (1 - wx) * wrapped(sample_amp, y0 + 1, x0) +
                   (1 - wy) * (    wx) * wrapped(sample_amp, y0,     x0 + 1) +
                   (    wy) * (    wx) * wrapped(sample_amp, y0 + 1, x0 + 1));
}

// The sample's amplitude resampled onto the mask's frequency lattice. Both grids
// are addressed in cycles/pixel, so samples of any size and aspect work; `scale`
// multiplies feature size, which divides frequency.
cv::Mat amplitude_grid(const cv::Mat& sample_amp, cv::Size sz, double scale) {
    cv::Mat amp(sz, CV_32F);
    for (int y = 0; y < sz.height; y++) {
        // Where this mask bin sits in the *sample's* spectrum. Past the sample's
        // Nyquist there is nothing to copy: asking for grain `scale` times
        // coarser is asking for a band-limited version of it, and the band ends
        // at 0.5/scale cycles per pixel.
        //
        // sample_amplitude_at() must not be asked past that edge. Its wrapping
        // is right for negative frequencies but for a frequency genuinely off
        // the end it folds — mask bin 0.4 c/px at scale 2 asks for 0.8 and gets
        // 0.2, near the peak. That reflected the main lobe back above the fold
        // as a second lobe of ~29% of peak power at scale 2, and at scale 4 it
        // folded repeatedly into noise.
        const double sfy = maskutil::bin_freq(y, sz.height) * scale;
        const bool y_off = std::abs(sfy) > 0.5;
        for (int x = 0; x < sz.width; x++) {
            const double sfx = maskutil::bin_freq(x, sz.width) * scale;
            const bool off_band = y_off || std::abs(sfx) > 0.5;
            const bool dc = (x == 0 && y == 0);
            amp.at<float>(y, x) = (dc || off_band) ? 0.0f
                : sample_amplitude_at(sample_amp, sfy * sample_amp.rows,
                                                  sfx * sample_amp.cols);
        }
    }
    return amp;
}

// Replace the field's amplitude spectrum with `amp`, keeping its phase.
void impose_amplitude(cv::Mat& f, const cv::Mat& amp, cv::Mat planes[2], cv::Mat& cplx) {
    planes[0] = f;
    planes[1] = cv::Mat::zeros(f.size(), CV_32F);
    cv::merge(planes, 2, cplx);
    cv::dft(cplx, cplx, cv::DFT_COMPLEX_OUTPUT);
    for (int y = 0; y < f.rows; y++) {
        cv::Vec2f* row = cplx.ptr<cv::Vec2f>(y);
        const float* a = amp.ptr<float>(y);
        for (int x = 0; x < f.cols; x++) {
            const float m = std::hypot(row[x][0], row[x][1]);
            const float k = (m > 1e-12f) ? a[x] / m : 0.0f;
            row[x][0] *= k;
            row[x][1] *= k;
        }
    }
    cv::idft(cplx, cplx, cv::DFT_SCALE);
    cv::split(cplx, planes);
    f = planes[0];
}

} // namespace

// The synthesis without the mask: the ordering the alternating projections
// settle on. build_spectral_mask is this plus `threshold_mask`; maketexture is
// this plus a map onto the sample's own values. Same code either way.
// The ordering the alternating projections settle on, given the amplitude to
// impose. Split out so the single-sample path (maketexture) and the set path
// (the generator) share everything after the spectrum is known.
static std::vector<int> order_from_amplitude(cv::Size sz, const cv::Mat& amp,
                                             int iters, uint64_t seed)
{
    const int H = sz.height, W = sz.width, N = H * W;
    std::mt19937_64 rng(seed);
    std::normal_distribution<float> gauss(0.0f, 1.0f);
    cv::Mat f(H, W, CV_32F);
    for (int i = 0; i < N; i++) f.ptr<float>()[i] = gauss(rng);

    std::vector<int> order(N);
    cv::Mat planes[2], cplx;
    for (int it = 0; it < iters; it++) {
        // The two projections the method alternates: give the field the
        // sample's amplitudes, then give it a flat histogram. Neither is a
        // fixed point of the other, which is why this iterates.
        impose_amplitude(f, amp, planes, cplx);
        maskutil::rank_uniform_signed(f, order);
    }

    return order;
}

std::vector<int> synthesise_spectral_order(cv::Size sz, const cv::Mat& sample,
                                           double scale, double highpass, int iters,
                                           double smooth, uint64_t seed)
{
    return order_from_amplitude(
        sz, amplitude_grid(sample_amplitude(sample, highpass, sz, smooth), sz, scale),
        iters, seed);
}

// The amplitude to impose, over however many scans there are.
//
// One scan returns its own grid untouched rather than going round through
// sqrt(a*a), which is not bit-exact in float and would change every mask built
// from a single sample.
static cv::Mat amplitude_for(const samples::Set& set, cv::Size sz, double scale,
                             double highpass, double smooth)
{
    cv::Mat amp = amplitude_grid(sample_amplitude(set[0], highpass, sz, smooth), sz, scale);
    if (set.size() == 1) return amp;

    cv::Mat power = amp.mul(amp);
    for (std::size_t i = 1; i < set.size(); i++) {
        cv::Mat a = amplitude_grid(sample_amplitude(set[i], highpass, sz, smooth), sz, scale);
        power += a.mul(a);
    }
    power /= (double)set.size();
    cv::sqrt(power, amp);
    return amp;
}

cv::Mat build_spectral_mask(cv::Size sz, const samples::Set& set,
                            double scale, double highpass, int iters, double smooth,
                            uint64_t seed)
{
    return maskutil::threshold_mask(
        order_from_amplitude(sz, amplitude_for(set, sz, scale, highpass, smooth),
                             iters, seed), sz);
}

// ---------------------------------------------------------------------------
// Default rules. See DEFAULTS.md.
// ---------------------------------------------------------------------------

double dft_work(int w, int h, int iters)
{
    const double n = (double)w * h;
    return iters * n * std::log2(std::max(n, 2.0));
}

void register_spectral_params(params::Params& P)
{
    // Named once. inpaint has always done this; the others repeated the
    // expression at every rule, which buries what each rule *says* under
    // what they all have in common.
    const auto mine = mask::generator("dft");

    // Work units per second, measured from this machine's own builds and cached
    // in the config file. The compiled-in value is only a first guess; the first
    // --budget run says so, and every build refines it.
    params::add_const(P, spectralmask::keys::Rate, 1.2e8,
                      mine, {{1.0, 1e15}},
                      "measured build throughput, work units per second",
                                        true);   // an intermediate: reaches the mask only via --budget

    // How many of the frequency cells one mask bin covers the sample's
    // periodogram is averaged over before it is imposed, quoted for a sample
    // the same area as the mask -- smooth_power scales it by the actual ratio.
    // A periodogram is one noisy realisation, and imposing it raw copies the
    // sample's speckle rather than its spectrum: film fit 0.41 raw, 0.33
    // smoothed.
    //
    // It used to be a flat 8 at every sample size, which is the value for a
    // sample one quarter the mask's area and much too much for a real scan. On
    // five film samples at the shipped 256^2 mask -- where a full scan runs 34
    // to 326 times the mask's area -- 8 was the *worst* of every value tried:
    // spectral fit 0.0120 against 0.0023 here on 4Xcrop, and the same 4-5x on
    // three of the others.
    params::add_const(P, spectralmask::keys::SmoothCells, 4.0,
                      mine, {{0.0, 256.0}},
                      "frequency cells the sample's periodogram is averaged over");

    params::add_const(P, spectralmask::keys::MaxRepeats, (long long)4,
                      mine, {{1, 1024}},
                      "tile repeats the grid may show before it looks regular");

    params::add_const(P, spectralmask::keys::MaxSize, (long long)1024,
                      mine, {{16, 8192}},
                      "largest tile worth building, absent a --budget");
    // 20 is where quality stops improving, not where the iteration stops
    // moving: measured against a film sample the spectral fit goes 0.0367 at 5
    // rounds, 0.0073 at 10, 0.0030 at 15, 0.0023 at 20 and 0.0023 at 30. (Those
    // figures are five times better than the ones this comment used to quote,
    // which were taken before smooth_cells was scaled to the sample; the
    // plateau is still at 20.)
    //
    // There is deliberately no convergence test here. The two projections never
    // reach an exact fixed point — 30 rounds still differ from 20 on 1.6% of
    // pixels, by one threshold level — so "stop when the ranking stabilises"
    // has nothing to detect. It converges in distribution, not in configuration.
    params::add_const(P, spectralmask::keys::Iters, (long long)20,
                      mine, {{1, 500}},
                      "alternating-projection rounds; quality plateaus here");

    // Measured from this machine's own builds, never chosen, so it is
    // reachable by --set and is not offered as an option.
    P.hide_option(spectralmask::keys::Rate);
}
