// SPDX-License-Identifier: GPL-3.0-or-later
// maskutil.h — the mechanics every mask generator repeats.
//
// Four generators build a toroidal, histogram-flat, 0..254 threshold mask, and
// each had its own copy of the plumbing that makes it one: wrap an index, sort
// by value, replace values by ranks, map ranks to threshold bytes, and
// high-pass a sample so a photograph's subject is not the texture. The
// algorithms differ completely; this is only what surrounds them.
//
// What is *not* here is as deliberate as what is. spectralmask's sample
// preparation subtracts the mean when the high-pass is off and never resamples,
// because its --mask-scale is a frequency remap rather than a resize; merging it
// with prepare_texture_sample() would have changed its output. Two rank
// conventions are offered rather than one for the same reason — the spectral
// method wants [-1, 1] and the others [0, 1).
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>
#include <opencv2/core.hpp>

namespace maskutil {

// Subtracting a Gaussian blur of this sigma per pixel of the requested cutoff is
// a (1 - G) high-pass whose half-power point sits at 1/cutoff cycles per pixel.
// Named because the figure appears in the generators' comments as well as here,
// and "0.1874" is not a searchable thing to have written down four times.
inline constexpr double kHighpassSigmaPerPixel = 0.1874;

// The signed frequency of DFT bin `i` of `n`, in cycles per pixel. The upper
// half of a row holds the negative frequencies, which is the whole of what this
// says; getting it wrong reads as a spectrum folded about its own Nyquist.
inline double bin_freq(int i, int n)
{
    return (i <= n / 2) ? (double)i / n : (double)(i - n) / n;
}

// Wrap an index that has strayed at most one period out of [0, n): a
// neighbourhood offset added to a coordinate already inside the grid. Branches
// rather than divides, which matters in the Gibbs and inpaint inner loops.
inline int wrap_near(int v, int n) { return v < 0 ? v + n : (v >= n ? v - n : v); }

// Wrap an index that may be any distance out, for a lookup addressed in
// frequency or by a kernel wider than the grid.
inline int wrap_any(int v, int n) { v %= n; return v < 0 ? v + n : v; }

// Rank r of n as a threshold byte. Spans 0..254 and never 255, so pure white
// always passes; every generator has to agree on this and now says it once.
inline uint8_t threshold_byte(long long r, long long n)
{
    const double top = (double)std::max<long long>(1, n - 1);
    return (uint8_t)std::clamp((int)std::lround((double)r * 254.0 / top), 0, 254);
}

// Indices 0..n-1 of `v` in ascending value order, equal values in index order:
// a stable sort by value. Computed as a parallel sort of (value, index) pairs,
// which orders the same way and is far faster on a large field -- each value
// travels with its index instead of being fetched from a scattered address at
// every comparison. 600 Mpx: 136 s as a stable sort of indices, 4 s like this.
std::vector<int> stable_order(const float* v, std::size_t n);

// Indices of a float image's pixels in ascending value order. Stable, so equal
// values keep their positional order and a run is not broken up by luck.
//
// Asserts CV_32F and continuity: it addresses the pixels as one flat run, so a
// row-strided view or a ROI would rank the padding between rows. Every caller
// passes a freshly allocated or cloned Mat, and the assert is what keeps that
// true now that this is shared rather than local to one generator.
std::vector<int> rank_order(const cv::Mat& f);

// Round every value to a multiple of `step`, in place. A no-op at step <= 0.
//
// The point is to throw away distinctions finer than the sample could record.
// A high-pass is a subtraction of a blur, and a blur of integers is not an
// integer, so it manufactures detail: a two-valued sample came out of one with
// 13277 distinct values in a 256^2 tile. Those extra values are not data, they
// are the blur, and ranking renders them at full contrast because ranking is
// scale-free -- which is a density defect and not a cosmetic one.
void quantise(cv::Mat& f, double step);

// The chance that two pixels drawn at random hold the same value: sum of the
// squared class frequencies.
//
// After quantise() this is the share of the finished mask's ordering that is
// decided by the tie-break rather than by the sample, so it says how much of
// the mask is really an imitation. Scale-free, and it separates a sample with
// no usable texture from a fine-grained one far better than counting distinct
// values does -- a real Panatomic-X scan has only 50 distinct levels left after
// quantising, fewer than several synthetic patterns, but its tie share is 0.065
// against their 0.39 to 1.00.
double tie_share(const cv::Mat& f);

// rank_order, but equal values are shuffled instead of keeping pixel order.
//
// Stable ranking gives a tie group the ranks of its members in raster order,
// which turns any large group into a gradient across the tile -- the failure
// build_direct_mask used to refuse outright. Randomising instead turns it into
// noise, which is what a threshold mask should do where it has nothing to say.
// Draws from its own generator seeded here, never the global rng, so a mask
// written out and loaded back reproduces its run exactly.
std::vector<int> rank_order_random(const cv::Mat& f, uint64_t seed);

// Sort into `order`, then replace each value by its rank on [0, 1) — the scale
// the markov field and the inpaint field are held on between sweeps.
void rank_uniform_unit(cv::Mat& f, std::vector<int>& order);

// The same on [-1, 1], which is what the spectral projection alternates with.
void rank_uniform_signed(cv::Mat& f, std::vector<int>& order);

// A field as a threshold mask. `order` lists the field's pixels by ascending
// value, as rank_order returns them, and the **highest value lights first**: it
// gets the lowest threshold, and MaskDither lights the lowest cells first.
//
// Every generator that imitates a sample builds a field in the sample's own
// sense -- bright where the grain is bright -- so this is what makes the dither
// show the grain rather than its negative. The convention is stated here, once,
// because every generator ends here; blue noise, whose order is the order its
// dots are placed in, hands that over reversed (see bluenoise.cpp).
cv::Mat threshold_mask(const std::vector<int>& order, cv::Size sz);

// A texture sample as the sample-imitating generators want it: high-passed so
// only its grain remains, then resampled, because feature size for them is
// measured in sample pixels and so resampling *is* --mask-scale. Unlike the dft
// method's pure frequency remap this feeds the interpolator's own smoothness
// into the statistics: above 1 the model learns the interpolation as grain.
cv::Mat prepare_texture_sample(const cv::Mat& sample, double highpass, double scale);

// Each pixel's quantile within its own neighbourhood: the randomised probability
// integral transform against the local distribution rather than the global one.
// The result is on [0, 1), and every neighbourhood of it is uniform.
//
// This is what `direct` needs and what neither a high-pass nor a fitted surface
// gives it. A ranked tile shows, at threshold g, the pixels below the tile's
// global g-quantile, so its local density is flat only where the *local*
// distribution matches the global one -- in mean, in amplitude, and in shape.
// Subtracting a blur equalises the mean and leaves the amplitude; dividing by a
// fitted mean surface equalises both only if grain amplitude is proportional to
// the mean, and on these scans it is not. Ranking against the neighbourhood
// equalises all three by construction, and since it depends only on order it is
// unchanged by any monotone remapping of the sample.
//
// **Ties stay ties until they are shuffled**, and that is load-bearing. A scan
// clipped at black holds a fifth of its pixels at code 0, which carry no
// information about where in the tail they lie; they are one tie group, broken
// at random here. Subtracting anything smooth first gives each of them a
// different value, ordered by the surface, and the darkest output levels then
// land wherever the surface was highest -- the shadow voids every subtractive
// preparation produced on Kodak-4X-1. So this runs on the sample itself, not on
// anything filtered from it.
//
// `sigma` is the Gaussian weight's, in pixels. The neighbourhood distributions
// are counted on a grid `sigma/3` times coarser and interpolated back, which is
// what makes it affordable: one weighted count per grey level, and 256 of them
// at full resolution are 38 ms each. At sigma/3 cells the weight is smooth
// across a cell and the grid widens it by a few per cent. Above 256 distinct
// values -- a 16-bit scan -- the levels are pooled into 256 equal-count groups,
// and within a group pixels keep their global order.
//
// Ties are broken from `seed` and nothing else, so the result is a pure
// function of its arguments.
cv::Mat local_quantile(const cv::Mat& f, double sigma, uint64_t seed);

// Moisan's periodic + smooth decomposition (2011): the part of `u` that tiles.
//
// Any image splits uniquely as u = p + s, where s is smooth -- harmonic
// everywhere except the border -- and carries the whole of the boundary
// mismatch, so p = u - s has no frame discontinuity. Treating an ordinary crop
// as periodic, which is what a DFT and a tiled mask both do, puts a jump all
// along its frame; that jump is a straight-line discontinuity, and a
// straight-line discontinuity spreads power along the perpendicular frequency
// axis. The paper's Figure 1 is that "cross structure" in the spectrum, and
// removing it is what this is for.
//
// Nothing is blurred and nothing is mirrored: the component that carried the
// jump is subtracted rather than smeared. What it costs is that s comes out of
// the whole tile, not just its edges, so it leaves a broad tilt -- see
// build_direct_mask, which takes that back out.
cv::Mat periodic_component(const cv::Mat& u);

// The lowest-cost connected path down a cost band, closed at the wrap.
//
// `cost` is L rows by k columns: L steps, and k places the cut may sit at each
// step. Returns one column index per step, with neighbouring steps differing by
// at most one -- so the cut is connected -- and the first and last pinned equal,
// so it closes. Without that last part the cut is itself a seam on the other
// axis.
//
// Both ends are pinned rather than merely brought within one of each other,
// which is not the same thing: a step of one at the wrap does not cost a
// near-miss, it splices in a pixel from a whole tile away, and that is an
// arbitrary value rather than a bounded error. Pinning is free because the
// search already runs once per starting column -- cost_[s][i] is the best path
// that started at s and has reached i.
std::vector<int> mincut_path(const cv::Mat& cost);

// Cut `c` down to `keep` along `axis` so that what remains wraps.
//
// Efros & Freeman's minimum error boundary cut (SIGGRAPH 2001). `c` is
// `keep + overlap` long on this axis, so its first `overlap` entries and its
// last `overlap` are two candidates for the same band: the crop's own start,
// and the continuation of what is about to become the tile's far edge. Every
// output pixel is taken from one or the other and never mixed, which is the
// whole reason to prefer this to a blend -- nothing is averaged, so nothing
// about the texture is altered anywhere, and the tile is still literally made
// of sample. build_direct_mask exists to say exactly that.
//
// The cut is placed where the two candidates agree, so the switch happens where
// the discontinuity is smallest. Switching at i means leaving the continuation
// at i-1 and taking the original at i, and the jump that creates is how far
// apart the two fields are there -- which is what the cost prices.
//
// Call it once per axis. One pass of two copies cannot do both at once: the
// tile breaks along the whole cross {x=0} u {y=0} and its half-shifted
// alternative along {x=W/2} u {y=H/2}, those two crosses intersect, and at the
// intersection a single choice would have to be both.
cv::Mat quilt_axis(const cv::Mat& c, int keep, int overlap, int axis);

// High-pass a field that is *already* a torus, and keep it one.
//
// The padding is what makes it toroidal: a reflecting filter would invent
// different neighbours at each edge and break the wrap, which for a field that
// is re-filtered repeatedly -- markov's, every sweep -- compounds. Padding by
// 3 sigma rather than tiling the whole field is the same thing for a Gaussian,
// which is under 1% of peak past there, and a ninth of the work.
//
// The rule this embodies, and the reason prepare_texture_sample above does the
// opposite: wrap the filter when the input is a torus, reflect when it is not
// one yet. A photograph is not.
void highpass_toroidal(cv::Mat& field, double px);

// How *directional* a field is: the structure-tensor coherence of the whole
// field, 0 where it has no preferred orientation and 1 where every gradient
// points the same way. The single-number form of what `calib::ridge_profile`
// measures per octave.
//
// It lives here rather than beside that because a generator reads it while it
// is building — inpaint compares its field against the sample after every
// sweep — and a generator should not have to depend on the calibration module
// to measure its own output.
//
// Gradients are wrapped central differences, because the fields this is asked
// about are tori.
// The summed structure tensor, exposed so several fields can be pooled before
// the ratio is taken: coherence is a ratio, so averaging per-field coherences is
// not the same quantity as the coherence of the whole.
struct Tensor { double xx = 0, yy = 0, xy = 0; };
Tensor structure_tensor(const cv::Mat& f);
double coherence_of(const Tensor& t);
inline double field_coherence(const cv::Mat& f) { return coherence_of(structure_tensor(f)); }

// --- Spectrum matching: holding a synthesised field to the sample's scale -----
//
// What it does. markov and inpaint end every sweep by calling
// impose_band_power. The field's 2D spectrum is divided into `n` rings of
// frequency, spaced evenly in log frequency from one cycle across the field
// out to the corner of the frequency plane, and every frequency in a ring is
// scaled by one factor so that the ring's mean power equals the sample's in
// the same ring. Phases are untouched, so the arrangement the sweep produced
// survives: what is set to the sample's is only *how much* structure there is
// at each scale, coarse blobs against fine grain. The field is then ranked flat
// again, as every sweep ends.
//
// Why. A sweep makes each neighbourhood agree with the sample and nothing
// compared the field's scale with the sample's, so it drifted: without the
// high-pass, inpaint ended with 1.85x Kodak-TriX's power coarser than 32 px and
// 0.57x Kodak-2484's. Of five ways tried to stop that, this was the one that
// moved every film to the sample's scale (CLAUDE.md, "Holding the sweeps to
// the sample's band power").
//
// `n` is --markov-bands and --inpaint-bands, 16 by default; 0 turns matching
// off. Fewer rings match only the spectrum's broad slope, more follow it more
// closely; the gain was measured at 16.
//
// band_reference measures the sample once for a given field size;
// impose_band_power applies that after each sweep; band_power is the
// measurement both use.
inline constexpr int kDefaultPowerBands = 16;

// Mean power per frequency cell, per pixel, in each of `n` rings spaced evenly
// in log frequency from 1/max(span.width, span.height) to the corner of the
// frequency plane. Per pixel, so a tile smaller than `span` measures the same
// rings as a field of that size; a ring with no cells in it reads 0. DC is
// excluded.
std::vector<double> band_power(const cv::Mat& f, cv::Size span, int n);

// The sample's power in each of `n` rings, for a field of size `field`.
// Measured on 16 tiles of that size -- or as near as the scans allow -- shared
// between the scans by area, at least one each, and spread evenly across each;
// every tile equalised at `equalise` px (local_quantile; 0 skips it) and ranked
// flat as the field is, then averaged. Empty when no scan offers a tile.
//
// Equalised because a film's crop is not all grain: the windows test/findcrops
// picks keep some soft shading, which direct's own equalising removes. Matched
// to that shading's power, a mask grew blobs of it and the dither's density
// wandered by up to 15/1000 between seeds; matched to the crop equalised, it
// reads 0.2/1000. Per tile, so a reference of whole scans costs milliseconds.
std::vector<double> band_reference(const std::vector<cv::Mat>& scans, cv::Size field,
                                   int n, double equalise);

// What a reference is equalised at: direct's default (four times the high-pass
// sigma at 32 px) and the scale test/findcrops chose the crops at, times
// --mask-scale. Measured only at this value.
inline constexpr double kReferenceEqualise = 24.0;

// Scale `field`'s spectrum, ring by ring, so each ring's power matches `ref`
// (band_reference's output; its length is the ring count). A ring that either
// side has no power in is left alone. In place, CV_32F; the caller re-ranks.
void impose_band_power(cv::Mat& field, const std::vector<double>& ref);

} // namespace maskutil
