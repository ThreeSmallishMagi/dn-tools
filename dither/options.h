// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
#include <string>

// Everything the command line settled, as one value the pipeline passes around.
//
// It duplicates about a dozen fields that also live in params::Params, and that
// duplication is deliberate: it is a *read-only* cache filled after the whole
// command line has been applied, because keeping a second writable copy
// silently broke --set. See doc/ARCHITECTURE.md, 6b.
struct Options {
    std::string inPath;
    std::string threshold = "round";
    std::string outPath = "dither.png";
    bool useSrgb = true;
    int levels = 2;
    int frames = 1;
    bool saveFrames = false;
    double spatial = 1.0;   // fraction of residual diffused spatially (1 = full FS, 0 = none);
                            // resolved in parseOptions, which defaults it to 0 when
                            // --threshold names a threshold explicitly
    uint64_t seed = 42;
    std::string levelSpacing = "srgb";    // where the display levels sit:
                                          // srgb | linear | device
    int supersample = 1;    // dither at n x resolution, then average back down
    int outSize = 0;        // longer edge of the output; 0 means the input's size
    std::string maskIn;     // empty = build the mask instead of loading one
    std::string maskSample; // sample for a generator that takes one
    std::string maskMethod; // blue | dft | markov | inpaint | direct; empty until parseOptions
                            // resolves it, which needs the whole command line
    double mask_scale = 1.0;
    double mask_highpass = 32.0;   // drop sample structure coarser than this; the
                                   // registry's per-generator default replaces it
    std::string maskOut;    // empty = don't write the mask
    int debug_row = -1;
    int debug_col = -1;
    bool explain = false;   // print the resolved parameters and stop
    bool calibrate = false; // measure markov.temp rather than deriving it
};

