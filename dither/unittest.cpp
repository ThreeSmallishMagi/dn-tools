// SPDX-License-Identifier: GPL-3.0-or-later
// Unit tests: the checks that are about an invariant of the code rather than
// about a property of an image.
//
// Everything under test/ reaches the program by running it and reading back a
// PNG, which is right for tone and density -- those are statements about
// pictures -- and wrong for arithmetic. It cost real bugs: test/budgettest had
// to reconstruct the level ladder in Python to say what it expected, assumed
// evenly spaced pixel values, and so could not express --level-spacing linear
// at all; a probe built on the same wrong assumption read a bias of 32.5 out of
// a program that was correct.
//
// These link dither_lib and call the functions. Nothing here writes a file or
// spawns a process, so a failure names a function rather than an image.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <numeric>
#include <random>
#include <set>
#include <string>
#include <tuple>
#include <vector>
#include <opencv2/opencv.hpp>
#include "engine.h"
#include "directmask.h"
#include "maskutil.h"
#include "maskcache.h"
#include "bluenoise.h"
#include "calib.h"
#include "inpaint.h"
#include "samples.h"
#include "mask.h"
#include "markov.h"
#include "markovmodel.h"
#include "generators.h"
#include "maskparams.h"
#include "srgb.h"

namespace {

struct Case {
    std::string name;
    std::function<std::pair<bool, std::string>()> run;
};
std::vector<Case>& cases() { static std::vector<Case> c; return c; }

struct Register {
    Register(const char* name, std::function<std::pair<bool, std::string>()> f)
    { cases().push_back({name, std::move(f)}); }
};
// __LINE__ has to be expanded before it is pasted, hence the two levels.
#define UT_JOIN2(a, b) a##b
#define UT_JOIN(a, b) UT_JOIN2(a, b)
#define CASE(name) \
    static std::pair<bool, std::string> UT_JOIN(case_, __LINE__)(); \
    static Register UT_JOIN(reg_, __LINE__)(name, UT_JOIN(case_, __LINE__)); \
    static std::pair<bool, std::string> UT_JOIN(case_, __LINE__)()

std::pair<bool, std::string> ok(const std::string& detail) { return {true, detail}; }
std::pair<bool, std::string> no(const std::string& detail) { return {false, detail}; }

// ---------------------------------------------------------------------------
// The level ladder. This is what test/budgettest reconstructed and got wrong,
// so it is the first thing worth asserting directly.
// ---------------------------------------------------------------------------

CASE("make_levels: rungs ascend and span the range, every spacing and count")
{
    for (vision::Spacing sp : {vision::Spacing::Srgb, vision::Spacing::Linear,
                               vision::Spacing::Device})
        for (int n : {2, 3, 4, 5, 8, 16, 17, 256})
            for (bool srgb : {true, false}) {
                const vision::LevelSet L = vision::make_levels(n, srgb, sp);
                if (L.count() != n)
                    return no("count " + std::to_string(L.count()) + " for levels " + std::to_string(n));
                if (L.px.front() != 0 || L.px.back() != 255)
                    return no("endpoints " + std::to_string(L.px.front()) + ".." +
                              std::to_string(L.px.back()) + " at levels " + std::to_string(n));
                if (std::abs(L.lin.front()) > 1e-6 || std::abs(L.lin.back() - 1.0) > 1e-6)
                    return no("linear endpoints not 0..1 at levels " + std::to_string(n));
                for (int k = 1; k < n; k++) {
                    if (L.lin[k] <= L.lin[k - 1])
                        return no("linear values not ascending at levels " + std::to_string(n));
                    // Pixel values are non-decreasing rather than strictly
                    // increasing on purpose: see the collision case below.
                    if (L.px[k] < L.px[k - 1])
                        return no("pixel values decrease at levels " + std::to_string(n));
                }
            }
    return ok("3 spacings x 8 counts x 2 curves");
}

CASE("make_levels: `uniform` is never set on a ladder that is not even")
{
    // to_display short-circuits to linear*(count-1) when this is set, and Space
    // becomes the identity, so setting it on an uneven ladder would silently
    // compute the wrong thing. The converse does not hold and must not be
    // asserted: the flag is decided by the *spacing* rather than by inspecting
    // the rungs, so it is conservative. It misses levels=2, which is trivially
    // even, and Srgb under --linear whenever levels-1 divides 255 -- there
    // px[k] is exactly 255k/(levels-1) and lin is px/255, so the rungs really
    // are even. Both take the ladder-search path instead of the fast one, which
    // is slower and gives the same answer. Only this direction is a bug.
    int missed = 0;
    for (vision::Spacing sp : {vision::Spacing::Srgb, vision::Spacing::Linear,
                               vision::Spacing::Device})
        for (int n = 2; n <= 256; n++)
            for (bool srgb : {true, false}) {
                const vision::LevelSet L = vision::make_levels(n, srgb, sp);
                bool even = true;
                for (int k = 0; k < n; k++)
                    if (std::abs(L.lin[k] - (double)k / (n - 1)) > 1e-6) even = false;
                if (L.uniform && !even)
                    return no("uniform set on an uneven ladder: levels " +
                              std::to_string(n) + " spacing " + std::to_string((int)sp) +
                              " srgb=" + std::to_string(srgb));
                missed += even && !L.uniform;
            }
    return ok("1530 ladders, none wrongly flagged (" + std::to_string(missed) +
              " even ones conservatively unflagged)");
}

CASE("make_levels: rungs share an 8-bit code only where 8 bits cannot separate them")
{
    // Under `linear` spacing the rungs are evenly spaced in light, and near
    // white that is finer than 8-bit sRGB can carry -- two rungs round to one
    // code and become indistinguishable in the output file. It is a limit of
    // the format, not an error, but anything that reads a level back *from* a
    // pixel value is ambiguous there, so it is worth pinning where it starts.
    for (vision::Spacing sp : {vision::Spacing::Srgb, vision::Spacing::Device})
        for (int n = 2; n <= 256; n++)
            for (bool srgb : {true, false}) {
                const vision::LevelSet L = vision::make_levels(n, srgb, sp);
                for (int k = 1; k < n; k++)
                    if (L.px[k] == L.px[k - 1])
                        return no("srgb/device spacing collided at levels " +
                                  std::to_string(n));
            }
    // linear + sRGB is the one that collides, and not until 122 levels --
    // which is what the assertion below pins. This said "above 127", which
    // is neither the measured figure nor consistent with it.
    int first = 0;
    for (int n = 2; n <= 256 && !first; n++) {
        const vision::LevelSet L = vision::make_levels(n, true, vision::Spacing::Linear);
        for (int k = 1; k < n; k++)
            if (L.px[k] == L.px[k - 1]) { first = n; break; }
    }
    // 122 is measured, not derived. The rough differential argument -- near
    // white d(sRGB)/d(linear) is about 0.44, so a step of 1/(n-1) falls under
    // one 8-bit code around n = 114 -- only says roughly where; exactly where
    // depends on how the rounding lands.
    if (first != 122)
        return no("linear+sRGB first collides at levels " + std::to_string(first) +
                  ", expected 122");
    return ok("srgb and device never collide; linear+sRGB first at 122 levels");
}

CASE("make_levels: device and srgb differ exactly when levels-1 does not divide 255")
{
    // The invariant doc/ARCHITECTURE.md and test/devicetest both rest on. Under
    // `device` a rung emits sRGB(k/(levels-1)); under `srgb` it emits whatever
    // its rounded 8-bit stand-in decodes to. Those coincide only when the
    // rounding is exact.
    for (int n = 2; n <= 64; n++) {
        const vision::LevelSet d = vision::make_levels(n, true, vision::Spacing::Device);
        const vision::LevelSet s = vision::make_levels(n, true, vision::Spacing::Srgb);
        bool same = true;
        for (int k = 0; k < n; k++)
            if (std::abs(d.lin[k] - s.lin[k]) > 1e-9) same = false;
        const bool divides = (255 % (n - 1)) == 0;
        if (same != divides)
            return no("levels " + std::to_string(n) + ": ladders " +
                      (same ? "agree" : "differ") + " but 255 % " +
                      std::to_string(n - 1) + " = " + std::to_string(255 % (n - 1)));
    }
    // "255 divides" had the operands the wrong way round: the test is
    // 255 % (n-1) == 0, which is levels-1 dividing 255, as the case title says.
    return ok("levels 2..64, agree iff levels-1 divides 255");
}

// ---------------------------------------------------------------------------
// Input depth. One decode path serves 8- and 16-bit files; the depth decides
// only how many distinct codes there are.
// ---------------------------------------------------------------------------

CASE("to_gray: keeps the bit depth, and never rescales by content")
{
    // It used to min-max normalise anything that was not 8-bit. That was not
    // just lossy: the scale came from the image's own extremes, so the same
    // tones decoded differently depending on the brightest pixel present. This
    // is that bug, written down: two images holding the same grey differ only
    // in whether a brighter pixel exists elsewhere.
    cv::Mat a(4, 4, CV_16U, cv::Scalar(20000));
    cv::Mat b = a.clone();
    b.at<uint16_t>(0, 0) = 60000;
    const cv::Mat ga = vision::to_gray(a), gb = vision::to_gray(b);
    if (ga.depth() != CV_16U || gb.depth() != CV_16U)
        return no("16-bit input did not stay 16-bit");
    if (ga.at<uint16_t>(3, 3) != gb.at<uint16_t>(3, 3))
        return no("a brighter pixel elsewhere changed this one: " +
                  std::to_string(ga.at<uint16_t>(3, 3)) + " vs " +
                  std::to_string(gb.at<uint16_t>(3, 3)));
    cv::Mat e(4, 4, CV_8U, cv::Scalar(7));
    if (vision::to_gray(e).depth() != CV_8U)
        return no("8-bit input did not stay 8-bit");
    return ok("depth preserved, content-independent");
}

CASE("decode_gray: 16-bit is exact, and agrees with 8-bit where the codes coincide")
{
    for (bool srgb : {true, false}) {
        // every 16-bit code decodes to decode_code of its own normalised value
        cv::Mat wide(1, 4, CV_16U);
        const uint16_t probe[] = {0, 1, 26728, 65535};
        for (int i = 0; i < 4; i++) wide.at<uint16_t>(0, i) = probe[i];
        const cv::Mat got = vision::decode_gray(wide, srgb);
        for (int i = 0; i < 4; i++) {
            const float want = vision::decode_code((float)probe[i] / 65535.0f, srgb);
            if (std::abs(got.at<float>(0, i) - want) > 1e-9f)
                return no("16-bit code " + std::to_string(probe[i]) + " decoded wrong");
        }
        // an 8-bit code p and the 16-bit code p*257 are the same colour, so
        // they must decode to the same light
        cv::Mat narrow(1, 256, CV_8U), same(1, 256, CV_16U);
        for (int p = 0; p < 256; p++) {
            narrow.at<uint8_t>(0, p) = (uint8_t)p;
            same.at<uint16_t>(0, p) = (uint16_t)(p * 257);
        }
        const cv::Mat dn = vision::decode_gray(narrow, srgb);
        const cv::Mat dw = vision::decode_gray(same, srgb);
        for (int p = 0; p < 256; p++)
            if (std::abs(dn.at<float>(0, p) - dw.at<float>(0, p)) > 1e-6f)
                return no("8-bit " + std::to_string(p) + " and 16-bit " +
                          std::to_string(p * 257) + " disagree");
    }
    return ok("both curves: exact, and 8/16 agree on shared codes");
}

// A gradient spanning four 8-bit codes. Eight bits gives it four steps; sixteen
// gives it a smooth ramp, and the difference between those is the whole point
// of accepting 16-bit input.
cv::Mat shallow_ramp16(int w, int h)
{
    cv::Mat m(h, w, CV_16U);
    for (int x = 0; x < w; x++) {
        const double t = (double)x / (w - 1);
        const uint16_t v = (uint16_t)std::lround(100 * 257 + t * 4 * 257);
        for (int y = 0; y < h; y++) m.at<uint16_t>(y, x) = v;
    }
    return m;
}

CASE("16-bit input is exploited, not merely accepted")
{
    // The claim: with 256 output levels and error diffusion, a 16-bit target
    // renders a gradient finer than its own 8-bit quantisation could. Scored as
    // RMS of the column means against what the picture really is, so the
    // dither's own noise averages out and only the tone remains.
    const cv::Mat ramp = shallow_ramp16(512, 256);
    cv::Mat eight;                                   // the same picture at 8 bits
    ramp.convertTo(eight, CV_8U, 1.0 / 257.0);

    const vision::LevelSet ladder = vision::make_levels(256, true, vision::Spacing::Srgb);
    const cv::Mat truth = vision::decode_gray(ramp, true);

    double err[2];
    const cv::Mat* inputs[2] = {&ramp, &eight};
    for (int i = 0; i < 2; i++) {
        rng.seed(42);
        RoundDither fn;
        const cv::Mat target = vision::decode_display(*inputs[i], ladder, true);
        const std::vector<cv::Mat> frames =
            ditherPass(target, ladder, fn, spatial_floydsteinberg, 1.0f, 256, 1, -1, -1);
        const cv::Mat shown = vision::encode_levels(frames[0], ladder);
        const cv::Mat light = vision::decode_gray(shown, true);
        double acc = 0;
        for (int x = 0; x < ramp.cols; x++) {
            double got = 0, want = 0;
            for (int y = 0; y < ramp.rows; y++) {
                got  += light.at<float>(y, x);
                want += truth.at<float>(y, x);
            }
            const double d = (got - want) / ramp.rows;
            acc += d * d;
        }
        err[i] = std::sqrt(acc / ramp.cols) * 1e6;
    }
    // Measured around 62 against 793 -- an order of magnitude. Asserted at 4x
    // so this is a check that the mechanism works, not a record of one number.
    if (!(err[0] * 4.0 < err[1])) {
        char buf[120];
        std::snprintf(buf, sizeof buf, "16-bit %.1f vs 8-bit %.1f (x1e-6); expected 4x better",
                      err[0], err[1]);
        return no(buf);
    }
    char buf[120];
    std::snprintf(buf, sizeof buf, "RMS %.1f vs %.1f (x1e-6), %.1fx finer",
                  err[0], err[1], err[1] / err[0]);
    return ok(buf);
}

// ---------------------------------------------------------------------------
// maskutil: the ranking every generator shares.
// ---------------------------------------------------------------------------

CASE("a parameter struct's defaults are the registry's")
{
    // InpaintParams{} and the registry are two statements of one fact, and they
    // drifted: the registry moved to 3 sweeps and 64 checks while the struct
    // still said 6 and 128, so a direct caller of the synthesis API got slower,
    // more worm-prone behaviour than the CLI. MarkovParams had an older instance
    // of the same split.
    params::Params P;
    register_inpaint_params(P);
    register_markov_params(P);
    register_mask_params(P);

    struct { const char* key; long long got; } want[] = {
        {inpaint::keys::Radius, InpaintParams{}.radius},
        {inpaint::keys::Iters,  InpaintParams{}.iters},
        {inpaint::keys::Db,     InpaintParams{}.db},
        {inpaint::keys::Dims,   InpaintParams{}.dims},
        {inpaint::keys::Checks, InpaintParams{}.checks},
        {inpaint::keys::Groups, InpaintParams{}.groups},
        {inpaint::keys::Scales, InpaintParams{}.scales},
        {markov::keys::Radius,  MarkovParams{}.radius},
        {markov::keys::Bins,    MarkovParams{}.bins},
        {markov::keys::Pairs,   MarkovParams{}.pairs},
        {markov::keys::Iters,   MarkovParams{}.iters},
    };
    for (const auto& w : want) {
        const long long reg = P.integer(w.key);
        if (reg != w.got)
            return no(std::string(w.key) + ": struct says " + std::to_string(w.got)
                      + ", registry says " + std::to_string(reg));
    }
    if (InpaintParams{}.cap != P.num(inpaint::keys::Cap)) return no("inpaint.cap differs");
    // markov.temp is deliberately not here: it is derived from bins, pairs and
    // iters, so the struct's 0.4 is the rule's answer at its anchor rather than
    // a second statement of the same constant.
    return ok("11 integer defaults and inpaint.cap agree; markov.temp is derived");
}

CASE("threshold_byte: spans 0..254 and a cell is the rank at its own quantile")
{
    // MaskDither fires on frac*254 > t, so a cell holding t has to *be* the rank
    // at quantile t/254 for the level to average to the target. Comparing
    // against 255 instead biased every mask-dithered image up by half a step.
    for (long long n : {2LL, 3LL, 255LL, 256LL, 1024LL, 65536LL}) {
        if (maskutil::threshold_byte(0, n) != 0)
            return no("rank 0 is not byte 0 at n=" + std::to_string(n));
        if (maskutil::threshold_byte(n - 1, n) != 254)
            return no("top rank is not byte 254 at n=" + std::to_string(n));
        for (long long r = 1; r < n; r++)
            if (maskutil::threshold_byte(r, n) < maskutil::threshold_byte(r - 1, n))
                return no("not monotone at n=" + std::to_string(n));
        // byte t should be the rank at quantile t/254, to within a rank
        for (long long r = 0; r < n; r += std::max(1LL, n / 97)) {
            const double q = (double)r / (double)(n - 1);
            const double t = maskutil::threshold_byte(r, n);
            if (std::abs(t / 254.0 - q) > 1.0 / 254.0 + 1e-9)
                return no("byte " + std::to_string((int)t) + " is not quantile " +
                          std::to_string(q) + " at n=" + std::to_string(n));
        }
    }
    return ok("6 sizes, endpoints, monotone, quantile");
}

CASE("threshold_mask: a field's brightest pixel lights first")
{
    // The convention every generator ends in. A field is held in the sample's
    // own sense, bright where the grain is bright, and MaskDither lights the
    // lowest cells first -- so the highest value has to get threshold 0 or the
    // dither shows the grain's negative. It did, for every sample-based mask,
    // until this was stated here once rather than nowhere.
    cv::Mat field(16, 16, CV_32F);
    cv::RNG(5).fill(field, cv::RNG::UNIFORM, 0.0, 1.0);
    const cv::Mat mask = maskutil::threshold_mask(maskutil::rank_order(field), field.size());
    cv::Point lo, hi;
    cv::minMaxLoc(field, nullptr, nullptr, &lo, &hi);
    if (mask.at<uint8_t>(hi) != 0 || mask.at<uint8_t>(lo) != 254)
        return no("brightest got " + std::to_string(mask.at<uint8_t>(hi)) + ", darkest " +
                  std::to_string(mask.at<uint8_t>(lo)));

    // And a builder hands back that mask, not a field to be turned round by
    // whoever calls it: direct's plain tile, taken straight, must anti-correlate
    // with the sample it was cut from.
    cv::Mat sample(96, 96, CV_8U);
    cv::RNG(9).fill(sample, cv::RNG::UNIFORM, 0, 256);
    const cv::Mat d = build_direct_mask({64, 64}, sample, 1.0, 0.0, 0.0, 0, 0.0, 1, nullptr);
    if (d.empty()) return no("direct refused a noise sample");
    cv::Mat a, b;
    sample(cv::Rect(16, 16, 64, 64)).convertTo(a, CV_64F);
    d.convertTo(b, CV_64F);
    cv::Mat ma, sa, mb, sb;
    cv::meanStdDev(a, ma, sa);
    cv::meanStdDev(b, mb, sb);
    const double r = cv::mean((a - ma.at<double>(0)).mul(b - mb.at<double>(0)))[0] /
                     (sa.at<double>(0) * sb.at<double>(0));
    if (r > -0.95)
        return no("direct's mask correlates " + std::to_string(r) + " with its sample, not -1");
    return ok("brightest gets 0, and direct's plain tile reads -1 against its sample");
}

CASE("rank_order: a permutation, and stable on ties")
{
    cv::Mat f(7, 11, CV_32F);
    cv::RNG r(12345);
    r.fill(f, cv::RNG::UNIFORM, 0.0f, 1.0f);
    f.at<float>(3, 4) = f.at<float>(3, 5) = 0.5f;      // a deliberate tie
    const std::vector<int> order = maskutil::rank_order(f);
    if (order.size() != f.total()) return no("wrong length");
    std::vector<char> seen(f.total(), 0);
    for (int i : order) {
        if (i < 0 || i >= (int)f.total() || seen[i]) return no("not a permutation");
        seen[i] = 1;
    }
    for (size_t i = 1; i < order.size(); i++)
        if (f.ptr<float>()[order[i]] < f.ptr<float>()[order[i - 1]])
            return no("not ascending");
    // the tie must keep positional order
    const int a = 3 * 11 + 4, b = 3 * 11 + 5;
    size_t pa = 0, pb = 0;
    for (size_t i = 0; i < order.size(); i++) {
        if (order[i] == a) pa = i;
        if (order[i] == b) pb = i;
    }
    if (pa > pb) return no("equal values were reordered");
    return ok("permutation, ascending, stable");
}

CASE("wrap_near and wrap_any agree wherever wrap_near is defined")
{
    // wrap_near branches instead of dividing and is only correct within one
    // period, which is all the neighbourhood loops ever need. Anything that
    // asked it for more would be a silent indexing bug.
    for (int n : {1, 2, 7, 64, 255}) {
        for (int v = -n; v < 2 * n; v++)
            if (maskutil::wrap_near(v, n) != maskutil::wrap_any(v, n))
                return no("disagree at v=" + std::to_string(v) + " n=" + std::to_string(n));
        for (int v = -5 * n; v < 5 * n; v++) {
            const int w = maskutil::wrap_any(v, n);
            if (w < 0 || w >= n) return no("wrap_any out of range");
        }
    }
    return ok("5 periods, one period either side and five for wrap_any");
}

// ---------------------------------------------------------------------------
// Moisan's periodic + smooth decomposition, against the paper's own criterion.
// ---------------------------------------------------------------------------

// A field with no frame discontinuity: white noise. Uncorrelated pixels are as
// different across the wrap as anywhere else, which is exactly what "no jump"
// means, and its spectrum is broadband -- so on-axis and off-axis power are
// alike and cross_ratio reads ~1. A few pure sinusoids would also be periodic
// but would put *no* power on the axes at all, making the reference 0 and the
// comparison vacuous.
cv::Mat periodic_field(int n)
{
    cv::Mat w(n, n, CV_32F);
    cv::RNG rng(20240914);
    rng.fill(w, cv::RNG::NORMAL, 0.0, 1.0);
    return w;
}

// Mean spectral power on the frequency axes over mean power off them. The
// paper's Figure 1 artifact as a number: a straight-line discontinuity along
// the frame spreads power along the perpendicular axis, so a field with a frame
// jump reads high and one without reads ~1.
double cross_ratio(const cv::Mat& f)
{
    cv::Mat planes[] = {f.clone(), cv::Mat::zeros(f.size(), CV_32F)}, cplx;
    cv::merge(planes, 2, cplx);
    cv::dft(cplx, cplx, cv::DFT_COMPLEX_OUTPUT);
    double on = 0, off = 0; int non = 0, noff = 0;
    for (int y = 0; y < f.rows; y++)
        for (int x = 0; x < f.cols; x++) {
            if (!y && !x) continue;                       // DC is not the artifact
            const cv::Vec2f c = cplx.at<cv::Vec2f>(y, x);
            const double p = (double)c[0]*c[0] + (double)c[1]*c[1];
            if (y == 0 || x == 0) { on += p; non++; } else { off += p; noff++; }
        }
    return (on / non) / (off / noff);
}

CASE("periodic_component: removes the DFT cross artifact, and only that")
{
    // The paper's criterion, on a field where the answer is known. `w` is
    // periodic by construction and so has no frame jump; adding a ramp creates
    // one without adding any texture, which is exactly the Figure 1 setup.
    const int n = 128;
    const cv::Mat w = periodic_field(n);
    cv::Mat u = w.clone();
    for (int y = 0; y < n; y++)
        for (int x = 0; x < n; x++) u.at<float>(y, x) += 0.05f * x;

    const double cw = cross_ratio(w), cu = cross_ratio(u);
    const cv::Mat p = maskutil::periodic_component(u);
    const double cp = cross_ratio(p);

    char buf[160];
    // The reference has to read near 1, or "the cross is gone" means nothing.
    if (!(cw > 0.6 && cw < 1.6))
        return no("reference field is not cross-free: " + std::to_string(cw));
    if (!(cu > 50.0)) {
        std::snprintf(buf, sizeof buf, "the ramp did not create a cross: %.1f", cu);
        return no(buf);
    }
    if (!(cp > 0.6 && cp < 1.6)) {
        std::snprintf(buf, sizeof buf, "cross survived the decomposition: %.1f -> %.1f", cu, cp);
        return no(buf);
    }
    std::snprintf(buf, sizeof buf, "cross %.0f -> %.2f, against %.2f for a field with no jump",
                  cu, cp, cw);
    return ok(buf);
}

CASE("periodic_component: s is harmonic inside, which is what makes it smooth")
{
    // The defining property. s carries the boundary mismatch and nothing else,
    // so away from the border it solves Laplace's equation exactly -- if it did
    // not, the decomposition would be removing texture rather than a jump.
    cv::Mat u = cv::imread(std::string("examples/Kodak-4X-1.png"), cv::IMREAD_GRAYSCALE);
    if (u.empty()) return no("cannot read examples/Kodak-4X-1.png");
    u = u(cv::Rect(0, 0, 128, 128)).clone();
    u.convertTo(u, CV_32F);
    const cv::Mat s = u - maskutil::periodic_component(u);

    double worst = 0;
    for (int y = 2; y < s.rows - 2; y++)
        for (int x = 2; x < s.cols - 2; x++) {
            const double lap = s.at<float>(y-1,x) + s.at<float>(y+1,x)
                             + s.at<float>(y,x-1) + s.at<float>(y,x+1)
                             - 4.0 * s.at<float>(y,x);
            worst = std::max(worst, std::abs(lap));
        }
    // Relative to the amplitude of s itself, since an absolute bound would only
    // be saying how big the picture is. And s has to be *something*: a
    // decomposition that returned u unchanged would leave s identically zero,
    // which is harmonic in the most useless possible way.
    double lo, hi; cv::minMaxLoc(s, &lo, &hi);
    double ulo, uhi; cv::minMaxLoc(u, &ulo, &uhi);
    if (hi - lo < 0.02 * (uhi - ulo))
        return no("s is ~0, so this proves nothing: range " + std::to_string(hi - lo) +
                  " against the image's " + std::to_string(uhi - ulo));
    const double rel = worst / std::max(hi - lo, 1e-9);
    if (!(rel < 1e-4)) {
        char buf[120];
        std::snprintf(buf, sizeof buf, "max |laplacian| is %.3g of s's range", rel);
        return no(buf);
    }
    char buf[120];
    std::snprintf(buf, sizeof buf, "max |laplacian| %.2g of s's range %.1f", rel, hi - lo);
    return ok(buf);
}

CASE("periodic_component: the mean is untouched, and p + (u - p) rounds back")
{
    // The title used to promise "u = p + s exactly", which this cannot show.
    // The test manufactures s as u - p, so p + s == u is float arithmetic
    // agreeing with itself, not evidence about the decomposition. What it *can*
    // establish is the mean, which is a real property: s has its DC term pinned
    // at zero, so p keeps u's mean, and that is asserted.
    //
    // The round trip is kept but demoted to what it is -- a bound on float
    // rounding -- and now actually asserted rather than printed. It was measured
    // into `worst`, formatted into the word "exact", and never compared with
    // anything.
    cv::Mat u = periodic_field(64);
    for (int y = 0; y < 64; y++)
        for (int x = 0; x < 64; x++) u.at<float>(y, x) += 0.3f * (x + y);
    const cv::Mat p = maskutil::periodic_component(u);

    double worst = 0, scale = 0;
    for (int y = 0; y < 64; y++)
        for (int x = 0; x < 64; x++) {
            const double s = u.at<float>(y,x) - p.at<float>(y,x);
            worst = std::max(worst, std::abs((p.at<float>(y,x) + s) - u.at<float>(y,x)));
            scale = std::max(scale, std::abs((double)u.at<float>(y,x)));
        }
    if (worst > 1e-5 * std::max(scale, 1.0))
        return no("p + (u - p) differs from u by " + std::to_string(worst) +
                  ", far more than float rounding on values up to " + std::to_string(scale));

    // The property that is actually about the algorithm.
    const double du = cv::mean(u)[0], dp = cv::mean(p)[0];
    if (std::abs(du - dp) > 1e-3 * std::max(std::abs(du), 1.0))
        return no("mean moved: " + std::to_string(du) + " -> " + std::to_string(dp));
    char buf[140];
    std::snprintf(buf, sizeof buf, "mean %.4f held; reconstruction within %.1e of float rounding",
                  du, worst);
    return ok(buf);
}

// ---------------------------------------------------------------------------
// The rest of maskutil. periodic_component had three cases above and the two
// index wrappers, threshold_byte and rank_order one each; these are the
// functions that had none. Every generator goes through most of them, so a
// fault here shows up as four generators being subtly wrong at once, which is
// the hardest kind of failure to attribute.
// ---------------------------------------------------------------------------

CASE("bin_freq: the upper half of a row holds the negative frequencies")
{
    // Getting this wrong reads as a spectrum folded about its own Nyquist,
    // which looks like a plausible spectrum -- so it needs asserting rather
    // than eyeballing.
    for (int n : {2, 3, 8, 9, 16, 17, 64}) {
        if (maskutil::bin_freq(0, n) != 0.0)
            return no("bin 0 of " + std::to_string(n) + " is not DC");
        // For even n the bin at n/2 is its own mirror -- it *is* Nyquist, where
        // +0.5 and -0.5 are the same frequency -- so it is the one bin the
        // symmetry below cannot speak about. Asserted separately rather than
        // skipped, since a sign convention that flipped there would go unseen.
        if (n % 2 == 0 && maskutil::bin_freq(n / 2, n) != 0.5)
            return no("Nyquist bin of " + std::to_string(n) + " is not +0.5");

        for (int i = 1; i < n; i++) {
            if (n % 2 == 0 && i == n / 2) continue;
            const double f = maskutil::bin_freq(i, n);
            // Conjugate symmetry: bin i and bin n-i are the same frequency with
            // opposite sign. This is the property the callers actually rely on.
            if (std::abs(f + maskutil::bin_freq(n - i, n)) > 1e-12)
                return no("bin " + std::to_string(i) + " of " + std::to_string(n) +
                          " is not the negative of bin " + std::to_string(n - i));
            if (std::abs(f) > 0.5 + 1e-12)
                return no("bin " + std::to_string(i) + " of " + std::to_string(n) +
                          " is past Nyquist at " + std::to_string(f));
            if (i <= n / 2 && f <= 0.0)
                return no("lower half bin " + std::to_string(i) + " is not positive");
            if (i > n / 2 && f >= 0.0)
                return no("upper half bin " + std::to_string(i) + " is not negative");
        }
    }
    return ok("DC, conjugate symmetry and the Nyquist bound, even and odd n");
}

CASE("rank_uniform_unit and _signed: order-preserving onto their stated range")
{
    // Both are monotone re-labellings, so what has to hold is that they change
    // no pixel's position in the order and that they land exactly on the range
    // the caller was promised -- markov and inpaint hold their field on [0, 1)
    // between sweeps, and the field's bin index is field * Qc, so a value that
    // reached 1.0 would index off the end of the table.
    std::mt19937_64 rng(7);
    cv::Mat f(13, 11, CV_32F);
    for (int i = 0; i < f.rows * f.cols; i++)
        f.ptr<float>()[i] = (float)(rng() % 50);     // heavy ties, on purpose
    const cv::Mat before = f.clone();

    for (int signedRange = 0; signedRange <= 1; signedRange++) {
        cv::Mat g = before.clone();
        std::vector<int> order;
        if (signedRange) maskutil::rank_uniform_signed(g, order);
        else             maskutil::rank_uniform_unit(g, order);

        const char* which = signedRange ? "signed" : "unit";
        if ((int)order.size() != g.rows * g.cols)
            return no(std::string(which) + ": order does not cover the field");
        // Monotone: walking the returned order, the values must ascend, and the
        // original values must not descend.
        for (size_t r = 1; r < order.size(); r++) {
            if (g.ptr<float>()[order[r]] <= g.ptr<float>()[order[r - 1]])
                return no(std::string(which) + ": ranks do not ascend along the order");
            if (before.ptr<float>()[order[r]] < before.ptr<float>()[order[r - 1]])
                return no(std::string(which) + ": order is not sorted by the input");
        }
        double lo = 1e30, hi = -1e30;
        for (int i = 0; i < g.rows * g.cols; i++) {
            lo = std::min(lo, (double)g.ptr<float>()[i]);
            hi = std::max(hi, (double)g.ptr<float>()[i]);
        }
        const int n = g.rows * g.cols;
        if (signedRange) {
            if (std::abs(lo + 1.0) > 1e-6 || std::abs(hi - 1.0) > 1e-6)
                return no("signed: range is [" + std::to_string(lo) + ", " +
                          std::to_string(hi) + "], not [-1, 1]");
        } else {
            // Half-open by construction: (r + 0.5) / n never reaches either end.
            if (std::abs(lo - 0.5 / n) > 1e-6 || std::abs(hi - (n - 0.5) / n) > 1e-6)
                return no("unit: range is [" + std::to_string(lo) + ", " +
                          std::to_string(hi) + "], not the half-open (0, 1)");
            if (lo <= 0.0f || hi >= 1.0f)
                return no("unit: touches an endpoint, which indexes off a bin table");
        }
    }
    return ok("monotone, ties resolved by the shared order, ranges exact");
}

CASE("threshold_mask: a threshold fires with exactly the probability asked of it")
{
    // "Histogram-flat" is the usual shorthand and it is loose: the mask is flat
    // in *rank* -- each of n ranks appears once -- and its *value* histogram is
    // deliberately not flat at the two ends. threshold_byte maps rank linearly
    // onto 0..254 through lround, so bytes 0 and 254 each collect only the half
    // interval that rounds to them and take about half the share the other 253
    // do. That is correct rather than a defect, and this is the property it is
    // correct *for*: MaskDither fires on frac*254 > t, so what has to hold is
    // that the fraction of cells below frac*254 is frac. Asserting an equal
    // count per value instead fails on exactly the end bins, which is how this
    // case was first written.
    for (cv::Size sz : {cv::Size(255, 4), cv::Size(16, 16), cv::Size(160, 96)}) {
        const int n = sz.area();
        std::vector<int> order(n);
        std::iota(order.begin(), order.end(), 0);
        std::shuffle(order.begin(), order.end(), std::mt19937_64(3));

        const cv::Mat m = maskutil::threshold_mask(order, sz);
        if (m.size() != sz || m.type() != CV_8U)
            return no("wrong size or type back");

        int hist[256] = {0};
        for (int i = 0; i < n; i++) hist[m.ptr<uint8_t>()[i]]++;
        if (hist[255] != 0) return no("emitted 255, which no frac can exceed");

        // The interior bins are the ones that must be even.
        const double share = (double)(n - 1) / 254.0;
        for (int v = 1; v <= 253; v++)
            if (std::abs(hist[v] - share) > 1.0 + 1e-9)
                return no("interior value " + std::to_string(v) + " has " +
                          std::to_string(hist[v]) + " pixels, expected about " +
                          std::to_string(share));
        for (int v : {0, 254})
            if (std::abs(hist[v] - share / 2.0) > 1.0 + 1e-9)
                return no("end value " + std::to_string(v) + " has " +
                          std::to_string(hist[v]) + " pixels, expected about " +
                          std::to_string(share / 2.0));

        // And the property all of that is for. One rank of slack, which is what
        // the mask's own resolution costs and is not removable by any offset.
        for (int step = 0; step <= 16; step++) {
            const double frac = step / 16.0;
            long long fired = 0;
            for (int i = 0; i < n; i++)
                if (frac * 254.0 > m.ptr<uint8_t>()[i]) fired++;
            if (std::abs((double)fired - frac * n) > 1.0 + (double)n / 254.0)
                return no("frac " + std::to_string(frac) + " fired " +
                          std::to_string(fired) + " of " + std::to_string(n) +
                          ", expected about " + std::to_string(frac * n));
        }
        // Descending in the supplied order: it lists a field from lowest to
        // highest, and the highest lights first.
        for (int r = 1; r < n; r++)
            if (m.ptr<uint8_t>()[order[r]] > m.ptr<uint8_t>()[order[r - 1]])
                return no("mask is not descending in the supplied field order");
        if (m.ptr<uint8_t>()[order[0]] != 254 || m.ptr<uint8_t>()[order[n - 1]] != 0)
            return no("endpoints are not 254 and 0");
    }
    return ok("fire probability tracks frac; interior flat, end bins half by design");
}

CASE("prepare_texture_sample: reflecting keeps a gradient's edge error small")
{
    // The project's rule is wrap the filter when the input is a torus, reflect
    // when it is not one yet -- and a photograph is not. This is the half that
    // reflects, and the hazard it avoids is specific: wrap-padding puts the
    // dark end of a gradient beside the bright end, so the blur errs in
    // opposite directions at the two edges and a tilt survives the high-pass.
    // On a pure ramp a reflecting high-pass leaves zero everywhere, edges
    // included, because a linear ramp reflected is still locally linear and a
    // Gaussian of a linear function is that function.
    // One code per column, so the ramp is exactly linear in 8 bits. Scaling it
    // to span 0..255 instead would round each column and leave a staircase of
    // up to half a code, which a high-pass keeps -- that is a property of the
    // test image and would be read here as a property of the filter.
    const int n = 96;
    cv::Mat ramp(n, n, CV_8U);
    for (int y = 0; y < n; y++)
        for (int x = 0; x < n; x++)
            ramp.at<uint8_t>(y, x) = (uint8_t)x;

    const cv::Mat hp = maskutil::prepare_texture_sample(ramp, 32.0, 1.0);
    if (hp.type() != CV_32F || hp.size() != ramp.size())
        return no("wrong type or size back");

    // Interior first: away from any edge a Gaussian of a linear function is
    // that function, so the high-pass must leave nothing at all.
    const int pad = (int)std::ceil(4 * maskutil::kHighpassSigmaPerPixel * 32.0);
    double interior = 0;
    for (int y = pad; y < n - pad; y++)
        for (int x = pad; x < n - pad; x++)
            interior = std::max(interior, std::abs((double)hp.at<float>(y, x)));
    if (interior > 0.05)
        return no("a ramp leaves " + std::to_string(interior) +
                  " grey levels in the interior, where it should leave none");

    // The edges are the interesting part, and reflecting does *not* leave them
    // clean -- a mirrored ramp is a V, so the blur sits above the ramp at one
    // end and below it at the other. What reflecting buys is that the error
    // stays the size of the local slope. Wrapping puts the dark end beside the
    // bright end, so it is the size of the whole tonal range, which is the
    // residual tilt the rule exists to avoid. Measured here against the same
    // image through the wrap-padding filter, so the comparison is like for like.
    cv::Mat wrapped;
    ramp.convertTo(wrapped, CV_32F);
    maskutil::highpass_toroidal(wrapped, 32.0);

    auto tilt = [n](const cv::Mat& f) {
        double lo = 0, hi = 0;
        for (int y = 0; y < n; y++) { lo += f.at<float>(y, 0); hi += f.at<float>(y, n - 1); }
        return std::abs(hi - lo) / n;      // how far the two edges disagree
    };
    const double tr = tilt(hp), tw = tilt(wrapped);
    if (!(tr * 4.0 < tw))
        return no("reflecting leaves a tilt of " + std::to_string(tr) +
                  " against wrapping's " + std::to_string(tw) +
                  "; reflecting is supposed to be the smaller by far");

    // Off, it is a bare float conversion and must not touch a single value --
    // the generators rely on that to get the sample unaltered.
    const cv::Mat raw = maskutil::prepare_texture_sample(ramp, 0.0, 1.0);
    for (int y = 0; y < n; y++)
        for (int x = 0; x < n; x++)
            if (raw.at<float>(y, x) != (float)ramp.at<uint8_t>(y, x))
                return no("highpass off altered a value");

    // And --mask-scale is a resize here, unlike dft's frequency remap.
    const cv::Mat half = maskutil::prepare_texture_sample(ramp, 0.0, 0.5);
    if (half.cols != n / 2 || half.rows != n / 2)
        return no("scale 0.5 gave " + std::to_string(half.cols) + " columns");
    // Not "leaves nothing behind", which the title promised and the body
    // disproves four lines later: a mirrored ramp is a V, so the edges keep a
    // residue. What reflecting buys is that the residue is the size of the
    // local slope rather than of the whole tonal range.
    return ok("interior clean; edge tilt " + std::to_string(tr) + " reflecting against "
              + std::to_string(tw) + " wrapping; off is exact; scale resizes");
}

CASE("highpass_toroidal: matches an exactly toroidal filter at the border")
{
    // The other half of the rule: this input *is* a torus and has to stay one,
    // which is why it wrap-pads. The padding width is the part worth pinning --
    // it is 4 sigma rather than 3, and the comment in maskutil.cpp quotes a
    // measurement to justify that. This is that measurement, so the constant
    // cannot be lowered back without the suite noticing.
    //
    // The reference is the same filter with no padding question at all: a
    // Gaussian applied as a transfer function in the frequency domain, which is
    // exactly circular by construction.
    const int n = 64;
    const double px = 24.0, sigma = maskutil::kHighpassSigmaPerPixel * px;
    std::mt19937_64 rng(11);
    cv::Mat field(n, n, CV_32F);
    for (int i = 0; i < n * n; i++)
        field.ptr<float>()[i] = (float)((rng() % 2001) / 1000.0 - 1.0);

    cv::Mat exact;
    {
        cv::Mat planes[] = {field.clone(), cv::Mat::zeros(n, n, CV_32F)}, cplx;
        cv::merge(planes, 2, cplx);
        cv::dft(cplx, cplx, cv::DFT_COMPLEX_OUTPUT);
        for (int y = 0; y < n; y++) {
            cv::Vec2f* row = cplx.ptr<cv::Vec2f>(y);
            const double fy = maskutil::bin_freq(y, n);
            for (int x = 0; x < n; x++) {
                const double fx = maskutil::bin_freq(x, n);
                // 1 - G: the high-pass whose low-pass half is this Gaussian.
                const double g = std::exp(-2.0 * CV_PI * CV_PI * sigma * sigma *
                                          (fx * fx + fy * fy));
                row[x] *= (float)(1.0 - g);
            }
        }
        cv::idft(cplx, exact, cv::DFT_SCALE | cv::DFT_REAL_OUTPUT);
    }

    cv::Mat got = field.clone();
    maskutil::highpass_toroidal(got, px);

    // Border and interior separately, because the whole point is that any
    // padding error is *localised at the border* -- averaged over the field it
    // would look negligible while sitting exactly where a seam is judged.
    double border = 0, interior = 0;
    for (int y = 0; y < n; y++)
        for (int x = 0; x < n; x++) {
            const double e = std::abs((double)got.at<float>(y, x) - exact.at<float>(y, x));
            const bool edge = (y == 0 || x == 0 || y == n - 1 || x == n - 1);
            (edge ? border : interior) = std::max(edge ? border : interior, e);
        }
    if (border > 1e-3)
        return no("border differs from an exactly circular filter by " +
                  std::to_string(border) + "; 4 sigma of wrap padding should hold it under 1e-3");
    if (interior > 1e-3)
        return no("interior differs by " + std::to_string(interior));

    // Below the cutoff it is a no-op, which the callers rely on to mean "off".
    cv::Mat untouched = field.clone();
    maskutil::highpass_toroidal(untouched, 1.0);
    if (cv::norm(untouched, field, cv::NORM_INF) != 0.0)
        return no("px <= 1 should be a no-op and is not");
    return ok("border " + std::to_string(border) + ", interior " +
              std::to_string(interior) + " against an exact circular filter");
}

CASE("mincut_path: connected, closed, and it finds the cheapest route")
{
    // Planted optimum. A triangle wave that returns to where it started is a
    // legal path by construction -- unit steps, both ends equal -- so giving it
    // cost 0 and everything else cost 1 makes it the unique minimum: any
    // deviation costs at least 1. Recovering it exactly is a stronger statement
    // than "the answer is cheap", which a greedy row-by-row pick would also
    // satisfy on an easier board.
    const int k = 5, period = 2 * (k - 1), L = 5 * period + 1;
    std::vector<int> planted(L);
    for (int y = 0; y < L; y++) {
        const int t = y % period;
        planted[y] = (t < k) ? t : period - t;
    }
    cv::Mat cost(L, k, CV_64F, cv::Scalar(1.0));
    for (int y = 0; y < L; y++) cost.at<double>(y, planted[y]) = 0.0;

    std::vector<int> got = maskutil::mincut_path(cost);
    if ((int)got.size() != L) return no("wrong length back");
    if (got != planted) return no("did not recover the planted zero-cost path");

    // The structural guarantees, on a board with no planted answer at all.
    std::mt19937_64 rng(19);
    cv::Mat rnd(37, 9, CV_64F);
    for (int y = 0; y < rnd.rows; y++)
        for (int x = 0; x < rnd.cols; x++)
            rnd.at<double>(y, x) = (double)(rng() % 1000);
    got = maskutil::mincut_path(rnd);
    if (got.front() != got.back())
        return no("path does not close: starts at " + std::to_string(got.front()) +
                  ", ends at " + std::to_string(got.back()));
    for (int y = 0; y < rnd.rows; y++) {
        if (got[y] < 0 || got[y] >= rnd.cols) return no("path left the band");
        if (y && std::abs(got[y] - got[y - 1]) > 1) return no("path is not connected");
    }
    // Determinism: the tie-break is fixed, so the same board gives the same path.
    if (maskutil::mincut_path(rnd) != got) return no("not deterministic");

    // A band one wide has exactly one path, and must not divide by anything.
    const cv::Mat thin(6, 1, CV_64F, cv::Scalar(2.0));
    if (maskutil::mincut_path(thin) != std::vector<int>(6, 0))
        return no("degenerate width-1 band");
    return ok("planted optimum recovered; closed, connected, deterministic");
}

CASE("quilt_axis: joins the wrap without averaging a single pixel")
{
    // Two properties, and the second is the one that distinguishes this from
    // every blend: the tile must *join*, and it must still be made entirely of
    // sample. build_direct_mask's whole claim -- everything true of the grain is
    // true of the mask -- survives only if no output pixel is a mixture.
    const int keep = 64, ov = 12, L = 48;
    std::mt19937_64 rng(23);
    cv::Mat wide(L, keep + ov, CV_32F);
    for (int i = 0; i < wide.rows * wide.cols; i++)
        wide.ptr<float>()[i] = (float)((rng() % 2001) / 1000.0 - 1.0);
    // Correlated, so that "adjacent pixels resemble each other" means something
    // and a seam is something the statistic below can see.
    cv::GaussianBlur(wide, wide, cv::Size(), 2.0, 2.0, cv::BORDER_REFLECT);

    auto wrap_over_interior = [](const cv::Mat& f) {
        double inter = 0, wrapd = 0;
        for (int y = 0; y < f.rows; y++) {
            for (int x = 0; x + 1 < f.cols; x++)
                inter += std::abs(f.at<float>(y, x + 1) - f.at<float>(y, x));
            wrapd += std::abs(f.at<float>(y, f.cols - 1) - f.at<float>(y, 0));
        }
        inter /= (double)f.rows * (f.cols - 1);
        wrapd /= f.rows;
        return wrapd / std::max(inter, 1e-12);
    };

    const cv::Mat plain = wide(cv::Rect(0, 0, keep, L)).clone();
    const cv::Mat cut = maskutil::quilt_axis(wide, keep, ov, 1);
    if (cut.size() != cv::Size(keep, L) || cut.type() != CV_32F)
        return no("wrong size or type back");

    const double before = wrap_over_interior(plain), after = wrap_over_interior(cut);
    if (!(before > 2.0))
        return no("the uncut crop does not even have a seam to fix (" +
                  std::to_string(before) + "), so this proves nothing");
    if (!(after < 1.6))
        return no("wrap is " + std::to_string(after) +
                  "x the interior difference after cutting; it should join like texture");

    // No mixing: every output pixel has to be one of the two candidates for its
    // own position, bit for bit. An average would land between them and pass any
    // statistical check while breaking the claim above.
    for (int y = 0; y < L; y++)
        for (int x = 0; x < keep; x++) {
            const float v = cut.at<float>(y, x);
            const bool own = v == wide.at<float>(y, x);
            const bool cont = x < ov && v == wide.at<float>(y, keep + x);
            if (!own && !cont)
                return no("pixel (" + std::to_string(y) + "," + std::to_string(x) +
                          ") is neither candidate -- it has been averaged");
        }

    // Transposing the input must transpose the output, or rows and columns have
    // been confused somewhere -- which nothing else here would catch, since the
    // test field is square in neither sense.
    const cv::Mat byRow = maskutil::quilt_axis(wide.t(), keep, ov, 0);
    if (cv::norm(byRow, cut.t(), cv::NORM_INF) != 0.0)
        return no("axis 0 is not axis 1 transposed");

    // Zero overlap is the no-repair path and must be the bare leading crop.
    const cv::Mat none = maskutil::quilt_axis(wide(cv::Rect(0, 0, keep, L)).clone(),
                                              keep, 0, 1);
    if (cv::norm(none, plain, cv::NORM_INF) != 0.0)
        return no("overlap 0 is not the plain crop");

    return ok("wrap/interior " + std::to_string(before) + " -> " +
              std::to_string(after) + "; no pixel averaged; both axes agree");
}

// ---------------------------------------------------------------------------
// The mask cache. Its only dangerous failure is a *wrong hit* -- a mask built
// for one configuration served silently for another -- so what has to be tested
// is that the key notices everything. Testing it by listing the parameters here
// would reproduce the bug it is guarding against, in a second place. This
// sweeps the registry instead.
// ---------------------------------------------------------------------------

namespace {
params::Params cache_registry()
{
    params::Params P;
    for (const Generator& g : generators()) g.register_params(P);
    register_mask_params(P);
    return P;
}
}

CASE("MaskDither: a negative value thresholds, and a rectangular mask is not transposed")
{
    // Two defects this class has had, neither of which any image-level check
    // could see.
    //
    // `(int)v` truncates toward zero, so a negative v -- which error diffusion
    // produces in the shadows -- gave lo = 0 and a *negative* frac. The
    // comparison could then never fire and the level was pinned at 0 whatever
    // the mask said. floor is what makes frac genuinely [0, 1).
    //
    // And the mask is indexed [row % rows][col % cols]. On a square mask a
    // transposition is invisible; this project's masks are usually square, and
    // `test/masktest` uses a non-square one for exactly this reason but only
    // looks at the mask, not at the indexing.
    //
    // The mask here holds its own column index, so what a cell says is where
    // the reader looked. No new_frame(), so the offsets are the (0, 0) the goal
    // pass uses and the indexing is direct.
    const int rows = 8, cols = 5;
    cv::Mat m(rows, cols, CV_8U);
    for (int y = 0; y < rows; y++)
        for (int x = 0; x < cols; x++)
            m.at<uint8_t>(y, x) = (uint8_t)(x * 50);     // 0, 50, 100, 150, 200

    MaskDither d(m);

    // Reading the right cell: frac just above a cell's threshold fires, just
    // below does not, and that identifies the cell uniquely.
    for (int y = 0; y < rows + 3; y++) {
        for (int x = 0; x < cols + 3; x++) {
            const int thr = m.at<uint8_t>(y % rows, x % cols);
            const double just_over  = (thr + 0.5) / 254.0;
            const double just_under = (thr - 0.5) / 254.0;
            if (d(10.0 + just_over, y, x) != 11)
                return no("at (" + std::to_string(y) + "," + std::to_string(x) +
                          ") a fraction above the cell did not fire -- wrong cell, "
                          "or rows and columns swapped");
            if (thr > 0 && d(10.0 + just_under, y, x) != 10)
                return no("at (" + std::to_string(y) + "," + std::to_string(x) +
                          ") a fraction below the cell fired");
        }
    }

    // Negative values: shifting v by a whole number must shift the answer by
    // the same whole number, because only frac(v) decides anything.
    //
    // **The non-firing fraction is what catches truncation**, and the first
    // version of this case missed it by testing only the firing one. At
    // v = -3 + f with f under the cell, floor gives lo = -3 and does not fire,
    // so -3; truncation gives lo = -2 and a negative frac that can never fire,
    // so -2. With f *over* the cell both answer -2, by different routes.
    for (int x = 0; x < cols; x++) {
        const int thr = m.at<uint8_t>(0, x);
        for (const double f : {(thr + 0.5) / 254.0, (thr - 0.5) / 254.0}) {
            if (f < 0.0 || f >= 1.0) continue;
            if (d(-3.0 + f, 0, x) + 5 != d(2.0 + f, 0, x))
                return no("a value five below decided differently from the same "
                          "fraction above zero, at column " + std::to_string(x) +
                          " with fraction " + std::to_string(f));
        }
    }

    return ok("reads [row][col] on a " + std::to_string(rows) + "x" +
              std::to_string(cols) + " mask, and negative values threshold");
}

CASE("samples: a scan keeps what its file holds, at any depth, on one scale")
{
    // Set::read used IMREAD_GRAYSCALE, which silently returns 8 bits, while
    // dither.cpp's own loader and maketexture both pass IMREAD_ANYDEPTH. A
    // 16-bit scan whose grain spans less than one 8-bit code arrived as a
    // constant image with nothing left to imitate.
    //
    // Stated generally, because that is the defect rather than the one loader:
    // the same content written at two depths must read back as the same
    // picture on the same scale, and the deeper file must keep the detail it
    // has. Nothing below mentions 257.
    // <filesystem>, not std::system: test/run's shellcheck refuses a path
    // handed to a shell anywhere in the tracked sources, and it is right to --
    // the rule is broader than the one call that once broke it, and a test is
    // not an exception to it.
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "dither-unittest-depth";
    std::filesystem::create_directories(dir);

    const int n = 64;
    cv::Mat wide(n, n, CV_16U);
    cv::RNG r(11);
    // A sixteenth of one 8-bit code of contrast: invisible to an 8-bit loader,
    // and real.
    r.fill(wide, cv::RNG::UNIFORM, 32768, 32768 + 16);
    cv::Mat narrow;                                  // the same scene, 8 bits
    wide.convertTo(narrow, CV_8U, 255.0 / 65535.0);

    const std::string wp = (dir / "wide.png").string(),
                      np = (dir / "narrow.png").string();
    cv::imwrite(wp, wide);
    cv::imwrite(np, narrow);

    const samples::Set w = samples::Set::read({wp}, nullptr);
    const samples::Set nn = samples::Set::read({np}, nullptr);
    if (w.empty() || nn.empty()) return no("a written sample would not read back");

    // One scale: the two agree on where the picture sits, to within a code.
    const double mw = cv::mean(w[0])[0], mn = cv::mean(nn[0])[0];
    if (std::abs(mw - mn) > 1.0)
        return no("the same scene reads at " + std::to_string(mw) + " at 16 bits and " +
                  std::to_string(mn) + " at 8, so the depths are on different scales");
    if (mw < 1.0 || mw > 254.0)
        return no("a mid-grey sample read back at " + std::to_string(mw) +
                  ", which is not the 0..255 code scale");

    // The deeper file kept what it has. An 8-bit copy of this content is
    // constant; if the 16-bit one is too, the depth was thrown away.
    std::set<float> vw, vn;
    for (int y = 0; y < n; y++)
        for (int x = 0; x < n; x++) {
            vw.insert(w[0].at<float>(y, x));
            vn.insert(nn[0].at<float>(y, x));
        }
    if (vw.size() <= vn.size())
        return no("the 16-bit scan came back with " + std::to_string(vw.size()) +
                  " distinct values against the 8-bit copy's " + std::to_string(vn.size()) +
                  "; its extra depth was discarded on load");

    // And a code means a code: what `direct.quantise` is measured in has to
    // follow the file, or 0.9 of one quantises away everything the depth
    // bought.
    if (!(w.code_step() < nn.code_step()))
        return no("a 16-bit scan reports a code step of " + std::to_string(w.code_step()) +
                  " against 8-bit's " + std::to_string(nn.code_step()));

    std::filesystem::remove_all(dir);
    return ok(std::to_string(vw.size()) + " values kept from 16 bits against " +
              std::to_string(vn.size()) + " from 8, both on the 0..255 scale");
}

CASE("samples: a set-valued transformation keeps the scans' relative contrast")
{
    // The pooled rank exists so that scans of one film keep their contrast
    // relative to each other -- a denser frame is grainier, and that is the
    // thing a model should learn. inpaint's coarse levels undid it: the helper
    // that block-averaged each scan also re-ranked each one alone, so every
    // scan came out of every coarse level with the whole unit range to itself.
    //
    // The general statement is that any Set-valued step must leave the scans
    // distinguishable. Checked on all three: the shared preparation, the
    // pooled ranking, and the coarse reduction that broke it.
    const int n = 64;
    cv::Mat a(n, n, CV_32F), b(n, n, CV_32F);
    for (int y = 0; y < n; y++)
        for (int x = 0; x < n; x++) {
            const float v = (float)(x % 16) - 7.5f;
            a.at<float>(y, x) = v;                   // one tenth of the other's
            b.at<float>(y, x) = v * 10.0f;           // contrast, same mean
        }
    const samples::Set raw = samples::Set::of(std::vector<cv::Mat>{a, b});

    auto spread_ratio = [](const samples::Set& s) {
        cv::Scalar m, sa, sb;
        cv::meanStdDev(s[0], m, sa);
        cv::meanStdDev(s[1], m, sb);
        return sa[0] > 0 ? sb[0] / sa[0] : 0.0;
    };

    const double pooled = spread_ratio(raw.ranked_unit());
    if (pooled < 1.5)
        return no("after the pooled ranking the two scans differ by only " +
                  std::to_string(pooled) + "x, so the ranking was per scan");

    // Every step that returns a Set, including the one that was wrong.
    const struct { const char* what; samples::Set s; } steps[] = {
        {"the shared preparation", raw.prepared(0.0, 1.0).ranked_unit()},
        {"a coarse level",         downsample_set(raw.ranked_unit(), 2)},
        {"two coarse levels",      downsample_set(raw.ranked_unit(), 4)},
    };
    for (const auto& step : steps) {
        const double got = spread_ratio(step.s);
        // Averaging legitimately narrows the gap; equalising it does not.
        if (got < 1.5)
            return no(std::string("after ") + step.what + " the two scans differ by " +
                      std::to_string(got) + "x against the pooled " +
                      std::to_string(pooled) + "x -- each was ranked alone");
    }
    return ok("ten-to-one contrast survives every step, pooled " +
              std::to_string(pooled) + "x");
}

CASE("srgb: the Mat helpers agree with the scalar ones on every code")
{
    // The 3-channel encoder added 0.5 and then called saturate_cast, which
    // rounds -- so it rounded twice and biased every other code upward. 96 of
    // 256 came back one high.
    //
    // General, because there are two paths to the same arithmetic and either
    // could drift: every code, every channel, against the scalar helpers the
    // pipeline itself uses.
    cv::Mat ramp(1, 256, CV_8UC3);
    for (int i = 0; i < 256; i++) ramp.at<cv::Vec3b>(0, i) = cv::Vec3b((uchar)i, (uchar)i, (uchar)i);

    const cv::Mat back = vision::linear_to_srgb(vision::srgb_to_linear(ramp));
    int moved = 0, worst = 0;
    for (int i = 0; i < 256; i++)
        for (int c = 0; c < 3; c++) {
            const int got = back.at<cv::Vec3b>(0, i)[c];
            if (got != i) { moved++; worst = std::max(worst, std::abs(got - i)); }
        }
    if (moved)
        return no(std::to_string(moved) + " of 768 channel values moved on a round trip, "
                  "worst by " + std::to_string(worst));
    return ok("all 256 codes round-trip on all three channels");
}

CASE("budget: it closes exactly at the edge of the domain, not only in the middle")
{
    // The matrix below runs to 128 frames at 256 levels -- 32640 display units,
    // where a float is exact. The CLI accepts 100000 frames, and above 2^24 a
    // float cannot separate adjacent integer budgets: the goal was narrowed to
    // one on its way to the threshold, and a pixel closed a unit short.
    //
    // General, and the reason the matrix missed it: a domain's edge is where
    // the arithmetic breaks, and a matrix of plausible values never goes there.
    // One pixel, so the frame count can be the largest the parser accepts.
    const int frames = 100000;                       // cli.cpp's own ceiling
    const int levels = 256;
    const vision::LevelSet ladder = vision::make_levels(levels, false, vision::Spacing::Srgb);

    // A 16-bit code whose target times the frame count lands just past a half,
    // in the range where float's spacing is 2.
    int wrong = 0, tried = 0;
    for (int code = 43118; code < 43138; code++) {
        const cv::Mat src(1, 1, CV_16U, cv::Scalar(code));
        const cv::Mat target = vision::decode_display(src, ladder, false);
        const double goal = (double)target.at<float>(0, 0) * frames;
        if (goal < 16777216.0) continue;             // below 2^24 a float is exact
        tried++;

        rng.seed(42);
        RoundDither fn;
        const std::vector<cv::Mat> out =
            ditherPass(target, ladder, fn, spatial_none, 0.0f, levels, frames, -1, -1);
        long long sum = 0;
        for (const cv::Mat& m : out) sum += m.at<uint8_t>(0, 0);

        if (sum != std::llround(goal)) {
            if (!wrong)
                std::fprintf(stderr, "    code %d: goal %.6f, closed at %lld\n",
                             code, goal, sum);
            wrong++;
        }
    }
    if (!tried) return no("no case reached 2^24, so nothing was tested");
    if (wrong)
        return no(std::to_string(wrong) + " of " + std::to_string(tried) +
                  " budgets past 2^24 closed on the wrong integer");
    return ok(std::to_string(tried) + " budgets past 2^24 close exactly, at " +
              std::to_string(frames) + " frames");
}

CASE("budget: --spatial scales the diffusion of a multi-frame run's totals")
{
    // The goal pass decides each pixel's total over all frames, and so the
    // average. It ran at full strength whatever --spatial said, so at 2 frames
    // --spatial 0.01 reproduced --spatial 1 byte for byte: only the split
    // between frames saw the setting. How far a total departs from the
    // undiffused round(target * frames) has to grow with --spatial.
    const int frames = 2, levels = 2;
    const vision::LevelSet ladder = vision::make_levels(levels, false, vision::Spacing::Srgb);
    cv::Mat target(64, 256, CV_32F);
    for (int c = 0; c < target.cols; c++) target.col(c).setTo((c + 0.5) / target.cols);

    std::vector<double> moved;
    for (const float s : {0.0f, 0.01f, 0.1f, 0.5f, 1.0f}) {
        RoundDither fn;
        const std::vector<cv::Mat> out = ditherPass(
            target, ladder, fn, s > 0 ? spatial_floydsteinberg : spatial_none, s, levels, frames, -1, -1);
        double sum = 0;
        for (int r = 0; r < target.rows; r++)
            for (int c = 0; c < target.cols; c++) {
                int total = 0;
                for (const cv::Mat& m : out) total += m.at<uint8_t>(r, c);
                sum += std::abs(total - std::lround(target.at<float>(r, c) * frames));
            }
        moved.push_back(sum / target.total());
    }
    std::string said;
    for (double m : moved) said += cv::format("%s%.3f", said.empty() ? "" : " / ", m);
    for (size_t i = 1; i < moved.size(); i++)
        if (!(moved[i] > moved[i - 1])) return no("not increasing in --spatial: " + said);
    return ok("mean departure from the undiffused total at --spatial 0 / 0.01 / 0.1 / 0.5 / 1: " + said);
}

CASE("budget: the size chosen is a size that was priced as affordable")
{
    // mask.cpp's kBudgetRefPairs. Repeated rather than exported because the
    // test has to price the way the policy prices, and a second name for it
    // here is honest about that being an assumption this makes.
    constexpr int kSizingRefPairs = 64;

    // The search priced a halving sequence from the image grid and then rounded
    // its winner *up* to the cache ladder without pricing it again: a 512 grid
    // at 1e8 work units a second took 512^2 at 0.94s, against a 1s budget, and
    // built the 1024^2 rung the same formula prices at 4.19s.
    //
    // General: whatever the rate, the budget and the picture, the size that
    // comes out must cost no more than was asked for -- unless a floor
    // overrode it, which is documented policy and is stated here rather than
    // assumed, by requiring the size to be at the floor when it is over budget.
    int checked = 0, floored = 0;
    for (const Generator& g : generators()) {
        for (const long long side : {256LL, 512LL, 1024LL, 3000LL}) {
            for (const double rate : {1e6, 1e7, 1e8, 1e9}) {
                for (const double budget : {0.25, 1.0, 4.0, 30.0}) {
                    params::Params P = cache_registry();
                    P.set(mask::keys::Threshold, std::string("mask"), params::Origin::User);
                    P.set(mask::keys::Method, std::string(g.name), params::Origin::User);
                    if (g.needs_sample)
                        P.set(mask::keys::Sample,
                              std::string("examples/Kodak-4X-1-crop.png"),
                              params::Origin::User);
                    P.set(mask::keys::ImageW, side, params::Origin::Measured);
                    P.set(mask::keys::ImageH, side, params::Origin::Measured);
                    P.set(mask::keys::SampleW, 4096LL, params::Origin::Measured);
                    P.set(mask::keys::SampleH, 4096LL, params::Origin::Measured);
                    P.set(mask::keys::Budget, budget, params::Origin::User);
                    P.set(std::string(g.name) + ".rate", rate, params::Origin::Measured);

                    const long long w = P.integer(mask::keys::SizeW);
                    const long long h = P.integer(mask::keys::SizeH);
                    // Priced with the same reference pair count the policy
                    // uses. The size is chosen before the pair count is, so
                    // the sizing model assumes a fixed 64 -- pricing the
                    // result with anything else would be comparing the answer
                    // against a question nobody asked.
                    const double cost = g.work(params::Resolver(P), (int)w, (int)h,
                                               kSizingRefPairs) / rate;
                    checked++;
                    if (cost <= budget * 1.000001) continue;

                    // Over budget is allowed only where nothing cheaper was
                    // available. What the policy builds when nothing is
                    // affordable is asked of the policy -- an impossible
                    // budget -- rather than assumed, so this knows neither the
                    // ladder's rungs nor the floors.
                    params::Params Floor = cache_registry();
                    Floor.set(mask::keys::Threshold, std::string("mask"), params::Origin::User);
                    Floor.set(mask::keys::Method, std::string(g.name), params::Origin::User);
                    if (g.needs_sample)
                        Floor.set(mask::keys::Sample,
                                  std::string("examples/Kodak-4X-1-crop.png"),
                                  params::Origin::User);
                    Floor.set(mask::keys::ImageW, side, params::Origin::Measured);
                    Floor.set(mask::keys::ImageH, side, params::Origin::Measured);
                    Floor.set(mask::keys::SampleW, 4096LL, params::Origin::Measured);
                    Floor.set(mask::keys::SampleH, 4096LL, params::Origin::Measured);
                    Floor.set(mask::keys::Budget, 1e-9, params::Origin::User);
                    Floor.set(std::string(g.name) + ".rate", rate, params::Origin::Measured);
                    const long long fw = Floor.integer(mask::keys::SizeW);
                    const long long fh = Floor.integer(mask::keys::SizeH);

                    if (w != fw || h != fh)
                        return no(std::string(g.name) + " at rate " + std::to_string(rate) +
                                  " and budget " + std::to_string(budget) + " on a " +
                                  std::to_string(side) + " grid chose " + std::to_string(w) +
                                  "x" + std::to_string(h) + ", priced at " +
                                  std::to_string(cost) + "s -- and it will build " +
                                  std::to_string(fw) + "x" + std::to_string(fh) +
                                  " when nothing is affordable, so that was available");
                    floored++;
                }
            }
        }
    }
    if (checked < 100) return no("only " + std::to_string(checked) + " combinations swept");
    return ok(std::to_string(checked) + " rate/budget/grid combinations, " +
              std::to_string(floored) + " where nothing affordable existed");
}

CASE("a mask is a pure function of its inputs, not of what the process did first")
{
    // It was only that by accident. cvflann builds its kd-forest with
    // `std::rand()` -- the split dimension through rand_int, the point order
    // through random_shuffle -- so the tree, and every approximate answer it
    // gave, depended on how many indexes the process had already built. The
    // accident held while a run built one index per level in a fixed order and
    // stopped holding when --mask-calibrate put four probe builds in front of
    // the real one: the same settings gave a different mask depending on
    // whether calibration had run, even when calibration decided nothing.
    //
    // The whole mask cache rests on this, so it is checked for every generator
    // rather than for the one that broke. The interleaved build is deliberately
    // *different* -- a different seed and a different parameter -- because an
    // identical one would be served from nothing and prove nothing.
    const samples::Set set = samples::Set::read({"examples/Kodak-4X-1-crop.png"}, nullptr);
    if (set.empty()) return no("could not read the sample");
    const cv::Size sz(32, 32);

    int checked = 0;
    for (const Generator& g : generators()) {
        params::Params P = cache_registry();
        P.set(mask::keys::Threshold, std::string("mask"), params::Origin::User);
        P.set(mask::keys::Method, std::string(g.name), params::Origin::User);
        if (g.needs_sample) P.set(mask::keys::Sample,
                                  std::string("examples/Kodak-4X-1-crop.png"),
                                  params::Origin::User);
        // Small enough to run four times per generator; this is about identity,
        // not about texture.
        if (P.known("inpaint.db"))     P.set("inpaint.db",     (long long)2000, params::Origin::User);
        if (P.known("inpaint.dims"))   P.set("inpaint.dims",   (long long)4,    params::Origin::User);
        if (P.known("inpaint.scales")) P.set("inpaint.scales", (long long)1,    params::Origin::User);
        if (P.known("inpaint.iters"))  P.set("inpaint.iters",  (long long)1,    params::Origin::User);
        if (P.known("inpaint.checks")) P.set("inpaint.checks", (long long)16,   params::Origin::User);
        if (P.known("markov.pairs"))   P.set("markov.pairs",   (long long)8,    params::Origin::User);
        if (P.known("markov.bins"))    P.set("markov.bins",    (long long)4,    params::Origin::User);
        if (P.known("markov.iters"))   P.set("markov.iters",   (long long)2,    params::Origin::User);
        if (P.known("dft.iters"))      P.set("dft.iters",      (long long)4,    params::Origin::User);

        static const samples::Set none;
        const MaskRequest q{sz, g.needs_sample ? set : none, g.needs_sample ? set : none,
                            7, nullptr, true, 2};
        const cv::Mat first = g.build(P, q);
        if (first.empty()) return no(std::string(g.name) + " built nothing");

        // Something else, to disturb whatever global state there is.
        params::Params Q = P;
        const MaskRequest other{sz, g.needs_sample ? set : none, g.needs_sample ? set : none,
                                99, nullptr, true, 2};
        (void)g.build(Q, other);

        const cv::Mat again = g.build(P, q);
        if (again.empty() || cv::norm(first, again, cv::NORM_INF) != 0.0)
            return no(std::string(g.name) + " built a different mask the second time, "
                      "so it is reading state that is not its inputs");
        checked++;
    }
    if (checked < 5) return no("only " + std::to_string(checked) + " generators checked");
    return ok(std::to_string(checked) + " generators reproduce across an unrelated build");
}

CASE("flags: every option the registry offers decodes back to its own key")
{
    // flag_for is lossy -- `.` and `_` both become `-` -- so the reverse
    // direction cannot decode by rule and must ask the registry. It used to
    // decode: mask.size.w emitted --mask-size-w and came back as mask.size_w,
    // which no rule has, so --list-options advertised a flag the parser
    // rejected. Two of them.
    params::Params P = cache_registry();
    int checked = 0;
    std::map<std::string, std::string> seen;
    for (const std::string& key : P.keys_ending("")) {
        if (!P.has_option(key)) continue;
        const std::string flag = params::Params::flag_for(key);
        const std::string back = P.key_for_flag(flag);
        if (back != key)
            return no(key + " is offered as " + flag + ", which decodes to " +
                      (back.empty() ? "nothing" : back));
        // Two keys emitting one flag would make the parser's answer depend on
        // map order, which is the other way this can go wrong.
        if (const auto it = seen.find(flag); it != seen.end())
            return no(flag + " is emitted by both " + it->second + " and " + key);
        seen[flag] = key;
        checked++;
    }
    if (checked < 20) return no("only " + std::to_string(checked) + " flags checked");
    return ok(std::to_string(checked) + " flags round-trip, none colliding");
}

CASE("flags: a text parameter is typed as text, whatever the value looks like")
{
    // value_from_text asks the registry rather than reading the spelling, so a
    // sample named `0001` stays `0001` instead of becoming the number 1 and
    // sending the resolver after a different file. is_text rests on every
    // numeric parameter declaring a range and no text one doing so, which is
    // the registry's own invariant -- and this is where it is pinned.
    params::Params P = cache_registry();
    int text = 0, numeric = 0;
    for (const std::string& key : P.keys_ending("")) {
        params::Params probe = cache_registry();
        if (probe.is_text(key)) {
            const params::Value v = params::value_from_text(probe, key, "0001");
            if (!std::holds_alternative<std::string>(v) ||
                std::get<std::string>(v) != "0001")
                return no(key + " is text but `0001` was retyped");
            text++;
        } else {
            const params::Value v = params::value_from_text(probe, key, "12");
            if (std::holds_alternative<std::string>(v))
                return no(key + " is numeric but `12` stayed a string");
            numeric++;
        }
    }
    if (text < 3) return no("only " + std::to_string(text) + " text parameters found");
    return ok(std::to_string(text) + " text and " + std::to_string(numeric) +
              " numeric parameters typed from the registry");
}

CASE("mask size: a derived size never exceeds what the sample can supply")
{
    // --supersample 4 on a 256x256 image asks for a 1024 tile, and `direct`
    // cuts its tile out of the sample, so a 512x512 sample has not got one.
    // That used to stop the run. The size rule now clamps, and this is the
    // property: for any generator that declares a sample_limit, the derived
    // size is within it on both axes -- whatever the floors would otherwise
    // have insisted on, since a floor is about repetition and this is about
    // what exists.
    for (const Generator& g : generators()) {
        if (!g.sample_limit) continue;
        for (int ss : {1, 4, 8, 16})
            for (auto dims : {std::pair{512, 512}, std::pair{1800, 950},
                              std::pair{300, 2000}}) {
                params::Params P = cache_registry();
                P.set(mask::keys::Threshold, std::string("mask"), params::Origin::User);
                P.set(mask::keys::Method, std::string(g.name), params::Origin::User);
                P.set(mask::keys::Sample, std::string("x.png"), params::Origin::User);
                P.set(mask::keys::ImageW, (long long)256, params::Origin::Measured);
                P.set(mask::keys::ImageH, (long long)256, params::Origin::Measured);
                P.set(mask::keys::Supersample, (long long)ss, params::Origin::User);
                P.set(mask::keys::SampleW, (long long)dims.first, params::Origin::Measured);
                P.set(mask::keys::SampleH, (long long)dims.second, params::Origin::Measured);

                const params::Resolver r(P);
                const long long w = P.integer(mask::keys::SizeW);
                const long long h = P.integer(mask::keys::SizeH);
                const long long limW = g.sample_limit(r, dims.first);
                const long long limH = g.sample_limit(r, dims.second);
                const std::string where = std::string(g.name) + " ss=" +
                    std::to_string(ss) + " sample " + std::to_string(dims.first) +
                    "x" + std::to_string(dims.second);
                if (w > limW || h > limH)
                    return no(where + " derived " + std::to_string(w) + "x" +
                              std::to_string(h) + ", past the limit " +
                              std::to_string(limW) + "x" + std::to_string(limH));
                if (w < 16 || h < 16)
                    return no(where + " derived a degenerate " + std::to_string(w) +
                              "x" + std::to_string(h));
            }

        // And with the sample unread -- 0, the value before anything has looked
        // at the file -- the clamp must not fire, or --explain with no sample
        // would report a mask of nothing.
        params::Params P = cache_registry();
        P.set(mask::keys::Threshold, std::string("mask"), params::Origin::User);
        P.set(mask::keys::Method, std::string(g.name), params::Origin::User);
        P.set(mask::keys::Sample, std::string("x.png"), params::Origin::User);
        if (P.integer(mask::keys::SizeW) < 128)
            return no(std::string(g.name) + " clamped against an unread sample");
    }
    return ok("derived sizes stay inside the sample on both axes, and 0 means unread");
}

CASE("integer parameters: a fractional value is refused, a derived hair is not")
{
    // The two spellings of one intent used to disagree. `--supersample 2.9`
    // went through a C cast and became 2; `--set supersample=2.9` stored a
    // double and llround made it 3. Neither is what anyone meant, and they are
    // not even the same wrong answer. The parser now refuses a fractional
    // value, and this is the other half: integer() refuses one too.
    params::Params P = cache_registry();
    P.set(mask::keys::Supersample, 2.9, params::Origin::User);
    bool threw = false;
    try { (void)P.integer(mask::keys::Supersample); }
    catch (const std::exception&) { threw = true; }
    if (!threw) return no("supersample=2.9 was silently rounded");

    // But a rule that computes in double and lands a hair off a whole number
    // must still work: the tolerance is for arithmetic, not for typing.
    for (double v : {4.0, 4.0 + 1e-13, 4.0 - 1e-13}) {
        params::Params Q = cache_registry();
        Q.set(mask::keys::Supersample, v, params::Origin::Derived);
        if (Q.integer(mask::keys::Supersample) != 4)
            return no("a derived " + std::to_string(v) + " did not read as 4");
    }

    // integer() takes a long long whole rather than through a double, which is
    // what lets a value past 2^53 survive. Not demonstrable through any
    // parameter here -- seed has the widest range and it stops at 9e15, just
    // under 2^53 -- so this asserts the largest a parameter can actually hold
    // and the property is noted rather than proved.
    const long long big = 9000000000000000LL;
    params::Params R = cache_registry();
    R.set(mask::keys::Seed, big, params::Origin::User);
    if (R.integer(mask::keys::Seed) != big)
        return no("the largest permitted seed did not survive the trip");
    return ok("fractional refused, derived rounding kept, wide seed exact");
}

CASE("mask cache key: values that differ, however slightly, key differently")
{
    // The key is hashed from serialized_of, not value_of, and this is why.
    // value_of is --explain's six significant figures; under it blue.sigma
    // 1.8000001 and 1.8000002 produced one cache entry and the second run was
    // served the first one's mask. A silent wrong answer, in the cache whose
    // entire design is that it must not give one.
    const double base = 1.8;
    const double steps[] = {1e-7, 1e-9, std::nextafter(base, 2.0) - base};
    for (double step : steps) {
        params::Params a = cache_registry(), b = cache_registry();
        for (params::Params* P : {&a, &b}) {
            P->set(mask::keys::Threshold, std::string("mask"), params::Origin::User);
            P->set(mask::keys::Method, std::string("blue"), params::Origin::User);
        }
        a.set(bluenoise::keys::Sigma, base, params::Origin::User);
        b.set(bluenoise::keys::Sigma, base + step, params::Origin::User);
        if (a.num(bluenoise::keys::Sigma) == b.num(bluenoise::keys::Sigma))
            continue;                      // the step vanished in the double
        if (maskcache::key_for(a, "").name == maskcache::key_for(b, "").name)
            return no("blue.sigma " + std::to_string(base) + " and " +
                      std::to_string(base + step) + " share a cache key, so one "
                      "run would be served the other's mask");
    }

    // And the display formatter must *not* have been widened to achieve it:
    // --explain is meant to be readable.
    params::Params P = cache_registry();
    P.set(bluenoise::keys::Sigma, base, params::Origin::User);
    if (P.value_of(bluenoise::keys::Sigma).size() > 8)
        return no("value_of got long: " + P.value_of(bluenoise::keys::Sigma) +
                  " -- the fix belongs in serialization, not in the table");

    // A double that has been through the config file must come back bit for
    // bit, or a measured rate is corrupted by its own storage.
    // In blue.rate's registered range [1, 1e15], since a write outside it
    // throws -- which is the right behaviour and not what this case is about.
    for (double v : {3486652.9836209579, 1.0000000000000002, 1e14,
                     123456789.123456789, 1.0 / 3.0 * 3.0}) {
        params::Params q = cache_registry();
        q.set(bluenoise::keys::Rate, v, params::Origin::Measured);
        const std::string text = q.serialized_of(bluenoise::keys::Rate);
        if (text.rfind("d:", 0) != 0) return no("no type tag: " + text);
        if (std::strtod(text.c_str() + 2, nullptr) != v)
            return no("serialize/parse lost " + text);
    }
    return ok("close doubles key apart, value_of stays short, doubles round-trip");
}

// One registry configured to use `method`, with a sample if that generator
// needs one. The cases below each start from a fresh one: resolving a value
// memoises it along with everything it depended on, so mutating a registry that
// has already been keyed can compare against a half-stale graph.
namespace {
params::Params configured_registry(const char* method, const std::string& sample)
{
    params::Params P = cache_registry();
    P.set(mask::keys::Threshold, std::string("mask"), params::Origin::User);
    P.set(mask::keys::Method, std::string(method), params::Origin::User);
    const Generator* g = find_generator(method);
    if (g && g->needs_sample) P.set(mask::keys::Sample, sample, params::Origin::User);
    return P;
}
}

CASE("mask cache key: every parameter that applies moves it, for every generator")
{
    // For each parameter a run uses, change it and require a different key. A
    // parameter that decides the mask and does not reach the key is exactly the
    // wrong hit this cache exists to prevent.
    //
    // Every generator, not one. This was written pinning method=markov while
    // claiming to sweep "every parameter that applies", so blue, dft, inpaint
    // and direct keys were never touched -- the title described a sweep the
    // body did not perform.
    const std::string sample = "examples/Kodak-4X-1.png";
    int swept = 0;
    std::vector<std::string> skipped;

    for (const Generator& g : generators()) {
        for (const std::string& key : cache_registry().keys_ending("")) {
            params::Params probe = configured_registry(g.name, sample);
            // Intermediates are left out of the key on purpose and are the next
            // case's business. Asked of the registry rather than matched by
            // name, so the two cases cannot disagree about which they are.
            if (probe.intermediate(key) || !probe.applies(key)) continue;
            // Moving one of these changes *which* parameters apply, so the key
            // would differ for a reason other than the one under test.
            if (key == mask::keys::Method || key == mask::keys::Threshold ||
                key == mask::keys::Sample || key == mask::keys::In) continue;

            params::Params before = configured_registry(g.name, sample);
            params::Params after  = configured_registry(g.name, sample);
            const std::string was = before.value_of(key);

            // Several candidates, because one does not fit every parameter: +1
            // leaves blue.eps outside its own range, and a string parameter has
            // no arithmetic at all. The first that lands inside the range wins,
            // so each parameter is perturbed by something it can actually hold.
            bool moved = false;
            auto attempt = [&](params::Value v) {
                if (moved) return;
                // A candidate that lands on the value already there is not a
                // perturbation: doubling a flag that reads 0 sets 0, which
                // succeeded and left the parameter untouched.
                try {
                    after.set(key, std::move(v), params::Origin::User);
                    moved = after.value_of(key) != was;
                } catch (...) { }
            };
            try {
                const double v = before.num(key);
                attempt(v * 2.0);
                attempt(v * 0.5);
                attempt(v + 1.0);
                attempt(v - 1.0);
            } catch (...) {
                // Not a number: a string, so offer the values it is allowed to
                // take. markov.weights is the only one, and `mi` is as valid as
                // the `flat` it defaults to.
                for (const char* alt : {"mi", "ml", "flat"})
                    if (was != alt) attempt(std::string(alt));
            }
            // Named rather than passed over. A parameter this cannot perturb is
            // a parameter this does not cover, and the old version counted its
            // silence as coverage.
            if (!moved || after.value_of(key) == was) {
                skipped.push_back(std::string(g.name) + "/" + key);
                continue;
            }

            const maskcache::Key kb = maskcache::key_for(before, g.needs_sample ? sample : "");
            const maskcache::Key ka = maskcache::key_for(after,  g.needs_sample ? sample : "");
            if (!kb.ok || !ka.ok)
                return no(std::string(g.name) + ": could not build a key");
            if (kb.name == ka.name)
                return no(std::string(g.name) + ": changing " + key + " from " + was +
                          " to " + after.value_of(key) + " left the cache key identical");
            swept++;
        }
    }

    if (swept < 40)
        return no("swept only " + std::to_string(swept) + " parameters across " +
                  std::to_string(generators().size()) + " generators");
    std::string detail = std::to_string(swept) + " parameters over " +
                         std::to_string(generators().size()) + " generators";
    if (!skipped.empty()) {
        detail += "; not perturbable: ";
        for (size_t i = 0; i < skipped.size(); i++)
            detail += (i ? ", " : "") + skipped[i];
    }
    return ok(detail);
}

CASE("mask cache key: an intermediate left out of it cannot change the mask")
{
    // The exclusion that makes the cache useful is also the one that could make
    // it wrong. A parameter declared intermediate is kept out of the key on the
    // grounds that it reaches the mask only by deciding some other parameter --
    // and the registry cannot check that claim, because what it asserts is
    // about which values the C++ *build* reads. Only building says.
    //
    // So: vary each intermediate and require that either the key moved (it
    // shifted something downstream, which is in the key in its own right) or
    // the mask is byte-identical. Key unchanged with the mask changed is the
    // wrong hit this whole design exists to prevent.
    //
    // blue, at a small size, because it needs no sample and every pipeline
    // intermediate applies to it.
    const samples::Set noSample;
    int checked = 0;
    for (const std::string& key : cache_registry().keys_ending("")) {
        auto setup = [&] {
            params::Params P = cache_registry();
            P.set(mask::keys::Threshold, std::string("mask"), params::Origin::User);
            P.set(mask::keys::Method, std::string("blue"), params::Origin::User);
            return P;
        };
        params::Params probe = setup();
        if (!probe.intermediate(key) || !probe.applies(key)) continue;

        params::Params before = setup(), after = setup();
        try {
            const double was = before.num(key);
            // Doubling rather than incrementing, so a rate or a budget moves
            // enough to change what it decides rather than rounding back to the
            // same answer.
            after.set(key, was > 0 ? was * 2.0 : 1.0, params::Origin::User);
        } catch (...) {
            // A string: mask.out, a path the mask is written to.
            after.set(key, before.str(key) + "elsewhere.png", params::Origin::User);
        }
        if (after.value_of(key) == before.value_of(key)) continue;   // range clamped it

        const maskcache::Key kb = maskcache::key_for(before, "");
        const maskcache::Key ka = maskcache::key_for(after, "");
        if (kb.name != ka.name) { checked++; continue; }   // moved something downstream

        const cv::Size sz(32, 32);
        const MaskRequest q{sz, noSample, noSample, 42, nullptr, true, 2};
        const Generator* blue = find_generator("blue");
        const cv::Mat mb = blue->build(before, q);
        const cv::Mat ma = blue->build(after, q);
        if (mb.empty() || ma.empty()) return no("blue failed to build for " + key);
        if (cv::norm(mb, ma, cv::NORM_INF) != 0.0)
            return no(key + " is declared intermediate, but changing it from " +
                      before.value_of(key) + " to " + after.value_of(key) +
                      " changed the mask while leaving the cache key identical -- "
                      "that is a stale-hit bug, either in the declaration or in "
                      "whatever now reads it");
        checked++;
    }
    if (checked < 4) return no("only " + std::to_string(checked) + " intermediates checked");
    return ok(std::to_string(checked) + " intermediates: each moves the key or leaves the mask alone");
}

CASE("mask cache key: a rate does not move it, and the sample's contents do")
{
    // Rates are the one deliberate exclusion, and the reason is that they are
    // re-measured on most runs -- included, the cache would never hit twice.
    // Safe because a rate reaches the mask only through --budget, whose effect
    // arrives as a size and a pair count, both parameters in their own right.
    const std::string sample = "examples/Kodak-4X-1.png";
    params::Params P = cache_registry();
    P.set(mask::keys::Threshold, std::string("mask"), params::Origin::User);
    P.set(mask::keys::Method, std::string("markov"), params::Origin::User);
    P.set(mask::keys::Sample, sample, params::Origin::User);

    const maskcache::Key base = maskcache::key_for(P, sample);
    if (!base.ok) return no("could not key a plain markov run");

    int rates = 0;
    for (const std::string& key : P.keys_ending(".rate")) {
        if (!P.applies(key)) continue;
        if (!P.intermediate(key))
            return no(key + " is not declared an intermediate, so every build "
                            "that re-measures it would miss the cache");
        P.set(key, P.num(key) * 2.0, params::Origin::Measured);
        rates++;
    }
    if (rates == 0) return no("no applicable rate to test with");
    if (maskcache::key_for(P, sample).name != base.name)
        return no("a re-measured rate invalidated the cache");

    // And the half that no parameter can express: the sample is named by path,
    // and a path says nothing about what is in the file today.
    //
    // The alternate key is checked for validity first. An unreadable sample
    // returns an empty name, which differs from base.name and would have
    // satisfied this comparison for entirely the wrong reason -- the test would
    // have passed on a missing file rather than on a different one.
    const maskcache::Key other = maskcache::key_for(P, "examples/Kodak-4X-2.png");
    if (!other.ok) return no("could not key the alternate sample");
    if (other.name == base.name)
        return no("a different sample gave the same key");

    // A sample that cannot be read is not keyable, so the caller builds and
    // does not cache rather than caching under a key that ignores it. Its own
    // assertion, not a side effect of the one above.
    if (maskcache::key_for(P, "examples/does-not-exist.png").ok)
        return no("an unreadable sample produced a usable key");

    return ok(std::to_string(rates) + " rates ignored; sample contents counted");
}

// A two-level field put through the high-pass, which is what the three cases
// below are all about: a sample the high-pass turns into thousands of distinct
// values, none of which the sample recorded.
namespace {
cv::Mat manufactured_detail_field()
{
    // Randomly placed rather than a regular pattern, because a periodic field
    // blurs to few distinct values and would understate the effect. This is
    // what a dithered flat tone looks like, which is the real case.
    cv::Mat two(128, 128, CV_32F);
    cv::RNG rng(99);
    for (int y = 0; y < 128; y++)
        for (int x = 0; x < 128; x++)
            two.at<float>(y, x) = rng.uniform(0, 2) ? 120.0f : 136.0f;
    maskutil::highpass_toroidal(two, 32.0);
    return two;
}
}

CASE("quantise: a high-pass manufactures detail, and rounding removes it")
{
    // Subtracting a blur of integers leaves non-integers, so a sample with two
    // grey levels yields a field with thousands. Ranking is scale-free, so it
    // draws that residue at full contrast.
    const cv::Mat hp = manufactured_detail_field();
    const size_t made = std::set<float>(hp.begin<float>(), hp.end<float>()).size();
    if (made < 1000)
        return no("expected the high-pass to manufacture detail, got " +
                  std::to_string(made) + " distinct values");

    cv::Mat q = hp.clone();
    maskutil::quantise(q, 0.9);
    // Every value on the grid, rounded to nearest so the two sides of zero
    // share one -- these fields straddle zero, and truncating would put the
    // negatives on a different grid from the positives.
    for (auto it = q.begin<float>(); it != q.end<float>(); ++it) {
        const double r = (double)*it / 0.9;
        if (std::abs(r - std::round(r)) > 1e-4)
            return no("quantise left a value off the grid");
    }
    // And a step of zero is a no-op, which is how the parameter is turned off.
    cv::Mat none = hp.clone();
    maskutil::quantise(none, 0.0);
    if (cv::norm(none, hp, cv::NORM_INF) != 0.0)
        return no("step 0 changed the field");
    return ok(std::to_string(made) + " values made from 2, all rounded onto the grid");
}

CASE("local_quantile: every neighbourhood comes out uniform, whatever went in")
{
    // The property `direct` relies on, asserted directly: a ranked tile is flat
    // in local density only where each neighbourhood's distribution matches the
    // whole tile's. So build a field where they match nowhere -- the mean ramps
    // one way, the amplitude ramps the other, and a third of it is clipped flat
    // at zero, which is the case every subtractive preparation got wrong -- and
    // require each block's quantiles to come out mean 1/2 and spread 1/sqrt(12).
    //
    // Twice: on integer codes, where each grey level is counted exactly, and
    // with a fractional part added, which is past 256 distinct values and takes
    // the pooled path instead. That path is not the rare one -- every 16-bit
    // scan and every --mask-scale other than 1 leaves fractional values.
    double worstMean = 0, worstSd = 0;
    for (const bool fractional : {false, true}) {
        cv::Mat f(192, 256, CV_32F);
        cv::RNG rng(91);
        for (int y = 0; y < f.rows; y++)
            for (int x = 0; x < f.cols; x++) {
                const double mean = 40.0 + 120.0 * x / (f.cols - 1);
                const double amp = 30.0 - 24.0 * y / (f.rows - 1);
                double v = std::round(std::max(0.0, mean - 50.0 + amp * rng.gaussian(1.0)));
                if (fractional && v > 0) v += rng.uniform(0.0, 0.99);
                f.at<float>(y, x) = (float)v;
            }
        const std::string which = fractional ? " on the pooled path" : "";
        const cv::Mat u = maskutil::local_quantile(f, 6.0, 5);

        double lo, hi;
        cv::minMaxLoc(u, &lo, &hi);
        if (lo < 0.0 || hi > 1.0) return no("quantiles outside [0, 1]" + which);

        for (int y = 0; y + 32 <= u.rows; y += 32)
            for (int x = 0; x + 32 <= u.cols; x += 32) {
                cv::Scalar m, sd;
                cv::meanStdDev(u(cv::Rect(x, y, 32, 32)), m, sd);
                worstMean = std::max(worstMean, std::abs(m[0] - 0.5));
                worstSd = std::max(worstSd, std::abs(sd[0] - 1.0 / std::sqrt(12.0)));
            }
        // The input's own block means run 0 to 110 codes apart and its block
        // spreads differ fivefold. A subtractive high-pass fails the spread
        // bound by 0.187.
        if (worstMean > 0.05)
            return no("a block's mean quantile is off by " + std::to_string(worstMean) + which);
        if (worstSd > 0.04)
            return no("a block's spread is off by " + std::to_string(worstSd) + which);

        // It depends only on order, so a monotone remapping of the input changes
        // nothing -- which is what lets it run on the sample as recorded,
        // clipped or not, rather than on something filtered from it. Squaring,
        // because a remap that compresses -- sqrt was tried -- merges float32
        // neighbours into new ties on the fractional field, and then the input
        // really has changed.
        const cv::Mat g = f.mul(f);
        if (cv::norm(maskutil::local_quantile(g, 6.0, 5), u, cv::NORM_INF) > 1e-6)
            return no("a monotone remapping of the input changed the result" + which);

        // Pure function of its arguments: the seed decides the ties and nothing
        // else. Only the integer field has ties for a seed to decide.
        if (cv::norm(maskutil::local_quantile(f, 6.0, 5), u, cv::NORM_INF) != 0.0)
            return no("same seed gave a different field" + which);
        if (!fractional && cv::norm(maskutil::local_quantile(f, 6.0, 6), u, cv::NORM_INF) == 0.0)
            return no("a different seed broke the ties identically");
    }
    return ok("block mean within " + std::to_string(worstMean).substr(0, 5) +
              " of 1/2 and spread within " + std::to_string(worstSd).substr(0, 5) +
              " of 1/sqrt(12), over a ramped, unevenly grainy, clipped field, "
              "counted exactly and pooled");
}

CASE("tie_share: 1 for a constant field, 1/n for n distinct values")
{
    cv::Mat allSame(16, 16, CV_32F, cv::Scalar(3.0f));
    if (std::abs(maskutil::tie_share(allSame) - 1.0) > 1e-12)
        return no("a constant field should read 1.0");

    cv::Mat allDiff(16, 16, CV_32F);
    for (int i = 0; i < 256; i++) allDiff.ptr<float>()[i] = (float)i;
    if (std::abs(maskutil::tie_share(allDiff) - 1.0 / 256.0) > 1e-12)
        return no("256 distinct values should read 1/256");

    // And the case it exists for: a quantised two-level sample reads as mostly
    // ties, which is what direct refuses on.
    cv::Mat q = manufactured_detail_field();
    maskutil::quantise(q, 0.9);
    const double flip = maskutil::tie_share(q);
    if (!(flip > 0.25))
        return no("a two-level sample should read as mostly ties, got " +
                  std::to_string(flip));
    return ok("endpoints exact; a two-level sample reads " + std::to_string(flip));
}

CASE("rank_order_random: a permutation, sorted by value, shuffled within ties")
{
    cv::Mat q = manufactured_detail_field();
    maskutil::quantise(q, 0.9);

    const std::vector<int> ro = maskutil::rank_order_random(q, 7);
    if (ro.size() != (size_t)q.total()) return no("not a full permutation");
    std::vector<char> seen(q.total(), 0);
    for (int idx : ro) {
        if (idx < 0 || idx >= (int)q.total() || seen[idx]) return no("not a permutation");
        seen[idx] = 1;
    }
    for (size_t r = 1; r < ro.size(); r++)
        if (q.ptr<float>()[ro[r]] < q.ptr<float>()[ro[r - 1]])
            return no("not sorted by value");

    // The property that matters: within the largest tie group, rank must not
    // track pixel index. Stable ranking makes a tie group a raster ramp, which
    // is a gradient across the tile and a density defect; random makes it noise.
    auto index_correlation = [&q](const std::vector<int>& order) {
        const float* v = q.ptr<float>();
        size_t bi = 0, bn = 0, i = 0;
        while (i < order.size()) {
            size_t j = i + 1;
            while (j < order.size() && v[order[j]] == v[order[i]]) j++;
            if (j - i > bn) { bn = j - i; bi = i; }
            i = j;
        }
        double sx = 0, sy = 0, sxy = 0, sxx = 0, syy = 0;
        for (size_t k = 0; k < bn; k++) {
            const double x = (double)k, y = (double)order[bi + k];
            sx += x; sy += y; sxy += x * y; sxx += x * x; syy += y * y;
        }
        const double n = (double)bn;
        const double num = n * sxy - sx * sy;
        const double den = std::sqrt((n * sxx - sx * sx) * (n * syy - sy * sy));
        return den > 0 ? num / den : 0.0;
    };
    const double shuffled = index_correlation(ro);
    const double stable   = index_correlation(maskutil::rank_order(q));
    if (stable < 0.99)
        return no("stable ranking should order a tie group by pixel index, got " +
                  std::to_string(stable));
    if (std::abs(shuffled) > 0.05)
        return no("random ranking still correlates with pixel index at " +
                  std::to_string(shuffled));
    return ok("group correlation " + std::to_string(stable) + " stable -> " +
              std::to_string(shuffled) + " shuffled");
}

CASE("rank_order_random: seeded from its argument, never the global rng")
{
    // --mask-out then --mask-in has to reproduce a run exactly, which it cannot
    // if the ranking draws from anything the caller also draws from.
    cv::Mat q = manufactured_detail_field();
    maskutil::quantise(q, 0.9);
    const std::vector<int> a = maskutil::rank_order_random(q, 7);
    if (maskutil::rank_order_random(q, 7) != a) return no("same seed gave a different order");
    if (maskutil::rank_order_random(q, 8) == a) return no("different seeds gave the same order");
    return ok("same seed reproduces, different seeds diverge");
}

CASE("bands: markov and inpaint end at the sample's power in every band")
{
    // Spectrum matching (maskutil.h): with it on, the finished mask's power in
    // every band has to be the sample's, and nearer it than with it off. Off is
    // `bands = 0`, the same value --markov-bands 0 and --inpaint-bands 0 set.
    const cv::Mat crop = cv::imread(std::string("examples/Kodak-4X-1-crop.png"),
                                    cv::IMREAD_GRAYSCALE);
    if (crop.empty()) return no("cannot read examples/Kodak-4X-1-crop.png");
    const samples::Set set = samples::Set::of(crop);
    const cv::Size sz(128, 128);
    const samples::Set prepared = set.prepared(0.0, 1.0);
    const int n = maskutil::kDefaultPowerBands;
    const std::vector<double> ref = maskutil::band_reference(
        std::vector<cv::Mat>(prepared.begin(), prepared.end()), sz, n, maskutil::kReferenceEqualise);
    if ((int)ref.size() != n) return no("no reference");

    auto worst = [&](const cv::Mat& mask) {
        cv::Mat f;
        mask.convertTo(f, CV_32F, 1.0 / 254.0);
        const std::vector<double> got = maskutil::band_power(f, sz, n);
        double w = 0;
        // Band 0 is skipped: a handful of cells at 1/128 cycles/px, which the
        // reference's equalising at 24 px has all but emptied, so the re-rank's
        // leakage there reads as a large ratio of two near-zeros.
        for (int b = 1; b < n; b++)
            if (got[b] > 0 && ref[b] > 0) w = std::max(w, std::abs(std::log10(got[b] / ref[b])));
        return w;
    };
    std::string said;
    for (const char* gen : {"markov", "inpaint"}) {
        double on = 0, off = 0;
        for (const int bands : {n, 0}) {
            cv::Mat m;
            if (std::string(gen) == "markov") {
                MarkovParams p; p.bands = bands;
                m = build_markov_mask(sz, set, p, 3, nullptr, &set);
            } else {
                InpaintParams p; p.bands = bands; p.db = 20000;
                m = build_inpaint_mask(sz, set, p, 3, nullptr, &set);
            }
            if (m.empty()) return no(std::string(gen) + " built nothing");
            (bands ? on : off) = worst(m);
        }
        if (!(on < 0.15)) return no(std::string(gen) + "'s worst band is " + std::to_string(on)
                                    + " log10 from the sample's, held");
        if (!(on < off)) return no(std::string(gen) + " is no nearer held than unheld");
        said += std::string(said.empty() ? "" : ", ") + gen + " " + cv::format("%.2f", on)
              + " against " + cv::format("%.2f", off);
    }
    // And the count is honoured: a reference measured in 4 bands has 4.
    if (maskutil::band_reference(std::vector<cv::Mat>(prepared.begin(), prepared.end()), sz, 4,
                                 maskutil::kReferenceEqualise).size() != 4)
        return no("a reference asked for in 4 bands does not have 4");
    return ok("worst band, log10 from the sample's, matched against not: " + said);
}

CASE("band_reference: every scan counts, every part of a scan counts, order does not")
{
    // It once took its sixteen tiles in raster order from the first scan big
    // enough, so a second scan changed nothing, the bottom of a tall scan was
    // never read, and swapping the files changed the target by 10^4 in a band.
    auto noise = [](int w, int h, double sigma, int seed) {
        cv::Mat f(h, w, CV_32F);
        cv::RNG(seed).fill(f, cv::RNG::NORMAL, 0.0, 1.0);
        if (sigma > 0) cv::GaussianBlur(f, f, cv::Size(), sigma);
        return f;
    };
    auto dist = [](const std::vector<double>& a, const std::vector<double>& b) {
        double d = 0;
        for (size_t i = 0; i < a.size() && i < b.size(); i++)
            if (a[i] > 0 && b[i] > 0) d = std::max(d, std::abs(std::log10(a[i] / b[i])));
        return d;
    };
    const cv::Size field(128, 128);
    const cv::Mat a = noise(512, 512, 0, 1), b = noise(512, 512, 2, 2);
    const auto onlyA = maskutil::band_reference({a}, field, 16, 24);
    const auto ab = maskutil::band_reference({a, b}, field, 16, 24);
    const auto ba = maskutil::band_reference({b, a}, field, 16, 24);
    if (dist(onlyA, ab) < 0.3) return no("a second, very different scan barely moved the target");
    if (dist(ab, ba) > 0.05) return no("the order of the scans moved the target by "
                                       + std::to_string(dist(ab, ba)));
    cv::Mat tall;
    cv::vconcat(noise(512, 512, 0, 3), noise(512, 512, 2, 4), tall);
    const auto top = maskutil::band_reference({tall(cv::Rect(0, 0, 512, 512)).clone()}, field, 16, 24);
    const auto whole = maskutil::band_reference({tall}, field, 16, 24);
    if (dist(top, whole) < 0.3) return no("the bottom half of a tall scan was not read");
    return ok("a second scan moves the target " + cv::format("%.2f", dist(onlyA, ab))
              + " log10, its order " + cv::format("%.3f", dist(ab, ba))
              + ", a tall scan's lower half " + cv::format("%.2f", dist(top, whole)));
}

CASE("calibration holds its probes to the reference the build uses")
{
    // A directory of scans with no crop: the build holds the mask to every scan,
    // and calibration once held its probes to the largest alone -- a different
    // synthesiser, measured and then applied to this one.
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path()
        / ("dither-calib-ref-" + std::to_string(std::random_device{}()));
    fs::create_directories(dir);
    cv::Mat a(128, 128, CV_8U), b(120, 120, CV_8U);
    cv::RNG(1).fill(a, cv::RNG::UNIFORM, 0, 256);
    cv::RNG(2).fill(b, cv::RNG::UNIFORM, 0, 256);
    cv::GaussianBlur(b, b, cv::Size(), 2.0);
    cv::imwrite((dir / "a.png").string(), a);
    cv::imwrite((dir / "b.png").string(), b);
    const auto in = calib::inputs_for(dir.string(), false);
    fs::remove_all(dir);
    if (!in) return no("the directory could not be read");
    const samples::Set build = samples::reference_for(dir.string(), in->set);
    if (in->reference.size() != 2 || build.size() != 2)
        return no("the calibration's reference has " + std::to_string(in->reference.size())
                  + " images where the build's has " + std::to_string(build.size()));
    MarkovParams p; p.pairs = 8; p.bins = 4; p.radius = 3; p.shortlist = 8;
    auto mb = fit_markov_model(in->set, p, {64, 64}, &build);
    auto mc = fit_markov_model(in->set, p, {64, 64}, &in->reference);
    if (!mb || !mc) return no("no model fitted");
    const cv::Mat x = sample_markov_mask({64, 64}, *mb, p, 1, nullptr);
    const cv::Mat y = sample_markov_mask({64, 64}, *mc, p, 1, nullptr);
    if (cv::countNonZero(x != y)) return no("a calibration probe and the build's mask differ");
    if (in->scoring.data != in->reference.primary().data)
        return no("scoring is not the reference's largest image");
    return ok("both scans in the reference, probe and build byte-identical");
}

CASE("direct: refuses a sample whose mask would be mostly coin flips")
{
    // The guard this replaced counted distinct values in the *tile* and wanted
    // 255 of them. That tested a quantity the high-pass defeats: a two-valued
    // sample sails past it with thousands. What matters is how much of the
    // finished ordering the sample actually decides, which is what tie_share
    // measures once the manufactured detail has been quantised away.
    const cv::Mat flat(256, 256, CV_8U, cv::Scalar(128));

    cv::Mat ramp(256, 256, CV_8U);
    for (int y = 0; y < 256; y++)
        for (int x = 0; x < 256; x++)
            ramp.at<uint8_t>(y, x) = (uint8_t)(96 + x * 32 / 255);

    // And it must not be over-eager. Real film measures 0.006 to 0.065 against
    // the bar of 0.25, and that includes a fine-grained Panatomic-X scan which
    // survives quantising with only 50 distinct levels -- fewer than several of
    // the synthetic patterns that are refused, which is why the bar is on the
    // tie share and not on a count.
    cv::Mat noise(256, 256, CV_8U);
    cv::RNG rng(4242);
    rng.fill(noise, cv::RNG::UNIFORM, 0, 256);

    // Both preparations, because the guard is a property of the sample and
    // neither gets to be exempt. Equalising is the one that needs it: it breaks
    // every tie at random, so a flat sample comes out of it as pure shuffle with
    // no ties left to count, and a guard measured afterwards would pass it.
    for (const double equalise : {0.0, 12.0}) {
        const auto build = [&](const cv::Mat& s, double q) {
            return build_direct_mask({64, 64}, s, 1.0, 32.0, equalise, 8, q, 1, nullptr);
        };
        if (!build(flat, 0.9).empty())
            return no("a featureless sample produced a mask");
        if (!build(ramp, 0.9).empty())
            return no("a smooth gradient produced a mask");
        const cv::Mat m = build(noise, 0.9);
        if (m.empty()) return no("a textured sample was refused");
        if (m.size() != cv::Size(64, 64) || m.type() != CV_8U)
            return no("wrong size or type");

        // Both halves of "with and without rounding", which the conclusion used
        // to claim while testing the flat sample only with it off and the
        // textured sample only with it on.
        if (!build(flat, 0.0).empty())
            return no("a featureless sample passed with quantising off");
        if (build(noise, 0.0).empty())
            return no("a textured sample was refused with quantising off");
    }

    // At --mask-scale other than 1 the cutoff is in the sample's pixels, so
    // the guard has to filter there too. Low-contrast grain with a 2 px
    // correlation is the case that separates the two: high-passed at 32 px
    // *after* a 4x enlargement it loses most of its texture and reads as
    // mostly ties, which refused it under equalising while the subtractive
    // path, filtering first, accepted it.
    cv::Mat grain(128, 128, CV_32F);
    cv::RNG grng(177);
    grng.fill(grain, cv::RNG::NORMAL, 0, 1);
    cv::GaussianBlur(grain, grain, cv::Size(), 2.0);
    cv::Scalar mu, sd;
    cv::meanStdDev(grain, mu, sd);
    cv::Mat soft;
    grain.convertTo(soft, CV_8U, 2.0 / sd[0], 128.0);
    const cv::Mat flat_small(128, 128, CV_8U, cv::Scalar(128));
    for (const double scale : {0.5, 4.0})
        for (const double equalise : {0.0, 12.0}) {
            const cv::Size sz = scale > 1.0 ? cv::Size(256, 256) : cv::Size(48, 48);
            const std::string at = " at --mask-scale " + std::to_string(scale).substr(0, 3)
                                 + (equalise > 0 ? ", equalised" : ", high-passed");
            if (build_direct_mask(sz, soft, scale, 32.0, equalise, 8, 0.9, 1, nullptr).empty())
                return no("low-contrast grain was refused" + at);
            if (!build_direct_mask(sz, flat_small, scale, 32.0, equalise, 8, 0.9, 1,
                                   nullptr).empty())
                return no("a featureless sample produced a mask" + at);
        }
    return ok("featureless and gradient refused, textured accepted, each with "
              "and without rounding, high-passed and equalised, and at scales "
              "either side of 1");
}

// ---------------------------------------------------------------------------
// The multi-frame budget. This is test/budgettest, converted: same matrix, same
// invariant, without the subprocess, the PNGs or the reconstructed ladder.
// ---------------------------------------------------------------------------

// A flat mask with no structure. MaskDither only requires a flat histogram over
// 0..254 -- what the pattern looks like is the generators' business and is
// tested by test/masktest -- so this exercises the same path in milliseconds
// rather than building void-and-cluster.
cv::Mat flat_mask(cv::Size sz, uint64_t seed)
{
    cv::Mat f(sz, CV_32F);
    cv::RNG r(seed);
    r.fill(f, cv::RNG::UNIFORM, 0.0f, 1.0f);
    return maskutil::threshold_mask(maskutil::rank_order(f), sz);
}

struct BudgetCase {
    const char*     threshold;
    int             frames, levels;
    bool            srgb;
    vision::Spacing spacing;
    // A 16-bit target instead of the 8-bit ramp. This is the digital-enlarger
    // claim: N 8-bit exposures summing to the right amount of light, where the
    // amount asked for is finer than any one exposure can express. With an
    // 8-bit target there is nothing below one level to distribute and the
    // budget has nothing to prove.
    bool            wide = false;
};

std::pair<bool, std::string> budget_holds(const cv::Mat& image, const BudgetCase& c)
{
    const vision::LevelSet ladder = vision::make_levels(c.levels, c.srgb, c.spacing);
    const cv::Mat target = vision::decode_display(image, ladder, c.srgb);

    rng.seed(42);                                   // the stochastic rules draw from this
    std::unique_ptr<DitherFn> fn;
    if (std::string(c.threshold) == "round")       fn = std::make_unique<RoundDither>();
    else if (std::string(c.threshold) == "random") fn = std::make_unique<RandomDither>();
    else                                           fn = std::make_unique<MaskDither>(
                                                        flat_mask({64, 64}, 7));

    // --spatial 0: no diffusion anywhere, which is what isolates the temporal
    // budget. With error diffusion on, a pixel's residual moves to its
    // neighbours and its displayed sum is no longer tied to its own goal.
    const std::vector<cv::Mat> frames =
        ditherPass(target, ladder, *fn, spatial_none, 0.0f, c.levels, c.frames, -1, -1);
    if ((int)frames.size() != c.frames) return no("wrong frame count");

    cv::Mat sum = cv::Mat::zeros(target.size(), CV_64F);
    for (const cv::Mat& f : frames) {
        if (f.type() != CV_8U) return no("frames are not CV_8U");
        cv::Mat as_double;
        f.convertTo(as_double, CV_64F);
        sum += as_double;
    }

    // The goal is dither_fn(target*frames), so for the deterministic rule the
    // sum is exactly lround of it, and for a stochastic one it is the floor or
    // the ceiling -- within 1 either way.
    const bool exact = std::string(c.threshold) == "round";
    double worst = 0; int bad = 0, badr = 0, badc = 0;
    for (int r = 0; r < target.rows; r++)
        for (int col = 0; col < target.cols; col++) {
            const double want = (double)target.at<float>(r, col) * c.frames;
            const double got  = sum.at<double>(r, col);
            const double dev  = exact ? std::abs(got - std::floor(want + 0.5))
                                      : std::abs(got - want);
            if (dev > worst) { worst = dev; badr = r; badc = col; }
            if (dev > (exact ? 0.0 : 1.0)) bad++;
        }
    if (bad) {
        char buf[160];
        std::snprintf(buf, sizeof buf, "%d/%d pixels off; worst %.3f at (%d,%d)",
                      bad, (int)target.total(), worst, badr, badc);
        return no(buf);
    }
    char buf[80];
    std::snprintf(buf, sizeof buf, "%d pixels, worst %.3f", (int)target.total(), worst);
    return ok(buf);
}

} // namespace

int main(int argc, char** argv)
{
    const std::string ref = argc > 1 ? argv[1] : "examples/ramp_256x256.png";
    const cv::Mat image = cv::imread(ref, cv::IMREAD_GRAYSCALE);
    if (image.empty()) {
        std::fprintf(stderr, "cannot read %s (pass its path as argv[1])\n", ref.c_str());
        return 1;
    }

    // The matrix test/budgettest carried, unchanged. The last six are the two
    // ladders that are not evenly spaced in pixel value, which is the case Space
    // exists for and the one the Python version could not express at all.
    const BudgetCase budget[] = {
        {"round",    4, 2, true,  vision::Spacing::Srgb},
        {"round",   16, 2, true,  vision::Spacing::Srgb},
        {"round",   64, 2, true,  vision::Spacing::Srgb},
        {"round",  128, 2, true,  vision::Spacing::Srgb},
        {"round",   16, 2, false, vision::Spacing::Srgb},
        {"round",   64, 2, false, vision::Spacing::Srgb},
        {"round",   16, 8, false, vision::Spacing::Srgb},
        {"round",   16, 8, true,  vision::Spacing::Srgb},
        {"random",   2, 2, true,  vision::Spacing::Srgb},
        {"random",   4, 2, true,  vision::Spacing::Srgb},
        {"random",  16, 2, true,  vision::Spacing::Srgb},
        {"random",  16, 8, true,  vision::Spacing::Srgb},
        {"random",  64, 8, true,  vision::Spacing::Srgb},
        {"mask",     4, 2, true,  vision::Spacing::Srgb},
        {"mask",    16, 2, true,  vision::Spacing::Srgb},
        {"mask",    16, 8, true,  vision::Spacing::Srgb},
        {"round",   16, 3, true,  vision::Spacing::Linear},
        {"round",   16, 8, true,  vision::Spacing::Linear},
        {"random",  16, 5, true,  vision::Spacing::Linear},
        {"mask",    16, 8, true,  vision::Spacing::Linear},
        {"round",   16, 3, true,  vision::Spacing::Device},
        {"random",  16, 8, true,  vision::Spacing::Device},
        // 16-bit targets, including the enlarger's own shape: 8-bit frames
        // (levels 256) exposed in sequence, where the sum has to land on an
        // amount of light no single frame could show.
        {"round",    4, 2,   true, vision::Spacing::Srgb,   true},
        {"round",   16, 256, true, vision::Spacing::Srgb,   true},
        {"round",   64, 256, true, vision::Spacing::Srgb,   true},
        {"random",  16, 256, true, vision::Spacing::Srgb,   true},
        {"mask",    16, 256, true, vision::Spacing::Srgb,   true},
        {"round",   16, 8,   true, vision::Spacing::Linear, true},
    };
    const cv::Mat wide = shallow_ramp16(256, 256);
    for (const BudgetCase& c : budget) {
        char name[110];
        const char* sp = c.spacing == vision::Spacing::Srgb   ? "srgb"
                       : c.spacing == vision::Spacing::Linear ? "linear" : "device";
        std::snprintf(name, sizeof name, "budget closes: %-6s frames=%-3d levels=%-3d %s%s%s",
                      c.threshold, c.frames, c.levels, sp, c.srgb ? "" : " (--linear)",
                      c.wide ? " [16-bit in]" : "");
        const cv::Mat& in = c.wide ? wide : image;
        cases().push_back({name, [&in, c] { return budget_holds(in, c); }});
    }

    std::printf("ref=%s\n\n%-62s %-6s %s\n", ref.c_str(), "case", "result", "detail");
    std::printf("%s\n", std::string(110, '-').c_str());
    int failed = 0;
    for (const Case& c : cases()) {
        // A case that throws is one failing case, not a dead run.
        //
        // Without this a single exception aborted the runner: no name against
        // the failure, no results for anything after it, and an abort rather
        // than an exit status. That is not hypothetical -- writing the cache
        // serialization case I set a value outside its registered range, and
        // the whole suite stopped with `terminate called after throwing` and
        // nothing to say which case had done it.
        //
        // OpenCV and the parameter registry both throw for ordinary
        // programming mistakes, and a mistake in one case should not hide the
        // other fifty-four. Only std::exception: a segfault or an assert is a
        // different kind of event, it cannot be caught here, and pretending
        // otherwise would be worse than letting it stop the run.
        bool passed = false;
        std::string detail;
        try {
            std::tie(passed, detail) = c.run();
        } catch (const std::exception& e) {
            detail = std::string("threw ") + e.what();
        } catch (...) {
            detail = "threw a non-std exception";
        }
        failed += !passed;
        std::printf("%-62s %-6s %s\n", c.name.c_str(), passed ? "PASS" : "FAIL",
                    detail.c_str());
    }
    std::printf("%s\n", std::string(110, '-').c_str());
    std::printf("OVERALL %s (%d of %zu)\n", failed ? "FAIL" : "PASS",
                (int)cases().size() - failed, cases().size());
    return failed ? 1 : 0;
}
