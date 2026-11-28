// SPDX-License-Identifier: GPL-3.0-or-later
// inpaint.cpp — see inpaint.h.
//
// The method is Efros-Leung, coarse to fine: to decide a pixel, find the
// neighbourhoods in the sample that most resemble the one around it and copy
// what they had in the middle. Everything here follows from that, and the three
// details that are not obvious are all things that were measured wrong first:
//
//   * The field is updated *in place*, a fraction of the pixels at a time.
//     Deciding every pixel from the previous sweep leaves each individually
//     plausible and neighbouring pairs inconsistent; splitting a sweep into
//     `groups` in-place passes costs nothing and was worth 19.6 -> 15.6 on
//     test/patchfit.
//   * The database is large, and is the binding constraint: 40k/100k/250k
//     neighbourhoods measure 15.6/12.9/10.7. A closer match is one whose centre
//     is consistent with the neighbourhood that asked for it, so what looks like
//     model noise is really search error.
//   * Both the sample and the field are held rank-flat throughout. The mask has
//     to be flat anyway, and it is what makes a neighbourhood cut from one
//     comparable with a neighbourhood cut from the other.
//
// A larger radius does not help — 4 and 5 measure 31.8 and 50.9 — because the
// database thins out in a bigger space. That is the same constraint as the
// database size, seen from the other end.
//
// **Sweeping worms.** Ridgeness grows monotonically with the sweep count, so
// `iters` trades ridging against clumping and has no convergent value — it is
// the parameter --mask-calibrate measures here, as markov.temp is for markov.
// The absent temperature removes the transition, not the ordering, so the
// ridges accumulate from the first sweep instead of switching on.

#include "inpaint.h"
#include "maskutil.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <ostream>
#include <random>
#include <vector>
#include <opencv2/opencv.hpp>
#include <opencv2/flann.hpp>
#include <opencv2/flann/random.h>

namespace {

using maskutil::rank_uniform_unit;

// Block-mean by `f`, then rank flat again: the coarse field has to be flat too,
// or the neighbourhoods cut from it are not comparable with the fine ones.
//
// Written out rather than cv::resize with INTER_AREA, which computes the same
// mean but not the same floats. That matters here and nowhere else: the field
// is re-ranked after every sweep, so a last-bit difference reorders it and
// propagates. Swapping the two changed 99.6% of the finished mask, worst case
// by 254 levels. Neither is wrong; this one is what every existing mask was
// made with.
cv::Mat downsample(const cv::Mat& src, int f)
{
    if (f == 1) return src;
    const int H = src.rows / f, W = src.cols / f;
    cv::Mat out(H, W, CV_32F);
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            float s = 0;
            for (int dy = 0; dy < f; dy++)
                for (int dx = 0; dx < f; dx++)
                    s += src.at<float>(y * f + dy, x * f + dx);
            out.at<float>(y, x) = s / (f * f);
        }
    return out;
}

// `downsample` over every scan, then one ranking over all of them.
//
// Not a method on samples::Set: which average to take is this generator's
// choice, documented above, and a shared class has no business making it. But
// the *ranking* must be the set's, and separating the two is the point. Ranking
// each scan as it was averaged put every scan's marginal back onto the whole
// unit range, which is exactly the cross-scan contrast `ranked_unit` exists to
// keep -- two fields differing tenfold in amplitude came out of the coarse
// level identical, so the database there learned a contrast model the finest
// level contradicts.
//
// One scan is unchanged to the bit: ranked_unit's arithmetic is
// rank_uniform_unit's, over the same stable order.
}   // the anonymous namespace pauses: downsample_set is declared in the
    // header so the test can assert its contract, which internal linkage
    // would hide.

samples::Set downsample_set(const samples::Set& set, int f)
{
    if (f == 1) return set;
    std::vector<cv::Mat> out;
    out.reserve(set.size());
    for (const cv::Mat& s : set) out.push_back(downsample(s, f));
    return samples::Set::of(std::move(out)).ranked_unit();
}

namespace {

// The sample's directionality over every scan. Coherence is a ratio of
// structure-tensor sums, so the tensors are pooled and the ratio taken once --
// averaging per-scan coherences would be a different quantity. Exact for one
// scan, which is what `inpaint.cap` was measured on.
static double set_coherence(const samples::Set& set)
{
    maskutil::Tensor t;
    for (const cv::Mat& f : set) {
        const maskutil::Tensor u = maskutil::structure_tensor(f);
        t.xx += u.xx; t.yy += u.yy; t.xy += u.xy;
    }
    return maskutil::coherence_of(t);
}

// One level's searchable database: neighbourhoods harvested from the sample,
// the PCA basis they are matched in, and the centre each of them came with.
class PatchDatabase {
public:
    // Neighbourhoods are drawn from every scan, shared out in proportion to the
    // centres each can offer -- a scan twice the area contributes twice as many.
    //
    // **One scan takes exactly the loop this always had**, two draws per patch
    // and no third for choosing a scan. A per-patch scan draw would consume the
    // generator differently and change every single-sample mask, which the
    // 76-run hash matrix exists to catch.
    PatchDatabase(const samples::Set& set, const std::vector<cv::Point>& ring, int r,
                  int want, int dims, std::mt19937_64& rng)
    {
        const int dim = (int)ring.size();
        std::vector<long long> usable;
        long long total = 0;
        for (const cv::Mat& s : set) {
            const long long u = (long long)std::max(0, s.rows - 2 * r - 1)
                              * (long long)std::max(0, s.cols - 2 * r - 1);
            usable.push_back(u);
            total += u;
        }
        count_ = (int)std::min<long long>(want, total);

        std::vector<int> share(set.size(), 0);
        long long given = 0;
        for (size_t k = 0; k < set.size() && total > 0; k++) {
            share[k] = (int)((long long)count_ * usable[k] / total);
            given += share[k];
        }
        // The rounding remainder, dealt round-robin so it depends on nothing
        // but the sizes.
        for (size_t k = 0; given < count_; k = (k + 1) % set.size())
            if (usable[k] > 0) { share[k]++; given++; }

        cv::Mat feat(count_, dim, CV_32F);
        centre_.create(count_, 1, CV_32F);
        int i = 0;
        for (size_t k = 0; k < set.size(); k++) {
            const cv::Mat& src = set[k];
            if (share[k] <= 0) continue;
            std::uniform_int_distribution<int> py(r, src.rows - r - 2),
                                               px(r, src.cols - r - 2);
            for (int j2 = 0; j2 < share[k]; j2++, i++) {
                const int y = py(rng), x = px(rng);
                float* row = feat.ptr<float>(i);
                for (int j = 0; j < dim; j++)
                    row[j] = src.at<float>(y + ring[j].y, x + ring[j].x);
                centre_.at<float>(i, 0) = src.at<float>(y, x);
            }
        }
        // Twelve dimensions is enough and more is worse — 24 over-commits,
        // matching so precisely that the field over-clumps.
        pca_ = cv::PCA(feat, cv::Mat(), cv::PCA::DATA_AS_ROW, dims);
        pca_.project(feat, proj_);
        proj_ = proj_.clone();

        // cvflann randomises the forest from the global C generator -- split
        // dimensions via rand_int, point order via random_shuffle, both on
        // std::rand() -- so without this the tree depends on how many indexes
        // the process built first, and the mask stops being a function of its
        // inputs. The cache requires that it is one.
        cvflann::seed_random((unsigned int)
            std::uniform_int_distribution<unsigned int>()(rng));
        index_ = std::make_unique<cv::flann::Index>(proj_, cv::flann::KDTreeIndexParams(4));
    }

    int size() const { return count_; }
    float centre(int i) const { return centre_.at<float>(i, 0); }

    // Row i of `nn` becomes the index of the neighbourhood nearest query row i.
    //
    // The search is about eighty per cent of a build — 4.6 s of each 4.6 s sweep
    // at 256², against 0.6 s with the checks turned down — and it splits
    // perfectly: a query's answer depends only on itself and a tree that nobody
    // writes to, and each row of the output has exactly one writer. So this is
    // the same answer on however many cores are going, and the mask is
    // byte-identical to the single-threaded build.
    //
    // Concurrent search on one index is supported rather than merely got away
    // with: cvflann's kd-tree keeps its visited-set on the stack and takes its
    // scratch heap from a pool keyed by thread id.
    //
    // **But that pool is guarded by one process-wide mutex, taken once per
    // query**, and the search does not scale past a couple of dozen threads
    // because of it. `Heap::getPooledInstance` (opencv2/flann/heap.h) locks a
    // function-static `cv::Mutex`, looks the calling thread's heap up in a
    // shared map, and walks the whole map evicting stale entries — all while
    // holding the lock. Every query from every core queues on that one lock, so
    // past the point where it saturates, another core is another waiter.
    //
    // Measured on 64 cores at a fixed 393216 queries, sweeping the stripe count
    // with every thread available: 4.7 s at 8 stripes, 2.7 at 16, **2.0 at 24**,
    // 2.0 at 32, 2.7 at 48, 3.2 at 64. Uncapped it takes the last of those.
    //
    // What is capped is *concurrency*, not chunk size, and that distinction is
    // measured rather than assumed: run the same sweep at two group sizes and
    // the optimum stays at 24–32 stripes while the rows each stripe gets moves
    // by 4x (170 against 680). A granularity rule would have tracked the rows.
    // It is the lock that saturates, and it saturates at a number of callers.
    //
    // The cap cannot be `cv::setNumThreads`, which is process-global: it would
    // quietly throttle every other parallel region in the program — the gemms
    // in `project` just above, `rank_uniform_unit`, `prepare_texture_sample`,
    // and blue's and markov's builds. `parallel_for_`'s third argument keeps it
    // to this one call.
    //
    // Byte-identity is unaffected, for the reason two paragraphs up: the answer
    // depends only on the query and on a tree nobody writes to, so how the rows
    // are divided cannot change it. The hash matrix is the check.
    static constexpr int kMaxSearchStripes = 24;

    void nearest(const cv::Mat& query, int checks, cv::Mat& nn)
    {
        cv::Mat q;
        pca_.project(query, q);
        q = q.clone();
        const int rows = query.rows;
        nn.create(rows, 1, CV_32S);
        cv::Mat dist(rows, 1, CV_32F);
        const int stripes = std::min(std::max(cv::getNumThreads(), 1),
                                     kMaxSearchStripes);
        cv::parallel_for_(cv::Range(0, rows), [&](const cv::Range& part) {
            cv::Mat nn_rows = nn.rowRange(part.start, part.end);
            cv::Mat dist_rows = dist.rowRange(part.start, part.end);
            index_->knnSearch(q.rowRange(part.start, part.end), nn_rows, dist_rows,
                              1, cv::flann::SearchParams(checks));
        }, (double)stripes);
    }

private:
    int count_ = 0;
    cv::PCA pca_;
    cv::Mat proj_, centre_;
    std::unique_ptr<cv::flann::Index> index_;
};


// The (2r+1)^2 window with the middle left out, which is the hole being filled.
std::vector<cv::Point> ring_offsets(int radius)
{
    std::vector<cv::Point> ring;
    for (int dy = -radius; dy <= radius; dy++)
        for (int dx = -radius; dx <= radius; dx++)
            if (dy || dx) ring.emplace_back(dx, dy);
    return ring;
}

// Each pixel's neighbourhood, read toroidally so the mask tiles, one row each.
void gather_queries(const cv::Mat& field, const std::vector<int>& pixels,
                           const std::vector<cv::Point>& ring, cv::Mat& query)
{
    const int H = field.rows, W = field.cols, dim = (int)ring.size();
    query.create((int)pixels.size(), dim, CV_32F);
    for (size_t i = 0; i < pixels.size(); i++) {
        const int y = pixels[i] / W, x = pixels[i] - y * W;
        float* row = query.ptr<float>((int)i);
        for (int j = 0; j < dim; j++)
            row[j] = field.at<float>(maskutil::wrap_near(y + ring[j].y, H),
                                     maskutil::wrap_near(x + ring[j].x, W));
    }
}

// This level's starting field: white noise at the coarsest, the level below
// nearest-upsampled at every other. Nearest rather than interpolated because the
// sweeps are about to decide every pixel anyway, and a smooth start would only
// be a smooth thing for them to agree with.
static cv::Mat start_field(const cv::Mat& previous, int W, int H, std::mt19937_64& rng)
{
    if (!previous.empty()) {
        cv::Mat up;
        cv::resize(previous, up, cv::Size(W, H), 0, 0, cv::INTER_NEAREST);
        return up;
    }
    cv::Mat field(H, W, CV_32F);
    std::uniform_real_distribution<float> uni(0.0f, 1.0f);
    for (int i = 0; i < H * W; i++) field.ptr<float>()[i] = uni(rng);
    return field;
}

// One level: decide every pixel from the database, `iters` times over.
//
// Each sweep is split into `groups` in-place passes so that later pixels
// condition on the values earlier ones just took. Deciding every pixel from the
// previous sweep instead leaves each individually plausible and neighbouring
// pairs inconsistent, which cost 19.6 against 15.6 on test/patchfit.
// Returns the number of sweeps actually run, which is `p.iters` unless the
// coherence cap stopped it early.
int sweep_level(cv::Mat& field, PatchDatabase& db,
                        const std::vector<cv::Point>& ring, const InpaintParams& p,
                        std::mt19937_64& rng, std::vector<int>& order,
                        double refCoherence, const std::vector<double>& refBands)
{
    std::vector<int> all(field.rows * field.cols);
    std::iota(all.begin(), all.end(), 0);
    std::vector<int> group;
    cv::Mat query, nn;
    int done = 0;
    for (int it = 0; it < p.iters; it++) {
        std::shuffle(all.begin(), all.end(), rng);
        for (int g = 0; g < p.groups; g++) {
            group.clear();
            for (size_t i = (size_t)g; i < all.size(); i += (size_t)p.groups)
                group.push_back(all[i]);
            if (group.empty()) continue;
            gather_queries(field, group, ring, query);
            db.nearest(query, p.checks, nn);
            for (size_t i = 0; i < group.size(); i++)
                field.ptr<float>()[group[i]] = db.centre(nn.at<int>((int)i, 0));
        }
        // Spectrum matching: rescale the field's power in each of p.bands
        // frequency bands to the sample's at this level, keeping the phases the
        // sweep produced (maskutil.h). Empty, and so skipped, when p.bands is 0.
        maskutil::impose_band_power(field, refBands);
        maskutil::rank_uniform_unit(field, order);
        done = it + 1;

        // Sweeping only ever adds structure, and the gain scales with what is
        // already there, so on a directional sample the loop runs away. Stop
        // once the field is more directional than its sample by `cap`.
        //
        // The absolute excess is not belt and braces: on an isotropic sample
        // both sides are near zero and the ratio compares two noise values.
        // See CLAUDE.md for the measurements behind both constants.
        constexpr double kMinExcess = 0.05;
        if (p.cap > 0 && refCoherence > 0) {
            const double fc = maskutil::field_coherence(field);
            if (fc > refCoherence * p.cap && fc - refCoherence > kMinExcess) break;
        }
    }
    return done;
}
} // namespace

double inpaint_work(int w, int h, int iters, int db, int dims, int scales, int checks)
{
    const double lg = (double)std::max(checks, 1);
    double pixels = 0, builds = 0;
    for (int l = 0; l < scales; l++) {
        const double f = (double)(1 << (scales - 1 - l));
        pixels += (double)w * h / (f * f);
        builds += (double)db * dims * std::log2(std::max(db, 2));
    }
    return iters * pixels * dims * lg + builds;
}




// The synthesis itself, with nothing of the mask about it: a prepared field in,
// a field of the same character out. `build_inpaint_mask` is this plus the two
// steps that make the result a *mask*, and `maketexture` is this plus the one
// step that makes it a texture — which is what lets that tool exercise exactly
// the code a dither run uses rather than a copy of it.
//
// The returned field is rank-uniform on [0,1], because the sweeps hold it that
// way; what a caller does with that ordering is the caller's business.
cv::Mat synthesise_inpaint_field(cv::Size sz, const cv::Mat& input,
                                 const InpaintParams& p, uint64_t seed,
                                 std::ostream* log)
{
    return synthesise_inpaint_field(sz, samples::Set::of(input), p, seed, log);
}

cv::Mat synthesise_inpaint_field(cv::Size sz, const samples::Set& prepared_in,
                                 const InpaintParams& p, uint64_t seed,
                                 std::ostream* log, const samples::Set* referencePrepared)
{
    // Ranked flat here rather than by the caller, because the sweeps hold the
    // field flat too and the two have to be in the same space: the field is
    // matched against neighbourhoods cut from this, in a PCA basis fitted on
    // them. `downsampled` returns the scans untouched at the finest level, so
    // leaving this to the caller silently gave that level an unranked sample.
    //
    // Ranked over every scan at once, so a value means the same thing in all of
    // them and a neighbourhood from one can match a neighbourhood from another.
    const samples::Set prepared = prepared_in.ranked_unit();
    const samples::Set reference = referencePrepared && !referencePrepared->empty()
                                 ? referencePrepared->ranked_unit() : prepared;

    const std::vector<cv::Point> ring = ring_offsets(p.radius);
    const int dims = std::min(p.dims, (int)ring.size());

    std::mt19937_64 rng(seed);
    cv::Mat field;                       // carried from level to level
    std::vector<int> order;

    for (int level = 0; level < p.scales; level++) {
        const int f = 1 << (p.scales - 1 - level);
        if (sz.width % f || sz.height % f) {
            if (log) *log << "  mask size " << sz.width << "x" << sz.height
                          << " does not divide by " << f << ", which level 1/" << f
                          << " needs\n";
            return {};
        }
        // Both coarse sizes checked here, before anything is built from
        // either. The sample's was checked after downsample had already
        // allocated and averaged a whole image to hand it a reason to stop.
        // The smallest scan governs: one database is cut from all of them, and
        // a scan too small at this level can offer no neighbourhood at all.
        const cv::Size least = prepared.smallest();
        const int srcH = least.height / f, srcW = least.width / f;
        if (srcH < 2 * p.radius + 4 || srcW < 2 * p.radius + 4) {
            if (log) *log << "  the smallest scan is " << srcW << "x" << srcH
                          << " at level 1/" << f << ", too small for a radius-"
                          << p.radius << " neighbourhood\n";
            return {};
        }

        const int H = sz.height / f, W = sz.width / f;

        // And the *field*, which nothing checked at all.
        //
        // The neighbourhood is read with wrap_near, which wraps an index at
        // most one period out -- a deliberate choice, since it is the inner
        // loop. On a field narrower than the neighbourhood that assumption
        // fails: a 16x16 mask at 5 levels starts at 1x1, and offset -3 on a
        // width of 1 gives wrap_near(-3, 1) = -2, which addresses outside the
        // field. Every one of those settings is inside its registered range,
        // and the observed end was an assertion inside cvflann.
        //
        // Refused rather than switched to wrap_any, because the model is the
        // reason. A neighbourhood that wraps onto itself offers the same pixel
        // twice as though it were two pieces of evidence, so the statistics it
        // feeds are not the statistics anyone asked for -- a wider wrap would
        // make the crash go away and leave the answer meaningless.
        if (W < 2 * p.radius + 1 || H < 2 * p.radius + 1) {
            if (log) *log << "  at level 1/" << f << " the field is " << W << "x" << H
                          << ", smaller than the " << (2 * p.radius + 1) << "x"
                          << (2 * p.radius + 1) << " neighbourhood it is read through, so "
                             "each pixel would be its own neighbour. Use a larger "
                             "--mask-size, fewer levels (--set inpaint.scales= below "
                          << p.scales << "), or a smaller --set inpaint.radius=\n";
            return {};
        }

        const samples::Set src = downsample_set(prepared, f);
        PatchDatabase db(src, ring, p.radius, p.db, dims, rng);

        field = start_field(field, W, H, rng);
        maskutil::rank_uniform_unit(field, order);

        // **Only the finest level is capped**, and the coarse ones are exempt
        // for a measured reason rather than for tidiness. `src` at a coarse
        // level is the sample *downsampled*, which destroys the grain it is
        // supposed to describe: Kodak-4X-1's coarse reference reads 0.020
        // against 0.216 at full resolution, a tenfold drop, while its field
        // reads 0.042 at both. Ratios against a reference that near zero are
        // meaningless, and they fire on exactly the films that are fine --
        // 4X-1 at 2.13 and Tri-X-032a at 2.07, neither of which has anything
        // wrong with it. The coarse levels are scaffolding; the finished field
        // is what has to match the sample.
        const double refCoherence = (f == 1) ? set_coherence(src) : 0.0;
        // What the spectrum matching aims at on this level: the reference at
        // this level's resolution -- block-averaged at a coarse level, as the
        // database is -- measured in p.bands frequency bands.
        std::vector<double> refBands;
        if (p.bands > 0) {
            const samples::Set r = downsample_set(reference, f);
            refBands = maskutil::band_reference(std::vector<cv::Mat>(r.begin(), r.end()),
                                                cv::Size(W, H), p.bands,
                                                maskutil::kReferenceEqualise * p.scale / f);
        }
        const int ran = sweep_level(field, db, ring, p, rng, order, refCoherence, refBands);
        if (log) {
            *log << "  level 1/" << f << ": " << W << "x" << H << ", " << ran
                 << " sweeps over " << db.size() << " neighbourhoods";
            // Said out loud, because a run that quietly did less than it was
            // asked is the shape of a silently dropped option.
            if (ran < p.iters)
                *log << " (stopped short of " << p.iters
                     << ": the field reached " << p.cap << "x the sample's "
                     << "directionality, which --inpaint-cap bounds)";
            *log << "\n";
        }
    }

    return field;
}

cv::Mat build_inpaint_mask(cv::Size sz, const samples::Set& set,
                           const InpaintParams& p, uint64_t seed,
                           std::ostream* log, const samples::Set* reference)
{
    const samples::Set refPrepared = reference && !reference->empty()
                                   ? reference->prepared(p.highpass, p.scale) : samples::Set();
    const cv::Mat field = synthesise_inpaint_field(
        sz, set.prepared(p.highpass, p.scale), p, seed, log,
        refPrepared.empty() ? nullptr : &refPrepared);
    if (field.empty()) return {};
    // The mask's own finishing step: put the ordering on the flat 0..254 scale
    // `MaskDither` thresholds against. maketexture puts the same ordering on the
    // sample's own values instead, which is the only difference between them.
    return maskutil::threshold_mask(maskutil::rank_order(field), sz);
}

void register_inpaint_params(params::Params& P)
{
    using namespace inpaint::keys;
    const auto mine = mask::generator("inpaint");

    params::add_const(P, Radius, (long long)inpaint::fallback::Radius,
                      mine, {{1, 8}},
                      "neighbourhood half-width; 4 and 5 both measure worse");
    // 2, by way of 6 and 3. Sweeps *worm* — see the header — so the count is a
    // quality-against-quality trade rather than a convergence one, and 6 was
    // fitted on the one film that likes it. 3 was measured with the high-pass
    // on; without it, test/patchfit against each film's crop prefers 2 on five
    // films of six and ties on the sixth (Kodak-4X 113 -> 79, 2484 48 -> 34,
    // TriX 153 -> 135, OldTriX 113 -> 87, Delta 51 -> 44, PanatomicX 61.6 / 61.7),
    // and inpaint's calibration ranks 2 first on the same five.
    params::add_const(P, Iters, (long long)inpaint::fallback::Iters,
                      mine, {{1, 64}}, "passes over the field, per level");
    // Spectrum matching after each sweep, in this many frequency bands; see
    // maskutil.h, "Spectrum matching", for what it does and why.
    params::add_const(P, Bands, (long long)inpaint::fallback::Bands,
                      mine, {{0, 64}},
                      "frequency bands each sweep's spectrum is matched to the sample's in; 0 turns matching off");
    params::add_const(P, Db, (long long)inpaint::fallback::Db,
                      mine, {{1000, 4000000}},
                      "sample neighbourhoods to match against; the binding constraint");
    params::add_const(P, Dims, (long long)inpaint::fallback::Dims,
                      mine, {{2, 64}}, "PCA dimensions the match is made in");
    // The kd-tree is approximate, and how approximate is the one knob that
    // trades quality for time here rather than changing what is being asked for.
    //
    // 64 rather than 128: across six films it is a wash on quality — the largest
    // move either way is 4% — and it is about a quarter off the search, which is
    // most of a build. The old 128 came from 32/128/512 measuring 11.9/9.4/10.8
    // on one film, where the flatness past 128 was read as the exact answer
    // having been reached. On a wider set the curve is flat from 64 up, so the
    // reading was right and the film was not representative of where it flattens.
    params::add_const(P, Checks, (long long)inpaint::fallback::Checks,
                      mine, {{1, 100000}}, "kd-tree leaves visited per query");
    params::add_const(P, Groups, (long long)inpaint::fallback::Groups,
                      mine, {{1, 256}},
                      "in-place passes a sweep is split into; 1 is all-at-once and worse");
    // `scales` rather than `levels`, which it was: --levels is the number of
    // output tones and this is the number of coarse-to-fine resolutions, two
    // unrelated things one word apart on the command line.
    params::add_const(P, Scales, (long long)inpaint::fallback::Scales,
                      mine, {{1, 5}},
                      "coarse-to-fine resolutions, so a small window still reaches coarse structure");

    // 1.25, with both ends measured. Of eleven films, only the two whose grain
    // carries a strong direction exceed 1.0 after a single sweep (1.30 and
    // 1.10); the other nine reach 1.25 at the earliest on their final sweep,
    // where breaking changes nothing. Lower than about 1.2 and it starts
    // cutting films that are fine short; higher than about 1.3 and it stops
    // catching 274a, which is the case it exists for.
    params::add_const(P, Cap, inpaint::fallback::Cap,
                      mine, {{0.0, 100.0}},
                      "stop sweeping once the field is this much more directional "
                      "than the sample; 0 never stops early");

    // 256 like markov, and for the same reason: this costs 13/40/146 seconds at
    // 128/256/512 on the machine it was written on, so the cap is a cost limit
    // rather than a quality one. --budget replaces it with a real measurement.
    params::add_const(P, MaxSize, (long long)256,
                      mine, {{8, 2048}}, "largest tile this generator will build unasked");
    params::add_const(P, MaxRepeats, (long long)4,
                      mine, {{1, 1024}}, "how often its tile may repeat before that shows");
    // Now that the search runs on every core, this is the one rate here that
    // depends on how many there are: single-threaded it measured 2.2e7 across
    // all three sizes, and on a 64-core machine it reads 0.99/1.1/1.4e8 at
    // 128/256/512 — no longer flat, because the serial setup is a bigger share
    // of a small build. 6e7 is deliberately between the two rather than either:
    // it is a first guess for a machine nobody has measured, and one real build
    // replaces it with that machine's own figure.
    params::add_const(P, Rate, 6.0e7,
                      mine, {{1.0, 1e15}},
                      "work units per second, measured from real builds",
                      true);   // an intermediate: reaches the mask only via --budget

    // Measured from this machine's own builds, never chosen, so it is
    // reachable by --set and is not offered as an option.
    P.hide_option(inpaint::keys::Rate);
}
