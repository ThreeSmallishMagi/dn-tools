# Defects found while reviewing the test coverage plan

Reviewed 2026-10-20 against working-tree source at HEAD `194cb97`.

I followed the entries in [TestCoverage.md](TestCoverage.md), reading the implementations, callers, inline helpers and declarations across its 48 source/header files. The findings below are defects supported by source and targeted reproductions, not a list of tests that are merely missing. The function inventory contains 365 named entries, including deleted operations and the stale declaration, plus 61 lambdas and 136 declaration links. Header declarations were checked against their implementations rather than treated as separate implementations.

The current CMake build succeeds and **all 60 existing unit cases pass**. Those checks do not cover the failures below. No production code was changed. This was source review with focused probes, not exhaustive execution of the coverage plan, a full sanitizer campaign, or a perceptual quality study.

P2 means a functional defect worth fixing; P3 means a lower-impact defect in a dormant helper, demonstration, or archived experiment. No P1 production failure was established.

## Findings

### F01 — P2: Loading texture samples silently destroys 16-bit detail

**Location:** [samples.cpp](../samples.cpp), `Set::read`, line 108. Callers include `dither_main` and all three sample-based calibration entry points.

`imread(..., IMREAD_GRAYSCALE)` converts native 16-bit samples to 8 bits before any generator sees them. The main input loader and `maketexture` explicitly preserve depth, so the same scan is treated differently depending on which workflow loads it. Low-contrast grain can disappear completely.

**Reproduced:** a seeded 64×64 16-bit texture with values in 32768..33023 loads as a constant 8-bit image with min=max=128. A direct-mask run on it fails with “100% of this mask's ordering would be decided at random”. The source contains real variation; the loader removed it. Markov, DFT, inpaint and calibration also receive the reduced data.

**Fix:** preserve native depth and convert samples to a common floating code scale before pooling different depths. Simply adding `IMREAD_ANYDEPTH` without defining that common scale would make mixed 8/16-bit scans and `direct.quantise` inconsistent.

**Regression:** load the same low-contrast fixture through `Set::read` and `Set::of`; compare prepared values/ranks under the chosen scale convention. Include mixed-depth equivalents and a CLI direct-mask case that retains genuine texture.

### F02 — P2: Inpaint loses pooled scan contrast at every coarse level

**Location:** [inpaint.cpp](../inpaint.cpp), `downsample` lines 58–73, `downsample_set` lines 79–85, and `synthesise_inpaint_field` lines 375 and 433; contract in [samples.h](../samples.h), lines 103–108.

The finest reference is ranked over all scans together, deliberately preserving their relative contrasts. At a coarse level, `downsample_set` calls `downsample` separately for each scan; that helper re-ranks each scan independently onto the entire unit range. This removes exactly the cross-scan contrast information that pooled ranking was introduced to preserve. Default `inpaint.scales=2` reaches this path whenever multiple scans are supplied.

**Reproduced against the actual private helper:** two equal-size, equal-mean fields with a tenfold amplitude difference have standard deviations 0.161122 and 0.375108 after pooled ranking (ratio 2.3281). After `downsample_set(..., 2)`, both are 0.288675 (ratio 1). The coarse database therefore learns a different cross-scan contrast model. The final visual quality impact has not been quantified.

**Fix:** separate block averaging from ranking. Average every scan, then rank the resulting set together. Preserve the existing single-scan arithmetic if pixel compatibility is required.

**Regression:** unequal-contrast, equal-mean scans at factors 1, 2 and 4; assert pooled ranking of block means, not identical per-scan marginal distributions. Exercise directory input with scales 1 and 2.

### F03 — P2: Advertised mask-width and mask-height flags are rejected

**Location:** [defaults.cpp](../defaults.cpp), `Params::flag_for` and `key_for_flag`, lines 139–159; [maskparams.h](../maskparams.h), size keys; [cli.cpp](../cli.cpp), generated-option dispatch at line 348.

`flag_for("mask.size.w")` emits `--mask-size-w`, but the reverse function produces `mask.size_w`. The registry contains the former key, so the emitted flag is unknown. Height fails in the same way. Both flags appear in `--list-options` and therefore in generated completion.

**Reproduced:**

```bash
B=/tmp/dither-coverage-build
"$B/dither" --list-options | grep -- '--mask-size-'
# --mask-size-h
# --mask-size-w
"$B/dither" --no-config --explain --mask-size-w 32
# Unknown option: --mask-size-w; exit 255
```

**Fix:** resolve generated flags through a registry-built flag-to-key map, detecting collisions. Alternatively hide these two flags and expose only the supported `--mask-size WxH` spelling. Do not globally replace every hyphen with a dot: underscore keys would then break.

**Regression:** every advertised flag must parse; every exposed registry key must round-trip through its emitted flag. Include nested dots and underscores.

### F04 — P2: `--mask-out` can succeed without producing the requested file

**Location:** [cli.cpp](../cli.cpp), line 297; [dither.cpp](../dither.cpp), lines 225–229.

The parser stores `maskOut` outside the parameter/applicability machinery. It neither implies mask thresholding nor conflicts with an explicitly selected non-mask threshold. The writer only runs inside the mask branch. A request for a mask file is silently ignored under the default round threshold.

**Reproduced:**

```bash
"$B/dither" examples/ramp_256x256.png --no-config \
  --mask-out /tmp/review-missing-mask.png --out /tmp/review-round.png
# exit 0; dither image written; requested mask absent
```

**Fix:** make the request participate in reconciliation, or reject it clearly unless a mask will be used. An explicit incompatible threshold should produce an error. Keep `--explain`'s intentional no-writing behavior.

**Regression:** standalone `--mask-out`, an explicitly conflicting round/random threshold, and normal generated/loaded-mask exports. Check both status and file existence.

### F05 — P2: Numeric-looking sample and mask paths are rewritten

**Location:** [cli.cpp](../cli.cpp), `parseOptions::set_typed`, lines 198–205; [defaults.cpp](../defaults.cpp), numeric-to-string formatting.

Generated string options are typed from the spelling of their value, rather than the parameter's declared type. A path or library name `0001` becomes integer 1; `1e3` becomes a formatted number. The subsequent resolver looks for a different file or sample library. Quoting the argument does not help because this conversion occurs after shell parsing.

**Reproduced:**

```bash
"$B/dither" --no-config --explain --mask-sample 0001
# mask.sample is displayed/stored as 1, not 0001
```

The same dispatch handles `--mask-in` and string values passed through `--set`. Explicitly prefixing a relative path with `./` avoids the conversion, but does not fix named sample libraries.

**Fix:** carry value types in the registry; parse string parameters as strings regardless of their contents. Share this logic with `maketexture` rather than maintaining two text-shape parsers.

**Regression:** actual files/directories and named libraries called `0001` and `1e3`; verify exact preservation and successful loading through both generated flags and `--set`.

### F06 — P2: Budget sizing rounds an affordable size up to an unaffordable rung

**Location:** [mask.cpp](../mask.cpp), `mask_side`, lines 115–130.

The budget search prices a halving sequence based on the image grid. After selecting an affordable side, it rounds that side **up** to the cache-size ladder without checking its cost again. This can violate the budget even when no quality floor requires the larger tile.

**Reproduced:** use a nominal 512×512 grid, DFT's default 20 iterations, rate 100,000,000 work units/s and budget 1 second:

```bash
"$B/dither" --no-config --explain --mask-method dft \
  --mask-sample examples/Kodak-4X-1.png --budget 1 \
  --set dft.rate=100000000
# mask.size.w = mask.size.h = 1024
```

The cost formula prices 512² at **0.9437184 s**, but the selected 1024² at **4.194304 s**. All floors in this case are 128; the available 256 rung costs **0.2097152 s** and satisfies them. This is a policy calculation error, independent of real-world timing uncertainty. The documented decision to let quality floors override budgets is not itself being reported as a defect.

**Fix:** price actual ladder candidates. Select an affordable candidate satisfying the floors where possible; only exceed the budget for an explicit floor policy, and report that reason.

**Regression:** deterministic injected rates and grids between ladder rungs; assert the selected rung fits the estimate whenever an affordable floor-satisfying rung exists.

### F07 — P2: Large frame counts lose a unit from the supposedly exact temporal budget

**Location:** [engine.cpp](../engine.cpp), `quantize_scan`, line 202, and the goal pass in `ditherPass`, lines 268–274.

Goals are stored in `CV_64F`, but the shared scan immediately narrows the returned goal to `float` before rounding. Near and above 2²⁴, a float cannot distinguish adjacent integer display budgets. Keeping the storage and running total in double does not recover the lost unit.

**Reproduced:** a single 16-bit pixel with code **43118**, linear decoding, 256 sRGB-spaced levels, round threshold, no diffusion and **100000 frames**. The decoded target times frame count is **16777430.725098**; the expected rounded budget is **16777431**, while the sum of raw output frame levels is **16777430**. This uses supported settings. The visual error is tiny, but it contradicts the exact-budget invariant checked by the existing tests at smaller frame counts.

**Fix:** retain double precision through goal quantization and rounding, including the threshold interface or a separate goal-quantization path. Audit diffusion residuals in that pass as well.

**Regression:** one-pixel tests around half-integers and the float integer-precision boundary, at the supported maximum frames/levels, comparing raw integer totals to a double-precision oracle.

### F08 — P3: The three-channel sRGB encoder rounds twice

**Location:** [srgb.cpp](../srgb.cpp), `vision::linear_to_srgb(const cv::Mat&)`, line 201.

The helper adds 0.5 and then calls `cv::saturate_cast<unsigned char>`, which already rounds floating values. This biases results upward. It is currently unused by the production dither pipeline, so this is a public-helper defect rather than an explanation for current grayscale output differences.

**Reproduced:** a three-channel 0..255 ramp passed through the Mat decode and encode helpers changes **96 of 256 codes**. Examples: 1→2, 3→4, 5→6, 7→8, 9→10.

**Fix:** round once, consistently with the scalar/gray helpers. Remove the extra half-unit adjustment when using a rounding cast.

**Regression:** all 256 codes round-trip through the Mat helpers on all three channels; test rounding boundaries and clipping separately.

### F09 — P2: The public Markov calibration function cannot be linked

**Location:** [markov.h](../markov.h), line 90, versus [markovcalib.cpp](../markovcalib.cpp), line 116.

The header declares `calibrate_markov(const cv::Mat&, ...)`; the definition accepts `const samples::Set&`. A client following the header compiles but fails to link. The normal CLI succeeds because its caller is in the same translation unit as the Set implementation. This was already identified in the coverage document and remains present.

**Reproduced:** compile a client including `markov.h` and calling `calibrate_markov(sample, p, false, false, 1, std::cerr)` against `libdither_lib.a`: the linker reports an undefined reference to the Mat signature.

**Fix:** either expose the Set signature consistently, or retain a Mat overload that forwards through `samples::Set::of`. If this is intentionally private, remove the misleading public declaration and make the implementation private.

**Regression:** a separate translation-unit public-header compile/link test. Calling only the internal CLI wrapper cannot detect this mismatch.

### F10 — P3: The temporal demonstration accepts inputs that invoke undefined behavior

**Location:** [temporaldither.cpp](../temporaldither.cpp), `main` lines 79–82, `bitmax` expansion at lines 35–36, and multiplication at line 37.

Depths and values are parsed with `atoi` and not validated. A source depth of 32 shifts a 32-bit unsigned value by 32; zero depth also creates a zero denominator. Even otherwise plausible wider depth/value combinations can overflow `value * targetmax` before conversion to float. This affects the separate demonstration executable, not `dither --frames`.

**Reproduced:**

```bash
c++ -std=c++17 -fsanitize=undefined,float-cast-overflow \
  -fno-sanitize-recover=all temporaldither.cpp -o /tmp/review-temporal
/tmp/review-temporal 32 2 128 0
# runtime error: shift exponent 32 is too large for 32-bit type 'unsigned int'
```

**Fix:** parse strictly, validate supported bit depths and value range, and widen the product before multiplying. Either explicitly support 32-bit endpoints using a wider representation or reject them.

**Regression:** malformed/negative/zero/32-or-greater depths, out-of-range values, and valid high-depth cases whose product exceeds 32 bits. Run these under UBSan.

### F11 — P3: The archived Markov experiment no longer compiles

**Location:** [experiments/markov-20260927/draw.cpp](../experiments/markov-20260927/draw.cpp), line 36.

`fit_markov_model(img, ...)` still passes a Mat to the Set-based API. A syntax-only compile fails with “invalid initialization of reference of type const samples::Set& from expression of type cv::Mat”. This file is not a CMake target; the production build is unaffected. The coverage document already records this incompatibility.

**Fix:** pass `samples::Set::of(img)`, or clearly retire the experiment. Its normal build should then be checked separately if it remains a supported research tool.

**Regression:** compile the archived driver against the current public/internal headers before using it for another parameter study.

## Verification and review limits

- Rebuilt `/tmp/dither-coverage-build` from the current working tree; all CMake targets succeeded.
- Ran `unittest examples/ramp_256x256.png`: **60/60 pass**.
- Used linked C++ probes for sample depth, Mat round-trip, mask sizing and exact frame totals; their source is below.
- Called the actual inpaint coarse helper in a separate test translation unit, included below.
- Checked CLI return codes, generated-option inventory, resolved values and requested output existence.
- Re-ran the separate public-API link failure and archived-driver compile failure.
- Used UBSan on the small temporal demonstration reproduction only. Production generators were not exhaustively sanitizer-tested.

I did not promote the following concerns to confirmed findings: generic registry memoization after mutation without a demonstrated production failure; cache metadata not being authoritative; intentional bottom-edge diffusion loss; deliberate mask histogram end bins; and arbitrary invalid inputs outside helper preconditions. Calibration scores and generated texture appearance still need corpus-based validation. A clean source review entry is not proof that the function is defect-free.

## Review ledger

Every file represented in the coverage document was inspected. “No additional confirmed defect” means no finding established in this review, not test coverage or a correctness guarantee. Shared findings cover both the implementation and the relevant declarations/callers.

| File | Result |
|---|---|
| [app.h](../app.h) | No additional confirmed defect |
| [bluenoise.cpp](../bluenoise.cpp) | No additional confirmed defect |
| [bluenoise.h](../bluenoise.h) | No additional confirmed defect |
| [calib.cpp](../calib.cpp) | No additional confirmed defect |
| [calib.h](../calib.h) | No additional confirmed defect |
| [cli.cpp](../cli.cpp) | F03, F04, F05 |
| [cli.h](../cli.h) | F03–F05 parser contract |
| [defaults.cpp](../defaults.cpp) | F03, F05 |
| [defaults.h](../defaults.h) | F03/F05 registry contract |
| [dftcalib.cpp](../dftcalib.cpp) | F01 sample-loading caller; no additional confirmed calibration defect |
| [dftcalib.h](../dftcalib.h) | No additional confirmed defect |
| [directmask.cpp](../directmask.cpp) | No additional confirmed defect |
| [directmask.h](../directmask.h) | No additional confirmed defect |
| [dither.cpp](../dither.cpp) | F01/F04 callers; frame pipeline traced for F07 |
| [engine.cpp](../engine.cpp) | F07 |
| [engine.h](../engine.h) | F07 float threshold interface |
| [experiments/inpaint-20260927/thread_control.cpp](../experiments/inpaint-20260927/thread_control.cpp) | No additional confirmed defect |
| [experiments/markov-20260927/draw.cpp](../experiments/markov-20260927/draw.cpp) | F11 |
| [generators.cpp](../generators.cpp) | F01/F02 dispatch; no additional confirmed defect |
| [generators.h](../generators.h) | No additional confirmed defect |
| [inpaint.cpp](../inpaint.cpp) | F02 |
| [inpaint.h](../inpaint.h) | F02 caller/API contract |
| [inpaintcalib.cpp](../inpaintcalib.cpp) | F01 sample-loading caller; no additional confirmed calibration defect |
| [inpaintcalib.h](../inpaintcalib.h) | No additional confirmed defect |
| [main.cpp](../main.cpp) | No additional confirmed defect |
| [maketexture.cpp](../maketexture.cpp) | Compared native-depth loading for F01 and parser typing for F05; no additional confirmed defect |
| [markov.cpp](../markov.cpp) | No additional confirmed defect |
| [markov.h](../markov.h) | F09 |
| [markovcalib.cpp](../markovcalib.cpp) | F01/F09 |
| [markovcalib.h](../markovcalib.h) | No additional confirmed defect |
| [markovmodel.h](../markovmodel.h) | F11 changed fit interface |
| [mask.cpp](../mask.cpp) | F06 |
| [mask.h](../mask.h) | F06 policy boundary |
| [maskcache.cpp](../maskcache.cpp) | No additional confirmed defect |
| [maskcache.h](../maskcache.h) | No additional confirmed defect |
| [maskparams.h](../maskparams.h) | F03 nested size keys |
| [maskutil.cpp](../maskutil.cpp) | No additional confirmed defect |
| [maskutil.h](../maskutil.h) | No additional confirmed defect |
| [options.h](../options.h) | No additional confirmed defect |
| [samples.cpp](../samples.cpp) | F01; pooled ranking checked for F02 |
| [samples.h](../samples.h) | F01/F02 contracts |
| [spectralmask.cpp](../spectralmask.cpp) | No additional confirmed defect |
| [spectralmask.h](../spectralmask.h) | No additional confirmed defect |
| [srgb.cpp](../srgb.cpp) | F08; F07 decoding/encoding checked |
| [srgb.h](../srgb.h) | F08 Mat API/scalar comparison |
| [srgbtest.cpp](../srgbtest.cpp) | No additional confirmed defect |
| [temporaldither.cpp](../temporaldither.cpp) | F10 |
| [unittest.cpp](../unittest.cpp) | Existing assertions inspected; gaps demonstrated by F01–F10, all 60 cases pass |

## Focused reproduction sources

These are review probes, not production test additions. Run from the repository root with the current CMake library. Save the first block as `/tmp/review_probe.cpp` and compile/run with:

```bash
c++ -std=c++17 -I. /tmp/review_probe.cpp \
  /tmp/dither-coverage-build/libdither_lib.a \
  $(pkg-config --cflags --libs opencv4) -o /tmp/review_probe
/tmp/review_probe
```

It creates `/tmp/review-sample16.png`; use that fixture with the direct-mask command below. The budget probe stops at the first selected precision-boundary case.

```cpp
#include "srgb.h"
#include "samples.h"
#include "engine.h"
#include "mask.h"
#include "generators.h"
#include "maskparams.h"
#include <opencv2/opencv.hpp>
#include <iostream>
#include <cmath>
int main(){
 cv::Mat ramp(1,256,CV_8UC3); for(int i=0;i<256;i++)ramp.at<cv::Vec3b>(0,i)=cv::Vec3b(i,i,i);
 auto back=vision::linear_to_srgb(vision::srgb_to_linear(ramp));int bad=0;for(int i=0;i<256;i++)if(back.at<cv::Vec3b>(0,i)[0]!=i){if(bad<5)std::cout<<"roundtrip "<<i<<" -> "<<(int)back.at<cv::Vec3b>(0,i)[0]<<"\n";bad++;}std::cout<<"roundtrip bad="<<bad<<"/256\n";
 cv::Mat sample(64,64,CV_16U);cv::RNG r(42);r.fill(sample,cv::RNG::UNIFORM,32768,33024);cv::imwrite("/tmp/review-sample16.png",sample);
 auto loaded=samples::Set::read({"/tmp/review-sample16.png"},nullptr);double lo,hi;cv::minMaxLoc(loaded[0],&lo,&hi);std::cout<<"sample original depth="<<sample.depth()<<" loaded depth="<<loaded[0].depth()<<" range="<<lo<<".."<<hi<<"\n";
 auto L=vision::make_levels(256,false,vision::Spacing::Srgb);
 for(int code=43000;code<65535;code++){cv::Mat src(1,1,CV_16U,cv::Scalar(code));auto t=vision::decode_display(src,L,false);double goal=(double)t.at<float>(0,0)*100000;float f=goal;if(std::floor(goal+.5)==std::lround(f))continue;RoundDither fn;auto frames=ditherPass(t,L,fn,spatial_none,0,256,100000,-1,-1);long long sum=0;for(auto &m:frames)sum+=m.at<unsigned char>(0,0);std::cout<<"budget code="<<code<<" goal="<<std::fixed<<goal<<" rounded="<<std::floor(goal+.5)<<" actual="<<sum<<"\n";break;}
 params::Params p;for(auto &g:generators())g.register_params(p);register_mask_params(p);p.set(mask::keys::Method,std::string("dft"),params::Origin::User);p.set(mask::keys::ImageW,512LL,params::Origin::Measured);p.set(mask::keys::ImageH,512LL,params::Origin::Measured);p.set(mask::keys::Budget,1.0,params::Origin::User);p.set("dft.rate",100000000.0,params::Origin::Measured);std::cout<<"budget mask side="<<p.integer(mask::keys::SizeW)<<"\n";
}
```

```bash
"$B/dither" examples/ramp_256x256.png --no-config \
  --mask-method direct --mask-sample /tmp/review-sample16.png \
  --mask-size 32x32 --direct-overlap 0 --mask-highpass 0 \
  --out /tmp/review-direct.png
```

For F02, save the following as `/tmp/review-inpaint.cpp` and compile with the same command, substituting the source and output filenames. Including the implementation gives this one-off probe access to the actual private helper; no production visibility change is needed.

```cpp
#include "inpaint.cpp"
#include <iostream>
int main(){
 cv::Mat a(64,64,CV_32F),b(64,64,CV_32F);
 for(int y=0;y<64;y++)for(int x=0;x<64;x++){float v=(x%16)-7.5f;a.at<float>(y,x)=v;b.at<float>(y,x)=v*10;}
 auto pooled=samples::Set::of(std::vector<cv::Mat>{a,b}).ranked_unit();auto coarse=downsample_set(pooled,2);
 for(auto s:{pooled,coarse}){cv::Scalar m,sa,sb;cv::meanStdDev(s[0],m,sa);cv::meanStdDev(s[1],m,sb);std::cout<<"stddevs="<<sa[0]<<","<<sb[0]<<" ratio="<<sb[0]/sa[0]<<"\n";}
}
```
