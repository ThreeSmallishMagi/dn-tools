// SPDX-License-Identifier: GPL-3.0-or-later
// defaults.h — parameters that work out their own values from the ones the user
// actually supplied. See DEFAULTS.md for the design and the parameter table.
//
// The parameters form a small dependency graph: markov.temp needs markov.bins
// and markov.pairs, mask.size will need supersample and mask.scale. Writing that
// as ordered assignments in parseOptions coupled the defaults to every part of
// the program and broke as soon as two rules were added in the wrong order.
//
// Here a parameter resolves lazily, the first time somebody asks for it, and the
// dependency graph is discovered by the act of asking — a rule that calls
// r.num("markov.bins") has thereby declared that it depends on it. Nothing
// states an evaluation order, and the recorded edges are what --explain prints.
//
// This header knows nothing about dithering. Each module registers its own rules
// from its own translation unit (see register_markov_params in markov.h), so the
// mechanism never learns what a Gibbs sweep is and the rules stay next to the
// code they configure.
#pragma once

#include <functional>
#include <iosfwd>
#include <optional>
#include <map>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace params {

// A number that consumed its whole string, or nothing. The "every character
// used" part is the point of them: "12abc", "" and "1.5"-where-an-integer-was
// are all errors, and three places were writing that check slightly
// differently — the CLI, --set, and the config file.
std::optional<double> parse_double(const std::string& s);
std::optional<long long> parse_integer(const std::string& s);

using Value = std::variant<double, long long, std::string>;

// Where a value came from. Reported by --explain, and the reason a user-supplied
// value is never recomputed. `Derived` and `Fallback` are told apart
// automatically: a rule that asked for nothing produced a constant. `Implied` is
// a default that was moved off its rule's answer so that something the user did
// supply would not be ignored — see reconcile().
enum class Origin { User, Measured, Implied, Derived, Fallback };
const char* origin_name(Origin);

class Params;

// What a rule is handed. Asking for a key resolves it recursively and records
// the dependency.
class Resolver {
public:
    explicit Resolver(Params& p) : p_(p) {}
    double      num(std::string_view key) const;
    long long   integer(std::string_view key) const;
    std::string str(std::string_view key) const;
    // No resource() accessor: the only non-scalar a rule has wanted so far is
    // the input's size, and image.width / image.height serve better than a
    // type-erased handle. Add one when something genuinely needs a cv::Mat.
private:
    Params& p_;
};

// A parameter that only matters under some conditions declares them here. The
// test and the phrase that names it travel together so they cannot drift apart:
// whoever writes the condition writes the sentence the user will read.
//
// Applicability governs *reporting* only — whether the user should be setting
// this at all — never resolution. A rule may legitimately read a parameter that
// does not apply to the run.
struct Applicability {
    std::function<bool(const Resolver&)> test;
    std::string scope;                          // e.g. "--mask-method markov"
    explicit operator bool() const { return (bool)test; }
};

struct Rule {
    std::string key;                                 // "markov.temp"
    std::function<Value(const Resolver&)> derive;    // required
    Applicability applies;                      // empty = always applies

    // Permitted range for a numeric parameter, checked on every write and on
    // every derived result. This lives on the rule rather than on whichever
    // option happened to expose it: a parameter that loses its option must not
    // lose its guard with it, and --set reaches everything.
    std::optional<std::pair<double, double>> range;

    std::string doc;                                 // shown by --explain

    // Values this parameter may be moved to when leaving it at its rule's answer
    // would strand an option the user did supply. Only a parameter that *gates*
    // others needs these — the ones whose value other parameters' Applicability
    // tests read. Empty means the rule's own answer is the only one on offer.
    std::vector<Value> candidates;

    // True when this parameter reaches the program's output *only* through other
    // parameters — it shapes them, and nothing downstream reads it directly.
    // `image.width` is the example: it exists so that mask.size can be derived
    // from the picture, and once the size is known the width has no further say.
    //
    // Whoever caches a result keyed on parameters wants the ones that are
    // actually consumed, not the ones those were computed from, because an
    // intermediate that moves without moving anything downstream is a spurious
    // miss. Dithering two images of different sizes with the same mask size used
    // to rebuild the identical mask for each.
    //
    // **The default is the safe direction.** Left alone a parameter counts, so
    // forgetting this on a new one costs a needless rebuild; setting it wrongly
    // costs a stale result. Only claim it when the parameter genuinely has no
    // reader of its own, and doc/DEFAULTS.md's table records which do.
    bool intermediate = false;

    // False when the program measures this for itself and a user has no business
    // typing it -- the input's dimensions, the sample's, and each generator's
    // measured throughput. Everything else is reachable as `--namespace-key`,
    // which is what makes the registry the single statement of what the command
    // line accepts.
    bool has_flag = true;
};

// A parameter whose default is a constant, which most are. Only a handful are
// computed from other parameters, and those are the ones a reader of a
// registration block should notice; spelling out an empty lambda for each of
// the rest buries them. Numeric literals still need their type spelled —
// `(long long)4` rather than `4` — because Value would not know which to pick.
void add_const(Params& P, std::string key, Value v, Applicability applies,
               std::optional<std::pair<double, double>> range, std::string doc,
               bool intermediate = false);

// A value as typed on a command line, converted to what the parameter declares
// it holds. Lives here rather than in either front end because dither and
// maketexture must agree about what `--inpaint-db 1e5` means, and a second copy
// of the rule is how they stopped agreeing about `--mask-sample 0001`.
Value value_from_text(const Params& P, std::string_view key, const std::string& text);

// What reconcile() did. `changed` is the defaults it moved and where to;
// `rescued` is the user-supplied parameters that would otherwise have been
// ignored. Both empty when the command line already made sense.
struct Reconciliation {
    std::vector<std::pair<std::string, std::string>> changed;
    std::vector<std::string> rescued;
    bool empty() const { return changed.empty(); }
};

class Params {
public:
    void add(Rule r);                                // modules register their own
    void set(std::string_view key, Value v, Origin o);
    bool known(std::string_view key) const;          // has a rule or a value

    // Resolve on demand. Numeric accessors convert between the two numeric
    // alternatives, so a rule need not care whether --set gave 16 or 16.0.
    double      num(std::string_view key);
    long long   integer(std::string_view key);
    std::string str(std::string_view key);

    // A resolved value rendered the way --explain renders it. The mask cache
    // builds its key out of these, so that what identifies a mask is the same
    // text the user can read back out of --explain.
    std::string value_of(std::string_view key);

    // The same value, but able to come back: round-trip precision for doubles
    // and a type tag. Anything that hashes or persists a parameter must use
    // this -- value_of is six significant figures, which made two distinct
    // parameters share one mask cache entry.
    std::string serialized_of(std::string_view key);

    // `blue.sigma` <-> `--blue-sigma`. The first dot becomes the hyphen that
    // separates namespace from name, and underscores inside the name become
    // hyphens too, so `dft.smooth_cells` is `--dft-smooth-cells`.
    //
    // Both directions live here because they are one fact: a flag the parser
    // accepts and the name --explain prints cannot drift apart if neither is
    // written down twice.
    //
    // **The reverse direction asks the registry**, and must. The forward one is
    // lossy -- `.` and `_` both become `-` -- so decoding by rule cannot work:
    // mask.size.w emitted --mask-size-w and decoded to mask.size_w, a key no
    // rule has, so the parser rejected a flag --list-options advertises.
    // Looking the flag up among the keys that could have produced it is the
    // only answer that cannot drift.
    static std::string flag_for(std::string_view key);
    std::string key_for_flag(std::string_view flag) const;

    // Say this parameter is measured rather than chosen, so it gets no flag.
    void hide_option(std::string_view key);
    bool has_option(std::string_view key) const;

    // The permitted range a rule declares, for a caller that must check a value
    // *before* writing it -- a cached calibration, say, where set() throwing
    // would take down a run the cache is supposed to be unable to affect.
    std::optional<std::pair<double, double>> range_of(std::string_view key) const;

    // The rule's own one-line description, which --explain and --help both
    // print. One sentence, written where the parameter is declared.
    std::string doc(std::string_view key) const;

    // Every parameter a user may type, in registry order.
    std::vector<std::string> options() const;

    // A parameter whose only permitted values are 0 and 1 is offered as a
    // switch: there is nothing to type after it. `--set key=0` still turns one
    // off, which is the one thing the switch spelling cannot say.
    bool is_switch(std::string_view key) const;

    // Does this parameter hold text? Asked by whatever turns a command line
    // into values, so that a path is a path whatever it looks like: typing it
    // from its spelling made `--mask-sample 0001` store the number 1, and no
    // quoting could help because the conversion happens after the shell.
    //
    // Answered from the declared range, which every numeric parameter carries
    // and no string one does -- the registry's own invariant, since a range is
    // checked on every write. unittest pins it.
    bool is_text(std::string_view key) const;

    // See Rule::intermediate. False for a key with no rule, which is the safe
    // answer: a value the user set by hand counts until something says it does not.
    bool intermediate(std::string_view key) const;

    bool applies(std::string_view key);
    bool measured(std::string_view key) const;   // came from the config, not a guess

    // True when the value came from the command line, and so must not be
    // recomputed by anything that would otherwise work it out. Deliberately
    // const and non-resolving, unlike origin_of: asking whether the user set
    // something must not itself run a rule and produce a value.
    bool user_set(std::string_view key) const;

    Origin origin_of(std::string_view key);      // resolves the key if it has to

    // Move unpinned defaults so that the options the user *did* supply are not
    // ignored. Naming --mask-sample is asking for a mask, so the threshold
    // default has no business staying on `round` and reporting the sample as
    // inapplicable; it should move, and say so.
    //
    // This reuses the Applicability tests rather than restating them: a rule
    // offers `candidates`, and the search asks each stranded parameter whether
    // it applies under a given assignment. Nothing here knows what a mask is,
    // and adding a generator with its own gating parameter needs no change.
    //
    // The search is exhaustive over the product of the candidate lists, which is
    // affordable because only gating parameters carry them — a handful of
    // combinations, evaluated once, before anything else resolves. It is skipped
    // above kMaxTrials so that a future rule cannot make startup quadratic
    // without anyone noticing.
    static constexpr int kMaxTrials = 256;
    Reconciliation reconcile();

    // What reconcile() could not rescue, and why. A parameter reaches here only
    // when the thing blocking it is another parameter *the user pinned* — had it
    // been a default, reconcile would have moved it. So this is not a warning
    // about a misplaced option, it is two requests that cannot both be honoured,
    // and the caller is expected to refuse rather than quietly drop one.
    //
    // `blocked_by` is found by recording which keys the applicability test reads
    // and keeping those the user set, so it names the actual other side of the
    // conflict rather than restating the condition.
    struct Conflict {
        std::string key;                     // the parameter that cannot apply
        std::string scope;                   // when it *would* apply
        std::vector<std::string> blocked_by; // user-set parameters ruling that out
    };
    std::vector<Conflict> conflicts();

    void explain(std::ostream& os);

    // Every key with a rule whose name ends in `suffix`. This exists so the
    // config's list of machine-measured rates is discovered rather than typed
    // out: it was typed out, a fourth generator was added, and --budget could
    // never learn its speed because nobody remembered the fourth string.
    std::vector<std::string> keys_ending(std::string_view suffix) const;

    // A tiny persisted subset of the parameters: values this machine measured
    // about itself, which would otherwise have to be re-measured every run.
    // Loaded values arrive as Origin::Measured, so a command-line setting still
    // wins over them. Deliberately not TOML or JSON — six numbers do not justify
    // a parser, let alone a dependency.
    void load_config(const std::string& path);
    bool save_config(const std::string& path, const std::vector<std::string>& keys);

    // Values cached in the config that have no rule, because their key carries a
    // hash of what they were measured for. They cannot ride the rules-checked
    // path, so they get their own: nothing validates them beyond being a number.
    std::optional<double> cached(std::string_view key) const;
    void remember(std::string_view key, double v);

private:
    friend class Resolver;
    const Value& resolve(std::string_view key);
    void check_range(std::string_view key, const Value& v) const;

    // reconcile()'s two halves: what the run would currently ignore, and
    // clearing back to what the user and the machine actually said.
    std::vector<std::string> stranded();
    void forget_unpinned();

    // reconcile()'s three steps. A Movable is a default that offers candidates
    // and that the user has not pinned; apply_trial() puts one assignment of
    // them in place and reports how many supplied options it still strands.
    struct Movable { std::string key; std::vector<Value> options; };
    std::vector<Movable> movable_defaults() const;
    long long apply_trial(const std::vector<Movable>& movable, const std::vector<int>& pick);

    // The keys an applicability test consults. resolve() already records edges
    // for whichever rule is running; a test runs under no rule, so it is given a
    // frame of its own to record into.
    std::vector<std::string> applicability_inputs(const Rule& r);

    struct Slot {
        Value v;
        Origin origin = Origin::Derived;
        std::vector<std::string> deps;   // recorded while the rule ran
        bool resolved = false;
    };
    std::map<std::string, Rule, std::less<>> rules_;
    std::map<std::string, Slot, std::less<>> slots_;
    std::vector<std::string> stack_;     // resolving stack, for cycle detection
    std::map<std::string, double, std::less<>> cached_;   // keyed by content hash
};

} // namespace params
