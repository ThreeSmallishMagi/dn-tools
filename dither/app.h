// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The whole program, minus the process entry point. `main.cpp` is a four-line
// wrapper around this, which is what lets everything else live in a library
// that a test can link against — see doc/ARCHITECTURE.md, S1. Returns a
// process exit status: 0 on success, -1 on any handled failure.
int dither_main(int argc, char** argv);
