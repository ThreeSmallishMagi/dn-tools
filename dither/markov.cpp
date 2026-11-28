// SPDX-License-Identifier: GPL-3.0-or-later
// markov.cpp — synthesise a threshold mask from a sample's local statistics.
//
// The model is a pairwise Markov random field. A pixel's neighbourhood
// constrains what it can be, and that constraint is stored not as one big
// conditional over the whole neighbourhood — which is unfittable, 11^120 cells
// for a +-5 window — but as a set of small ones, each conditioned on a *pair* of
// neighbours. A pair carries what a single neighbour cannot: ((x-2,y),(x-1,y))
// sees a gradient, ((x-1,y),(x+1,y)) sees whether the centre sits in a trough or
// on a ridge. Each such table is a cheap expert on the centre pixel, and the
// field is Gibbs-sampled from their combined verdict.
//
// Two things have to be decided rather than tabulated:
//
//   Which pairs. There are 7140 of them inside +-5, and most say nothing: a pair
//   is uninteresting exactly when the distribution it collects is the overall
//   pixel distribution, which is mutual information with the centre pixel at
//   zero. So MI is the score, and it is the whole of what "attention" amounts to
//   here. It is not the whole selection *rule*, though: MI falls off with
//   distance and with departure from the sample's strongest axis, so ranking by
//   it alone buys sixty near-duplicates of the nearest horizontal pair. The rule
//   below stratifies by distance, works in symmetry orbits, and penalises
//   re-using an offset already spoken for.
//
//   How to combine them. Flat 1/K, and this is measured rather than assumed.
//   Weighting by what each expert knows is the obvious move, and fitting the
//   weights by maximum likelihood is the principled version of it — both were
//   tried (`--mask-weights mi` and `ml`). ML weights predict the sample's own
//   pixels 0.09 nats better than flat, and both concentrate about 68% of the
//   vote on the better half of the pairs. Both then make the *texture* worse:
//   at each scheme's own best temperature, level-set fit reads flat 0.198,
//   mi 0.226, ml 0.239, and directionality drifts from the sample's -0.2 dB to
//   +0.2 .. +0.7. The reason is that a pair scores low precisely when it is far
//   away or off-axis, which is also when its information is least redundant with
//   everything else. Prediction rewards piling weight on the strong, mutually
//   redundant near pairs; the texture needs the long-range and off-axis
//   constraints that only the weak pairs carry. Down-weighting them is buying
//   accuracy with the very thing being synthesised.
//
// What this captures that the dft method cannot is higher-order structure: the
// spectral method randomises phase and so keeps only how much of each frequency
// there is, while a conditional table sees the shape of a neighbourhood. Against
// a film sample that shows up in the level sets — film clumps hard in the
// shadows and barely at all in the highlights, an asymmetry dft flattens
// completely and this reproduces about half of. What it gives up is the spectrum
// itself, which dft matches to a tenth of the error, and exactness at long
// range: beyond `radius` the model has nothing to say. `temp` slides between the
// two; it is the one scalar that decides the outcome, and it cannot be chosen by
// likelihood — see the note on calibration in CLAUDE.md.

#include "markov.h"
#include "maskparams.h"
#include "markovmodel.h"
#include "maskutil.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <memory>
#include <numeric>
#include <random>
#include <string>
#include <vector>
#include <opencv2/imgproc.hpp>

namespace {

// How many candidate pairs the greedy selection should have to choose from, as a
// multiple of the pairs actually wanted. The shortlist is sized to hit this (see
// the markov.shortlist rule), so it grows with --markov-pairs instead of capping
// it. 12 is what the old fixed shortlist of 40 happened to give at the default
// 64 pairs, so the default selection is unchanged.
constexpr double kPoolFactor = 12.0;
// Total reuse penalty one offset may accumulate, which markov.reuse holds fixed
// as the pair count moves it. Pinned so the rule returns 0.35 at 64 pairs.
constexpr double kReuseBudget = 1.12;

// Counts per table cell to aim for when thinning the sample. Past this the
// estimate stops improving and the screening pass just costs more.
constexpr double kCountsPerCell = 24.0;

// Pseudo-counts mixing each conditioning cell toward the marginal. A cell the
// sample never visited then predicts the marginal — i.e. says nothing — rather
// than predicting noise.
constexpr float kPrior = 8.0f;

// Penalty for re-using an offset already selected, per use. Without it the
// greedy pick returns the same two or three high-MI neighbours in every pair.



// Rank values into exactly-flat bin indices: every bin holds n/nbins of the
// pixels. Both the sample and the field in progress are quantised this way, so
// the tables are fitted on the same flat marginal a threshold mask is required
// to have — the model's marginal and the mask's constraint are the same thing.
// Every scan quantised at both resolutions, ranked over all of them together.
// See markovmodel.h for why the rank is pooled rather than per scan.
//
// For one scan this is exactly a stable sort of its pixels, which is what the
// single-Mat version did, so a single-sample mask is unchanged.
Quantised quantise_set(const samples::Set& fields, int Qc, int Qv)
{
    CV_Assert(Qc > 0 && Qc <= 256 && Qv > 0 && Qv <= 256);
    const samples::Set::Ordering o = fields.ordering();
    const long long n = (long long)o.order.size();

    Quantised q;
    for (const cv::Mat& f : fields) {
        q.qn.emplace_back(f.size(), CV_8U);
        q.qv.emplace_back(f.size(), CV_8U);
    }
    // Every rank writes its own pixel, so the ranks can be split between threads.
    cv::parallel_for_(cv::Range(0, (int)n), [&](const cv::Range& range) {
        for (long long r = range.start; r < range.end; r++) {
            const size_t g = (size_t)o.order[(size_t)r];
            const size_t s = o.scan_of(g);
            const size_t off = g - o.base[s];
            q.qn[s].ptr<uint8_t>()[off] = (uint8_t)(r * Qc / n);
            q.qv[s].ptr<uint8_t>()[off] = (uint8_t)(r * Qv / n);
        }
    });
    return q;
}

// The columns `part` selects, for one scan. The split is per scan because the
// scans have different widths.
inline void column_span(const cv::Mat& m, Columns part, int radius, int& lo, int& hi)
{
    const int mid = m.cols / 2;
    lo = (part == Columns::Right) ? std::max(mid + radius, radius) : radius;
    hi = (part == Columns::Left)  ? std::min(mid - radius, m.cols - radius)
                                  : m.cols - radius;
}

// High-pass to leave the grain rather than the subject (the same (1 - Gaussian)
// and the same half-power convention as spectralmask.cpp and maskstats), then
// resample to put the sample's features at the requested size.

// Mutual information, in nats, of a joint count table laid out as [cell][centre].
// The estimator is biased upward by about (nb-1)(nv-1)/(2*total), but that is the
// same for every candidate of a given table shape, so the ranking is unaffected.
double mutual_information(const std::vector<float>& n, int nb, int nv)
{
    double total = 0;
    for (size_t i = 0; i < (size_t)nb * nv; i++) total += n[i];
    if (total <= 0) return 0.0;

    // Both marginals in one pass. The row sums were previously recomputed inside
    // the loop that uses them, after the column sums had already walked the
    // same table.
    std::vector<double> pv(nv, 0.0), pb(nb, 0.0);
    for (int b = 0; b < nb; b++)
        for (int c = 0; c < nv; c++) {
            const double p = n[(size_t)b * nv + c] / total;
            pv[c] += p;
            pb[b] += p;
        }

    double mi = 0;
    for (int b = 0; b < nb; b++) {
        if (pb[b] <= 0) continue;
        for (int c = 0; c < nv; c++) {
            double pbc = n[(size_t)b * nv + c] / total;
            if (pbc > 0 && pv[c] > 0) mi += pbc * std::log(pbc / (pb[b] * pv[c]));
        }
    }
    return mi;
}

// Count centre values against one or two quantised neighbours. The sample is not
// toroidal, so the border is skipped rather than wrapped: wrapping would invent
// neighbourhoods that never occurred in it. `step` thins the centres visited —
// every candidate pair costs a pass, and past a few dozen counts per cell more
// of the same sample buys nothing but time.
// `x0`/`x1` restrict the columns visited, which is what lets the report fit on
// one strip of the sample and score itself on another.
void tally(const Quantised& q, int Qv, int radius, int step,
           Offset d1, const Offset* d2, int Qc, std::vector<float>& n,
           Columns part = Columns::All)
{
    std::fill(n.begin(), n.end(), 0.0f);
    for (size_t s = 0; s < q.size(); s++) {
        const cv::Mat& qn = q.qn[s];
        const cv::Mat& qv = q.qv[s];
        int lo, hi;
        column_span(qn, part, radius, lo, hi);
        for (int y = radius; y < qn.rows - radius; y += step)
            for (int x = lo; x < hi; x += step) {
                size_t cell = qn.at<uint8_t>(y + d1.dy, x + d1.dx);
                if (d2) cell = cell * Qc + qn.at<uint8_t>(y + d2->dy, x + d2->dx);
                n[cell * Qv + qv.at<uint8_t>(y, x)] += 1.0f;
            }
    }
}

// Blur a strided axis of a 3D table with [1 2 1]/4, edges replicated. Applied to
// all three axes it is the smoothing that lets neighbouring conditioning cells
// share evidence — adjacent quantisation bins mean nearly the same thing, so
// treating their counts as independent throws away most of the sample.
void blur_axis(std::vector<float>& t, int outer, int len, int inner)
{
    if (len < 3) return;
    std::vector<float> line(len);
    for (int o = 0; o < outer; o++)
        for (int j = 0; j < inner; j++) {
            const size_t base = (size_t)o * len * inner + j;
            for (int i = 0; i < len; i++) line[i] = t[base + (size_t)i * inner];
            for (int i = 0; i < len; i++)
                t[base + (size_t)i * inner] = 0.25f * line[std::max(i - 1, 0)]
                                            + 0.50f * line[i]
                                            + 0.25f * line[std::min(i + 1, len - 1)];
        }
}

// One fitted pair: where it looks, how much it knows, and log P(centre | pair).
// `mi` selected it and is then only reported — see the header on why it must not
// also weight it.
// A fitted pair: the two offsets it conditions on, its mutual information with
// the centre, and its table. Deliberately no weight — that is a property of the
// call, not of the fit, and lives beside the model rather than inside it.


// The eight symmetries of the square, as maps on an offset. Selection works in
// whole orbits under this group, because greedy MI on its own does not: the
// sample's strongest axis wins every pick and the chosen set ends up (measurably)
// nine tenths horizontal, which costs the finished mask about 1.7 dB of spurious
// directionality. That is the smallest of the three levers on directionality —
// see the note on `temp`, which is much the largest — but it is also the one that
// is free. Taking orbits balances the *coverage*
// only: each image gets its own table, fitted to what the sample actually does
// in that direction, so a genuinely directional sample still comes out
// directional — through the tables, rather than through how many times its
// direction happened to get picked.
inline Offset symmetry(Offset d, int k)
{
    int dy = d.dy, dx = d.dx;
    if (k & 4) std::swap(dy, dx);
    return {(k & 1) ? -dy : dy, (k & 2) ? -dx : dx};
}

// The offsets inside the neighbourhood, plus the reverse lookup the orbit walks
// need.
struct Neighbourhood {
    int radius, side;
    std::vector<Offset> off;
    std::vector<int> at;            // (dy, dx) -> index into off, -1 for the centre

    explicit Neighbourhood(int r) : radius(r), side(2 * r + 1), at(side * side, -1) {
        for (int dy = -r; dy <= r; dy++)
            for (int dx = -r; dx <= r; dx++)
                if (dy || dx) {
                    at[(dy + r) * side + (dx + r)] = (int)off.size();
                    off.push_back({dy, dx});
                }
    }
    int index(Offset d) const {
        if (std::abs(d.dy) > radius || std::abs(d.dx) > radius) return -1;
        return at[(d.dy + radius) * side + (d.dx + radius)];
    }
};

// Shortlist the offsets pairs may be drawn from: the best orbit in each distance
// band, then the next best, and so on. Round-robin over the bands rather than
// straight top-k, because mutual information falls off with distance — ranked
// flat, every survivor is a distance-1 neighbour, and a model that only sees
// distance 1 cannot reproduce grain any coarser than that.
std::vector<int> shortlist_offsets(const Neighbourhood& nb,
                                   const std::vector<double>& mi, int want)
{
    std::vector<int> byMi(nb.off.size());
    std::iota(byMi.begin(), byMi.end(), 0);
    std::stable_sort(byMi.begin(), byMi.end(),
                     [&](int a, int b) { return mi[a] > mi[b]; });

    std::vector<char> in(nb.off.size(), 0);
    std::vector<int> keep;
    auto add_orbit = [&](int i) {
        for (int k = 0; k < 8; k++) {
            int j = nb.index(symmetry(nb.off[i], k));
            if (j >= 0 && !in[j]) { in[j] = 1; keep.push_back(j); }
        }
    };
    for (bool progress = true; progress && (int)keep.size() < want; ) {
        progress = false;
        for (int band = 1; band <= nb.radius && (int)keep.size() < want; band++)
            for (int i : byMi)
                if (nb.off[i].band() == band && !in[i]) { add_orbit(i); progress = true; break; }
    }
    return keep;
}

struct Cand { int a, b; double mi; };   // a, b index the shortlist

// Greedy pick with a re-use penalty, emitting each winner's whole symmetry orbit.
// Optionally restricted to pairs reaching past `nearBand`: called once to fill a
// reserved far-pair quota, then once unrestricted for the rest. Without the
// reservation the near pairs — which always win on MI — take every slot, and
// grain any coarser than two pixels never gets modelled.
// The unpicked candidate with the best score: its mutual information, damped by
// how often its two offsets have already been used, which is what stops the
// selection returning sixty views of the same strong direction.
int best_unused_candidate(const std::vector<Cand>& cands, const Neighbourhood& nb,
                          const std::vector<int>& shortlist, const std::vector<int>& uses,
                          const std::vector<char>& used, int nearBand, bool farOnly,
                          double reuse)
{
    int best = -1;
    double bestScore = 0;
    for (size_t i = 0; i < cands.size(); i++) {
        if (used[i]) continue;
        const Offset& oa = nb.off[shortlist[cands[i].a]];
        const Offset& ob = nb.off[shortlist[cands[i].b]];
        if (farOnly && oa.band() <= nearBand && ob.band() <= nearBand) continue;
        const double s = cands[i].mi / (1.0 + reuse * (uses[cands[i].a] + uses[cands[i].b]));
        if (best < 0 || s > bestScore) { best = (int)i; bestScore = s; }
    }
    return best;
}

// Take a candidate's whole orbit under the eight symmetries of the square, not
// just the candidate. Greedy MI on its own returns ~90% horizontal pairs on a
// film scan whose horizontal MI genuinely runs 2x the vertical, and the finished
// mask inherits that; taking orbits moved it from +5.7 dB to +4.0.
void add_symmetry_orbit(int seed_cand, const std::vector<Cand>& cands,
                        const std::vector<int>& cellOf, const Neighbourhood& nb,
                        const std::vector<int>& shortlist, const std::vector<int>& place,
                        int want, std::vector<int>& uses, std::vector<char>& used,
                        std::vector<int>& out)
{
    const int n = (int)shortlist.size();
    for (int k = 0; k < 8 && (int)out.size() < want; k++) {
        const int ia = nb.index(symmetry(nb.off[shortlist[cands[seed_cand].a]], k));
        const int ib = nb.index(symmetry(nb.off[shortlist[cands[seed_cand].b]], k));
        if (ia < 0 || ib < 0) continue;
        const int pa = place[ia], pb = place[ib];     // back to shortlist positions
        if (pa < 0 || pb < 0 || pa == pb) continue;
        const int ci = cellOf[std::min(pa, pb) * n + std::max(pa, pb)];
        if (ci < 0 || used[ci]) continue;
        used[ci] = 1;
        uses[cands[ci].a]++;
        uses[cands[ci].b]++;
        out.push_back(ci);
    }
}

void pick_pairs(const std::vector<Cand>& cands, const std::vector<int>& cellOf,
                const Neighbourhood& nb, const std::vector<int>& shortlist,
                const std::vector<int>& place, int nearBand, bool farOnly,
                int want, double reuse, std::vector<int>& uses,
                std::vector<char>& used, std::vector<int>& out)
{
    while ((int)out.size() < want) {
        const int best = best_unused_candidate(cands, nb, shortlist, uses, used,
                                               nearBand, farOnly, reuse);
        if (best < 0) return;
        add_symmetry_orbit(best, cands, cellOf, nb, shortlist, place, want,
                           uses, used, out);
        used[best] = 1;                               // even if its orbit filled up
    }
}

// Turn a count table into log P(centre | cell), the same smoothing and prior the
// fitted experts get. Split out so the report can refit on a strip of the sample
// without duplicating the recipe.
void to_logp(std::vector<float>& tbl, int cells, int Qc, int Qv,
             std::vector<float>& logp)
{
    blur_axis(tbl, 1,     Qc, Qc * Qv);
    blur_axis(tbl, Qc,    Qc, Qv);
    blur_axis(tbl, cells, Qv, 1);
    logp.resize((size_t)cells * Qv);
    for (int b = 0; b < cells; b++) {
        double sum = 0;
        for (int c = 0; c < Qv; c++) sum += tbl[(size_t)b * Qv + c];
        for (int c = 0; c < Qv; c++) {
            double q = (tbl[(size_t)b * Qv + c] + kPrior / Qv) / (sum + kPrior);
            logp[(size_t)b * Qv + c] = (float)std::log(q);
        }
    }
}

// Mean negative log-likelihood, in nats, that the pooled model assigns to the
// sample's own centre pixels over a strip of columns. This is the model exactly
// as the Gibbs sweep uses it — same weights, same softmax — so it measures what
// is actually sampled from rather than any single expert.
double pooled_logloss(const Quantised& q, int Qv, int Qc,
                      int radius, int step, const std::vector<Expert>& experts,
                      const std::vector<double>& weights,
                      const std::vector<std::vector<float>>& logp, Columns part)
{
    std::vector<double> acc(Qv);
    double total = 0;
    long count = 0;
    for (size_t s = 0; s < q.size(); s++) {
    const cv::Mat& qn = q.qn[s];
    const cv::Mat& qv = q.qv[s];
    int lo, hi;
    column_span(qn, part, radius, lo, hi);
    for (int y = radius; y < qn.rows - radius; y += step)
        for (int x = lo; x < hi; x += step) {
            std::fill(acc.begin(), acc.end(), 0.0);
            for (size_t k = 0; k < experts.size(); k++) {
                int b1 = qn.at<uint8_t>(y + experts[k].d1.dy, x + experts[k].d1.dx);
                int b2 = qn.at<uint8_t>(y + experts[k].d2.dy, x + experts[k].d2.dx);
                const float* lp = &logp[k][((size_t)b1 * Qc + b2) * Qv];
                for (int c = 0; c < Qv; c++) acc[c] += weights[k] * lp[c];
            }
            double m = *std::max_element(acc.begin(), acc.end()), sum = 0;
            for (int c = 0; c < Qv; c++) sum += std::exp(acc[c] - m);
            total -= acc[qv.at<uint8_t>(y, x)] - m - std::log(sum);
            count++;
        }
    }
    return count ? total / count : 0.0;
}

// Fit the experts' weights by maximum likelihood on the sample's own centre
// pixels, holding the total sum fixed so only the *direction* is learned — the
// scale is temperature, and temperature is not a likelihood question (sharpening
// every conditional always improves one-step prediction, but the Gibbs chain
// that consumes them then over-couples). Concave in w, so plain projected
// gradient ascent reaches the global optimum. The gradient is the usual
// exponential-family one: observed feature minus its expectation under the pool.
std::vector<double> fit_weights_ml(const Quantised& quant, int Qv, int Qc,
                                   int radius, int step, double total,
                                   const std::vector<Expert>& experts,
                                   const std::vector<std::vector<float>>& logp,
                                   int iters = 30)
{
    const size_t K = experts.size();
    std::vector<double> w(K, total / K), grad(K), acc(Qv), q(Qv);
    const int gstep = step * 3;                 // the gradient tolerates a thinner pass
    for (int it = 0; it < iters; it++) {
        std::fill(grad.begin(), grad.end(), 0.0);
        long n = 0;
        for (size_t si = 0; si < quant.size(); si++) {
        const cv::Mat& qn = quant.qn[si];
        const cv::Mat& qv = quant.qv[si];
        for (int y = radius; y < qn.rows - radius; y += gstep)
            for (int x = radius; x < qn.cols - radius; x += gstep) {
                std::fill(acc.begin(), acc.end(), 0.0);
                for (size_t k = 0; k < K; k++) {
                    int b1 = qn.at<uint8_t>(y + experts[k].d1.dy, x + experts[k].d1.dx);
                    int b2 = qn.at<uint8_t>(y + experts[k].d2.dy, x + experts[k].d2.dx);
                    const float* lp = &logp[k][((size_t)b1 * Qc + b2) * Qv];
                    for (int c = 0; c < Qv; c++) acc[c] += w[k] * lp[c];
                }
                double m = *std::max_element(acc.begin(), acc.end()), s = 0;
                for (int c = 0; c < Qv; c++) { q[c] = std::exp(acc[c] - m); s += q[c]; }
                for (int c = 0; c < Qv; c++) q[c] /= s;
                const int truth = qv.at<uint8_t>(y, x);
                for (size_t k = 0; k < K; k++) {
                    int b1 = qn.at<uint8_t>(y + experts[k].d1.dy, x + experts[k].d1.dx);
                    int b2 = qn.at<uint8_t>(y + experts[k].d2.dy, x + experts[k].d2.dx);
                    const float* lp = &logp[k][((size_t)b1 * Qc + b2) * Qv];
                    double expect = 0;
                    for (int c = 0; c < Qv; c++) expect += q[c] * lp[c];
                    grad[k] += lp[truth] - expect;
                }
                n++;
            }
        }
        if (!n) break;
        // Step, clip to non-negative, then rescale to the fixed total: the
        // projection that keeps this a search over direction alone.
        double gnorm = 0;
        for (double g : grad) gnorm += g * g / (double)(n * n);
        gnorm = std::sqrt(gnorm) + 1e-12;
        const double rate = 0.5 * total / (K * gnorm);
        double sum = 0;
        for (size_t k = 0; k < K; k++) {
            w[k] = std::max(0.0, w[k] + rate * grad[k] / n);
            sum += w[k];
        }
        if (sum <= 0) { std::fill(w.begin(), w.end(), total / K); break; }
        for (double& v : w) v *= total / sum;
    }
    return w;
}


// Subtract a toroidal Gaussian blur from the field in place. Padding the way
// spectralmask.cpp does, because the field wraps and reflecting at the seam
// would put a fold in the finished mask.
void highpass_field(std::vector<float>& field, int H, int W, double px)
{
    cv::Mat f(H, W, CV_32F, field.data());
    maskutil::highpass_toroidal(f, px);
}

// What select_pairs decided: the pairs, and how much of the neighbourhood it
// actually drew them from. The second used to come back through an out
// parameter, which reads as an afterthought and does not scale if another
// diagnostic is ever wanted.
struct PairSelection {
    std::vector<std::pair<Offset, Offset>> pairs;
    size_t shortlist_used = 0;
};

// Choose which neighbour pairs will condition each pixel: screen single offsets
// by mutual information, shortlist them in symmetry orbits across distance
// bands, then greedily pick pairs from among those. All the index bookkeeping
// that needs — which offset sits where in the shortlist, which pair sits where
// in the candidate list — lives and dies in here, and the caller gets offsets.
PairSelection
select_pairs(const Quantised& q, int Qv, int Qc, int cells,
             int radius, int step, const Neighbourhood& nb,
             int wantPairs, int shortlistSize, double reuse)
{
    // Screen single offsets, then form pairs only among the survivors.
    std::vector<float> tbl((size_t)cells * Qv);
    std::vector<double> miSingle(nb.off.size());
    for (size_t i = 0; i < nb.off.size(); i++) {
        tally(q, Qv, radius, step, nb.off[i], nullptr, Qc, tbl);
        miSingle[i] = mutual_information(tbl, Qc, Qv);
    }
    // The rule states the shortlist it would like; only here is it known how
    // many offsets the neighbourhood actually has, after `radius` was clamped to
    // fit the sample and the mask. Rules state intent, call sites clamp.
    std::vector<int> shortlist =
        shortlist_offsets(nb, miSingle, shortlistSize);

    // Where each shortlisted offset sits in the shortlist, and where each pair of
    // them sits in `cands`: the orbit walk arrives holding offsets and needs both.
    const int n = (int)shortlist.size();
    std::vector<int> place(nb.off.size(), -1), cellOf((size_t)n * n, -1);
    for (int i = 0; i < n; i++) place[shortlist[i]] = i;

    std::vector<Cand> cands;
    for (int a = 0; a + 1 < n; a++)
        for (int b = a + 1; b < n; b++) {
            tally(q, Qv, radius, step, nb.off[shortlist[a]],
                  &nb.off[shortlist[b]], Qc, tbl);
            cellOf[(size_t)a * n + b] = (int)cands.size();
            cands.push_back({a, b, mutual_information(tbl, cells, Qv)});
        }

    // Reserve half the slots for pairs reaching past the +-2 core. The near pairs
    // always win on MI, and on their own they only ever reproduce grain two
    // pixels across.
    const int want = std::min<int>(wantPairs, cands.size());
    if (want < wantPairs)
        fprintf(stderr, "markov: only %d pairs available, not %d — a +-%d neighbourhood"
                        " has %zu offsets, and the %zu shortlisted make %zu pairs\n",
                want, wantPairs, radius, nb.off.size(), shortlist.size(), cands.size());
    std::vector<int> uses(n, 0), chosen;
    std::vector<char> used(cands.size(), 0);
    pick_pairs(cands, cellOf, nb, shortlist, place, 2, true,  want / 2, reuse,
               uses, used, chosen);
    pick_pairs(cands, cellOf, nb, shortlist, place, 2, false, want, reuse,
               uses, used, chosen);


    PairSelection sel;
    sel.pairs.reserve(chosen.size());
    for (int ci : chosen)
        sel.pairs.emplace_back(nb.off[shortlist[cands[ci].a]], nb.off[shortlist[cands[ci].b]]);
    sel.shortlist_used = shortlist.size();
    return sel;
}

// Print how much sample data stood behind the fit, and how much of what the
// model learned is noise. Diagnostic only: it resolves nothing and, in
// particular, never touches the caller's rng, so --markov-report leaves the mask
// byte-identical.
// 1. Is there enough sample data behind each table?
void report_occupancy(int Qv, int Qc, int cells, int step,
                      const std::vector<double>& occupancy)
{
    const long centres = std::lround(std::accumulate(
        occupancy.begin(),
        occupancy.begin() + std::min<size_t>(cells, occupancy.size()), 0.0));
    std::vector<double> occ = occupancy;
    std::sort(occ.begin(), occ.end());
    auto pct = [&](double q) { return occ.empty() ? 0.0 : occ[(size_t)(q * (occ.size() - 1))]; };

    // Weight each cell by how often it is actually visited: the neighbours are
    // correlated, so most off-diagonal cells are near-empty *by construction*
    // and an unweighted count makes a healthy model look starved.
    double massInPriorCells = 0, priorShare = 0, tot = 0;
    for (double c : occ) tot += c;
    for (double c : occ) {
        if (tot <= 0) break;
        double wgt = c / tot;
        if (c < kPrior) massInPriorCells += wgt;
        priorShare += wgt * (kPrior / (c + kPrior));
    }

    const double starvedCells = occ.empty() ? 0.0 :
        100.0 * std::count_if(occ.begin(), occ.end(),
                              [](double c) { return c < kPrior; }) / occ.size();
    const char* fed = (massInPriorCells < 0.01 && pct(0.10) >= 30) ? "well fed"
                    : (massInPriorCells < 0.05) ? "adequate"
                    : "THIN — lower --markov-bins, or use a bigger sample";

    fprintf(stderr, "\n  1. is there enough sample data behind each table?\n");
    fprintf(stderr, "     Each neighbour pair gets one table. A \"cell\" is one combination of\n"
                    "     the two neighbours' brightnesses, holding what the centre pixel did.\n");
    fprintf(stderr, "       table shape     %d cells x %d centre bins   (--markov-bins %d)\n",
            cells, Qv, Qc);
    char counted[32];
    snprintf(counted, sizeof counted, "%ld", centres);
    std::string withCommas(counted);
    for (int i = (int)withCommas.size() - 3; i > 0; i -= 3) withCommas.insert(i, ",");
    fprintf(stderr, "       counted         %s sample pixels per table (%s)\n",
            withCommas.c_str(),
            step == 1 ? "every px" : ("1 px in " + std::to_string(step)).c_str());
    fprintf(stderr, "       counts per cell p10 %.0f   median %.0f   p90 %.0f   max %.0f\n",
            pct(0.10), pct(0.50), pct(0.90), occ.empty() ? 0.0 : occ.back());
    fprintf(stderr, "       near-empty      %.1f%% of cells hold under %.0f counts, and they take\n"
                    "                       %.2f%% of the lookups the sampler actually makes\n",
            starvedCells, (double)kPrior, 100.0 * massInPriorCells);
    fprintf(stderr, "       guessed         %.2f%% of the fitted probability came from smoothing\n"
                    "                       rather than from data\n", 100.0 * priorShare);
    fprintf(stderr, "     -> cells are %s\n", fed);
}

// 2. Is it learning texture, or memorising this sample?
void report_holdout(const Quantised& q, int Qv, int Qc, int cells,
                    int radius, int step, const std::vector<Expert>& experts,
                    const std::vector<double>& weights)
{
    // Refit on the left strip, score on the right, with a `radius` gap so no
    // neighbourhood straddles the two. Half the data overfits more than the
    // shipped model does, so the gap below is an upper bound on the real one.
    // Split within each scan rather than across them: that keeps the gap
    // meaningful and asks the same question whatever the sample is made of.
    std::vector<std::vector<float>> half(experts.size());
    std::vector<float> t2((size_t)cells * Qv);
    for (size_t k = 0; k < experts.size(); k++) {
        tally(q, Qv, radius, step, experts[k].d1, &experts[k].d2, Qc, t2,
              Columns::Left);
        to_logp(t2, cells, Qc, Qv, half[k]);
    }
    double tr = pooled_logloss(q, Qv, Qc, radius, step, experts, weights, half, Columns::Left);
    double te = pooled_logloss(q, Qv, Qc, radius, step, experts, weights, half, Columns::Right);
    const double base = std::log((double)Qv);      // knowing nothing
    const double known = base - te, gap = te - tr;
    const double pct_noise = known > 0 ? 100.0 * gap / known : 0.0;

    // exp(log-loss) is how many equally-likely choices the model is still
    // deciding between — the same information in a unit that can be pictured.
    fprintf(stderr, "\n  2. is it learning texture, or memorising this sample?\n");
    fprintf(stderr, "     Refitted on each scan's left half and scored on its right half, so\n"
                    "     the right half is data this model has never seen.\n");
    fprintf(stderr, "       guessing        %5.1f equally likely centre bins — knowing nothing\n", (double)Qv);
    fprintf(stderr, "       on unseen half  %5.1f  <- what the model narrows it to  (%.3f nats learned)\n",
            std::exp(te), known);
    fprintf(stderr, "       on its own half %5.1f  <- would be far lower if memorising (%.3f nats better)\n",
            std::exp(tr), gap);
    if (gap <= 0.0)
        // The split assumes the sample looks the same on both sides, which a
        // flat grain patch does and a whole photograph does not. A negative
        // gap means the right half was simply the easier one.
        fputs("     -> no measurable overfitting: it does no better on the half it was\n"
              "        fitted to. If the sample is a whole picture rather than a flat\n"
              "        patch of texture, its two halves differ and this test is weak\n", stderr);
    else if (pct_noise < 5.0)
        fprintf(stderr, "     -> %.1f%% of what it learned is noise: not overfitting, and there is\n"
                        "        headroom — raise --markov-bins, then --markov-pairs\n", pct_noise);
    else if (pct_noise < 15.0)
        fprintf(stderr, "     -> %.1f%% of what it learned is noise: near this sample's capacity,\n"
                        "        so more bins or pairs will buy little\n", pct_noise);
    else if (pct_noise < 100.0)
        fprintf(stderr, "     -> %.1f%% of what it learned is noise: OVERFITTING — lower\n"
                        "        --markov-bins, or fit a larger sample\n", pct_noise);
    else
        // Past this the ratio stops being a useful number — the two
        // perplexities say it plainly instead.
        fprintf(stderr, "     -> it predicts its own half (%.1f) far better than unseen data\n"
                        "        (%.1f): MEMORISING, not learning — lower --markov-bins, or\n"
                        "        fit a larger sample\n", std::exp(tr), std::exp(te));
    fputs("        (fitted on half the data, so this overstates the real figure)\n", stderr);
}

// 3. Would fitted weights beat flat 1/K?
void report_weights(const Quantised& q, int Qv, int Qc,
                    int radius, int step, const std::vector<Expert>& experts,
                     double temp)
{
    // Reported, never used: this says whether flat 1/K is leaving anything on
    // the table, which is the only way to know the default is the right one.
    std::vector<std::vector<float>> full(experts.size());
    for (size_t k = 0; k < experts.size(); k++) full[k] = experts[k].logp;
    const double total = 1.0 / std::max(temp, 1e-3);
    std::vector<double> wml = fit_weights_ml(q, Qv, Qc, radius, step, total,
                                             experts, full);
    // Flat weights built here rather than taken from the caller: the caller's
    // are whatever --mask-weights asked for, so under `mi` or `ml` the row
    // labelled "flat 1/K" was comparing the fit against itself.
    const std::vector<double> flat(experts.size(), total / experts.size());
    double llFlat = pooled_logloss(q, Qv, Qc, radius, step, experts, flat, full, Columns::All);
    double llMl   = pooled_logloss(q, Qv, Qc, radius, step, experts, wml,     full, Columns::All);

    // How concentrated the fitted weights are, against MI and against flat.
    std::vector<size_t> byMi(experts.size());
    std::iota(byMi.begin(), byMi.end(), 0);
    std::stable_sort(byMi.begin(), byMi.end(),
                     [&](size_t a, size_t b) { return experts[a].mi > experts[b].mi; });
    double topHalf = 0;
    for (size_t i = 0; i < byMi.size() / 2; i++) topHalf += wml[byMi[i]];
    double miTop = 0, miAll = 0;
    for (size_t i = 0; i < byMi.size(); i++) {
        miAll += std::max(experts[byMi[i]].mi, 0.0);
        if (i < byMi.size() / 2) miTop += std::max(experts[byMi[i]].mi, 0.0);
    }
    fprintf(stderr, "\n  3. would fitted weights beat flat 1/K?\n");
    fprintf(stderr, "       share of the vote held by the better half of the pairs:\n");
    fprintf(stderr, "         flat 1/K  50.0%%   fitted %.1f%%   by mutual information %.1f%%\n",
            100.0 * topHalf / total, miAll > 0 ? 100.0 * miTop / miAll : 50.0);
    fprintf(stderr, "       log-loss    flat %.4f   fitted %.4f   (%+.4f nats)\n",
            llFlat, llMl, llFlat - llMl);
    fputs(llFlat - llMl < 0.005
          ? "     -> fitted weights buy essentially nothing; flat 1/K is already right\n\n"
          : "     -> fitted weights are measurably better than flat 1/K\n\n", stderr);
}

void report_fit(const Quantised& q, int Qv, int Qc, int cells,
                int radius, int step, const std::vector<Expert>& experts,
                const std::vector<double>& weights,
                const std::vector<double>& occupancy, double temp)
{
    fputs("\n  fit report (diagnostic only — the mask is the same without it)\n", stderr);
    report_occupancy(Qv, Qc, cells, step, occupancy);
    report_holdout(q, Qv, Qc, cells, radius, step, experts, weights);
    report_weights(q, Qv, Qc, radius, step, experts, temp);
}

// Gibbs-sample the field to equilibrium and return the ranking that results:
// `order[r]` is the index of the pixel with rank r. Everything the sampler needs
// beyond the experts is a scalar, so this is the whole of the stochastic part.
std::vector<int> gibbs_field(int H, int W, int Qv, int Qc,
                             const std::vector<Expert>& experts,
                             const std::vector<double>& weights,
                             const MarkovParams& p, uint64_t seed,
                             const std::vector<double>& refBands)
{
    // Gibbs sweeps over a toroidal field, in a fresh random order each sweep so
    // the sampler carries no scan direction into the texture. The seed is this
    // function's own, never the caller's global rng: --mask-out then --mask-in
    // has to reproduce a run exactly.
    std::mt19937_64 rng(seed);
    // N is H*W and was passed in beside them, which is a second chance to be
    // wrong about a thing the other two already say.
    const int N = H * W;
    std::vector<int> order(N);
    std::iota(order.begin(), order.end(), 0);
    std::shuffle(order.begin(), order.end(), rng);

    std::vector<float> field(N);
    for (int r = 0; r < N; r++) field[order[r]] = (r + 0.5f) / N;   // flat, white

    std::vector<float> acc(Qv), prob(Qv);
    std::uniform_real_distribution<float> uni(0.0f, 1.0f);
    for (int it = 0; it < p.iters; it++) {
        std::shuffle(order.begin(), order.end(), rng);
        for (int idx : order) {
            const int y = idx / W, x = idx - y * W;
            std::fill(acc.begin(), acc.end(), 0.0f);
            for (size_t k = 0; k < experts.size(); k++) {
                const Expert& e = experts[k];
                int b1 = (int)(field[maskutil::wrap_near(y + e.d1.dy, H) * W + maskutil::wrap_near(x + e.d1.dx, W)] * Qc);
                int b2 = (int)(field[maskutil::wrap_near(y + e.d2.dy, H) * W + maskutil::wrap_near(x + e.d2.dx, W)] * Qc);
                const float* lp = &e.logp[((size_t)std::min(b1, Qc - 1) * Qc
                                         + std::min(b2, Qc - 1)) * Qv];
                const float w = (float)weights[k];
                for (int c = 0; c < Qv; c++) acc[c] += w * lp[c];
            }
            float m = *std::max_element(acc.begin(), acc.end()), sum = 0;
            for (int c = 0; c < Qv; c++) { prob[c] = std::exp(acc[c] - m); sum += prob[c]; }

            float u = uni(rng) * sum, run = 0;
            int c = Qv - 1;
            for (int i = 0; i < Qv; i++) { run += prob[i]; if (u <= run) { c = i; break; } }
            // Uniform inside the chosen bin: the model has nothing finer to say,
            // but a continuous value keeps the final ranking from breaking ties
            // by luck alone.
            field[idx] = (c + uni(rng)) / Qv;
        }
        // The model is silent past its own radius, and an uninformed field
        // drifts there: pairwise potentials that agree locally still random-walk
        // at long wavelength, so unchecked sweeps grow blobs tens of pixels
        // across that are in no sample. --mask-highpass is already the claim
        // that the sample has no structure coarser than that, so hold the field
        // to the same claim. Without this the mask's spectrum peaks at DC. At
        // markov's default of no high-pass this does nothing, and the band
        // holding below is what keeps the long wavelengths at the film's level.
        // Scaled by --mask-scale, because prepare_texture_sample resamples the sample:
        // after it, what was `highpass` sample pixels is highpass * scale mask
        // pixels, and cutting at the unscaled figure would suppress exactly the
        // coarse band a user asking for coarser grain wants kept.
        highpass_field(field, H, W, p.highpass * p.scale);

        // Spectrum matching: rescale the field's power in each frequency band
        // to the sample's, keeping the phases the sweep produced (maskutil.h).
        // The high-pass above acts only below one frequency, and not at all at
        // markov's default of 0. Empty, and so skipped, at markov.bands 0.
        if (!refBands.empty()) {
            cv::Mat f(H, W, CV_32F, field.data());
            maskutil::impose_band_power(f, refBands);
        }

        // Re-impose the flat marginal. Cheap, and it corrects the drift a
        // product of overlapping experts introduces — they are not calibrated,
        // so their combination pulls toward the middle of the range.
        cv::Mat f(H, W, CV_32F, field.data());
        maskutil::rank_uniform_unit(f, order);
    }
    return order;
}





// Raw counts per conditioning cell, taken before smoothing borrows evidence
// between neighbouring cells — this is "how much data is in each bin", which is
// what the report needs and what the smoothed table can no longer say.
void append_occupancy(const std::vector<float>& tbl, int cells, int Qv,
                      std::vector<double>& occupancy)
{
    occupancy.resize(occupancy.size() + cells);
    double* dst = occupancy.data() + occupancy.size() - cells;
    for (int b = 0; b < cells; b++) {
        double s = 0;
        for (int c = 0; c < Qv; c++) s += tbl[(size_t)b * Qv + c];
        dst[b] = s;
    }
}

// One pair's table, smoothed over all three axes and turned into log
// probabilities mixed toward the marginal. `tbl` arrives holding this pair's
// counts and is consumed; the mutual information is scored on the smoothed
// table, which is the one the sampler actually uses.
Expert fit_expert(Offset d1, Offset d2, std::vector<float>& tbl,
                  int cells, int Qc, int Qv)
{
    // to_logp is the same three blurs and the same arithmetic, and it leaves
    // tbl smoothed -- which is the table the mutual information has always been
    // scored on, so it can be read afterwards instead of before.
    //
    // The second review refused this deduplication for want of proof that the
    // arithmetic was identical, which was the right call on the evidence then.
    // The third supplied it: fifty tables spanning Qc = 2..32, including
    // all-zero ones, producing exactly equal smoothed tables, logp vectors and
    // mutual information. Checked again here against the whole program, since
    // agreeing on a table is not the same as agreeing on a mask.
    Expert e;
    e.d1 = d1;
    e.d2 = d2;
    to_logp(tbl, cells, Qc, Qv, e.logp);
    e.mi = mutual_information(tbl, cells, Qv);
    return e;
}


// Draw a mask from an already-fitted model. This is the only half that depends
// on `temp`, which is why calibration can vary it without refitting.

} // namespace

std::unique_ptr<MarkovModel> fit_markov_model(const samples::Set& set, const MarkovParams& p,
                                              cv::Size sz, const samples::Set* reference)
{
    const int H = sz.height, W = sz.width;

    const samples::Set fields = set.prepared(p.highpass, p.scale);

    // The neighbourhood has to fit inside every scan (with its border skipped)
    // and inside the mask (which wraps, so it must not overlap itself). The
    // smallest scan governs, since one table is fitted from all of them.
    const cv::Size least = fields.smallest();
    int radius = p.radius;
    radius = std::min({radius, (least.width - 1) / 2 - 1, (least.height - 1) / 2 - 1,
                       (W - 1) / 2, (H - 1) / 2});
    if (radius < 1) {
        fprintf(stderr, "markov: sample or mask too small for a neighbourhood\n");
        return {};
    }
    if (radius != p.radius)
        fprintf(stderr, "markov: neighbourhood reduced to +-%d to fit\n", radius);

    const int Qc = p.bins;                       // conditioning quantisation
    const int Qv = std::min(2 * Qc, 64);         // centre quantisation: finer, because
                                                 // the mask needs more than Qc distinct
                                                 // arrangements to threshold with
    const int cells = Qc * Qc;

    // One ranking over every scan, so a bin means the same thing in all of them.
    const Quantised q = quantise_set(fields, Qc, Qv);

    const Neighbourhood nb(radius);

    // Every candidate pair costs a pass over the sample, so thin the centres
    // visited to roughly what the tables can absorb. Summed over the scans,
    // because one pass now visits all of them.
    double usable = 0;
    for (const cv::Mat& f : fields)
        usable += (double)std::max(0, f.rows - 2 * radius)
                * (double)std::max(0, f.cols - 2 * radius);
    const int step = std::max(1, (int)std::sqrt(usable / (kCountsPerCell * cells * Qv)));

    // The rule states the shortlist it would like; only here is it known how many
    // offsets the neighbourhood actually has, after `radius` was clamped to fit
    // the sample and the mask. Rules state intent, call sites clamp.
    const PairSelection sel =
        select_pairs(q, Qv, Qc, cells, radius, step, nb, p.pairs,
                     std::min<int>(p.shortlist, (int)nb.off.size()), p.reuse);
    const std::vector<std::pair<Offset, Offset>>& pairs = sel.pairs;

    // Fit each chosen pair: count, smooth over all three axes, mix toward the
    // marginal, take logs.
    std::vector<Expert> experts;
    std::vector<float> tbl((size_t)cells * Qv);
    std::vector<double> occupancy;      // raw counts per conditioning cell, all pairs
    experts.reserve(pairs.size());
    for (const auto& [d1, d2] : pairs) {
        tally(q, Qv, radius, step, d1, &d2, Qc, tbl);
        if (p.report) append_occupancy(tbl, cells, Qv, occupancy);
        experts.push_back(fit_expert(d1, d2, tbl, cells, Qc, Qv));
    }
    if (experts.empty()) {
        fprintf(stderr, "markov: no usable neighbour pairs\n");
        return {};
    }


    auto m = std::make_unique<MarkovModel>();
    m->experts = std::move(experts);
    m->occupancy = std::move(occupancy);
    m->quant = std::move(q);
    m->Qv = Qv; m->Qc = Qc; m->cells = cells; m->radius = radius; m->step = step;
    m->shortlistUsed = sel.shortlist_used;
    m->offsetCount = nb.off.size();
    if (p.bands > 0) {
        // What the spectrum matching aims at: the reference -- the film's crop
        // where it has one -- rather than the scans the tables were counted
        // from (samples::reference_for), in p.bands frequency bands.
        const samples::Set r = reference && !reference->empty()
                             ? reference->prepared(p.highpass, p.scale) : fields;
        m->bands = maskutil::band_reference(std::vector<cv::Mat>(r.begin(), r.end()), sz,
                                            p.bands, maskutil::kReferenceEqualise * p.scale);
    }
    m->size = sz;
    return m;
}

std::vector<int> sample_markov_order(cv::Size sz, const MarkovModel& m, const MarkovParams& p,
                           uint64_t seed, std::ostream* log)
{
    const int H = sz.height, W = sz.width;
    const std::vector<Expert>& experts = m.experts;
    const int Qv = m.Qv, Qc = m.Qc, cells = m.cells, radius = m.radius, step = m.step;
    const Quantised& q = m.quant;
    const std::vector<double>& occupancy = m.occupancy;
    const size_t shortlistUsed = m.shortlistUsed;

    // Combine as a plain geometric mean — see the header on why MI must not also
    // weight it. Summing the logs unweighted would instead treat sixty
    // overlapping views of the same two pixels as sixty independent
    // measurements; either way the weights sum to 1/temp, so `attend` changes
    // only how the total is distributed and the two are comparable at equal
    // temperature.
    // The weights belong to this call, not to the model: they are a function of
    // this temperature and this weighting mode, and calibration samples the same
    // fitted model at eight temperatures. Writing them back into the shared
    // experts is what stopped the model being const, and what stops those probes
    // from running side by side.
    const double invTemp = 1.0 / std::max(p.temp, 1e-3);
    double miSum = 0;
    for (const Expert& e : experts) miSum += std::max(e.mi, 0.0);
    std::vector<double> w(experts.size());
    for (size_t k = 0; k < experts.size(); k++)
        w[k] = (p.weights == "mi" && miSum > 0 ? std::max(experts[k].mi, 0.0) / miSum
                                               : 1.0 / experts.size()) * invTemp;
    if (p.weights == "ml") {
        std::vector<std::vector<float>> lp(experts.size());
        for (size_t k = 0; k < experts.size(); k++) lp[k] = experts[k].logp;
        w = fit_weights_ml(q, Qv, Qc, radius, step, invTemp, experts, lp);
    }

    if (log)
        *log << "  " << experts.size() << " pairs from " << shortlistUsed
             << " offsets (of " << m.offsetCount << "), bins " << Qc << "/" << Qv
             << ", sample step " << step << ":\n";

    // How much data each conditioning cell actually got, and whether the model is
    // learning the sample's texture or its speckle. Reads nothing and writes
    // nothing outside this block — in particular it never touches `rng`, so the
    // mask is identical whether or not the report ran.
    if (p.report)
        report_fit(q, Qv, Qc, cells, radius, step, experts, w, occupancy, p.temp);

    // The band reference was taken for the size the model was fitted at; a
    // draw at another size has none rather than the wrong one.
    static const std::vector<double> none;
    return gibbs_field(H, W, Qv, Qc, experts, w, p, seed,
                       p.bands > 0 && m.size == sz ? m.bands : none);
}

cv::Mat sample_markov_mask(cv::Size sz, const MarkovModel& m, const MarkovParams& p,
                           uint64_t seed, std::ostream* log)
{
    return maskutil::threshold_mask(sample_markov_order(sz, m, p, seed, log), sz);
}

std::vector<int> synthesise_markov_order(cv::Size sz, const cv::Mat& sample,
                                         const MarkovParams& p, uint64_t seed,
                                         std::ostream* log)
{
    auto m = fit_markov_model(samples::Set::of(sample), p, sz);
    return m ? sample_markov_order(sz, *m, p, seed, log) : std::vector<int>();
}

cv::Mat build_markov_mask(cv::Size sz, const samples::Set& set,
                          const MarkovParams& p, uint64_t seed,
                          std::ostream* log, const samples::Set* reference)
{
    auto m = fit_markov_model(set, p, sz, reference);
    if (!m) return {};
    const std::vector<int> order = sample_markov_order(sz, *m, p, seed, log);
    return order.empty() ? cv::Mat() : maskutil::threshold_mask(order, sz);
}

double markov_work(int w, int h, int iters, int pairs)
{
    return (double)w * h * iters * pairs;
}

void register_markov_params(params::Params& P)
{
    // Named once. inpaint has always done this; the others repeated the
    // expression at every rule, which buries what each rule *says* under
    // what they all have in common.
    const auto mine = mask::generator("markov");

    // mask.calibrate lives with the generator it calibrates, not with the mask
    // geometry it is spelled alongside. A parameter rather than a bare flag in
    // Options, so the two things every other option gets apply to it: naming it
    // on its own implies the generator it belongs to, and naming it against a
    // different one is a conflict rather than a note beside a zero exit status.
    params::add_const(P, mask::keys::Calibrate, (long long)0,
                      mask::calibratable(), {{0, 1}},
                      "measure this generator's settings for the sample instead of "
                      "deriving them");

    using namespace markov::keys;

    // Work units per second, measured from this machine's own builds and cached
    // in the config file. The compiled-in value is only a first guess; the first
    // --budget run says so, and every build refines it.
    params::add_const(P, Rate, 4.8e7,
                      mine, {{1.0, 1e15}},
                      "measured build throughput, work units per second",
                                        true);   // an intermediate: reaches the mask only via --budget


    params::add_const(P, Radius, (long long)5,
                      mine, {{1, 32}},
                      "half-width of the neighbourhood pairs are drawn from");

    params::add_const(P, Weights, std::string("flat"),
                      mine, {},
                      "how each pair's vote is weighted: flat, mi or ml (flat measures best)");

    params::add_const(P, Report, (long long)0,
                      mine, {{0, 1}}, "print the fitting diagnostic");
    // Spectrum matching after each sweep, in this many frequency bands; see
    // maskutil.h, "Spectrum matching", for what it does and why.
    params::add_const(P, Bands, (long long)maskutil::kDefaultPowerBands,
                      mine, {{0, 64}},
                      "frequency bands each sweep's spectrum is matched to the sample's in; 0 turns matching off");

    params::add_const(P, MaxRepeats, (long long)4,
                      mine, {{1, 1024}},
                      "tile repeats the grid may show before it looks regular");

    params::add_const(P, MaxSize, (long long)256,
                      mine, {{16, 8192}},
                      "largest tile worth building, absent a --budget");

    params::add_const(P, Bins, (long long)16,
                      mine, {{2, 32}},
                      "conditioning bins per neighbour; a pair's table is bins x bins cells");

    // Diversity penalty in the greedy pair pick: a candidate's mutual
    // information is divided by 1 + reuse*(uses of its two offsets), so an
    // offset already spent is worth less the next time. DEFAULTS.md proposed
    // deriving this from pairs/shortlist, on the grounds that more pairs drawn
    // from one shortlist need a stronger penalty. They do -- but the mechanism
    // already supplies it. The shortlist rule pins C(shortlist,2) at 12*pairs,
    // so shortlist grows as sqrt(pairs) and uses-per-offset grows as sqrt(pairs)
    // too: 1.5 at 16 pairs, 3.2 at 64, 6.5 at 256, 12.8 at 1000. The penalty
    // denominator therefore climbs 2.1 -> 3.2 -> 5.5 -> 10.0 on its own, with
    // this held constant. A rule in pairs/shortlist would be a second copy of a
    // dependence the greedy loop already has.
    //
    // What is *not* already supplied is the coefficient, and it has to fall as
    // pairs rise for the same reason: with uses growing as sqrt(pairs), holding
    // this fixed makes the total penalty an offset accumulates grow with it, and
    // the pick ends up refusing good repeats. So hold the product constant --
    // reuse * uses-per-offset, where uses-per-offset is 2*pairs/shortlist.
    //
    // Measured on three films (4Xcrop, Kodak-2484-1, AutoRex-4X) at 64 and 256
    // pairs, scored by both test/levelfit and test/patchfit, three seeds each.
    // All six film-metric cells agree the optimum *falls* with pair count --
    // the opposite of what DEFAULTS.md posited -- and where it is resolvable it
    // falls by about half for four times the pairs, which is this rule. The
    // median optimum at 64 pairs is 0.35, which is what this returns there, so
    // the default is unchanged and today's output is byte-identical.
    //
    // At 256 pairs the rule asks 0.173 where the constant asked 0.35, and five
    // of the six cells improve (levelfit 0.197->0.184, 0.999->0.952,
    // 0.386->0.294; patchfit 32.4->30.1, 21.1->19.8; AutoRex patchfit
    // 16.0->18.2 is the one that does not). The spread between films is wide --
    // 2484 and AutoRex want 0 at 256 pairs where 4Xcrop wants 0.15 -- so this
    // is the direction and the scaling, not a per-sample optimum. Like
    // markov.temp, the last of that is a property of the sample.
    P.add({ Reuse,
            [](const params::Resolver& r) -> params::Value {
                const double pairs = (double)r.integer(Pairs);
                const double shortlist = (double)r.integer(Shortlist);
                return kReuseBudget * shortlist / (2.0 * pairs);
            },
            mine,
            {{0.0, 100.0}},
            "how much a repeated offset is penalised when picking pairs" });

    // 3, and it took an iters term in the markov.temp rule to find it. This was
    // 12 on the strength of a level-set sweep at *fixed* temperature, which is
    // the wrong comparison: the temperature that suits the field depends on how
    // long it has been swept, so every sweep count was being judged at 12's
    // temperature. With both free -- a 7x8 grid over iters 2..20 and temp
    // 0.20..0.50 on two films, scored by test/patchfit and test/levelfit -- the
    // ordering reverses and fewer sweeps wins:
    //
    //   iters              2      3      4      5      8     12     20
    //   patchfit 4Xcrop  17.5   20.5   22.7   24.7   28.1   30.0   31.4
    //   patchfit AutoRex 10.3   12.2   13.4   13.9   15.9   18.0   18.5
    //   levelfit 4Xcrop  0.18   0.19   0.20   0.19   0.20   0.20   0.20
    //   levelfit AutoRex 0.26   0.19   0.18   0.18   0.18   0.19   0.19
    //
    // each column at its own best temperature. patchfit falls monotonically as
    // sweeps are removed; levelfit on AutoRex was the dissent, and 3 was where
    // both were satisfied.
    //
    // 2 now, on a wider measurement. Six films -- the three that fitted the old
    // rule plus Kodak-2484, Delta 3200 and Panatomic-X, which did not -- each
    // scored at 256^2 against its own 768^2 crop:
    //
    //   patchfit, derived temperature   4X-1  4X-2  AutoRex  2484  Delta  PanX
    //     iters 3                       31.3  54.1  26.4     26.8  21.9   34.8
    //     iters 2                       27.6  34.2  18.8     25.0  13.9   28.4
    //
    // Every film, and by a wide margin on three of them. Under --mask-calibrate
    // it is 5 of 6, the exception being 2484 -- whose calibration pins at the
    // bracket floor of 0.25 for both sweep counts, so that film is the one
    // place the comparison is not really being made. The levelfit dissent that
    // held 3 in place does not survive either: at its own derived temperature
    // 2 now reads 0.303 on AutoRex against 3's 0.237, but wins the aggregate
    // there by 26.4 to 18.8, and wins clumping outright on three of the six.
    //
    // And it is a third faster, which is the rare direction for this parameter.
    //
    // What actually improves is the worming, which is what this parameter buys
    // and spends. Against the old default on 4Xcrop, patchfit's coarse
    // ridgeness -- the clumps stretching into connected ridges, measured rather
    // than eyeballed -- goes from +10.0 to +1.7, near inpaint's -1.2, and the
    // fine-mid envelope correlation from -15.2 to -8.6, better than the -11 to
    // -15 that used to hold on every setting tried.
    params::add_const(P, Iters, (long long)2,
                      mine, {{1, 500}}, "Gibbs sweeps over the field");

    // Sized so the greedy pick has kPoolFactor candidates per pair it must
    // choose, which is what stops --markov-pairs being silently capped: pairs are
    // drawn from C(shortlist, 2) candidates, so a fixed shortlist is a fixed
    // ceiling. Inverting C(n,2) >= kPoolFactor * pairs gives this. It is an
    // intent — build_markov_mask clamps it to the offsets a neighbourhood that
    // has been shrunk to fit the sample can actually supply.
    P.add({ Shortlist,
            [](const params::Resolver& r) -> params::Value {
                const double pairs = (double)r.integer(Pairs);
                return (long long)std::ceil(
                    (1.0 + std::sqrt(1.0 + 8.0 * kPoolFactor * pairs)) / 2.0);
            },
            mine,
            {{2, 4096}},
            "offsets pairs may be drawn from; wide enough to leave the pick a choice" });

    // 64 by default. Given a budget, more pairs measurably helps — 256 scores
    // 0.158 against 64's 0.190 — but only to a point: 512 and 1000 measure
    // worse at any temperature, so the allocation stops at the useful maximum
    // rather than spending time to make the mask worse. --markov-temp follows
    // automatically, since its rule reads this.
    P.add({ Pairs,
            [](const params::Resolver& r) -> params::Value {
                const double budget = r.num(mask::keys::Budget);
                if (budget <= 0) return (long long)64;
                const double w = (double)r.integer(mask::keys::SizeW);
                const double h = (double)r.integer(mask::keys::SizeH);
                const double iters = (double)r.integer(Iters);
                const double rate = r.num(Rate);
                long long best = 64;
                for (long long p : {256LL, 128LL, 64LL})
                    if (markov_work((int)w, (int)h, (int)iters, (int)p) / rate <= budget) {
                        best = p; break;
                    }
                return best;
            },
            mine,
            {{1, 4096}},
            "neighbour pairs conditioning each pixel" });

    // Temperature is the one scalar that decides the outcome, and it moves with
    // table richness, pair count *and* sweep count: more pairs divide a fixed
    // total vote (the weights sum to 1/temp), so the pool broadens and needs
    // re-sharpening; finer bins sharpen each expert, so the pool needs softening;
    // and more sweeps is more time spent below T_c, which has to be paid for by
    // sitting closer to it.
    //
    // The iters term is what this rule was missing, and its absence is why
    // markov.iters could not be chosen: with temp fixed at 0.40 every sweep
    // count was being judged at the wrong temperature. Measured as a 7x8 grid
    // over iters 2..20 and temp 0.20..0.50 on two films, scored by both
    // test/patchfit and test/levelfit. All four film-metric surfaces agree the
    // optimum rises with log2(iters), at 0.072 / 0.060 / 0.054 / 0.048 per
    // octave; 0.059 is the mean. Anchored at 12 sweeps, where it returns 0.40 --
    // so this is a strict generalisation of the rule it replaces, and would be
    // byte-identical had the sweep count stayed at 12.
    //
    // Fitted to measured optima (bins, pairs) -> temp: (16,64) 0.40-0.41,
    // (16,256) 0.38, (32,64) 0.44. The base is pinned at 0.40 rather than 0.41
    // because at the defaults 0.40 measured slightly better (level-set fit 0.190
    // against 0.198) and it keeps today's output byte-identical; the two are
    // within the noise of each other either way.
    //
    // This is a starting point, not the last word — the optimum is a property of
    // the sample too, which is what --mask-calibrate will measure.
    //
    // **The anchor depends on whether the sample is high-passed.** Everything
    // above was measured at --mask-highpass 32, and 0.40 is that regime's.
    // Without the high-pass the model also learns the scans' tone, which
    // couples it harder, and the optimum moves up: --mask-calibrate on six
    // films measures 0.52 to 1.15 for the default flat weighting and radius 5.
    // One value has to serve a film not yet seen, so it was chosen by sweeping
    // a fixed temperature over those six and scoring with test/patchfit against
    // each film's crop: 0.45 / 0.55 / 0.65 / 0.75 / 0.905 read 50.7 / 50.6 /
    // 52.6 / 55.5 / 60.3 mean and 79 / 86 / 95 / 107 / 125 worst, against the
    // old 0.247's 63.0 and 96. 0.65 at the anchor gives 0.50 at the defaults.
    // The other terms were measured at 32 and are kept as they are; only the
    // two regimes have been measured, so anything above 1 px takes 32's.
    P.add({ Temp,
            [](const params::Resolver& r) -> params::Value {
                const double bins  = (double)r.integer(Bins);
                const double pairs = (double)r.integer(Pairs);
                const double iters = (double)r.integer(Iters);
                const double anchor = r.num(mask::keys::Highpass) > 1.0 ? 0.40 : 0.65;
                return anchor + 0.059 * std::log2(iters / 12.0)
                              + 0.030 * std::log2(bins / 16.0)
                              - 0.015 * std::log2(pairs / 64.0);
            },
            mine,
            {{0.01, 100.0}},
            "sharpness of the pairs' pooled verdict" });

    // Measured from this machine's own builds, never chosen, so it is
    // reachable by --set and is not offered as an option.
    P.hide_option(markov::keys::Rate);
}
