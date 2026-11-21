// SPDX-License-Identifier: GPL-3.0-or-later
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <algorithm>

// 64-bit, since `value * targetmax` overflows 32 bits inside the depths accepted.
#define bitmax(depth) ((1ull << (depth)) - 1ull)

// Dithering strategies
typedef enum { DITHER_ROUND=0, DITHER_FLOOR =1, DITHER_RANDOM=2 } dither_strategy;

// Double, since a float cannot hold a 31-bit level exactly. Signed, since the
// accumulator goes below zero; dither() clamps to the display's levels.
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
    const auto shown = [&](double v) { return (level)std::clamp(dither_fn(v), 0LL, (long long)targetmax); };

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
        if (display_ave < targetmax && (display_ave + total_displayed_ave) < desired_brightness*frame - 0.5)
            display_ave++;
        else if (display_ave > 0 && (display_ave + total_displayed_ave) > desired_brightness*frame + 0.5)
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
    int source = atoi(argv[1]);
    int target = atoi(argv[2]);
    level value = strtoull(argv[3], nullptr, 10);
    dither_strategy strategy = (dither_strategy)atoi(argv[4]);
    if( argc ==6)
        srand(atoi(argv[5]));

    printf("\n\nSource %d bit, Target %d bit strategy:%d\n", source, target, strategy);
        switch (strategy) {
            case DITHER_ROUND: dither(value, source, target, 40, dither_round); break;
            case DITHER_FLOOR: dither(value, source, target, 40, dither_floor); break;
            case DITHER_RANDOM: dither(value, source, target, 40, dither_random); break;
        }
}

