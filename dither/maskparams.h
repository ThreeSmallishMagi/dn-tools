// SPDX-License-Identifier: GPL-3.0-or-later
// maskparams.h — pipeline-level parameter keys, and the applicability tests the
// generators share.
//
// These belong to no single generator: they are the settings that decide which
// generator runs at all. Each generator needs to be able to say "I apply when I
// am the one being used", which is a statement about these, so the names live
// here rather than inside any one of them.
#pragma once
#include <string>
#include "defaults.h"

namespace mask::keys {
inline constexpr auto Threshold   = "threshold";
inline constexpr auto Method      = "mask.method";
inline constexpr auto In          = "mask.in";
inline constexpr auto Out         = "mask.out";
inline constexpr auto Sample      = "mask.sample";
inline constexpr auto Scale       = "mask.scale";
inline constexpr auto Highpass    = "mask.highpass";
inline constexpr auto SizeW       = "mask.size.w";
inline constexpr auto SizeH       = "mask.size.h";
inline constexpr auto Supersample = "supersample";
inline constexpr auto OutSize     = "out.size";
inline constexpr auto ImageW      = "image.width";
inline constexpr auto ImageH      = "image.height";
inline constexpr auto Budget      = "budget";
inline constexpr auto Calibrate   = "mask.calibrate";
inline constexpr auto Seed        = "seed";
inline constexpr auto SampleW     = "sample.width";
inline constexpr auto SampleH     = "sample.height";
}

namespace mask {

// True when a mask is actually going to be built: the mask threshold is in use,
// and no --mask-in has supplied one ready-made.
inline bool building(const params::Resolver& r)
{
    return r.str(keys::Threshold) == "mask" && r.str(keys::In).empty();
}

// Applicability for a parameter that matters whenever the mask threshold is in
// use, whether the mask is built here or loaded ready-made. Narrower than
// building() and broader than any generator: --mask-in is the case, and without
// this it declared no condition at all, so nothing noticed that supplying a mask
// under the default --threshold round meant the mask was never consulted.
inline params::Applicability thresholding()
{
    return { [](const params::Resolver& r) { return r.str(keys::Threshold) == "mask"; },
             "--threshold mask" };
}

// Applicability for a parameter belonging to one generator. The scope phrase
// covers the whole condition, not just the method: these are equally moot when
// --mask-in supplies a mask ready-made, and a message naming only the method
// would send the reader looking in the wrong place.
inline params::Applicability generator(const char* method)
{
    std::string m = method;
    return { [m](const params::Resolver& r) {
                 return building(r) && r.str(keys::Method) == m;
             },
             "building a mask with --mask-method " + m };
}

// Applicability for a facility rather than a setting: it applies when the
// generator in use implements it, whichever that turns out to be.
//
// mask.calibrate was declared generator("markov"), and had to change when dft
// and inpaint gained calibrations of their own. Asking the table whether the
// generator in use *has* the ability never needs changing again.
params::Applicability calibratable();

// Applicability for a parameter shared by every generator that takes a sample.
inline params::Applicability from_sample()
{
    return { [](const params::Resolver& r) {
                 return building(r) && r.str(keys::Method) != "blue";
             },
             "building a mask from a sample" };
}

// Applicability for a parameter that matters whenever a mask is being built.
inline params::Applicability any_generator()
{
    return { [](const params::Resolver& r) { return building(r); },
             "building a mask (--threshold mask, no --mask-in)" };
}

} // namespace mask
