#!/bin/sh
set -eu

cmake -B build
cmake --build build
if command -v pandoc >/dev/null 2>&1; then
    pandoc -s -t man README.md | man -l -
else
    less README.md
fi
