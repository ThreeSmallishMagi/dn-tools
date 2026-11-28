// SPDX-License-Identifier: GPL-3.0-or-later
// maskcache.cpp — see maskcache.h.
#include "maskcache.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <filesystem>
#include <ostream>
#include <vector>
#include <opencv2/imgcodecs.hpp>
#include <unistd.h>
#include "maskparams.h"
#include "samples.h"

namespace fs = std::filesystem;

namespace maskcache {
namespace {

// Total bytes of cached masks to keep. A 1024x1024 mask is a megabyte and
// barely compresses -- it is noise, which is the point of it -- so this is
// about 250 of the largest masks anyone builds, or thousands of ordinary ones.
// A constant rather than a parameter on purpose: a parameter would join the
// registry, and everything in the registry ends up in the key below, so
// changing the cap would throw the cache away.
constexpr uintmax_t kCapBytes = 256ull * 1024 * 1024;

// FNV-1a. Not a cryptographic hash and does not need to be: this identifies
// configurations a user chose, not ones an attacker picked, and a collision
// costs a wrong mask rather than a compromise.
struct Fnv {
    uint64_t h = 1469598103934665603ull;
    void add(const void* p, size_t n)
    {
        const auto* b = static_cast<const unsigned char*>(p);
        for (size_t i = 0; i < n; i++) { h ^= b[i]; h *= 1099511628211ull; }
    }
    void add(const std::string& s) { add(s.data(), s.size()); }
};

// The executable's own identity, so that rebuilding a generator invalidates
// every mask it made. Size and modification time rather than a hash of the
// file: a hash would read tens of megabytes on every run to detect something
// the metadata already reports.
//
// /proc/self/exe is Linux, which is what this is built for. If it is not there
// the compile time stands in, which is weaker -- it only changes when this file
// is recompiled -- so it is worth knowing which one is in play. That is what
// the `build` line in every key's detail file records.
std::string build_identity()
{
    std::error_code ec;
    const fs::path exe = fs::read_symlink("/proc/self/exe", ec);
    if (!ec) {
        // Each result checked before the next call can overwrite its code. As
        // written, a failed file_size was masked by a successful
        // last_write_time and the garbage size went into the key.
        std::error_code se, te;
        const auto size = fs::file_size(exe, se);
        const auto when = fs::last_write_time(exe, te);
        if (!se && !te)
            return std::to_string(size) + ":" +
                   std::to_string(when.time_since_epoch().count());
    }
    return std::string(__DATE__) + " " + __TIME__ + " (no /proc/self/exe)";
}

// Keep a filename to characters that mean the same thing on every filesystem.
std::string tame(const std::string& s, size_t cap)
{
    std::string out;
    for (char c : s) {
        if (out.size() >= cap) break;
        out += (std::isalnum((unsigned char)c) || c == '.' || c == '-') ? c : '_';
    }
    return out;
}

// Length first, then content: without the frame, a list of files hashes as one
// concatenated stream, so [A, B+C] and [A+B, C] are the same key although they
// decode to different samples.
bool hash_file(const std::string& path, Fnv& f)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::error_code ec;
    const auto size = (uint64_t)fs::file_size(path, ec);
    if (ec) return false;
    f.add(&size, sizeof size);
    char buf[64 * 1024];
    while (in.read(buf, sizeof buf) || in.gcount())
        f.add(buf, (size_t)in.gcount());
    // eof() distinguishes "read it all" from "stopped early". Returning true on
    // any stop meant a truncated read produced a confident hash of a prefix --
    // a different sample keyed as though it were this one, which is the wrong
    // hit the whole key exists to prevent.
    return in.eof();
}

std::string hex(uint64_t v)
{
    char buf[17];
    std::snprintf(buf, sizeof buf, "%016llx", (unsigned long long)v);
    return buf;
}

} // namespace

std::string dir_for(const std::string& configPath)
{
    if (configPath.empty()) return {};
    return (fs::path(configPath).parent_path() / "masks").string();
}

Key key_for(params::Params& P, const std::string& samplePath, bool crop_first)
{
    Key k;
    std::string detail;
    Fnv f;

    // Every parameter this run actually uses, at the value --explain shows.
    // Walking the registry rather than naming the keys here is the whole design:
    // it means a generator added tomorrow is covered today, and that the one
    // way to get a wrong hit -- a value that decides the mask and is not in the
    // key -- is impossible for anything the registry holds.
    // What the key is built from, so that an encoding change cannot let an old
    // entry answer a new question. Bumped when the serialization below changes
    // shape; entries under an older version simply never match and age out.
    //
    // Version 2 replaced format() with serialized_of(). Under version 1 the key
    // carried six significant figures, so blue.sigma 1.8000001 and 1.8000002
    // shared one entry and the second was served the first's mask.
    // /3 began hashing every file a directory resolves to rather than one;
    // /4 frames each with its length so a grouping cannot be mistaken.
    // /5 because a sample-based mask is now the reflection of what /4 stored:
    // the executable's identity already changed with it, and this says why.
    f.add(std::string("maskcache/5"));

    for (const std::string& key : P.keys_ending("")) {
        // Intermediates are left out, and that is the same rule twice over.
        // A parameter that reaches the mask only by deciding another parameter
        // has nothing to add once that other one is here, and including it
        // causes misses that are pure waste: `image.width` used to put two
        // byte-identical masks in the cache because two pictures had different
        // sizes. `<generator>.rate` is the same shape -- it is re-measured on
        // most runs and reaches the bytes only through --budget, whose effect
        // arrives as mask.size and markov.pairs.
        if (P.intermediate(key)) continue;
        if (!P.applies(key)) continue;
        // Two encodings on purpose. `detail` is the sidecar a person reads, in
        // --explain's own vocabulary; the hash gets round-trip precision and a
        // type tag, because six significant figures is a display decision and
        // was silently merging distinct configurations into one entry.
        detail += key + " = " + P.value_of(key) + "\n";

        // Delimited rather than concatenated. Without separators the pairs
        // (ab, c) and (a, bc) are the same bytes, so two different
        // configurations could hash alike for a reason that has nothing to do
        // with the hash function. 0x1F and 0x1E cannot occur in a key, and a
        // parameter value that contained them would still be distinguished by
        // the length the separators impose.
        f.add(key);
        f.add(std::string("\x1f"));
        f.add(P.serialized_of(key));
        f.add(std::string("\x1e"));
    }

    if (!samplePath.empty()) {
        // Every file a name resolves to, in sorted order, each framed by its
        // length. The mask is a function of all of them, so hashing one would
        // be this cache's worst failure: a wrong hit rather than a miss.
        const std::vector<std::string> paths = samples::resolve(samplePath, crop_first);
        if (paths.empty()) return k;                  // let the build report it
        Fnv sf;
        const std::string n = std::to_string(paths.size());
        sf.add(n.data(), n.size());
        for (const std::string& p : paths)
            if (!hash_file(p, sf)) return k;          // likewise
        detail += "sample.content = " + hex(sf.h) + " (" + n + " file"
                + (paths.size() == 1 ? "" : "s") + ")\n";
        f.add(hex(sf.h));

        // And the film's crop when the build did not take it as the sample:
        // markov and inpaint are held to it (samples::reference_for), so an
        // edited crop changes their masks too. Taken whatever the generator, as
        // a crop the mask did not read costs a miss at worst.
        if (!crop_first)
            if (const std::string crop = samples::is_path(samplePath)
                                       ? std::string() : samples::crop_for(samplePath);
                !crop.empty()) {
                Fnv cf;
                if (!hash_file(crop, cf)) return k;
                detail += "reference.content = " + hex(cf.h) + "\n";
                f.add(hex(cf.h));
            }
    }

    const std::string build = build_identity();
    detail += "build = " + build + "\n";
    f.add(build);

    // Readable first, so `ls ~/.config/dither/masks` is informative, then the
    // digest, which is what actually decides identity. The readable part is
    // allowed to be lossy; the digest is not.
    const std::string readable =
        tame(P.value_of(mask::keys::Method), 16) + "-" +
        P.value_of(mask::keys::SizeW) + "x" + P.value_of(mask::keys::SizeH) +
        "-hp" + tame(P.value_of(mask::keys::Highpass), 8) +
        "-sc" + tame(P.value_of(mask::keys::Scale), 8) +
        "-seed" + tame(P.value_of(mask::keys::Seed), 12);

    k.name = readable + "-" + hex(f.h);
    k.detail = detail;
    k.ok = true;
    return k;
}

cv::Mat load(const std::string& dir, const Key& k, std::ostream* log)
{
    if (dir.empty() || !k.ok) return {};
    const fs::path png = fs::path(dir) / (k.name + ".png");
    std::error_code ec;
    if (!fs::exists(png, ec)) return {};
    // imread throws on some malformed files and merely returns empty on
    // others. A corrupt cache entry must cost a rebuild, never the run.
    cv::Mat mask;
    try { mask = cv::imread(png.string(), cv::IMREAD_GRAYSCALE); }
    catch (const std::exception&) { return {}; }
    if (mask.empty()) return {};

    // Touch it, so eviction is by last use rather than by age. A mask built
    // once and used daily should outlive one built daily and never used again.
    fs::last_write_time(png, fs::file_time_type::clock::now(), ec);
    if (log)
        *log << "Reusing cached " << mask.cols << "x" << mask.rows << " mask "
             << png.string() << "\n";
    return mask;
}

// One entry written, or not. Errors are the caller's to ignore -- see store.
static bool write_entry(const fs::path& dir, const Key& k, const cv::Mat& mask)
{
    std::error_code ec;
    const fs::path png = dir / (k.name + ".png");
    // .png last, because cv::imwrite picks its encoder from the extension and
    // fails on anything it does not recognise -- silently, as far as a caller
    // that only checks the return value is concerned.
    const fs::path tmp = dir / (k.name + ".tmp" + std::to_string(::getpid()) + ".png");

    // Written under a unique name and renamed, so a second dither running the
    // same command cannot read it halfway through. Rename is atomic within a
    // directory, and losing the race means the other process's identical bytes
    // win.
    if (!cv::imwrite(tmp.string(), mask)) { fs::remove(tmp, ec); return false; }

    // The sidecar before the rename, so the .png only ever appears with its
    // explanation already beside it.
    {
        std::ofstream txt(dir / (k.name + ".txt"));
        txt << k.detail;
        txt.close();
        if (!txt) { fs::remove(tmp, ec); return false; }
    }
    fs::rename(tmp, png, ec);
    if (ec) { fs::remove(tmp, ec); return false; }
    return true;
}

// Evict least-recently-used entries until the directory is under the cap.
// `just_written` is spared: on a cache whose cap is smaller than a single mask
// that would delete this run's own entry and leave the cache permanently empty
// rather than permanently full.
static void trim_cache(const fs::path& dir, const fs::path& just_written)
{
    std::error_code ec;
    std::vector<std::pair<fs::file_time_type, fs::path>> entries;
    uintmax_t total = 0;

    // Explicit increment, because the range-for form of directory_iterator
    // throws on a failed increment however carefully its constructor was given
    // an error_code -- and a directory changing under an optional cache is
    // ordinary, not exceptional.
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        const fs::path path = it->path();
        if (path.extension() != ".png") continue;
        // A run that died between imwrite and rename leaves one of these.
        // Skipped rather than swept: the process that owns it may still be
        // writing, and its name carries that process's id precisely so this
        // one does not have to guess. They are small and rare, and deleting a
        // live writer's file to reclaim a megabyte is a poor trade.
        if (path.stem().string().find(".tmp") != std::string::npos) continue;
        std::error_code fe;
        const auto sz = fs::file_size(path, fe);  if (fe) continue;
        const auto tm = fs::last_write_time(path, fe); if (fe) continue;
        total += sz;
        entries.emplace_back(tm, path);
    }
    if (total <= kCapBytes) return;

    std::sort(entries.begin(), entries.end());
    for (const auto& [when, path] : entries) {
        if (total <= kCapBytes) break;
        if (path == just_written) continue;
        std::error_code fe;
        const auto sz = fs::file_size(path, fe);
        if (fe) continue;
        // Only count what was actually reclaimed. Subtracting first meant a
        // failed removal still satisfied the loop, which stopped evicting while
        // the directory was over the cap -- and the .txt removal then
        // overwrote the .png removal's error code, so nothing could tell.
        if (!fs::remove(path, fe) || fe) continue;
        std::error_code te;
        fs::remove(path.parent_path() / (path.stem().string() + ".txt"), te);
        total -= sz;
    }
}

void store(const std::string& dir, const Key& k, const cv::Mat& mask)
{
    if (dir.empty() || !k.ok || mask.empty()) return;
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (ec) return;

    // Nothing here may stop a run that has already produced its mask. The cache
    // is an optimisation, so a full disk, a read-only directory or an OpenCV
    // encoder that throws costs the reuse and nothing else -- cv::imwrite and
    // cv::imread signal some failures by returning and others by throwing, and
    // a caller that checks only the return value is handling half of them.
    try {
        const fs::path d(dir);
        if (write_entry(d, k, mask)) trim_cache(d, d / (k.name + ".png"));
    } catch (const std::exception&) {
        // Deliberately silent: a mask that could not be cached is not news,
        // and the run it belongs to has already succeeded.
    }
}

} // namespace maskcache
