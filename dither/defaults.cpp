// SPDX-License-Identifier: GPL-3.0-or-later
// defaults.cpp — see defaults.h.

#include "defaults.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <unistd.h>
#include <limits>
#include <ostream>
#include <sstream>
#include <stdexcept>

namespace params {

std::optional<double> parse_double(const std::string& s)
{
    try {
        size_t used = 0;
        const double d = std::stod(s, &used);
        if (used == s.size()) return d;
    } catch (const std::exception&) { }
    return std::nullopt;
}

std::optional<long long> parse_integer(const std::string& s)
{
    try {
        size_t used = 0;
        const long long n = std::stoll(s, &used);
        if (used == s.size()) return n;
    } catch (const std::exception&) { }
    return std::nullopt;
}

const char* origin_name(Origin o)
{
    switch (o) {
        case Origin::User:     return "user";
        case Origin::Measured: return "measured";
        case Origin::Implied:  return "implied";
        case Origin::Derived:  return "derived";
        case Origin::Fallback: return "fallback";
    }
    return "?";
}

namespace {

// The two numeric alternatives as one double, so "is this a number?" is one
// question rather than the three slightly different ones it had become.
std::optional<double> as_number(const Value& v)
{
    if (auto* d = std::get_if<double>(&v)) return *d;
    if (auto* i = std::get_if<long long>(&v)) return (double)*i;
    return std::nullopt;
}

[[noreturn]] void not_a_number(std::string_view key)
{
    throw std::runtime_error("parameter '" + std::string(key) + "' is not a number");
}

// Pops the resolving stack however the scope is left, including on an exception
// from a rule. resolve() had a try/catch whose only body was the pop.
struct StackFrame {
    std::vector<std::string>& stack;
    StackFrame(std::vector<std::string>& s, std::string key) : stack(s)
    { stack.push_back(std::move(key)); }
    ~StackFrame() { stack.pop_back(); }
    // Only ever an automatic guard; copying one would pop twice.
    StackFrame(const StackFrame&) = delete;
    StackFrame& operator=(const StackFrame&) = delete;
};

// Leading and trailing blanks removed. File scope rather than rebuilt for every
// line of the config.
std::string trim(const std::string& t)
{
    const auto b = t.find_first_not_of(" \t\r");
    const auto e = t.find_last_not_of(" \t\r");
    return b == std::string::npos ? std::string() : t.substr(b, e - b + 1);
}

// For a person to read: six significant figures, which is the right amount for
// an --explain table and the wrong amount for anything that has to come back.
std::string format(const Value& v)
{
    if (auto* d = std::get_if<double>(&v)) {
        char buf[32];
        std::snprintf(buf, sizeof buf, "%.6g", *d);
        return buf;
    }
    if (auto* i = std::get_if<long long>(&v)) return std::to_string(*i);
    return std::get<std::string>(v);
}

// For a machine to read back, or to hash: every double that differs reads back
// differently, and every value carries its type.
//
// Using format() for this was a real defect and not a theoretical one. The mask
// cache hashes what value_of() returns, so two parameters that agree to six
// figures shared one cache entry: blue.sigma=1.8000001 and 1.8000002 produced
// one mask, and the second run was silently served the first one's. A cache
// whose whole design is "never serve a wrong hit" was losing the distinction in
// its own key. save_config had the same loss at rest, writing a measured rate
// as 3.6061e+06 and reading back a different number than it had.
//
// max_digits10 is the round-trip guarantee: seventeen significant digits is
// exactly enough that every distinct double prints distinctly. %.17g rather
// than an ostream because the stream carries a locale, and a locale that uses a
// comma for the decimal point would make the identity depend on the
// environment. printf's %g is locale-sensitive too in principle; the C locale
// is the process default and nothing here calls setlocale.
//
// The type tag matters for the hash rather than for reading: without it the
// long long 2 and the string "2" are the same bytes, so two parameters of
// different types could collide even when their text differs in meaning.
std::string serialize(const Value& v)
{
    if (auto* d = std::get_if<double>(&v)) {
        char buf[40];
        std::snprintf(buf, sizeof buf, "d:%.*g", std::numeric_limits<double>::max_digits10, *d);
        return buf;
    }
    if (auto* i = std::get_if<long long>(&v)) return "i:" + std::to_string(*i);
    return "s:" + std::get<std::string>(v);
}

} // namespace

std::string Params::value_of(std::string_view key) { return format(resolve(key)); }
std::string Params::serialized_of(std::string_view key) { return serialize(resolve(key)); }

std::string Params::flag_for(std::string_view key)
{
    std::string f = "--";
    bool seenDot = false;
    for (char c : key) {
        if (c == '.' && !seenDot) { f += '-'; seenDot = true; }
        else if (c == '_' || c == '.') f += '-';
        else f += c;
    }
    return f;
}

std::string Params::key_for_flag(std::string_view flag) const
{
    if (flag.size() < 3 || flag[0] != '-' || flag[1] != '-') return {};
    for (const auto& [key, rule] : rules_)
        if (rule.has_flag && flag_for(key) == flag) return key;
    return {};
}

void Params::hide_option(std::string_view key)
{
    const auto it = rules_.find(key);
    if (it != rules_.end()) it->second.has_flag = false;
}

std::optional<std::pair<double, double>> Params::range_of(std::string_view key) const
{
    const auto it = rules_.find(std::string(key));
    return it == rules_.end() ? std::nullopt : it->second.range;
}

bool Params::has_option(std::string_view key) const
{
    const auto it = rules_.find(key);
    return it != rules_.end() && it->second.has_flag &&
           key.find('.') != std::string_view::npos;
}

std::string Params::doc(std::string_view key) const
{
    const auto it = rules_.find(key);
    return it == rules_.end() ? std::string() : it->second.doc;
}

std::vector<std::string> Params::options() const
{
    std::vector<std::string> out;
    for (const auto& [k, r] : rules_)
        if (r.has_flag && k.find('.') != std::string::npos) out.push_back(k);
    return out;
}

bool Params::is_switch(std::string_view key) const
{
    const auto it = rules_.find(key);
    return it != rules_.end() && it->second.range &&
           it->second.range->first == 0.0 && it->second.range->second == 1.0;
}

Value value_from_text(const Params& P, std::string_view key, const std::string& text)
{
    if (!P.is_text(key)) {
        if (text.find_first_of(".eE") == std::string::npos) {
            if (const auto n = parse_integer(text)) return *n;
        } else if (const auto d = parse_double(text)) {
            return *d;
        }
    }
    return text;
}

bool Params::is_text(std::string_view key) const
{
    const auto it = rules_.find(key);
    return it != rules_.end() && !it->second.range;
}

bool Params::intermediate(std::string_view key) const
{
    const auto it = rules_.find(key);
    return it != rules_.end() && it->second.intermediate;
}

double Resolver::num(std::string_view key) const      { return p_.num(key); }
long long Resolver::integer(std::string_view key) const { return p_.integer(key); }
std::string Resolver::str(std::string_view key) const  { return p_.str(key); }

void add_const(Params& P, std::string key, Value v, Applicability applies,
               std::optional<std::pair<double, double>> range, std::string doc,
               bool intermediate)
{
    P.add({ std::move(key),
            [v = std::move(v)](const Resolver&) { return v; },
            std::move(applies), range, std::move(doc), {}, intermediate });
}

void Params::add(Rule r)
{
    std::string key = r.key;
    rules_.insert_or_assign(std::move(key), std::move(r));
}

// Throws if `v` is numeric and falls outside the rule's declared range.
void Params::check_range(std::string_view key, const Value& v) const
{
    auto rule = rules_.find(key);
    if (rule == rules_.end() || !rule->second.range) return;
    const std::optional<double> num = as_number(v);
    if (!num) return;
    const double d = *num;
    const auto [lo, hi] = *rule->second.range;
    if (!(d >= lo && d <= hi)) {
        char buf[256];
        std::snprintf(buf, sizeof buf,
                      "%s: %g is outside the permitted range [%g, %g]",
                      std::string(key).c_str(), d, lo, hi);
        throw std::runtime_error(buf);
    }
}

void Params::set(std::string_view key, Value v, Origin o)
{
    check_range(key, v);
    Slot& s = slots_[std::string(key)];
    s.v = std::move(v);
    s.origin = o;
    s.resolved = true;
    s.deps.clear();
}

bool Params::known(std::string_view key) const
{
    return rules_.find(key) != rules_.end() || slots_.find(key) != slots_.end();
}

const Value& Params::resolve(std::string_view key)
{
    // Record the edge from whoever is asking. This is the whole of dependency
    // tracking: a rule declares its inputs by using them.
    if (!stack_.empty()) {
        auto& deps = slots_[stack_.back()].deps;
        if (std::find(deps.begin(), deps.end(), key) == deps.end())
            deps.emplace_back(key);
    }

    auto it = slots_.find(key);
    if (it != slots_.end() && it->second.resolved) return it->second.v;

    auto rule = rules_.find(key);
    if (rule == rules_.end())
        throw std::runtime_error("no rule or value for parameter '" + std::string(key) + "'");

    if (std::find(stack_.begin(), stack_.end(), key) != stack_.end()) {
        std::string cycle;
        for (const auto& k : stack_) cycle += k + " -> ";
        throw std::runtime_error("parameter cycle: " + cycle + std::string(key));
    }

    Value v;
    {
        StackFrame frame(stack_, std::string(key));
        slots_[stack_.back()].deps.clear();   // may already exist; reset its edges
        v = rule->second.derive(Resolver(*this));
    }

    // Looked up again rather than held across derive(): the rule recursively
    // inserts into slots_, and while std::map would keep the reference valid,
    // not relying on that keeps this correct if the container ever changes.
    //
    // A rule that asked for nothing produced a constant, which is a fallback
    // rather than a derivation. Nothing has to declare which it is.
    check_range(key, v);            // a rule may not produce nonsense either
    Slot& s = slots_[std::string(key)];
    s.v = std::move(v);
    s.origin = s.deps.empty() ? Origin::Fallback : Origin::Derived;
    s.resolved = true;
    return s.v;
}

double Params::num(std::string_view key)
{
    if (const std::optional<double> d = as_number(resolve(key))) return *d;
    not_a_number(key);
}

long long Params::integer(std::string_view key)
{
    const Value& v = resolve(key);
    // Taken whole when it already is one, so a value beyond a double's exact
    // integer range survives the trip.
    if (auto* i = std::get_if<long long>(&v)) return *i;
    if (const std::optional<double> d = as_number(v)) {
        // Rounding a fractional value here is how --set disagreed with the
        // flags: `--supersample 2.9` was truncated to 2 by the parser, while
        // `--set supersample=2.9` arrived as a double and quietly became 3.
        // The flags now refuse a fractional value, so this refuses one too and
        // the two spellings answer alike.
        //
        // A value the user supplied must be integral exactly. The tolerance
        // exists only for a derived rule, whose arithmetic is in double and can
        // land a hair off a whole number; applying it to user input is what let
        // `--set seed=1.0000005` through while `--seed` refused it.
        const double nearest = std::round(*d);
        const double slack = origin_of(key) == Origin::User ? 0.0 : 1e-6;
        if (std::abs(*d - nearest) > slack) {
            // Enough digits to show the fraction: format() is %.6g and renders
            // 1000000000.4 as "1e+09", a number that looks whole in a message
            // saying it is not.
            char shown[40];
            std::snprintf(shown, sizeof shown, "%.12g", *d);
            throw std::runtime_error(std::string(key) + " is used as a whole number, but is "
                                     + shown);
        }
        return (long long)nearest;
    }
    not_a_number(key);
}

std::string Params::str(std::string_view key)
{
    const Value& v = resolve(key);
    if (auto* s = std::get_if<std::string>(&v)) return *s;
    return format(v);
}

bool Params::applies(std::string_view key)
{
    auto rule = rules_.find(key);
    if (rule == rules_.end() || !rule->second.applies) return true;
    return rule->second.applies.test(Resolver(*this));
}

std::optional<double> Params::cached(std::string_view key) const
{
    auto it = cached_.find(key);
    return it == cached_.end() ? std::nullopt : std::optional<double>(it->second);
}

void Params::remember(std::string_view key, double v)
{
    cached_.insert_or_assign(std::string(key), v);
}

bool Params::measured(std::string_view key) const
{
    auto it = slots_.find(key);
    return it != slots_.end() && it->second.origin == Origin::Measured;
}

bool Params::user_set(std::string_view key) const
{
    auto it = slots_.find(key);
    return it != slots_.end() && it->second.origin == Origin::User;
}

Origin Params::origin_of(std::string_view key)
{
    resolve(key);
    return slots_.find(key)->second.origin;
}

// The parameters the user pinned that the run, as it currently stands, would
// ignore. What survives reconcile() is what conflicts() reports.
std::vector<std::string> Params::stranded()
{
    std::vector<std::string> out;
    for (const auto& [k, s] : slots_) {
        if (s.origin != Origin::User) continue;
        auto rule = rules_.find(k);
        if (rule == rules_.end() || !rule->second.applies) continue;
        // A rule that throws under a speculative assignment has not told us
        // anything useful about it; count the parameter as still stranded so
        // that assignment cannot win.
        bool ok = false;
        try { ok = rule->second.applies.test(Resolver(*this)); } catch (...) { }
        if (!ok) out.push_back(k);
    }
    return out;
}

// Drop everything the user did not pin and the machine did not measure, so each
// trial starts from the same slate. Values derived under one assumption must not
// survive into the next: mask.method's own rule reads mask.sample, and a
// mask.method cached while the threshold was still `round` would be answering a
// question about a different run.
void Params::forget_unpinned()
{
    for (auto it = slots_.begin(); it != slots_.end(); ) {
        const Origin o = it->second.origin;
        if (o == Origin::User || o == Origin::Measured) ++it;
        else it = slots_.erase(it);
    }
}

// Defaults free to move: those offering candidates that the user has not pinned.
// rules_ is sorted, so the order — and therefore every tie-break below — is
// deterministic.
std::vector<Params::Movable> Params::movable_defaults() const
{
    std::vector<Movable> movable;
    for (const auto& [k, r] : rules_) {
        if (r.candidates.empty()) continue;
        auto s = slots_.find(k);
        if (s != slots_.end() && (s->second.origin == Origin::User ||
                                  s->second.origin == Origin::Measured)) continue;
        movable.push_back({k, r.candidates});
    }
    return movable;
}

// Put one assignment in place and report how many supplied options it strands.
// `pick[i]` indexes movable[i].options, with 0 meaning "leave this one alone".
long long Params::apply_trial(const std::vector<Movable>& movable,
                              const std::vector<int>& pick)
{
    forget_unpinned();
    for (size_t i = 0; i < movable.size(); i++)
        if (pick[i]) set(movable[i].key, movable[i].options[pick[i] - 1], Origin::Implied);
    return (long long)stranded().size();
}

Reconciliation Params::reconcile()
{
    if (stranded().empty()) { forget_unpinned(); return {}; }
    const std::vector<Movable> movable = movable_defaults();
    if (movable.empty()) { forget_unpinned(); return {}; }

    // "Does the search fit?", asked without computing how far past the limit it
    // would go. The product was accumulated first and compared afterwards, so a
    // registry large enough to matter would overflow the very counter meant to
    // stop it -- and a signed overflow can land anywhere, including back under
    // the cap. Today's registry cannot reach it; a cap that only works while
    // nothing tests it is not a cap.
    long long trials = 1;
    bool fits = true;
    for (const auto& m : movable) {
        const long long factor = (long long)m.options.size() + 1;
        if (factor > kMaxTrials / trials) { fits = false; break; }
        trials *= factor;
    }
    if (!fits || trials > kMaxTrials) { forget_unpinned(); return {}; }

    // Mixed-radix walk over the product of {leave alone} x candidates. Index 0
    // is "leave alone", so the all-zero assignment is the status quo and is
    // evaluated first — which makes it the tie-break winner for free.
    std::vector<int> pick(movable.size(), 0), best;
    long long best_left = -1, best_moves = 0;
    for (long long t = 0; t < trials; t++) {
        long long n = t, moves = 0;
        for (size_t i = 0; i < movable.size(); i++) {
            const int radix = (int)movable[i].options.size() + 1;
            pick[i] = (int)(n % radix);
            n /= radix;
            moves += pick[i] != 0;
        }
        const long long left = apply_trial(movable, pick);
        // Fewest ignored options wins; among equals, the fewest changes to get
        // there. Moving a default the user did not ask about is a cost.
        if (best_left < 0 || left < best_left || (left == best_left && moves < best_moves)) {
            best_left = left; best_moves = moves; best = pick;
        }
    }

    forget_unpinned();
    const std::vector<std::string> before = stranded();

    apply_trial(movable, best);
    Reconciliation out;
    for (size_t i = 0; i < movable.size(); i++)
        if (best[i])
            out.changed.emplace_back(movable[i].key, format(movable[i].options[best[i] - 1]));
    if (out.changed.empty()) return {};

    // Report only what the move actually rescued. Anything still stranded is
    // conflicts()' business, and saying it was rescued would be a lie.
    const std::vector<std::string> after = stranded();
    for (const auto& k : before)
        if (std::find(after.begin(), after.end(), k) == after.end())
            out.rescued.push_back(k);
    return out;
}

std::vector<std::string> Params::applicability_inputs(const Rule& r)
{
    // A key no rule can have, so it cannot collide with a real one or read as a
    // cycle. Erased again below: nothing outside this function should see it.
    static const std::string kProbe = "\x01applicability";
    {
        StackFrame frame(stack_, kProbe);
        slots_[kProbe].deps.clear();
        try { r.applies.test(Resolver(*this)); } catch (...) { }
    }
    std::vector<std::string> deps = slots_[kProbe].deps;
    slots_.erase(kProbe);
    return deps;
}

std::vector<Params::Conflict> Params::conflicts()
{
    std::vector<Conflict> out;
    for (const auto& k : stranded()) {
        const Rule& rule = rules_.find(k)->second;
        Conflict c{k, rule.applies.scope, {}};
        for (const auto& d : applicability_inputs(rule)) {
            auto s = slots_.find(d);
            if (d != k && s != slots_.end() && s->second.origin == Origin::User)
                c.blocked_by.push_back(d);
        }
        out.push_back(std::move(c));
    }
    return out;
}

void Params::explain(std::ostream& os)
{
    // Every key that has a rule, plus any the user set that does not.
    std::vector<std::string> keys;
    for (const auto& [k, _] : rules_) keys.push_back(k);
    for (const auto& [k, _] : slots_)
        if (rules_.find(k) == rules_.end()) keys.push_back(k);
    std::sort(keys.begin(), keys.end());

    // Parameters belonging to a generator this run will not use are listed once
    // by name rather than given a row each: with four generators registered,
    // three quarters of the table would otherwise be "does not apply".
    std::vector<std::string> live, idle;
    for (const auto& k : keys) (applies(k) ? live : idle).push_back(k);

    size_t kw = 9, vw = 5;                       // "parameter", "value"
    for (const auto& k : live) {
        kw = std::max(kw, k.size());
        vw = std::max(vw, format(resolve(k)).size());
    }
    auto pad = [&os](const std::string& s, size_t w) {
        os << s << std::string(w > s.size() ? w - s.size() : 0, ' ') << "  ";
    };

    // The origin column is the useful half of this table and its vocabulary is
    // five words long, so it is spelled out rather than left to be inferred.
    // DEFAULTS.md is where the reasoning behind each rule lives; this is only
    // what the values came out as.
    os << "Every parameter this run will use, and where its value came from.\n"
          "doc/DEFAULTS.md has the reasoning behind each rule.\n\n"
          "  user      you gave it, so nothing recomputes it\n"
          "  implied   assumed, so that an option you did give is not ignored\n"
          "  measured  read off the input image, or timed on this machine\n"
          "  derived   computed from the parameters named on the right\n"
          "  fallback  a constant — its rule had nothing to compute from\n\n  ";
    pad("parameter", kw); pad("value", vw); pad("origin", 8); os << "derived from\n";
    for (const auto& k : live) {
        const Slot& s = slots_.find(k)->second;
        os << "  ";
        pad(k, kw);
        pad(format(s.v), vw);
        if (s.deps.empty()) { os << origin_name(s.origin) << "\n"; continue; }
        pad(origin_name(s.origin), 8);
        for (size_t i = 0; i < s.deps.size(); i++) os << (i ? ", " : "") << s.deps[i];
        os << "\n";
    }

    if (!idle.empty()) {
        os << "\n  not used by this run: ";
        for (size_t i = 0; i < idle.size(); i++) os << (i ? ", " : "") << idle[i];
        os << "\n";
    }

    os << "\n";
    for (const auto& k : live) {
        auto r = rules_.find(k);
        if (r == rules_.end() || r->second.doc.empty()) continue;
        os << "  ";
        pad(k, kw);
        os << r->second.doc << "\n";
    }
}

std::vector<std::string> Params::keys_ending(std::string_view suffix) const
{
    std::vector<std::string> out;
    for (const auto& [k, _] : rules_)
        if (k.size() >= suffix.size() &&
            k.compare(k.size() - suffix.size(), suffix.size(), suffix) == 0)
            out.push_back(k);
    return out;                          // rules_ is sorted, so this is too
}

void Params::load_config(const std::string& path)
{
    std::ifstream in(path);
    if (!in) return;                        // absent is normal, not an error
    std::string line;
    while (std::getline(in, line)) {
        const auto hash = line.find('#');
        if (hash != std::string::npos) line.erase(hash);
        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = trim(line.substr(0, eq));
        std::string val = trim(line.substr(eq + 1));
        if (key.empty() || val.empty()) continue;

        // Typed since the round-trip serializer went in. An untagged line is
        // from a version that wrote six significant figures, and its numbers
        // are not the numbers that were measured -- so it is skipped rather
        // than read, and the next build writes a faithful one. Silently
        // accepting it would mean a value that has been through a save/load
        // cycle differs from one that has not, which is the defect this
        // encoding exists to remove.
        if (val.size() < 2 || val[1] != ':') continue;
        const char type = val[0];
        val.erase(0, 2);
        if (val.empty()) continue;
        if (type != 'd' && type != 'i') continue;   // no string is persisted yet

        const std::optional<double> d = parse_double(val);
        if (!d) continue;                     // a corrupt line is just ignored
        // A cached measurement has to be finite before anything computes with
        // it. An inf or a nan here parses cleanly and then propagates silently
        // through whatever reads it, which for a calibration entry is a
        // temperature and for a rate is the whole --budget allocation.
        if (!std::isfinite(*d)) {
            std::fprintf(stderr, "ignoring %s in %s: %s is not a finite number\n",
                         key.c_str(), path.c_str(), val.c_str());
            continue;
        }
        if (rules_.find(key) != rules_.end()) {
            // set() enforces the parameter's range, and a *stale* file is
            // exactly how a value comes to be outside one -- narrow a range and
            // every config written before it becomes fatal. This file is a
            // cache that says it is safe to delete, so a value it can no longer
            // justify is dropped with a word about it, and the next build
            // measures a fresh one. A programmer error still throws; this is
            // only the read boundary being a boundary.
            try { set(key, *d, Origin::Measured); }
            catch (const std::exception& e) {
                std::fprintf(stderr, "ignoring a cached value in %s: %s\n",
                             path.c_str(), e.what());
            }
        }
        else cached_.insert_or_assign(key, *d);         // hashed calibration entries
    }
}

bool Params::save_config(const std::string& path, const std::vector<std::string>& keys)
{
    const std::filesystem::path file(path);
    if (file.has_parent_path()) {
        std::error_code ec;
        std::filesystem::create_directories(file.parent_path(), ec);
        if (ec) return false;
    }
    // Written beside the destination and renamed over it, rather than opened
    // on top of it. An ofstream on `path` truncates immediately, so a run
    // interrupted between that and the last line left no config at all --
    // trading a stale file, which is recoverable, for no file, which loses
    // every measurement this machine had made. Rename within a directory is
    // atomic, so a reader sees the old file or the new one and never half.
    //
    // This does not merge concurrent writers: the last one to rename wins, and
    // the loser's measurements are simply remeasured next time.
    const std::string tmp = path + ".tmp" + std::to_string(::getpid());
    std::ofstream out(tmp);
    if (!out) return false;
    out << "# dither: values measured about this machine. Safe to delete —\n"
           "# it only costs the time to measure them again.\n";
    for (const auto& k : keys) {
        auto it = slots_.find(k);
        if (it == slots_.end() || !it->second.resolved) continue;
        // serialize, not format: this file is read back, and a rate that
        // returns as a different number than it left as is a measurement
        // silently corrupted by its own storage.
        out << k << " = " << serialize(it->second.v) << "\n";
    }
    // One line per sample-and-settings combination ever calibrated. They are
    // never evicted: forty bytes each, and the file says it is safe to delete.
    for (const auto& [k, v] : cached_) out << k << " = " << serialize(v) << "\n";

    // Closed before it is judged. The check used to run on a stream that was
    // still open, so a failure in the final flush -- which is where a full disk
    // reports itself -- was reported as success.
    out.close();
    std::error_code ec;
    if (!out) { std::filesystem::remove(tmp, ec); return false; }
    std::filesystem::rename(tmp, path, ec);
    if (ec) { std::filesystem::remove(tmp, ec); return false; }
    return true;
}

} // namespace params
