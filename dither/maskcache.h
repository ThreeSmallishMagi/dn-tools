// SPDX-License-Identifier: GPL-3.0-or-later
// maskcache.h — masks are expensive and deterministic, so build each one once.
//
// A 256x256 inpaint mask costs nine seconds and a void-and-cluster mask ten,
// and the same command line produces the same bytes every time. Storing the
// result beside the rate cache in ~/.config/dither and looking there first
// turns the second run into a file read.
//
// **The whole difficulty is the key**, because the failure mode is not a miss,
// it is a *wrong hit*: a mask built for one configuration served silently for
// another, which is an incorrect image that looks plausible. So the key is
// derived from the parameter registry rather than from a list maintained here.
// Every parameter that applies to the run goes into it, at the value --explain
// would print, which means a new generator or a new parameter is covered on the
// day it is added and nobody has to remember this file exists.
//
// Three things decide a mask and are not parameters, so they are added by hand:
//
//   - **the sample's contents.** mask.sample is a path, and a path says nothing
//     about what is in the file today. Hashed.
//   - **the build.** A generator whose algorithm changes produces different
//     bytes from identical parameters, so the executable's own identity is part
//     of the key. Relinking invalidates the cache, which is what makes it safe
//     to leave on while working on a generator.
//   - **the mask size**, which is a parameter, but is worth naming here because
//     it is the one people expect to be treated as "compatible". It is not: a
//     window of a clustered mask is not histogram-flat even when the whole mask
//     is, so a larger cached mask cannot serve a smaller request.
//
// What is deliberately left out is every parameter a rule declares to be an
// *intermediate* -- one that reaches the mask only by deciding some other
// parameter, which is then in the key in its own right. `image.width` is one:
// it settles mask.size and has no further say, so two pictures of different
// sizes asking for the same mask size want the same mask. `<generator>.rate`
// is another; it is re-measured on most runs, so including it would stop the
// cache ever hitting twice, and it reaches the bytes only through --budget --
// whose own effect arrives as a mask size and a pair count, both parameters in
// their own right.
//
// Which parameters those are is declared on each rule rather than listed here,
// for the same reason the rest of the key is walked out of the registry: a list
// in this file is a second place to forget. It has been forgotten once already
// -- the config writer named three rates by string literal, a fourth generator
// arrived without the fourth string, and --budget could never learn its speed.
#pragma once
#include <iosfwd>
#include <string>
#include <opencv2/core.hpp>
#include "defaults.h"

namespace maskcache {

// What identifies one mask. `name` is the file's stem -- readable parameters
// followed by a digest of all of them -- and `detail` is the full key, written
// beside the mask so a cache entry can be read rather than guessed at.
struct Key {
    bool ok = false;
    std::string name;
    std::string detail;
};

// Where the masks live, given the rate cache's path. Empty in, empty out, which
// is what makes --no-config turn the whole thing off: it already means "neither
// read nor write ~/.config/dither", and this is in there.
std::string dir_for(const std::string& configPath);

// The key for the mask this run is about to build. `samplePath` may be empty
// for a generator that needs none; `crop_first` is passed to samples::resolve, as
// the build does, so the key hashes the files the mask is actually made from. Not ok if the sample could not be read, in
// which case the caller should build and not cache -- the build will report the
// real error.
Key key_for(params::Params& P, const std::string& samplePath, bool crop_first = false);

// The cached mask, or empty. Empty `dir` always misses.
cv::Mat load(const std::string& dir, const Key& k, std::ostream* log);

// Store, evicting least-recently-used entries past the size cap. Failure is
// silent by design: a cache that cannot be written is a slow program, not a
// broken one, and a run that works offline should not fail because $HOME is
// read-only.
void store(const std::string& dir, const Key& k, const cv::Mat& mask);

} // namespace maskcache
