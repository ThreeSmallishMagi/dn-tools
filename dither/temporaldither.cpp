// SPDX-License-Identifier: GPL-3.0-or-later
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <algorithm>

// Widened, because `value * targetmax` overflows 32 bits well inside the
// depths this accepts, and a shift by 32 is undefined rather than zero.
#define bitmax(depth) ((1ull << (depth)) - 1ull)

// Dithering strategies
typedef enum { DITHER_ROUND=0, DITHER_FLOOR =1, DITHER_RANDOM=2 } dither_strategy;

// Double and 64-bit throughout, because at 31 bits a level is past what a float
// holds exactly (2^31 - 1 rounds up to 2^31) and past what an int holds at all.
// A strategy's answer is signed: the accumulator goes below zero whenever the
// target is under one level and a frame has just shown a whole one, and its
// floor is then -1, which converted to an unsigned type ran the display up to
// full brightness. The clamp in dither() decides what is actually shown.
typedef unsigned long long level;

long long dither_round(double accumulator) {
    return (long long)floor(accumulator + 0.5);
}

long long dither_floor(double accumulator) {
    return (long long)floor(accumulator);
}

long long dither_random(double accumulator) {
    const double whole = floor(accumulator);
    const double random_threshold = (double)rand() / RAND_MAX;
    return (long long)whole + (accumulator - whole > random_threshold ? 1 : 0);
}

 /*  e.g. 3bit image to 2bit display
  *  Source/desired brightness: value / sourcemax  e.g. 2/7
  *  Target/display brightness: display / targetmax e.g. some d/3
  *     therefore desired display brightness is  value * targetmax / sourcemax  e.g. d=2*3/7
  *     this is tracked in desired_brightness
  */
void dither(level value, unsigned sourcedepth, unsigned targetdepth, unsigned frames,
                        long long (*dither_fn)(double) = dither_round)
{
    const level sourcemax = bitmax(sourcedepth);
    const level targetmax = bitmax(targetdepth);
    const double desired_brightness = (double)value * targetmax / sourcemax;

    // Clamped to the levels the display has at both ends. Clamping the
    // accumulator's choice keeps its ledger exact: the shortfall stays in it.
    const auto shown = [&](double v) {
        return (level)std::min(std::max(dither_fn(v), 0LL), (long long)targetmax);
    };

    double accumulator = 0.0;
    level total_displayed_acc = 0;
    level total_displayed_ave = 0;

    printf("value:%llu/%llu ideal target:%.2f\n", value, sourcemax, desired_brightness);
    printf("%-5s %-7s %-13s %-13s | %-10s %-13s %-13s %-13s\n", "frame", "display", "accumulator", "overall error",
        "display_ave", "da_average", "error ave", "total error");

    for (unsigned frame = 1; frame <= frames; frame++) {
        // accumulator method
        accumulator += desired_brightness;
        level display_acc = shown(accumulator);   // Use dithering strategy
        accumulator -= display_acc;               // Keep fractional error
        // ---
        total_displayed_acc += display_acc;           // unused for calculation
        double display_average_acc = (double)total_displayed_acc / frame;
        double overall_error_acc = desired_brightness - display_average_acc;

        // average display method
        level display_ave = shown(desired_brightness);
        if (display_ave < targetmax
            && (double)(display_ave + total_displayed_ave) < desired_brightness*frame - 0.5)
            display_ave++;
        else if (display_ave > 0
                 && (double)(display_ave + total_displayed_ave) > desired_brightness*frame + 0.5)
            display_ave--;
        total_displayed_ave += display_ave;
        double display_average_ave = (double)total_displayed_ave/frame;
        double overall_error_ave=  (desired_brightness - display_average_ave)/frame;

        printf("%-5u %-7llu %-13.2f %-13.4f | %-10llu %-13.2f %-13.3f %-13.2f\n", 
               frame, display_acc, accumulator, overall_error_acc,
            display_ave, display_average_ave, overall_error_ave, overall_error_ave*frame*sourcemax);
    }
}

int main(int argc, char *argv[])
{
    if (argc < 5) {
        printf("Usage: %s <source bits> <target bits> <value> <dithering strategy>  (round:0, floor:1, random:2)\n", argv[0]);
        return 1;
    }
    // Checked rather than assumed: a depth of 0 divides by zero, a depth of 32
    // shifts a 32-bit value by its own width, and a value above the source's
    // range is not a brightness. All three are reachable with atoi and a
    // plausible command line.
    auto number = [&](const char* s, long lo, long hi, const char* what) -> long {
        char* end = nullptr;
        const long v = strtol(s, &end, 10);
        if (end == s || *end || v < lo || v > hi) {
            printf("%s must be a whole number in [%ld, %ld], not '%s'\n", what, lo, hi, s);
            exit(1);
        }
        return v;
    };
    int source = (int)number(argv[1], 1, 31, "source bits");
    int target = (int)number(argv[2], 1, 31, "target bits");
    level value = (level)number(argv[3], 0, (long)bitmax(source), "value");
    dither_strategy strategy = (dither_strategy)number(argv[4], 0, 2, "strategy");
    if( argc ==6)
        srand((unsigned)number(argv[5], 0, 2147483647, "seed"));

    printf("\n\nSource %d bit, Target %d bit strategy:%d\n", source, target, strategy);
    // Enough rows to see the accumulator settle, which is what the table is for.
    const int frames = 40;
    switch (strategy) {
        case DITHER_ROUND:  dither(value, source, target, frames, dither_round);  break;
        case DITHER_FLOOR:  dither(value, source, target, frames, dither_floor);  break;
        case DITHER_RANDOM: dither(value, source, target, frames, dither_random); break;
        default:
            // Anything else printed the header and stopped, successfully.
            printf("Unknown strategy %d: expected round:0, floor:1 or random:2\n",
                   (int)strategy);
            return 1;
    }
    return 0;
}

