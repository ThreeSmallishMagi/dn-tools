// SPDX-License-Identifier: GPL-3.0-or-later
// The command line: parsing, the usage text, and the reporting that goes with
// resolving defaults. Nothing here dithers or builds a mask.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <climits>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <opencv2/opencv.hpp>
#include "cli.h"
#include "engine.h"
#include "generators.h"
#include "mask.h"
#include "maskparams.h"
#include "samples.h"
#include "srgb.h"

// cv::imwrite reports an unwritable path by returning false and an unknown
// extension by throwing; neither should reach the user as a core dump.
bool write_image(const std::string& path, const cv::Mat& img)
{
    std::string why;
    try {
        if (cv::imwrite(path, img)) return true;
    } catch (const cv::Exception& e) { why = e.what(); }
    // The friendly line first, since an unwritable path or an extension OpenCV
    // has no encoder for is the usual cause and needs no stack of its own. What
    // OpenCV said follows, because guessing which of those it was from the path
    // alone is the slow way to find out.
    fprintf(stderr, "Could not write %s\n", path.c_str());
    if (!why.empty()) fprintf(stderr, "  %s\n", why.c_str());
    return false;
}


// A pair of whole numbers around a delimiter, with the whole string consumed.
//
// sscanf("%dx%d") stops at the first character it cannot use and still reports
// success for the prefix it managed, so `--mask-size 32x32garbage` was accepted
// as 32x32 and `--debug-pixel 1,2garbage` as (1,2). A trailing typo is a typo,
// and params::parse_integer -- which the registry already uses -- refuses one.
static bool parse_pair(const std::string& v, char delim, int lo, int& a, int& b)
{
    const auto at = v.find(delim);
    if (at == std::string::npos || at == 0 || at + 1 == v.size()) return false;
    const auto x = params::parse_integer(v.substr(0, at));
    const auto y = params::parse_integer(v.substr(at + 1));
    if (!x || !y) return false;
    if (*x < lo || *y < lo || *x > INT_MAX || *y > INT_MAX) return false;
    a = (int)*x; b = (int)*y;
    return true;
}

// "WxH", both positive.
static bool parse_size(const std::string& v, int& w, int& h)
{
    return parse_pair(v, 'x', 1, w, h);
}

// "ROW,COL", both non-negative.
static bool parse_pixel(const std::string& v, int& row, int& col)
{
    return parse_pair(v, ',', 0, row, col);
}

// "30", "30s" or "2m" as seconds. The unit is the friendly part of the idea.
static bool parse_budget_seconds(std::string v, double& out)
{
    double mult = 1.0;
    if (!v.empty() && (v.back() == 's' || v.back() == 'm')) {
        mult = v.back() == 'm' ? 60.0 : 1.0;
        v.pop_back();
    }
    const std::optional<double> d = params::parse_double(v);
    if (!d) return false;
    out = *d * mult;
    return true;
}

// What reconcile() moved, and which of the user's options that rescued.
static void report_reconciliation(const params::Reconciliation& rec)
{
    if (rec.empty())
        return;
    std::cerr << "Note: assuming ";
    const char *sep = "";
    for (const auto &[k, v] : rec.changed) {
        std::cerr << sep << k << "=" << v;
        sep = ", ";
    }
    bool first = true;
    for (const auto &r : rec.rescued) {
        std::cerr << (first ? ", so that " : ", ") << r;
        first = false;
    }
    std::cerr << (rec.rescued.size() == 1 ? " applies\n" : " apply\n");
}

// Params is the authority; Options is a read-only cache of it, filled once the
// whole command line (including every --set) has been applied. Writing these
// anywhere else would reintroduce the two-sources-of-truth bug this replaced:
// --set mask.scale=2 used to move the mask size and leave the generator on 1.
static void fill_options_from_params(Options& opt, params::Params& P)
{
    opt.threshold      = P.str(mask::keys::Threshold);
    opt.maskMethod     = P.str(mask::keys::Method);
    opt.maskIn         = P.str(mask::keys::In);
    opt.maskOut        = P.str(mask::keys::Out);
    opt.maskSample     = P.str(mask::keys::Sample);
    opt.mask_scale     = P.num(mask::keys::Scale);
    opt.mask_highpass  = P.num(mask::keys::Highpass);
    opt.supersample    = (int)P.integer(mask::keys::Supersample);
    opt.outSize        = (int)P.integer(mask::keys::OutSize);
    opt.calibrate      = P.integer(mask::keys::Calibrate) != 0;
    opt.seed           = (uint64_t)P.integer(mask::keys::Seed);
}

// A parameter as the user would have typed it. The conflict is about their
// command line, so it names their flags: `mask.in rules out mask.scale` makes
// the reader translate back. The two halves of --mask-size are one flag, and a
// parameter with no flag of its own is reachable only through --set. The
// options parsed by hand, --threshold and --seed among them, have flags the
// registry does not know about.
static std::string as_typed(const params::Params& P, const std::string& key)
{
    if (key == mask::keys::SizeW || key == mask::keys::SizeH) return "--mask-size";
    const std::string flag = params::Params::flag_for(key);
    const bool own = P.has_option(key) && P.key_for_flag(flag) == key;
    return own || option_takes_value(P, flag) ? flag : "--set " + key;
}

// Whatever reconcile() could not rescue is blocked by something the user
// pinned, so there is no default left to move and no honest way to satisfy
// both. Refusing beats dropping one silently — which is what a note next to a
// zero exit status amounts to. True when there were none.
static bool report_conflicts(params::Params& P)
{
    bool clean = true;
    std::vector<std::string> said;
    for (const auto& c : P.conflicts()) {
        clean = false;
        const std::string subject = as_typed(P, c.key);
        if (std::find(said.begin(), said.end(), subject) != said.end()) continue;
        said.push_back(subject);
        // The subject and its verb have to agree, and the subject may be one
        // option, several, or -- if nothing the user set is to blame -- the run.
        std::string blocker;
        for (size_t i = 0; i < c.blocked_by.size(); i++)
            blocker += (i ? " and " : "") + as_typed(P, c.blocked_by[i]);
        const bool plural = c.blocked_by.size() > 1;
        if (blocker.empty()) blocker = "this run";
        std::cerr << subject << " applies to " << c.scope << ", which " << blocker
                  << (plural ? " rule out" : " rules out")
                  << (c.blocked_by.empty() ? "\n" : "; drop one of them\n");
    }
    return clean;
}

// Does this option consume the argument after it?
//
// The config pre-scan in dither_main has to find --config and --no-config
// before the parser runs, and without this it was guessing: it started at
// argv[2], so `dither --no-config ...` was ignored outright, and it matched any
// argv equal to the string, so `--out --no-config` disabled configuration while
// the parser took --no-config as a filename. Two passes over one command line
// disagreeing about what a token is.
//
// The list below is only the flags that are *not* parameters -- the pipeline's
// own, which have hand-written arms because several of them do more than write a
// value. Every namespaced option answers from the registry, so a parameter
// added tomorrow is covered without touching this.
bool option_takes_value(const params::Params& P, const std::string& flag)
{
    static const char* takes[] = {
        "--threshold", "--spatial", "--out", "--seed", "--levels",
        "--level-spacing", "--frames", "--supersample", "--out-size", "--mask-out",
        "--mask-size", "--config", "--budget", "--set", "--debug-pixel",
    };
    for (const char* t : takes)
        if (flag == t) return true;
    const std::string key = P.key_for_flag(flag);
    if (!key.empty() && P.has_option(key)) return !P.is_switch(key);
    return false;   // --help, --linear, --save-frames, --explain,
}                   // --no-config, and anything unknown

// Where the option list starts: argv[1] unless it is the input image. The
// pre-scan and the parser must agree on this, which they did not.
int first_option_index(int argc, char** argv)
{
    return (argc > 1 && argv[1][0] != '-') ? 2 : 1;
}

bool parseOptions(int argc, char** argv, Options& opt, params::Params& P)
{
    // argv[1] is the input image unless it looks like an option, which is what
    // lets `dither --explain` be asked without one.
    int index = first_option_index(argc, argv);
    if (index == 2) opt.inPath = argv[1];
    bool spatial_set = false, bad = false;

    // Every parameter write goes through here so a range violation is reported
    // once, the same way, whether it arrived as an option or through --set.
    auto set_param = [&](std::string_view key, params::Value v) {
        try { P.set(key, std::move(v), params::Origin::User); }
        catch (const std::exception& e) { fprintf(stderr, "%s\n", e.what()); bad = true; }
    };

    // A parameter written from text, typed by what the text looks like: an
    // integer stays an integer so that integer parameters are not silently
    // widened. Shared by --set and by every generated `--namespace-key`, so the
    // two spellings cannot disagree about what a value means -- which is the
    // whole point of generating the flags from the same registry --set reaches.
    auto set_typed = [&](const std::string& key, const std::string& val) {
        set_param(key, params::value_from_text(P, key, val));
    };

    for (; index < argc && !bad; ) {
        std::string k = argv[index++];
        auto need = [&]() -> std::string {
            if (index >= argc) {
                fprintf(stderr, "Missing value for %s\n", k.c_str());
                bad = true; return "";
            }
            return argv[index++];
        };
        // Numeric arguments are parsed and range-checked here, so a typo names
        // the offending option instead of throwing std::stod out of main. The
        // level ceiling is real: level indices travel in a CV_8U mat.
        // An integer option, read as one. These went through need_num and a C
        // cast, so `--supersample 2.9` silently became 2 -- while
        // `--set supersample=2.9` stored 2.9 and rounded to 3 at first use.
        // Two spellings of one intent, disagreeing about the answer.
        //
        // 2.0 is accepted: that is the integer written differently. 2.9 is
        // refused, because every reading of it is a guess at what was meant.
        auto need_int = [&](long long lo, long long hi) -> long long {
            const std::string v = need();
            if (!bad) {
                const std::optional<double> d = params::parse_double(v);
                if (d && *d == std::floor(*d) && *d >= (double)lo && *d <= (double)hi)
                    return (long long)*d;
                if (d && *d != std::floor(*d))
                    fprintf(stderr, "Bad value for %s: '%s' (expected a whole number)\n",
                            k.c_str(), v.c_str());
                else
                    fprintf(stderr, "Bad value for %s: '%s' (expected a whole number "
                                    "in [%lld, %lld])\n", k.c_str(), v.c_str(), lo, hi);
                bad = true;
            }
            return lo;
        };
        auto need_num = [&](double lo, double hi) -> double {
            std::string v = need();
            if (!bad) {
                const std::optional<double> d = params::parse_double(v);
                if (d && lo <= *d && *d <= hi) return *d;
                fprintf(stderr, "Bad value for %s: '%s' (expected a number in [%g, %g])\n",
                        k.c_str(), v.c_str(), lo, hi);
                bad = true;
            }
            return lo;
        };
        if      (k == "--help" || k == "-h") { usage(); exit(0); }
        // What `--mask-sample <TAB>` offers. Printed by the program rather than
        // worked out again in bash, so the completion cannot drift from the
        // resolver: one list, one implementation. Absent from --help because it
        // is for the completion script and not for a person.
        // Every option this program accepts, for the completion script. The
        // ceilings are documented as a grid of --<gen>-max-size placeholders,
        // so scraping the help text misses ten real flags.
        else if (k == "--list-options") {
            for (const std::string& key : P.options())
                printf("%s\n", params::Params::flag_for(key).c_str());
            for (const char* f : {"--threshold", "--spatial", "--out", "--seed",
                                  "--levels", "--level-spacing", "--frames",
                                  "--supersample", "--mask-size", "--mask-out",
                                  "--config",
                                  "--budget", "--set", "--debug-pixel", "--linear",
                                  "--save-frames", "--explain", "--no-config", "--help"})
                printf("%s\n", f);
            exit(0);
        }
        else if (k == "--list-samples") {
            for (const std::string& n : samples::library_names())
                printf("%s\n", n.c_str());
            exit(0);
        }
        // `blue` is what --threshold mask used to be called, back when
        // void-and-cluster was the only generator. Normalised here so exactly one
        // spelling reaches the rest of the program.
        else if (k == "--threshold")  {
            opt.threshold = need();
            if (opt.threshold == "blue") opt.threshold = "mask";
            set_param(mask::keys::Threshold, opt.threshold);
        }
        else if (k == "--spatial")    { opt.spatial     = need_num(0.0, 1.0); spatial_set = true; }
        else if (k == "--linear")      opt.useSrgb     = false;
        else if (k == "--out")         opt.outPath     = need();
        else if (k == "--seed")        set_param(mask::keys::Seed, need_int(0, 9000000000000000LL));
        else if (k == "--levels")      opt.levels      = (int)need_int(2, 256);
        else if (k == "--level-spacing") opt.levelSpacing = need();
        else if (k == "--frames")      opt.frames      = (int)need_int(1, 100000);
        else if (k == "--save-frames") opt.saveFrames  = true;
        else if (k == "--supersample") { set_param(mask::keys::Supersample, need_int(1, 16)); }
        else if (k == "--out-size")    { set_param(mask::keys::OutSize, need_int(0, 65536)); }
        // Any generator.
        else if (k == "--mask-out")    set_param(mask::keys::Out, need());
        else if (k == "--mask-size") {
            int w = 0, h = 0;
            if (!parse_size(need(), w, h)) {
                fprintf(stderr, "Bad --mask-size value, use WxH format\n"); return false;
            }
            set_param(mask::keys::SizeW, (long long)w);
            set_param(mask::keys::SizeH, (long long)h);
        }
        // Every generator that takes a sample: the sample and how it is read.
        // Range-checked here, at the boundary, then handed to the registry: a
        // user value is pinned as Origin::User and never recomputed.
        // Bounds are declared once, on the rule; need_num only has to parse.
        else if (k == "--explain")     { opt.explain = true; }
        // Both are consumed by the pre-scan in run(), which has to see them
        // before the config is loaded and so before this loop runs. Accepted
        // here only so they are not rejected as unknown.
        else if (k == "--no-config")   { }
        else if (k == "--config")      { need(); }
        else if (k == "--budget") {
            double secs = 0;
            if (!parse_budget_seconds(need(), secs)) {
                fprintf(stderr, "Bad --budget value, use seconds like 30 or 30s or 2m\n");
                return false;
            }
            set_param(mask::keys::Budget, secs);
        }
        else if (k == "--set") {
            std::string v = need();
            auto eq = v.find('=');
            if (eq == std::string::npos) {
                fprintf(stderr, "--set wants key=value, got '%s'\n", v.c_str()); return false;
            }
            const std::string key = v.substr(0, eq), val = v.substr(eq + 1);
            if (!P.known(key)) {
                fprintf(stderr, "--set: unknown parameter '%s' (see --explain)\n", key.c_str());
                return false;
            }
            set_typed(key, val);
        }
        else if (k == "--debug-pixel") {
            if (!parse_pixel(need(), opt.debug_row, opt.debug_col)) {
                fprintf(stderr, "Bad --debug-pixel value, use ROW,COL format\n"); return false;
            }
        }
        else {
            // Any parameter, by the one rule: `--namespace-key` is the key
            // `namespace.key`. Nothing here names a generator or its settings --
            // a new parameter is a new option the day it is registered, and the
            // name --explain prints is the name the parser accepts because
            // neither is written down twice.
            const std::string key = P.key_for_flag(k);
            if (!key.empty() && P.known(key) && P.has_option(key)) {
                if (P.is_switch(key)) set_param(key, (long long)1);
                else                  set_typed(key, need());
                continue;
            }
            {
                // A flag that moved says where it went. These were generator-
                // specific options wearing the shared `mask-` prefix, which is
                // reserved for what applies whatever generator is chosen; a
                // script written against the old spelling deserves better than
                // being told the name is unknown, since it plainly was known.
                static const std::pair<const char*, const char*> renamed[] = {
                    {"--mask-sigma",  "--blue-sigma"},
                    {"--mask-nbhd",   "--markov-radius"},
                    {"--mask-pairs",  "--markov-pairs"},
                    {"--mask-bins",   "--markov-bins"},
                    {"--mask-temp",   "--markov-temp"},
                    {"--mask-report", "--markov-report"},
                };
                for (const auto& [was, now] : renamed)
                    if (k == was) {
                        fprintf(stderr, "%s is now %s: --mask-* is for options that "
                                        "apply to every generator, and this one does not\n",
                                was, now);
                        return false;
                    }
                fprintf(stderr, "Unknown option: %s\n", k.c_str());
                return false;
            }
        }
    }
    if (bad) return false;

    // The threshold, normalised and checked in one place before anything reads
    // it. Two reasons it cannot stay in the --threshold arm above.
    //
    // `--set threshold=blue` reaches the registry without passing that arm, so
    // the legacy spelling survived into applicability tests and reconcile,
    // neither of which has ever tested for it. And the name was never validated
    // at all: --threshold not-a-rule ran, printing `threshold=not-a-rule` in
    // its own summary, and --explain accepted it and exited 0. Generator and
    // spacing names were both checked; this one was missed.
    {
        std::string t = P.str(mask::keys::Threshold);
        if (t == "blue") {
            t = "mask";
            P.set(mask::keys::Threshold, t, P.origin_of(mask::keys::Threshold));
        }
        if (t != "round" && t != "random" && t != "mask") {
            fprintf(stderr, "--threshold must be round, random or mask "
                            "(blue is the old spelling of mask)\n");
            return false;
        }
    }

    // Move whichever defaults have to move for the options that *were* given to
    // mean something, before anything reads them. Asking for --mask-sample is
    // asking for a mask; leaving --threshold on `round` and then reporting the
    // sample as ignored answers a question nobody asked.
    report_reconciliation(P.reconcile());

    // A generator that imitates a sample, named without one, imitates the
    // default film if the library has it. This has to come after reconcile —
    // which is what may have moved the method there — and before anything
    // reads mask.sample, since set() does not re-resolve what already has. It
    // must also never reach mask.method's own rule: a sample there turns blue
    // into dft, so a bare `--threshold mask` would change generator.
    if (P.str(mask::keys::Sample).empty() && P.str(mask::keys::Threshold) == "mask"
        && P.str(mask::keys::In).empty()) {
        const std::string method = P.str(mask::keys::Method);
        const Generator* g = find_generator(method);
        if (g && g->needs_sample
            && !samples::resolve(kDefaultFilm, uses_crop(method)).empty()) {
            P.set(mask::keys::Sample, std::string(kDefaultFilm), params::Origin::Implied);
            std::cerr << "Note: assuming mask.sample=" << kDefaultFilm
                      << ", since --mask-method " << method << " imitates a sample\n";
        }
    }

    // Naming a threshold opts out of error diffusion; an explicit --spatial wins
    // either way, whichever order the two appear in. Asked via the origin rather
    // than a flag from the parse loop so that a threshold reconcile() had to
    // imply counts as named — the user asked for it, just not by that name.
    if (!spatial_set)
        opt.spatial = P.origin_of(mask::keys::Threshold) == params::Origin::Fallback
                    ? 1.0 : 0.0;

    if (opt.levelSpacing != "linear" && opt.levelSpacing != "srgb" &&
        opt.levelSpacing != "device") {
        fprintf(stderr, "--level-spacing must be srgb, linear or device\n"); return false;
    }

    fill_options_from_params(opt, P);

    const Generator* gen = find_generator(opt.maskMethod);
    if (!gen) {
        fprintf(stderr, "--mask-method must be %s\n",
                generator_list(any_gen).c_str());
        return false;
    }

    // Whatever this generator needs checked that a range cannot express. Only
    // the selected one: a value that is wrong for a generator this run is not
    // using is already refused by Params::conflicts().
    if (gen->validate && !gen->validate(P)) return false;

    // A supplied mask is the mask; nothing below it is consulted, so check the
    // generator's own arguments only when one is actually going to run.
    const bool building = opt.threshold == "mask" && opt.maskIn.empty();
    const bool imitating = gen->needs_sample;

    // Reached only when the default film is not installed either.
    if (building && imitating && opt.maskSample.empty()) {
        fprintf(stderr, "--mask-method %s imitates a sample; add --mask-sample <img>,"
                        " run install_dither to install %s as the default, or name"
                        " --mask-method %s to build noise from scratch\n",
                opt.maskMethod.c_str(), kDefaultFilm, kDefaultGenerator);
        return false;
    }
    // Only reachable now that blue is nameable: it used to be impossible to ask
    // for void-and-cluster with a sample present, because the sample was itself
    // the switch. Refusing beats silently ignoring one of the two.
    if (building && !imitating && !opt.maskSample.empty()) {
        fprintf(stderr, "--mask-method %s builds noise from scratch and cannot use"
                        " --mask-sample; name %s to imitate it\n",
                opt.maskMethod.c_str(), generator_list(imitating_gen).c_str());
        return false;
    }

    return report_conflicts(P);
}

// Shown when there is nothing to do: enough to get started, short enough to
// read. --help carries the full reference.
void usage_brief() {
    fputs(R"(usage: dither <image> [options]

Dithers in linear light: a threshold rule (--threshold) plus error diffusion
(--spatial). The defaults need no options at all.

  dither photo.jpg --out out.png                  round-to-nearest, full FS
  dither photo.jpg --threshold mask               blue-noise mask instead
  dither photo.jpg --mask-sample Kodak-TriX --supersample 8
                                                  grain from a film in the library
  dither photo.jpg --mask-sample grain.png --supersample 8
                                                  imitate your own scanned grain

  --threshold round|random|mask    how each pixel picks its level
  --levels <n>                     output tones, 2-256
  --spatial <frac>                 error diffusion, 1 full to 0 none
  --supersample <n>                dither at n x resolution, average back down
  --out-size <side>                fit the output in a side x side square
  --out <path>                     output file

  --explain                        show every parameter and where it came from
  --help                           the full option reference
)", stdout);
}

// The per-generator option lists, from the registry.
//
// These used to be written by hand beside the generators they describe, and
// went stale the way a second copy of anything does: a flag that moved had to
// be corrected in two places, and one that was never added to the text simply
// did not exist as far as a reader was concerned. Every fact below -- the name,
// whether it takes a value, the default and the permitted range -- is already
// declared on the rule, so writing it out again could only ever disagree.
//
// What is *not* generated is the paragraph above describing what each generator
// is for. That is the part a table cannot hold, and the reason this was deferred
// as long as it was: the answer is to generate the facts and keep the prose, not
// to choose between them.
// The two families every generator declares, tabulated once instead of five
// times.
//
// `max_size` and `max_repeats` were ten of the thirty-three generated rows, and
// both names are long enough to wrap, so they took about thirty of the ninety-
// odd generated lines — a third of the per-generator help, spent on the two
// parameters a user is least likely to reach for, since --mask-size overrides
// both and --budget replaces them outright.
//
// They earn a table rather than a cut because the interesting fact about them is
// comparative: blue may repeat sixty-four times where markov may repeat four,
// and that difference is the whole reason the two are parameters at all. Five
// separate rows state each number and hide the comparison; one grid shows it.
// The doc string is shared for the same reason — it was already identical five
// times over, being a property of the family and not of any generator.
static const char* kSharedFamilies[] = {"max_size", "max_repeats"};

static bool is_shared_family(const std::string& key)
{
    const size_t dot = key.find('.');
    if (dot == std::string::npos) return false;
    for (const char* f : kSharedFamilies)
        if (key.compare(dot + 1, std::string::npos, f) == 0) return true;
    return false;
}

static void usage_ceilings(params::Params& P)
{
    // One column per generator that has the row, sized to whichever is wider:
    // the generator's name or its longest value.
    std::vector<const Generator*> cols;
    for (const Generator& g : generators()) cols.push_back(&g);

    std::vector<size_t> w;
    for (const Generator* g : cols) {
        size_t n = std::string(g->name).size();
        for (const char* f : kSharedFamilies) {
            const std::string key = std::string(g->name) + "." + f;
            if (P.has_option(key)) n = std::max(n, P.value_of(key).size());
        }
        w.push_back(n + 2);
    }

    std::printf("\nCost ceilings, one pair per generator (--blue-max-size,\n"
                "--markov-max-repeats, and so on). Each generator declares the largest\n"
                "tile it is worth building and how often that tile may repeat before the\n"
                "repetition shows — blue noise is locally featureless so a repeat of it\n"
                "is too, where a markov tile reads as banding by the sixteenth. A derived\n"
                "--mask-size honours both; a given one overrides them, and --budget\n"
                "replaces them with a time.\n");

    std::printf("%22s", "");
    for (size_t i = 0; i < cols.size(); i++)
        std::printf("%*s", (int)w[i], cols[i]->name);
    std::printf("\n");

    for (const char* f : kSharedFamilies) {
        std::string label = "  --<gen>-" + std::string(f);
        for (char& c : label) if (c == '_') c = '-';
        std::printf("%-22s", label.c_str());
        for (size_t i = 0; i < cols.size(); i++) {
            const std::string key = std::string(cols[i]->name) + "." + f;
            std::printf("%*s", (int)w[i],
                        P.has_option(key) ? P.value_of(key).c_str() : "-");
        }
        std::printf("\n");
    }
}

static void usage_generators()
{
    params::Params P;
    for (const Generator& g : generators()) g.register_params(P);
    register_mask_params(P);

    usage_ceilings(P);

    for (const Generator& g : generators()) {
        const std::string prefix = std::string(g.name) + ".";
        std::vector<std::string> mine;
        for (const std::string& key : P.options())
            if (key.compare(0, prefix.size(), prefix) == 0
                && !is_shared_family(key)) mine.push_back(key);
        if (mine.empty()) continue;

        std::printf("\n%s options (--mask-method %s):\n", g.name, g.name);
        // Resolved with this generator selected: a default derived from the
        // method -- markov.temp follows mask.highpass, which follows the method
        // -- otherwise comes out as it would for blue.
        params::Params Pg = P;
        Pg.set(mask::keys::Method, std::string(g.name), params::Origin::User);
        for (const std::string& key : mine) {
            const size_t start_col = 22;
            const std::string flag = params::Params::flag_for(key);
            std::string lhs = "  " + flag + (P.is_switch(key) ? "" : " <v>");
            if (lhs.size() < start_col) lhs.resize(start_col, ' ');
            else                 lhs += " ";

            std::string tail = P.doc(key);
            if (!P.is_switch(key)) tail += " (default " + Pg.value_of(key) + ")";

            // Wrapped at the width the hand-written text used, so a generated
            // block sits in the same page as the authored ones above it.
            std::string line = lhs;
            size_t col = start_col;
            std::istringstream words(tail);
            bool first = true;
            for (std::string w; words >> w; ) {
                if (!first && col + 1 + w.size() > 78) {
                    line += "\n" + std::string(start_col, ' ');
                    col = start_col;
                    first = true;
                }
                if (!first) { line += ' '; col++; }
                line += w;
                col += w.size();
                first = false;
            }
            std::printf("%s\n", line.c_str());
        }
    }
    std::printf("\nEvery option above is the parameter of the same name: --blue-sigma is\n"
                "blue.sigma, which --explain prints. `--set key=value` reaches any of them\n"
                "by key, including the few with no option of their own.\n");
}

void usage() {
    fputs(R"(usage: dither <image> [options]

Treats input as sRGB by default and dithers in linear light. A dither is a
threshold rule (--threshold) plus error diffusion (--spatial); the defaults
give round-to-nearest with full Floyd-Steinberg at 2 levels.

Input may be 8- or 16-bit; output is always 8-bit. 16-bit input is what lets
--spatial and --frames render tones finer than one output level: against an
8-bit input there is nothing below a level to diffuse or to spread over frames.
For a smooth gradient on an 8-bit display try --levels 256 --spatial 1; to
expose one on a device that shows 8-bit frames in sequence, add --frames N.

Thresholds (--threshold):
  round      deterministic round to nearest level (default)
  random     stochastic threshold — fast, grainy
  mask       threshold against a noise mask; best perceptual quality. Which
             mask is --mask-method, below (`blue` is the old name for this)

  Naming any mask option selects `mask` on its own — --mask-sample grain.png is
  a request for a mask, so there is no need to also say --threshold mask. Any
  default moved this way is reported as `Note: assuming ...`. Options you name
  are never moved, so if two of them cannot both be honoured — --mask-scale
  alongside --mask-in, say, which supplies a finished mask rather than building
  one — the run stops and says which two, rather than ignoring one of them.

Options:
  --threshold <name>  threshold rule to use (default: round)
  --spatial <frac>    how much of the accumulated Floyd-Steinberg error the
                      threshold rule sees (1 = full FS, 0 = none). The residual
                      itself is always diffused in full, which is what keeps
                      the tone right at values in between.
                      Defaults to 1, or to 0 when --threshold is given
  --out <path>        output file path (default: dither.png)
  --linear            treat pixel values as raw linear light (skip the sRGB
                      ↔ linear conversion that normally wraps the dither)
  --levels <n>        number of output tones, 2-256 (default 2)
  --level-spacing <s> where those tones sit. srgb (default) puts equal steps in
                      pixel value — 3 levels are 0/128/255 — and is right when
                      the 8-bit file is the display signal. linear puts equal
                      steps in linear light: 3 levels are 0/188/255. device puts
                      equal steps in the display's own levels, for a panel with
                      exactly --levels of them (a 3-bit mono screen, say); the
                      file then only carries the codes, and srgb's rounding of
                      them to 8 bits is an error rather than the point. srgb and
                      device agree exactly whenever n-1 divides 255
  --frames <n>        dither n independent frames and average them (default 1)
  --supersample <n>   dither at n x resolution and average each n x n block back
                      down, in linear light. Gives n^2+1 tones per output pixel
                      and lets a mask's grain sit finer than the picture detail
  --out-size <side>   fit the output inside a side x side square, keeping its
                      aspect ratio. With --supersample the scaling is done from
                      the dither grid in one step rather than via the input's
                      size, so nothing is quantised to 8 bits in between. Set it
                      to the input's longer edge times --supersample and the
                      dither grid comes out whole, nothing averaged back down
  --save-frames       also write each frame as <stem>-N.png
  --seed <n>          random seed (default 42)
  --debug-pixel <r,c> print per-frame ideal/display/displayed for pixel (r, c)
  --budget <time>     spend about this long building the mask, and pick the
                      mask size and pair count to suit — 30, 30s or 2m. Left
                      unset, each generator's own fixed ceiling applies, which
                      is what every earlier version did. Timing is calibrated
                      from this machine's own builds (see --config)
  --config <path>     where to keep what this machine has worked out
                      (default ~/.config/dither/cache.conf, XDG_CONFIG_HOME
                      honoured). Holds measured build speeds, and names the
                      directory masks/ beside it where built masks are kept,
                      and the calibrations made for samples, which every run
                      on that sample uses. Safe to delete: speeds and masks are
                      rebuilt on demand, and a sample goes back to the built-in
                      rules until it is calibrated again.
                      A built mask is reused whenever every parameter, the
                      sample's contents and this binary are unchanged -- a mask
                      is a pure function of those, so a reused one is
                      byte-identical to a rebuilt one
  --no-config         neither read nor write any of that: masks are rebuilt
                      every run, which for inpaint or blue is seconds, and no
                      calibration is used
  --explain           validate the options, print every parameter as resolved
                      with where its value came from, and exit without dithering
  --set <key=value>   override any parameter by name, including ones with no
                      option of their own; --explain lists the names. Later
                      settings win, so this and a named option can be mixed
  --help, -h          show this message


=== --threshold mask: which mask, and how it is built ===

Mask generators (--mask-method). blue needs nothing else; dft, markov,
inpaint and direct each imitate a --mask-sample, Kodak-4X from the sample
library if none is named, and differ in what they keep of it:
  blue       void-and-cluster blue noise, built from scratch. The default, and
             what you want unless you are imitating a texture
  dft        match the sample's amplitude spectrum. Cheap, and spectrally near
             exact, but phase is randomised: a weave comes back as woven-looking
             noise, and clumping that varies with tone comes back uniform
  markov     fit conditional distributions over pairs of neighbours and
             Gibbs-sample them. Keeps the higher-order structure dft discards —
             notably dark grain that clumps harder than light grain — at the
             cost of seconds rather than tenths
  inpaint    fill each pixel from the sample neighbourhood that most resembles
             the one around it. Conditions on the whole neighbourhood rather
             than pooling pairs of it, which removes markov's grainy floor.
             The best of the three that synthesise, by test/patchfit, and the
             slowest. markov and inpaint match each sweep's power spectrum to
             the sample's (--markov-bands, --inpaint-bands), so the structure
             they grow stays at the grain's scale
  direct     do not imitate the sample — use it. Equalise it (rank each pixel
             within its neighbourhood), cut a tile out of it, rank that flat.
             Everything true of the grain is true of the mask, so it beats all
             three imitators and is what they are measured against. Two costs:
             it cannot invent, so the sample must be at least mask-sized (a
             derived --mask-size shrinks to fit, a pinned one is refused), and a
             crop of a photograph has to be made to tile, which it does by
             cutting the seam along the path where the tile and its own
             continuation differ least

Any generator:
  --mask-method <m>   generator to use: blue, dft, markov, inpaint or direct.
                      Defaults to blue, or to dft when --mask-sample names a
                      sample to imitate
  --mask-size <WxH>   generate a WxH tile and repeat it over the image. Left
                      unset it is derived from the image, the high-pass, the
                      scale and what the generator can afford — and for direct,
                      from what the sample can supply. --explain shows the
                      value and everything it came from
  --mask-in <path>    use this image as the mask instead of building one. A
                      mask-building option given as well is then refused rather
                      than ignored, since it cannot mean anything here
  --mask-out <path>   write the mask in use to this path
  --mask-sample <s>   the texture to imitate — a flat patch of film grain gives
                      a mask that dithers with that grain's character. Three
                      things can be named, and a path you type always wins over
                      a library name:
                        a file        that scan
                        a directory   every scan in it, combined
                        a film name   the same, from the sample library at
                                      $XDG_CONFIG_HOME/dither/samples/<name>
                      A film is not one scan — grain varies with exposure and
                      development — so a directory of scans of one stock is
                      more data for the same model. markov and inpaint
                      combine them. direct and dft take a film name to mean
                      the film's crop, $XDG_CONFIG_HOME/dither/crops/<name>.png:
                      direct's tile *is* the sample, and dft would copy a
                      scan's edges into the mask as streaks. Without a crop,
                      direct takes the largest scan and dft pools them, and
                      each says which. Save a square
                      crop of clean grain there to give your own film one.
                      Unnamed, a generator that imitates takes Kodak-4X.
                      `source completion/dither` to complete film names
  --mask-scale <s>    feature size relative to the sample: 2 is twice as coarse,
                      0.5 twice as fine (default 1). dft remaps frequency;
                      markov and inpaint resample the sample, so above 1 they
                      also learn the interpolator's smoothness as grain
  --mask-highpass <px> ignore structure in the sample coarser than px pixels,
                      0 to disable. Default 32 for dft and direct, 0 for markov
                      and inpaint, which model the sample's values as they are.
                      A whole photograph is mostly its subject, so with 0 the
                      mask inherits the subject, not the grain. For markov it
                      also holds the field down past the modelled radius; at 0
                      spectrum matching (--markov-bands) does that instead
  --mask-calibrate    measure this generator's settings for the sample instead
                      of deriving them. Three generators can. markov fits the
                      model once, then draws probe masks across every radius,
                      weighting and temperature and keeps the combination whose
                      grain clumps most like the sample's. inpaint searches its
                      sweep count, which trades clumping against ridging at a
                      rate that is a property of the film, and keeps the default
                      unless some rung beats it by a clear margin. dft searches
                      its spectrum smoothing, and usually finds the area rule
                      has already adapted it. All three score against the
                      film's crop where it has one. The built-in
                      rules have no sample term, so they answer the same way for
                      every film; the measured optimum does not. Deterministic,
                      and cached against the sample and settings -- and a
                      cached calibration is used by every later run on that
                      sample without this flag. install_samples calibrates the
                      shipped films that way; --no-config ignores it

)", stdout);
    usage_generators();
}


// ~/.config/dither/cache.conf by default, honouring XDG_CONFIG_HOME.
std::string config_path()
{
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg && *xdg)
        return (std::filesystem::path(xdg) / "dither" / "cache.conf").string();
    if (const char* home = std::getenv("HOME"); home && *home)
        return (std::filesystem::path(home) / ".config" / "dither" / "cache.conf").string();
    return "";
}
