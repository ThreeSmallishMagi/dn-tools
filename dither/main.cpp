// SPDX-License-Identifier: GPL-3.0-or-later
// The process entry point, and deliberately nothing else: everything it calls
// lives in libdither so that tests can reach it without going through a
// subprocess and a PNG. See doc/ARCHITECTURE.md.
#include <cstdio>
#include <exception>
#include "app.h"

int main(int argc, char** argv)
{
    try {
        return dither_main(argc, argv);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return -1;
    }
}
