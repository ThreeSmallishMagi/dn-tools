// SPDX-License-Identifier: GPL-3.0-or-later
// The sample as the mask: equalise it, take a tile, make it join, rank it flat.
//
// The last step is what every other generator already does to its sample, and
// the first is the one place this program does not high-pass -- see
// register_direct_params for why, and maskutil::local_quantile for what it
// does instead. The interesting step is the middle one.
//
// **A crop does not tile.** Its left edge has never met its right, so laying it
// out repeatedly puts a discontinuity every `width` pixels, and MaskDither
// indexes the mask modulo its size — the seam lands on the image as a hard grid.
// Three ways to fix that are worse than the one used here:
//
//   Mirroring the tile is exactly periodic and exactly wrong: the eye finds
//   reflection symmetry immediately, and a grain mask made of butterflies looks
//   like nothing film has ever done.
//
//   Blending a band across each seam destroys texture in a cross, which is
//   visible for the same reason the seam was: the defect is a straight line at a
//   fixed period.
//
//   Keeping only a block of low frequencies is periodic by construction but
//   throws away the fine detail, which for grain is the entire content.
//
//   A fourth, and the one this used to do: Moisan's periodic + smooth
//   decomposition (2011), which splits the tile as u = p + s with s smooth and
//   carrying the whole boundary jump, so p tiles. It is a correct and elegant
//   answer to a slightly different question, and it loses on both counts that
//   matter here. s comes out of the *whole* tile rather than its edges, so it
//   leaves a broad tilt that rank-flattening does not remove -- ranking is a
//   global monotone map, so it flattens the histogram over the tile while
//   preserving the tilt as a swing in the local mean, which is a density defect
//   and what --supersample samples. And it over-corrects: on Kodak-4X-1 its
//   wrapping pairs end up 0.84x and 0.51x as *different* as its interior pairs,
//   so the join is smoother than the texture around it -- a soft seam rather
//   than a hard one, and no less visible. maskutil::periodic_component is still
//   there, and unittest still holds it against the paper's own criterion.
//
// What is used instead is Efros & Freeman's minimum error boundary cut (2001).
// Cut the tile out of a crop that is `overlap` larger on each axis, so its
// leading band has two candidates -- the crop's own start, and the continuation
// of what is about to become the far edge -- and switch between them along the
// path where they differ least, found by dynamic programming. Once per axis.
//
// Two things follow, and they are why this wins. **Every output pixel comes
// from exactly one place in the sample**: nothing is averaged, so nothing about
// the grain is altered anywhere, and the claim in directmask.h survives whole.
// And there is no global solve, so there is no mechanism by which the interior
// can acquire a smooth field -- the defect above, and the one that rules out
// the band-blending alternative too.
//
// Measured against the decomposition it replaces, on two films: the wrapping
// pairs come to 0.99/1.03 and 1.11/0.83 of the interior difference where Moisan
// gave 0.84/0.51 and 0.57/0.73, and test/patchfit falls from 3.12 and 2.74 to
// 2.64 and 2.35 -- below those films' own real-patch floors of 2.83 and 3.13,
// which is what "it is the sample" is supposed to mean. See test/seamlab, where
// the comparison lives.
#include <algorithm>
#include <cmath>
#include <ostream>
#include <vector>
#include <opencv2/opencv.hpp>
#include "directmask.h"
#include "maskutil.h"

// Above this share of the mask's ordering being decided by the tie-break rather
// than by the sample, the mask is noise wearing the sample's name. See the
// measurement at the use below.
static constexpr double kMaxTieShare = 0.25;

static void refuse_untextured(std::ostream& log, double highpass, double flip)
{
    log << "  the sample has almost no texture left after --mask-highpass "
        << highpass << ": " << (int)std::lround(flip * 100)
        << "% of this mask's ordering would be decided at random rather than by "
           "the sample, against 7% for the worst real film scan measured. Use a "
           "smaller --mask-highpass to keep more of it, a sample with some "
           "texture in it, or --mask-method blue if noise is what you want.\n";
}

cv::Mat build_direct_mask(cv::Size sz, const cv::Mat& sample,
                          double scale, double highpass, double equalise, int overlap,
                          double quantStep, uint64_t seed, std::ostream* log)
{
    // The same preparation every sample-based generator applies -- minus the
    // high-pass when the sample is to be equalised instead, which has to see
    // the sample itself. See maskutil::local_quantile for why.
    const bool equalising = equalise > 0.0;
    const cv::Mat prepared =
        maskutil::prepare_texture_sample(sample, equalising ? 0.0 : highpass, scale);
    if (prepared.cols < sz.width || prepared.rows < sz.height) {
        if (log)
            *log << "  sample is " << prepared.cols << "x" << prepared.rows
                 << " after --mask-scale, too small for a " << sz.width << "x"
                 << sz.height << " mask. This generator takes a tile of the "
                    "sample rather than synthesising one, so it cannot make a "
                    "mask larger than what it was given: use --mask-size, a "
                    "larger sample, or a generator that imitates.\n";
        return {};
    }

    // The overlap has two limits and only one of them was being applied.
    //
    // It needs a crop larger than the tile by the overlap, which the *sample*
    // must supply -- and where the sample cannot, take what it can rather than
    // refusing. The seamlab sweep found no knee anywhere from 4 px to 64, so a
    // narrower band costs nothing measurable, while refusing would turn a
    // sample that is exactly mask-sized into an error, and mask_side() asks for
    // one routinely because it derives the mask from the image.
    //
    // It must also be smaller than the *tile*: quilt_axis cuts a band out of
    // what it keeps, so an overlap at least as wide as the tile leaves nothing
    // to keep. Nothing enforced that. direct.overlap and mask.size have
    // separate ranges and no range can express a relation between two
    // parameters, so `--mask-size 16x16 --set direct.overlap=32` reached
    // quilt_axis and died on its assertion -- an internal invariant printed at
    // the user, for a combination they are perfectly entitled to type.
    //
    // The rule cannot fix this either: mask.size already depends on
    // direct.overlap through direct_sample_limit, so making direct.overlap
    // depend on mask.size would be a cycle.
    const int room = std::min(prepared.cols - sz.width, prepared.rows - sz.height);
    const int ov = std::max(0, std::min(overlap, room));
    if (ov < overlap && log)
        *log << "  sample allows an overlap of only " << ov << " px, not "
             << overlap << "; cutting the seam through that instead\n";

    // Shrink for want of sample, refuse for want of tile. The asymmetry is the
    // point: a small sample is the ordinary case and the band is not worth an
    // error, whereas an overlap wider than the tile is a contradiction in what
    // was asked for, and quietly halving it would hide the mistake rather than
    // report it.
    const int shortest = std::min(sz.width, sz.height);
    if (ov >= shortest) {
        if (log)
            *log << "  the seam is cut through a band " << ov << " px wide, which does "
                    "not fit in a " << sz.width << "x" << sz.height << " tile -- the cut "
                    "needs something left over to keep. Use --mask-size larger than "
                 << ov << " on both axes, or --set direct.overlap= below " << shortest
                 << " (0 turns the seam cut off and takes a plain crop).\n";
        return {};
    }

    // Centred, because the middle of a scan is the part most likely to be the
    // subject the user framed, and so the grain they mean.
    const cv::Rect roi((prepared.cols - sz.width - ov) / 2,
                       (prepared.rows - sz.height - ov) / 2,
                       sz.width + ov, sz.height + ov);
    // A read-only view of the prepared sample. Owning starts where something
    // is actually written: quilt_axis returns its own result, and the
    // zero-overlap branch clones the crop it keeps. Three image-sized copies
    // used to be made where one is needed -- the ROI was cloned, the
    // no-overlap branch cloned that clone, and the field cloned it again.
    cv::Mat crop = prepared(roi);

    if (equalising) {
        // Refuse before equalising, and on what the high-pass would have left.
        // The transform breaks every tie at random, so its output has almost
        // none whatever went in -- a flat sample comes out as pure shuffle and
        // would sail past the guard below. Whether the sample has texture finer
        // than --mask-highpass is a property of the sample, and that is the
        // question the guard asks; this asks it of the same crop.
        //
        // So the field is the subtractive preparation itself -- high-passed in
        // the sample's pixels, then scaled -- with the crop taken out of it,
        // which lines up because the resize alone decides the size. Filtering
        // the already-scaled crop at `highpass` instead cut at the wrong
        // frequency whenever --mask-scale was not 1: at 4 it removed grain the
        // cutoff was meant to keep and refused textured samples as flat.
        cv::Mat hp = maskutil::prepare_texture_sample(sample, highpass, scale)(roi).clone();
        maskutil::quantise(hp, quantStep);
        const double flip = maskutil::tie_share(hp);
        if (flip > kMaxTieShare) {
            if (log) refuse_untextured(*log, highpass, flip);
            return {};
        }

        // Ranked within its neighbourhood over a margin of 4 sigma, so the
        // tile's edges see the sample beyond them where there is any. Only
        // where the crop reaches the sample's own edge is the neighbourhood
        // truncated. Before the cut, as the high-pass is: nothing may filter
        // once the tile joins.
        const int m = (int)std::ceil(4.0 * equalise);
        const cv::Rect wide = cv::Rect(roi.x - m, roi.y - m, roi.width + 2 * m,
                                       roi.height + 2 * m)
                            & cv::Rect(0, 0, prepared.cols, prepared.rows);
        const cv::Mat region = prepared(wide).clone();
        crop = maskutil::local_quantile(region, equalise, seed)(roi - wide.tl());
        if (log)
            *log << "  each pixel ranked within its " << (int)std::lround(equalise)
                 << " px neighbourhood rather than high-passed\n";
    }

    cv::Mat field;
    if (ov > 0) {
        // Columns first, on the full taller crop: the bottom `ov` rows are
        // about to be spliced in by the row pass, so they need their column
        // join made too. Cutting a tile-height crop and then adding rows would
        // splice in rows that had never been joined.
        field = maskutil::quilt_axis(crop, sz.width, ov, 1);
        field = maskutil::quilt_axis(field, sz.height, ov, 0);
    } else {
        field = crop(cv::Rect(0, 0, sz.width, sz.height)).clone();
    }
    // Nothing filters after this. The cut is a rearrangement of pixels the
    // sample already held, and a filter here would put values between them
    // back in -- which is exactly the sub-level ordering the high-pass above
    // was placed *before* the crop to avoid.

    // Throw away detail the sample could not have recorded, then rank.
    //
    // These two steps are a pair and neither works alone. The high-pass
    // manufactures sub-code detail -- a two-valued sample gives a tile with
    // 13277 distinct values -- and ranking is scale-free, so it draws that blur
    // residue at full contrast as a swing in the local mean. Quantising removes
    // it by collapsing everything finer than one input code back into ties.
    // But ties ranked in pixel order become a gradient across the tile, which
    // is worse than the disease: measured on a flat dithered sample, the
    // finished mask's local-mean spread reads 5.35 untouched, 36.40 quantised
    // and ranked stably, and 0.70 quantised and ranked with the ties shuffled.
    // Film is indifferent to all of it -- 0.1% of its pixels tie at all, and
    // patchfit reads 3.040 against 3.074 -- which is why this went unnoticed.
    // Equalised, the field is already a continuous quantile with its ties
    // shuffled, and quantising it at an input code would collapse it whole.
    if (!equalising)
        maskutil::quantise(field, quantStep);

    // What is left has to be mostly the sample rather than mostly the shuffle.
    //
    // The old test here counted distinct values in the *tile* and demanded 255,
    // one per threshold byte. It was testing a quantity the high-pass defeats:
    // the flat sample above sails past it with 13277. The share of pixel pairs
    // that tie is the honest measure, because after the quantise that is
    // exactly the fraction of the mask's ordering decided by the coin flip.
    //
    // 0.25 sits in an empty gap. Measured over nineteen images: every real film
    // scan lands between 0.006 and 0.065 -- including a fine-grained
    // Panatomic-X, which survives with only 50 distinct levels and would fail
    // any threshold on the count -- while flat fields, ramps and line patterns
    // land between 0.39 and 1.00. Nothing measured falls between 0.16 and 0.39.
    if (!equalising) {
        const double flip = maskutil::tie_share(field);
        if (flip > kMaxTieShare) {
            if (log) refuse_untextured(*log, highpass, flip);
            return {};
        }
    }

    // Flat 0..254 by rank, which is what makes it a threshold mask at all.
    // Ranking is monotone, so a tile that joins before it still joins after.
    const std::vector<int> order = maskutil::rank_order_random(field, seed);
    return maskutil::threshold_mask(order, sz);
}

double direct_work(int w, int h, int overlap)
{
    // A sort of the tile, plus the two cuts. The sort dominates by orders of
    // magnitude at any sane overlap -- a 256 tile sorts 65536 values and its
    // cuts visit about 2*268*12*12 cells -- but the overlap term is here
    // because it is the one that grows quadratically, so a large --set
    // direct.overlap should be priced rather than ignored.
    //
    // This used to be 3*n*log2(n) for two Fourier transforms and a sort, which
    // was right when the seam was repaired by a Poisson solve. There are no
    // transforms now, so direct.rate had to be re-measured with it.
    const double n = (double)w * h;
    const double ov = (double)std::max(0, overlap);
    return n * std::log2(std::max(n, 2.0)) + ov * ov * ((double)w + h);
}

void register_direct_params(params::Params& P)
{
    // Named once. inpaint has always done this; the others repeated the
    // expression at every rule, which buries what each rule *says* under
    // what they all have in common.
    const auto mine = mask::generator("direct");

    // Re-measured when the seam repair changed: two Fourier transforms went
    // away and a dynamic program arrived, and the work formula changed with
    // them, so the old figure was a rate for a different unit. Taken as the
    // marginal cost between a 512 and a 1024 tile, which cancels process
    // start-up; --budget refines it from real builds in any case.
    //
    // Equalising is not in direct_work and this figure was not re-measured for
    // it: about 7 ms at a 256 tile and 0.2 s at a 1012 one, one weighted count
    // per grey level on a coarse grid. --budget refines the rate from real
    // builds, so the error is transient. Worth knowing rather than worth a term.
    params::add_const(P, direct::keys::Rate, 1.6e8,
                      mine, {{1.0, 1e15}},
                      "measured build throughput, work units per second",
                                        true);   // an intermediate: reaches the mask only via --budget

    // A tile of real grain repeats as visibly as markov's does -- it is real
    // grain -- so it gets markov's limit rather than blue's.
    params::add_const(P, direct::keys::MaxRepeats, (long long)4,
                      mine, {{1, 1024}},
                      "tile repeats the grid may show before it looks regular");

    // Bounded by the sample rather than by time: a bigger tile costs almost
    // nothing to cut, and the build refuses when the sample cannot supply it.
    params::add_const(P, direct::keys::MaxSize, (long long)1024,
                      mine, {{16, 8192}},
                      "largest tile worth building, absent a --budget");

    // How wide a band the seam is cut through. Two sigma of the high-pass that
    // produced the field, because that is the scale over which it stays
    // correlated and so the scale over which the cut has independent places to
    // go. Scaled, since --mask-scale resamples the sample and moves that scale
    // with it -- the same reasoning markov's field high-pass follows.
    //
    // The floor of 8 is structural, not measured: the sweep in test/seamlab
    // found the cut sound at every width from 4 px to 64, so this is only
    // making sure the dynamic program has somewhere to go. It is also doing
    // real work in one case it cannot justify -- with --mask-highpass 0 the
    // field keeps the photograph's own low frequencies, its correlation length
    // is tens of pixels, and two sigma is zero. `direct` with the high-pass off
    // is already degenerate, since the mask then inherits the subject rather
    // than the grain, but 8 is a guess there rather than a derivation.
    //
    // 0 disables the repair and takes a plain centred crop -- with
    // --direct-equalise 0 as well, the test/refmask behaviour, and what
    // test/masktest measures the correction against.
    //
    //
    // Under --direct-equalise nothing is high-passed, and this still reads
    // --mask-highpass, which is deliberate: what the overlap wants is the
    // field's correlation length, and the high-pass figure is this program's
    // standing claim about where a sample's grain ends.
    // One input code, less a hair, so every difference the sample could record
    // survives and everything finer -- which is the high-pass's own arithmetic
    // rather than data -- collapses into ties for the ranking to shuffle.
    //
    // A flat constant rather than a rule, on the evidence: on film the step
    // barely matters at all (patchfit 3.040 unquantised, 3.074 at 0.9, 3.050 at
    // 0.3), while on a sample with no texture it decides everything. There is
    // nothing here for a rule to fit.
    //
    // One caveat it cannot express. prepare_texture_sample high-passes and
    // *then* resizes, so under --mask-scale below 1 the interpolator averages
    // several already-high-passed values and the real floor drops below one
    // code -- roughly with the square of the scale, though not exactly, since
    // it depends on how the resampling lands. Lower this by hand there if the
    // grain looks coarser than it should. 0 disables the rounding entirely.
    //
    // Under the default --direct-equalise the field itself is never quantised:
    // it is a continuous quantile with its ties already shuffled. This then
    // decides only the texture check, which is measured on the high-passed
    // crop whichever preparation builds the mask.
    params::add_const(P, direct::keys::Quantise, 0.9,
                      mine, {{0.0, 16.0}},
                      "round the high-passed tile to this grid, in input codes, "
                      "before the texture check (and before ranking, at "
                      "--direct-equalise 0)");

    // The sample is equalised, not high-passed: each pixel becomes its quantile
    // within a Gaussian neighbourhood (maskutil::local_quantile). This is the
    // one place the program does not high-pass, because `direct` is the one
    // generator whose field *is* the mask -- ranked straight onto the threshold
    // scale, so any neighbourhood whose distribution differs from the tile's
    // becomes a patch of the wrong density. The other three fit a model with no
    // term for that.
    //
    // A high-pass equalises a neighbourhood's mean and leaves its amplitude,
    // its shape and any clipping. The polynomial flattening this replaced
    // divided by a fitted mean surface on the premise that grain amplitude is
    // proportional to the mean, and on these scans it is not: PanatomicX's
    // amplitude is 4.8 at the top of its crop and 4.4 at the bottom while the
    // mean falls 47 to 27, so dividing *made* an amplitude gradient, 3.1 to 5.2,
    // and the ranked tile came out three times as dense at its bottom edge as
    // its top. Tiled, that is a line at every seam.
    //
    // sigma is four times the high-pass's own for --mask-highpass, which is
    // 24 px at the default. Equalising forces every neighbourhood of this size
    // to the same density, so whatever clumping the grain has at larger scales
    // than sigma is removed with the drift. By grainfit `raw` over the fourteen
    // crops (lower is closer; block is the window the shape is measured in):
    //
    //   sigma        0     12     16     24     32
    //   block 12  2.48   1.91   1.86   1.86   1.89
    //   block 48  3.88   2.14   1.81   1.62   1.60
    //
    // A 12 px block cannot see the scale being chosen, and that is the
    // measurement that once put this at 12.
    //
    // The cost is picture content: a wider window lets through more structure
    // the window is smaller than, so the crop matters more. test/findcrops
    // searches for windows that are clean at this sigma.
    //
    // 0 restores the subtractive high-pass, byte for byte.
    P.add({ direct::keys::Equalise,
            [](const params::Resolver& r) -> params::Value {
                return 4.0 * maskutil::kHighpassSigmaPerPixel
                     * r.num(mask::keys::Highpass) * r.num(mask::keys::Scale);
            },
            mine,
            {{0.0, 4096.0}},
            "rank each pixel within a neighbourhood of this sigma, in pixels, "
            "instead of high-passing; 0 high-passes (--mask-highpass)" });

    P.add({ direct::keys::Overlap,
            [](const params::Resolver& r) -> params::Value {
                const double sigma = maskutil::kHighpassSigmaPerPixel
                                   * r.num(mask::keys::Highpass) * r.num(mask::keys::Scale);
                return (long long)std::max(8L, std::lround(2.0 * sigma));
            },
            mine,
            {{0, 4096}},
            "width of the band the tile's seam is cut through, in pixels" });

    // Measured from this machine's own builds, never chosen, so it is
    // reachable by --set and is not offered as an option.
    P.hide_option(direct::keys::Rate);
}
