// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <iosfwd>
#include <string>
#include <vector>
#include <opencv2/core.hpp>
#include <iosfwd>
#include "defaults.h"
#include "samples.h"

// The mask generators, as data rather than as branches.
//
// Adding a generator used to mean editing dither.cpp in seven places — a work
// formula, the mask.method default's candidate list, a name whitelist, the
// "does it need a sample" predicate, a build branch, a pair-count special case
// and a registration call — even though DEFAULTS.md said a generator needed no
// change there. That was true of the parameter *rules*, which each generator
// already registers for itself, and false of the generator. Four of the seven
// were string comparisons on the method name in a file with no other reason to
// know the names.
//
// Now each generator is one row in the table below. Adding one means writing
// its module and adding a row; nothing in dither.cpp names a generator.
// See doc/ARCHITECTURE.md.

// What a build needs that the parameter registry does not hold.
struct MaskRequest {
    cv::Size       size;
    // The scans behind --mask-sample, empty exactly when the generator needs
    // none. A set rather than one image because the three that fit a model pool
    // additive statistics over it, so none of them needs the scans flattened
    // into one rectangle -- see samples.h. `direct` takes `primary()`.
    const samples::Set& samples;
    // What the synthesis is held to: samples::reference_for -- the film's crop
    // where it has one, else `samples` again. Empty with `samples`.
    const samples::Set& reference;
    uint64_t       seed;
    std::ostream*  log;
    // Only blue's announcement prints these, and neither is a parameter. They
    // ride along rather than being resolved because the registry has no key for
    // either; if blue's log line ever loses them, they should go too.
    bool           srgb;
    int            levels;
};


struct Generator {
    const char* name;

    // Whether it imitates a --mask-sample. Drives the "needs a sample" and
    // "cannot use one" errors, the shared announcement, and which generators
    // reconcile() may assume — so the sample rule is stated once, here.
    bool needs_sample;

    // Offered by reconcile() as a value mask.method could be moved to, when
    // some other supplied option implies a mask is being built from a sample.
    // Every sample generator is offered; blue is not, because it configures
    // nothing the others do not and offering it would make the search ambiguous
    // for options all of them share.
    //
    // inpaint was not offered until this became a column, which is what P1 cost
    // in practice: `--set inpaint.db=50000 --mask-sample x.png` refused the run
    // where the same shape for markov succeeded, because the candidate list was
    // a literal written before inpaint existed.
    bool offer_in_reconcile;

    // Can this generator use several scans of one film at once?
    //
    // True for the three that fit a model: more scans are more data, and each
    // pools additive statistics over the whole `samples::Set`. False for
    // `direct`, whose tile *is* the sample -- a tile spanning two scans would
    // cross a join between unrelated frames, which is the one defect it works
    // hardest to avoid, so it takes `Set::primary()` instead.
    bool combines;

    // Does a film name mean the film's crop, `crops/<film>.png`, rather than
    // its scans? True for `direct`, whose tile is cut out of the sample, and
    // for `dft`, which imposes the sample's whole 2D spectrum: a scan's door
    // frames and horizons are oriented edges, each a line of power through the
    // origin, and dft reproduces them as directional streaks that are no part
    // of the grain. A crop is a window chosen to hold grain and nothing else.
    // Measured against a held-out window of each film, the angular profile of
    // a dft mask from the crop is closer on five films of six, Kodak-4X by 8x.
    // Without a crop the film's scans are used, as for the others; a typed path
    // is always used as given.
    bool prefers_crop;

    // --mask-highpass when the user gives none, in pixels. 32 for dft and
    // direct, which take a crop of one tone. 0 for markov and inpaint, which
    // model the sample's values as they are: a high-pass there removes tone
    // and whatever depends on it before the model sees it, and was found to
    // hide defects rather than only the picture.
    double default_highpass;


    void (*register_params)(params::Params&);

    // Work units for a w*h mask, divided by <name>.rate to predict seconds.
    // `pairs` is the pair count to assume: the mask-size allocation passes a
    // fixed reference because the size is chosen before the pair count is, and
    // recording a finished build passes 0 to mean "resolve it yourself".
    // Generators that have no pair count ignore it.
    double (*work)(const params::Resolver& r, int w, int h, int pairs);

    // Build the mask. Empty means it has already said why on `log`.
    cv::Mat (*build)(params::Params& P, const MaskRequest& q);


    // Anything its parameters need checked that a numeric range cannot say --
    // a string that must be one of a set, say. Null when there is nothing.
    // Called only for the generator actually selected. False means it has said
    // why. A user value that is wrong for a generator this run is not using is
    // already refused by Params::conflicts().
    bool (*validate)(params::Params& P);

    // The largest mask this generator can make from a sample `sampleSide` across,
    // on the axis being sized. Null when the sample's size is no limit, which is
    // the usual case: dft, markov and inpaint fit a model and can then synthesise
    // a field of any size at all.
    //
    // `direct` is the exception and the reason this column exists. Its tile *is*
    // the sample, so it cannot make a mask bigger than one, and the derived size
    // has to know that before the build does -- otherwise a perfectly ordinary
    // --supersample run asks for a tile the sample cannot supply and stops.
    long long (*sample_limit)(const params::Resolver& r, long long sampleSide);

    // Measure one of this generator's parameters for the sample at hand rather
    // than deriving it, for --mask-calibrate. Null when it has nothing to
    // measure. False only on an error that should stop the run.
    // Takes the sample's path rather than the image: whether a sample even has
    // to be read is the generator's business -- markov's first act is to check
    // whether a command-line value has already outranked the measurement, and
    // it says so without reading anything.
    //
    // `measure` false asks only for an answer already in the config -- a
    // calibration made earlier, typically by install_samples -- and applies it
    // if there is one, saying nothing if there is not. True also searches when
    // there is none, which is what --mask-calibrate asks for.
    //
    // `read` is the sample as the build has already read it, when it has, so
    // the lookup a plain run makes does not decode every scan a second time --
    // on a mask-cache hit from Kodak-TriX's scans that doubled the run.
    bool (*calibrate)(params::Params& P, const std::string& sample_path,
                      uint64_t seed, const std::string& cfg, std::ostream& log,
                      bool measure, const samples::Set* read);
};

// markov's parameter block, resolved. Shared because --mask-calibrate needs the
// same block the build will use; anything else is a calibration of one model
// applied to another.
struct MarkovParams;
MarkovParams markov_params_from(params::Params& P);

// The same for inpaint, which calibrates its sweep count.
struct InpaintParams;
InpaintParams inpaint_params_from(params::Params& P);

// Every generator, in the order they are offered to the user.
const std::vector<Generator>& generators();

// "a, b or c" over the generators matching `pick`, for a message. Generated
// rather than written out, because a hand-written list is what let
// "name dft or markov to imitate it" survive inpaint being added.
std::string generator_list(bool (*pick)(const Generator&));
inline bool any_gen(const Generator&)      { return true; }
inline bool imitating_gen(const Generator& g) { return g.needs_sample; }

// The values reconcile() may move mask.method to, built from the table.
std::vector<params::Value> reconcile_candidates();

// By name, or nullptr. Callers that have already validated the name may assume
// non-null; parseOptions is what validates it, against this same table.
const Generator* find_generator(const std::string& name);

// Whether a film name given to `method` resolves to the film's crop. The build
// and the cache key both ask, so they cannot disagree about the files.
inline bool uses_crop(const std::string& method)
{
    const Generator* g = find_generator(method);
    return g && g->needs_sample && g->prefers_crop;
}

// The two defaults mask.method picks between, which are a policy about which
// generator to assume rather than a property of any one of them.
inline constexpr auto kDefaultGenerator       = "blue";   // no sample given
inline constexpr auto kDefaultSampleGenerator = "dft";    // a sample was given

// The film assumed when a generator that imitates a sample is named without
// one. Kodak-4X because the derived defaults were fitted on it — markov.temp's
// rule and dft.smooth_cells' area term both — so it is the one sample those
// rules are known to be right for.
inline constexpr auto kDefaultFilm = "Kodak-4X";
