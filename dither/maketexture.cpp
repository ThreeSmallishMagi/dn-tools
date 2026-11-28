// SPDX-License-Identifier: GPL-3.0-or-later
// maketexture — synthesise a texture from a patch of an image, using the code
// `dither --mask-method inpaint|markov|dft` builds its masks with.
//
// Every measure of the synthesis in this project is a statistic over a field
// that has already been high-passed and ranked flat. This answers the question
// those cannot: cut a patch out of a photograph, synthesise a replacement from
// it, paste it back (`--paste`), and look for the join. A model that works
// leaves nothing to find, and the way a bad one fails — wrong grain size, wrong
// contrast, visible ridges — says which part is wrong.
//
// It is the same code, not a copy. Each generator already produced an ordering
// and then made a mask of it; a mask maps that ordering onto a flat 0..254
// scale, and a texture maps it onto the sample's own sorted values. Nothing
// before that last step differs.
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include <opencv2/opencv.hpp>

#include "cli.h"
#include "defaults.h"
#include "generators.h"
#include "inpaint.h"
#include "mask.h"
#include "markov.h"
#include "maskparams.h"
#include "maskutil.h"
#include "spectralmask.h"

namespace {

// Bounds the products that generator code computes in int.
constexpr long long kMaxSide = 16384;
constexpr long long kMaxArea = 64LL << 20;

void print_usage()
{
    std::fputs(R"(usage: maketexture <image> [options]

Synthesises a texture from a patch of <image> with the same generators dither
builds masks from, and optionally pastes it back so the join can be looked for.

  --mask-method <m>   inpaint (default), markov or dft
  --out <path>        where to write it (default texture.png)
  --sample WxH+X+Y    the patch to learn from. Default: the largest centred
                      square that fits, capped at 512
  --size WxH          the texture to make (default: the sample's size)
  --paste <path>      also write the whole input with the sampled region
                      replaced. If the synthesis works, the region cannot be
                      found. Requires --size to equal the sample's size
  --highpass <px>     another spelling of --mask-highpass. Models only
                      structure finer than this and puts the sample's coarser
                      half back afterwards, so a patch whose tone drifts still
                      matches its surroundings (default 0 — model everything)
  --seed <n>          default 1

Every parameter of the chosen generator is settable by its own flag, exactly as
in dither: --inpaint-iters, --markov-temp, --dft-smooth-cells and the rest.
Options belonging to another generator are refused rather than ignored.

blue and direct are not offered: blue imitates nothing, and direct is a crop of
the sample rather than a model of it.
)", stdout);
}

struct Request {
    std::string in, out = "texture.png", paste, method = "inpaint";
    cv::Rect sample;
    cv::Size size;
    uint64_t seed = 1;
    bool haveSample = false, haveSize = false;
};

[[noreturn]] void fail(const std::string& why) { throw std::runtime_error(why); }

// This tool synthesises one field from one patch. A generator's own parameters
// apply, and so do the two that describe the texture being imitated; the rest
// of the registry is dither's workflow -- loading a ready-made mask, writing
// one, calibrating, supersampling an image -- and accepting those would mean
// taking an option and not doing what it asks. --mask-in did exactly that.
bool accepted(const std::string& key)
{
    if (key == mask::keys::Scale || key == mask::keys::Highpass) return true;
    const auto dot = key.find('.');
    if (dot == std::string::npos) return false;
    const std::string ns = key.substr(0, dot), leaf = key.substr(dot + 1);
    // max_size, max_repeats and rate drive dither's automatic mask sizing.
    // This tool is told its size, so it never reads them.
    if (leaf == "max_size" || leaf == "max_repeats" || leaf == "rate") return false;
    for (const Generator& g : generators())
        if (ns == g.name && g.name != std::string("blue")
                         && g.name != std::string("direct")) return true;
    return false;
}

// Every option this program accepts, for the completion script.
void list_options()
{
    params::Params P;
    for (const Generator& g : generators()) g.register_params(P);
    register_mask_params(P);
    for (const std::string& k : P.options())
        if (accepted(k)) std::printf("%s\n", params::Params::flag_for(k).c_str());
    for (const char* f : {"--mask-method", "--out", "--sample", "--size",
                          "--paste", "--highpass", "--seed", "--help"})
        std::printf("%s\n", f);
}

long long checked_dim(const std::string& v, const char* what)
{
    const auto n = params::parse_integer(v);
    if (!n || *n < 1 || *n > kMaxSide)
        fail(std::string(what) + " must be a whole number in 1.." + std::to_string(kMaxSide)
             + ", not '" + v + "'");
    return *n;
}

// "WxH+X+Y" or "WxH". sscanf is not a checked conversion — `--size
// 4294967312x16` wrapped to 16 and reported success — so this tokenises and
// uses the registry's parser, which refuses a trailing character.
cv::Rect parse_rect(const std::string& s, bool withOffset, const char* what)
{
    const auto x = s.find('x');
    if (x == std::string::npos) fail(std::string(what) + ": expected WxH in '" + s + "'");
    const long long w = checked_dim(s.substr(0, x), what);
    const std::string rest = s.substr(x + 1);
    long long h, ox = 0, oy = 0;
    if (withOffset) {
        const auto p1 = rest.find('+');
        if (p1 == std::string::npos)
            fail(std::string(what) + ": expected WxH+X+Y in '" + s + "'");
        h = checked_dim(rest.substr(0, p1), what);
        const std::string off = rest.substr(p1 + 1);
        const auto p2 = off.find('+');
        if (p2 == std::string::npos)
            fail(std::string(what) + ": expected WxH+X+Y in '" + s + "'");
        const auto a = params::parse_integer(off.substr(0, p2));
        const auto b = params::parse_integer(off.substr(p2 + 1));
        if (!a || !b || *a < 0 || *b < 0 || *a > kMaxSide || *b > kMaxSide)
            fail(std::string(what) + ": offsets must be whole and non-negative");
        ox = *a; oy = *b;
    } else {
        h = checked_dim(rest, what);
    }
    if (w * h > kMaxArea)
        fail(std::string(what) + ": " + std::to_string(w) + "x" + std::to_string(h)
             + " is more than " + std::to_string(kMaxArea) + " pixels");
    return cv::Rect((int)ox, (int)oy, (int)w, (int)h);
}

// The typing cli.cpp applies, from the one implementation both call, so
// --inpaint-db means one thing to both programs.
void set_typed(params::Params& P, const std::string& key, const std::string& val)
{
    P.set(key, params::value_from_text(P, key, val), params::Origin::User);
}

Request parse(int argc, char** argv, params::Params& P)
{
    Request r;
    r.in = argv[1];
    // This frontend models the whole patch unless told otherwise; dither's
    // registry default of 32 is for masks, which want grain without subject.
    // Set before the flags so --highpass and --mask-highpass both override it.
    P.set(mask::keys::Highpass, 0.0, params::Origin::User);
    // Generic writes wait for the method, so a flag belonging to another
    // generator can be refused rather than quietly accepted and never read.
    std::vector<std::pair<std::string, std::string>> pending;

    for (int i = 2; i < argc; i++) {
        const std::string a = argv[i];
        auto next = [&](const std::string& what) {
            if (i + 1 >= argc) fail(what + " needs a value");
            return std::string(argv[++i]);
        };
        if      (a == "--help" || a == "-h") { print_usage(); std::exit(0); }

        else if (a == "--mask-method") r.method = next(a);
        else if (a == "--out")         r.out    = next(a);
        else if (a == "--paste")       r.paste  = next(a);
        else if (a == "--sample") {
            r.sample = parse_rect(next(a), true, "--sample");
            r.haveSample = true;
        } else if (a == "--size") {
            const cv::Rect q = parse_rect(next(a), false, "--size");
            r.size = cv::Size(q.width, q.height);
            r.haveSize = true;
        } else if (a == "--seed") {
            const auto n = params::parse_integer(next(a));
            if (!n || *n < 0) fail("--seed must be a whole number, zero or more");
            r.seed = (uint64_t)*n;
        }
        // A second spelling of the registry's value rather than a variable of
        // its own: as a variable it was applied after the registry write, so
        // `--mask-highpass 32` announced and used 0.
        else if (a == "--highpass") pending.emplace_back(mask::keys::Highpass, next(a));
        else if (a.rfind("--", 0) == 0) {
            const std::string key = P.key_for_flag(a);
            if (key.empty() || !P.has_option(key)) fail("Unknown option: " + a);
            if (!accepted(key))
                fail(a + " is a dither option; maketexture synthesises a texture "
                         "from a patch and has no use for it");
            pending.emplace_back(key, P.is_switch(key) ? std::string("1") : next(a));
        } else fail("Unexpected argument: " + a);
    }

    if (r.method != "inpaint" && r.method != "markov" && r.method != "dft")
        fail("--mask-method must be inpaint, markov or dft (blue imitates nothing, "
             "and direct is a crop of the sample rather than a model of it)");
    // The applicability rules are written for dither, where a generator's
    // parameters matter only when a mask is being built. maketexture is always
    // building one, so it says so and the same rules then answer correctly.
    P.set(mask::keys::Threshold, std::string("mask"), params::Origin::User);
    P.set(mask::keys::Method, r.method, params::Origin::User);

    // In order, so a repeated flag means its last occurrence.
    for (const auto& [k, v] : pending) {
        if (!P.applies(k)) fail(k + " does not apply to --mask-method " + r.method);
        set_typed(P, k, v);
    }
    const Generator* g = find_generator(r.method);
    if (g && g->validate && !g->validate(P)) fail("invalid settings for " + r.method);
    return r;
}

int run(int argc, char** argv)
{
    params::Params P;
    for (const Generator& g : generators()) g.register_params(P);
    register_mask_params(P);
    const Request r = parse(argc, argv, P);

    const cv::Mat image = cv::imread(r.in, cv::IMREAD_ANYDEPTH | cv::IMREAD_GRAYSCALE);
    if (image.empty()) fail("Could not read " + r.in);

    cv::Rect sample = r.sample;
    if (!r.haveSample) {
        const int side = std::min({image.cols, image.rows, 512});
        sample = cv::Rect((image.cols - side) / 2, (image.rows - side) / 2, side, side);
    }
    if ((sample & cv::Rect(0, 0, image.cols, image.rows)) != sample)
        fail("--sample " + std::to_string(sample.width) + "x" + std::to_string(sample.height)
             + "+" + std::to_string(sample.x) + "+" + std::to_string(sample.y)
             + " is not inside the " + std::to_string(image.cols) + "x"
             + std::to_string(image.rows) + " image");

    const cv::Size want = r.haveSize ? r.size : sample.size();
    const double scale    = P.num(mask::keys::Scale);
    const double highpass = P.num(mask::keys::Highpass);

    // Pasting puts the texture back where the sample came from, so the coarse
    // half restored into it has to be that region's. At a different size there
    // is no such correspondence — the tone came from the wrong pixels and the
    // join showed a step regardless of the model — so the combination is
    // refused rather than silently approximated.
    if (!r.paste.empty() && want != sample.size())
        fail("--paste needs --size to equal the sample's size ("
             + std::to_string(sample.width) + "x" + std::to_string(sample.height)
             + "), because the restored tone belongs to the region being replaced");

    const cv::Mat patch = image(sample).clone();
    std::fprintf(stderr, "Synthesising %dx%d %s from %dx%d+%d+%d of %s "
                         "(highpass %g, scale %g) …\n",
                 want.width, want.height, r.method.c_str(), sample.width, sample.height,
                 sample.x, sample.y, r.in.c_str(), highpass, scale);

    // With a high-pass the coarse half is kept and added back: the generator
    // models texture, not large-scale tone, and a small window cannot represent
    // a gradient across the patch.
    cv::Mat coarse;
    if (highpass > 1.0) {
        cv::Mat f;
        patch.convertTo(f, CV_32F);
        const double sigma = maskutil::kHighpassSigmaPerPixel * highpass;
        cv::GaussianBlur(f, coarse, cv::Size(), sigma, sigma, cv::BORDER_REFLECT);
        if (coarse.size() != want)
            cv::resize(coarse, coarse, want, 0, 0, cv::INTER_LINEAR);
    }

    const cv::Mat prepared = maskutil::prepare_texture_sample(patch, highpass, scale);
    if (prepared.cols < 4 || prepared.rows < 4)
        fail("the sample is " + std::to_string(prepared.cols) + "x"
             + std::to_string(prepared.rows) + " after --mask-scale, too small to model");

    std::vector<int> order;
    if (r.method == "inpaint") {
        const cv::Mat field = synthesise_inpaint_field(want, prepared, inpaint_params_from(P),
                                                       r.seed, &std::cerr);
        if (field.empty()) return 1;        // it has said which level and why
        order = maskutil::rank_order(field);
    } else if (r.method == "dft") {
        order = synthesise_spectral_order(want, patch, scale, highpass,
                                          (int)P.integer(spectralmask::keys::Iters),
                                          P.num(spectralmask::keys::SmoothCells), r.seed);
    } else {
        order = synthesise_markov_order(want, patch, markov_params_from(P), r.seed, &std::cerr);
        if (order.empty()) return 1;        // the fit has said why
    }

    // What makes it a texture rather than a mask: the rank-r pixel takes the
    // patch's r-th smallest value. Monotone, so it reorders nothing.
    //
    // This gives the prepared patch's distribution *resampled* to the output's
    // pixel count — its exact histogram only when the two counts match, and
    // altered again by the coarse half and by clipping on write.
    std::vector<float> sorted(prepared.ptr<float>(),
                              prepared.ptr<float>() + prepared.total());
    std::sort(sorted.begin(), sorted.end());
    cv::Mat tex(want, CV_32F);
    const double step = (double)(sorted.size() - 1) / std::max((int)order.size() - 1, 1);
    for (size_t i = 0; i < order.size(); i++)
        tex.ptr<float>()[order[i]] = sorted[(size_t)std::lround(i * step)];
    if (!coarse.empty()) tex += coarse;

    cv::Mat outImg;
    tex.convertTo(outImg, image.type());
    if (!write_image(r.out, outImg)) return 1;
    std::fprintf(stderr, "wrote %s\n", r.out.c_str());

    if (!r.paste.empty()) {
        cv::Mat merged = image.clone();
        outImg.copyTo(merged(sample));
        if (!write_image(r.paste, merged)) return 1;
        std::fprintf(stderr, "wrote %s — the replaced region is %dx%d+%d+%d\n",
                     r.paste.c_str(), sample.width, sample.height, sample.x, sample.y);
    }
    return 0;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 2 || !std::strcmp(argv[1], "--help") || !std::strcmp(argv[1], "-h")) {
        print_usage();
        return argc < 2 ? 1 : 0;
    }
    if (!std::strcmp(argv[1], "--list-options")) { list_options(); return 0; }
    // Ordinary user errors arrive here as exceptions rather than aborts: a bad
    // range, an unwritable path, an extension OpenCV has no encoder for.
    try {
        return run(argc, argv);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "%s\n", e.what());
        return 1;
    }
}
