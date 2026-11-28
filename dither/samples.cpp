// SPDX-License-Identifier: GPL-3.0-or-later
// samples.cpp — see samples.h.
#include "samples.h"

#include "maskutil.h"

#include <algorithm>
#include <numeric>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <ostream>
#include <opencv2/opencv.hpp>

namespace fs = std::filesystem;

namespace samples {

namespace {

// What cv::imread will take and this project uses. Checked case-insensitively,
// because a directory the user filled by hand will have .JPG in it eventually.
bool is_image(const fs::path& p)
{
    std::string e = p.extension().string();
    for (char& c : e) c = (char)std::tolower((unsigned char)c);
    return e == ".png" || e == ".jpg" || e == ".jpeg" || e == ".tif"
        || e == ".tiff" || e == ".bmp" || e == ".pgm" || e == ".webp";
}

// Every image directly inside `dir`, sorted. Not recursive: a film directory
// holds scans, and a subdirectory in one is more likely a mistake than a
// deliberate grouping this should flatten.
// A fresh error_code per entry, and an increment that reports rather than
// throws: sharing one across the loop meant a single dangling symlink made
// every later entry look like a failure too.
std::vector<std::string> images_in(const fs::path& dir)
{
    std::vector<std::string> out;
    std::error_code ec;
    fs::directory_iterator it(dir, ec), end;
    for (; !ec && it != end; it.increment(ec)) {
        std::error_code ee;
        if (it->is_regular_file(ee) && !ee && is_image(it->path()))
            out.push_back(it->path().string());
    }
    std::sort(out.begin(), out.end());
    return out;
}

} // namespace

std::string library_root()
{
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg && *xdg)
        return (fs::path(xdg) / "dither" / "samples").string();
    if (const char* home = std::getenv("HOME"); home && *home)
        return (fs::path(home) / ".config" / "dither" / "samples").string();
    return {};
}

std::string crops_root()
{
    const std::string root = library_root();
    return root.empty() ? root : (fs::path(root).parent_path() / "crops").string();
}

std::string crop_for(const std::string& spec)
{
    const std::string root = crops_root();
    if (root.empty() || spec.empty() || is_path(spec)
        || spec.find('/') != std::string::npos)
        return {};
    std::error_code ec;
    const fs::path p = fs::path(root) / (spec + ".png");
    return fs::is_regular_file(p, ec) ? p.string() : std::string();
}

bool is_path(const std::string& spec)
{
    std::error_code ec;
    return !spec.empty() && fs::exists(spec, ec);
}

std::vector<std::string> resolve(const std::string& spec, bool crop_first)
{
    if (spec.empty()) return {};
    std::error_code ec;

    auto expand = [&](const fs::path& p) -> std::vector<std::string> {
        if (fs::is_directory(p, ec)) return images_in(p);
        if (fs::is_regular_file(p, ec)) return {p.string()};
        return {};
    };

    // A path the user typed wins; see the header for why.
    if (auto direct = expand(spec); !direct.empty()) return direct;
    // ...but an empty directory that was named literally is still that
    // directory, and must not fall through to a library lookup that might
    // happen to share its name.
    if (fs::is_directory(spec, ec)) return {};

    if (crop_first)
        if (std::string crop = crop_for(spec); !crop.empty()) return {crop};

    const std::string root = library_root();
    if (root.empty()) return {};
    return expand(fs::path(root) / spec);
}

std::vector<std::string> library_names()
{
    std::vector<std::string> out;
    const std::string root = library_root();
    if (root.empty()) return out;
    std::error_code ec;
    fs::directory_iterator it(root, ec), end;
    for (; !ec && it != end; it.increment(ec)) {
        std::error_code ee;
        if (it->is_directory(ee) && !ee) {
            if (!images_in(it->path()).empty())
                out.push_back(it->path().filename().string());
        } else if (it->is_regular_file(ee) && !ee && is_image(it->path()))
            out.push_back(it->path().filename().string());
    }
    std::sort(out.begin(), out.end());
    return out;
}

// A scan at whatever depth it was stored in, on the one scale everything below
// measures in: 0..255 as float, so a "code" means an 8-bit code whatever the
// file is. Without that common scale `direct.quantise`, which is documented in
// input codes, would mean 1/257 of one on a 16-bit scan, and mixed-depth
// directories would pool values 257x apart.
//
// Float, not 8-bit, because the point is to *keep* the sub-code detail a 16-bit
// scan has. 8-bit input comes through as the same numbers in a wider type, so
// everything downstream -- all of which starts with convertTo(CV_32F) -- is
// unchanged to the bit.
static cv::Mat to_code_scale(const cv::Mat& m, double& step)
{
    double scale = 1.0;
    switch (m.depth()) {
        case CV_8U:  case CV_8S:  scale = 1.0;              break;
        case CV_16U: case CV_16S: scale = 255.0 / 65535.0;  break;
        case CV_32F: case CV_64F: scale = 255.0;            break;   // taken as [0,1]
        default:                  scale = 1.0;              break;
    }
    // One source code in these units, which is also the conversion factor: the
    // scale maps the file's full range onto 0..255, so it maps one of its codes
    // onto exactly that much. A float source has no codes; 16-bit is the finest
    // thing that can be stored, and claiming finer would make the tie guard
    // meaningless.
    step = (m.depth() == CV_32F || m.depth() == CV_64F) ? 255.0 / 65535.0 : scale;
    cv::Mat f;
    m.convertTo(f, CV_32F, scale);
    return f;
}

Set Set::read(const std::vector<std::string>& paths, std::ostream* log)
{
    Set out;
    for (const std::string& p : paths) {
        // ANYDEPTH, like dither.cpp's own loader and maketexture's: without it
        // imread silently returns 8 bits, and a low-contrast 16-bit grain scan
        // arrives as a constant image with nothing left to imitate.
        cv::Mat m = cv::imread(p, cv::IMREAD_GRAYSCALE | cv::IMREAD_ANYDEPTH);
        if (m.empty()) {
            if (log) *log << "  skipping unreadable sample " << p << "\n";
            continue;
        }
        double step = 1.0;
        out.scans_.push_back(to_code_scale(m, step));
        out.steps_.push_back(step);
    }
    if (log && out.scans_.size() > 1) {
        long long px = out.pixels();
        *log << "  " << out.scans_.size() << " scans, "
             << cv::format("%.1f", (double)px / 1e6) << " Mpx, each used whole ("
             << px << " px)\n";
    }
    return out;
}

Set Set::of(const cv::Mat& one)
{
    Set out;
    if (!one.empty()) out.scans_.push_back(one);
    return out;
}

Set Set::of(std::vector<cv::Mat> scans)
{
    Set out;
    out.scans_ = std::move(scans);
    return out;
}

std::size_t Set::primary_index() const
{
    CV_Assert(!scans_.empty());
    std::size_t best = 0;
    for (std::size_t i = 1; i < scans_.size(); i++)
        if ((long long)scans_[i].cols * scans_[i].rows >
            (long long)scans_[best].cols * scans_[best].rows)
            best = i;
    return best;
}

const cv::Mat& Set::primary() const { return scans_[primary_index()]; }

double Set::code_step() const
{
    if (steps_.empty()) return 1.0;
    return steps_[primary_index()];
}

Set Set::prepared(double highpass, double scale) const
{
    Set out;
    out.scans_.reserve(scans_.size());

    // One offset per scan, measured over the whole scan because the whole scan
    // is what gets used. Per scan and never smaller: within-scan density
    // variation is real grain behaviour -- amplitude tracks density -- and is
    // what a model should be learning rather than having flattened out of it.
    // A shift and not a rescale, for the same reason.
    double common = 0.0;
    for (const cv::Mat& m : scans_) common += cv::mean(m)[0];
    common /= (double)std::max<std::size_t>(1, scans_.size());

    for (const cv::Mat& m : scans_) {
        cv::Mat f;
        m.convertTo(f, CV_32F);
        if (scans_.size() > 1) f += common - cv::mean(f)[0];
        out.scans_.push_back(maskutil::prepare_texture_sample(f, highpass, scale));
    }
    return out;
}

std::size_t Set::Ordering::scan_of(std::size_t flat) const
{
    return (std::size_t)(std::upper_bound(base.begin(), base.end(), flat) - base.begin()) - 1;
}

Set::Ordering Set::ordering() const
{
    const long long n = pixels();
    std::vector<float> all;
    all.reserve((std::size_t)n);

    Ordering o;
    o.base.assign(scans_.size() + 1, 0);
    for (std::size_t i = 0; i < scans_.size(); i++) {
        const cv::Mat& f = scans_[i];
        CV_Assert(f.type() == CV_32F && f.isContinuous());
        const std::size_t len = (std::size_t)f.rows * f.cols;
        all.insert(all.end(), f.ptr<float>(), f.ptr<float>() + len);
        o.base[i + 1] = o.base[i] + len;
    }

    // Stable, so equal values keep their position order and a run is not broken
    // up by luck -- the same contract maskutil::rank_order states, which for one
    // scan this reduces to exactly.
    o.order = maskutil::stable_order(all.data(), all.size());
    return o;
}

Set Set::ranked_unit() const
{
    const Ordering o = ordering();
    const int n = (int)o.order.size();

    Set out;
    out.scans_.reserve(scans_.size());
    for (const cv::Mat& f : scans_) out.scans_.emplace_back(f.size(), CV_32F);

    // `(r + 0.5f) / n` in float, which is maskutil::rank_uniform_unit's exact
    // arithmetic -- for one scan this must come out bit for bit the same.
    // Every rank writes its own pixel, so the ranks can be split between threads.
    cv::parallel_for_(cv::Range(0, n), [&](const cv::Range& range) {
        for (int r = range.start; r < range.end; r++) {
            const std::size_t g = (std::size_t)o.order[(std::size_t)r];
            const std::size_t s = o.scan_of(g);
            out.scans_[s].ptr<float>()[g - o.base[s]] = (r + 0.5f) / n;
        }
    });
    return out;
}

long long Set::pixels() const
{
    long long n = 0;
    for (const cv::Mat& m : scans_) n += (long long)m.cols * m.rows;
    return n;
}

cv::Size Set::smallest() const
{
    if (scans_.empty()) return {};
    int w = scans_[0].cols, h = scans_[0].rows;
    for (const cv::Mat& m : scans_) { w = std::min(w, m.cols); h = std::min(h, m.rows); }
    return {w, h};
}

Set reference_for(const std::string& spec, const Set& set)
{
    if (!is_path(spec))
        if (const std::string crop = crop_for(spec); !crop.empty()) {
            Set c = Set::read({crop}, nullptr);
            if (!c.empty()) return c;
        }
    return set;
}

} // namespace samples
