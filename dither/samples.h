// SPDX-License-Identifier: GPL-3.0-or-later
// samples.h — what `--mask-sample` names, and the scans behind it.
//
// A film is not one scan. The grain of a stock varies with exposure and
// development, and a model fitted to a single frame learns that frame's
// density as much as the emulsion's character. So `--mask-sample` may name a
// *directory* of scans of the same film, and every one of them becomes data.
//
// The library lives at `$XDG_CONFIG_HOME/dither/samples/<Manufacturer-FilmName>/`,
// one directory per stock, and a bare name is looked up there. The bash
// completion in `completion/dither` offers exactly what `library_names()`
// returns, so what you can type and what exists cannot drift.
#pragma once

#include <cstddef>
#include <string>
#include <vector>
#include <iosfwd>
#include <opencv2/core.hpp>

namespace samples {

// `$XDG_CONFIG_HOME/dither/samples`, or `~/.config/dither/samples`. Empty if
// neither variable is set, which is not an error — it only means there is no
// library and a path is the only way to name a sample.
std::string library_root();

// `$XDG_CONFIG_HOME/dither/crops`, beside the library: one square image per
// film, `<film>.png`, for the generator that takes one tile rather than
// pooling every scan. A tile *is* its sample, so it has to come from a window
// chosen to hold grain and nothing else, and a whole scan is not that.
// `install_samples` cuts the shipped films' crops from the windows
// `examples/films.tsv` records; a user's own film gets one by saving a crop
// here under the film's name. Empty when there is no config directory.
std::string crops_root();

// The crop `crops_root()` holds for film `spec`, or empty. Only a bare film
// name can have one: a path the user typed is used as given.
std::string crop_for(const std::string& spec);

// Whether `spec` names something on disk, which is what makes it a path rather
// than a library name.
bool is_path(const std::string& spec);

// The files `spec` names, sorted so the result does not depend on the order a
// directory happens to enumerate in — a cache key is built from this list, so
// the build and the key must both come through here with the same `crop_first`.
//
// **A literal path wins over a library name**, and deliberately: a path the
// user typed is unambiguous, and letting the library shadow `./Kodak-TriX`
// would mean the same command did different things on different machines. So:
//
//   an existing file        -> just that file
//   an existing directory   -> every image in it
//   a film with a crop      -> that crop, when `crop_first`
//   anything else           -> <library_root>/<spec>, by the first two rules
//
// Empty when nothing matched, or when a directory matched and held no image;
// the caller reports which, since it knows what was asked for.
std::vector<std::string> resolve(const std::string& spec, bool crop_first = false);

// Every film the library offers, for the completion script and for an error
// message that can say what *is* available.
std::vector<std::string> library_names();

// The scans behind one `--mask-sample`, each kept whole.
//
// **Why this is a set and not one Mat.** Flattening the scans into a single
// image forces them to share a width, since a Mat is rectangular. Cropping each
// to the narrowest threw away 52% of Kodak-4X and 80% of Kodak-2484, where one
// portrait frame pulled the common width down; cutting them into bands of that
// width recovered the content but added a join per band, and no arrangement of
// one rectangle avoids both. The generators that take several scans -- markov
// and inpaint, and dft for a film with no crop -- all pool *additive* statistics — a spectrum, a table of pair counts, a
// neighbourhood database — so none of them ever needed one rectangle. Handed
// the scans as they are, they keep every pixel and no join exists at all, not
// even the scan-to-scan ones that predate the banding.
//
// So consumers iterate. What lives here is what more than one of them would
// otherwise repeat; what each does with a scan stays with that generator.
class Set {
public:
    // Read every path, skipping unreadable ones with a note on `log`.
    static Set read(const std::vector<std::string>& paths, std::ostream* log);

    // One image, for a caller that already has it: maketexture works on a patch
    // the user cut out by hand, not on a film directory.
    static Set of(const cv::Mat& one);

    // Scans a consumer has already made, which is how a generator that
    // transforms every scan its own way hands the result back -- inpaint's
    // coarse levels, whose block average is a choice it documents and must
    // keep making for itself.
    static Set of(std::vector<cv::Mat> scans);

    bool        empty() const { return scans_.empty(); }
    std::size_t size()  const { return scans_.size(); }
    const cv::Mat& operator[](std::size_t i) const { return scans_[i]; }
    std::vector<cv::Mat>::const_iterator begin() const { return scans_.begin(); }
    std::vector<cv::Mat>::const_iterator end()   const { return scans_.end(); }

    // The one scan a generator that cannot combine must use: largest by area,
    // ties by the order `resolve` returned, which is sorted by path. `direct`
    // is the case — its tile *is* the sample, so a tile spanning two scans
    // would cross a join between unrelated frames.
    const cv::Mat& primary() const;

    // Every scan high-passed and resampled, each still whole: the preparation
    // markov and inpaint share, via maskutil::prepare_texture_sample.
    //
    // **The common-mean shift happens here**, and the reason is not the one it
    // used to have. It existed to stop a join carrying a tone step; there are
    // no joins now. What makes it load-bearing is that
    // `prepare_texture_sample` only removes DC when the high-pass is on, so at
    // `--mask-highpass 0` the scans keep their exposures — and then markov's
    // pooled rank sorts one whole scan above another, making its bins mean
    // "which scan" rather than "which grain level", while inpaint's
    // neighbourhoods from different scans can never match each other. Inert
    // whenever the high-pass is on, which is the default.
    Set prepared(double highpass, double scale) const;

    // Every scan replaced by its rank on [0, 1), ranked over all the scans
    // **together**. One scale serves all of them, so a value says where a pixel
    // sits among every pixel of the film rather than among its own frame's --
    // which keeps the contrast differences between exposures, real grain
    // behaviour that per-scan ranking would normalise away.
    Set ranked_unit() const;

    // The pooled ordering the above is built from, for a consumer that wants
    // ranks on some other scale: markov quantises to two different bin counts.
    // `order` indexes a flat concatenation of the scans in ascending value
    // order; `base[i]` is where scan i starts in it.
    struct Ordering {
        std::vector<int>    order;
        std::vector<std::size_t> base;
        std::size_t scan_of(std::size_t flat) const;   // which scan a flat index is in
    };
    Ordering ordering() const;

    // One code of the file this scan came from, on the 0..255 scale the scans
    // are kept in: 1.0 for an 8-bit file, 255/65535 for a 16-bit one. What
    // `direct.quantise` is measured in -- it exists to throw away detail finer
    // than the sample could record, and that is a property of the file rather
    // than of the scale we chose to hold it on.
    double code_step() const;     // of primary(), the scan `direct` uses

    long long pixels()   const;   // summed area
    cv::Size  smallest() const;   // least width and least height, which need
                                  // not come from the same scan: both are
                                  // clamps, and the smaller always governs

private:
    std::size_t primary_index() const;

    std::vector<cv::Mat> scans_;
    // One per scan, parallel to scans_. Empty when the scans did not come from
    // files -- Set::of hands over pixels whose provenance it does not state --
    // and then code_step() answers for an 8-bit source, which is what the
    // 0..255 scale looks like.
    std::vector<double> steps_;
};

// What a synthesis is held to and scored against: the film's crop when `spec`
// is a film name that has one, else `set` itself. markov and inpaint learn from
// every scan, photographs and all; the crop is the window chosen to hold only
// grain, so it is what says how much structure there should be at each scale
// (see maskutil::impose_band_power) and what calibration compares with. Held
// in the scans, the photographs' blobs set the scale, and the dither's density
// wandered with them by up to 15/1000.
Set reference_for(const std::string& spec, const Set& set);

} // namespace samples
