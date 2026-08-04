// SPDX-License-Identifier: GPL-3.0-or-later
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <algorithm>

#define bitmax(depth) ((1u<<depth)-1u)

// Dithering strategies
typedef enum { DITHER_ROUND=0, DITHER_FLOOR =1, DITHER_RANDOM=2 } dither_strategy;

unsigned dither_round(float accumulator) {
    return (unsigned)(accumulator + 0.5f);
}

unsigned dither_floor(float accumulator) {
    return (unsigned)accumulator;
}

unsigned dither_random(float accumulator) {
    float fractional = accumulator - (int)accumulator;
    float random_threshold = (float)rand() / RAND_MAX;
    return (unsigned)accumulator + (fractional > random_threshold ? 1 : 0);
}

 /*  e.g. 3bit image to 2bit display
  *  Source/desired brightness: value / sourcemax  e.g. 2/7
  *  Target/display brightness: display / targetmax e.g. some d/3
  *     therefore desired display brightness is  value * targetmax / sourcemax  e.g. d=2*3/7
  *     this is tracked in desired_brightness
  */
void dither(unsigned value, unsigned sourcedepth, unsigned targetdepth, unsigned frames,
                        unsigned (*dither_fn)(float) = dither_round)
{
    const unsigned sourcemax = bitmax(sourcedepth);
    const unsigned targetmax = bitmax(targetdepth);
    const float desired_brightness = (float)(value * targetmax) / sourcemax;

    float accumulator = 0.0f;
    unsigned total_displayed_acc = 0;
    unsigned total_displayed_ave = 0;

    printf("value:%d/%d ideal target:%.2f\n", value, sourcemax, desired_brightness);
    printf("%-5s %-7s %-13s %-13s | %-10s %-13s %-13s %-13s\n", "frame", "display", "accumulator", "overall error",
        "display_ave", "da_average", "error ave", "total error");

    for (unsigned frame = 1; frame <= frames; frame++) {
        // accumulator method
        accumulator += desired_brightness;
        unsigned display_acc = dither_fn(accumulator); // Use dithering strategy
        accumulator -= display_acc;               // Keep fractional error
        // ---
        total_displayed_acc += display_acc;           // unused for calculation
        float display_average_acc = (float)total_displayed_acc / frame;
        float overall_error_acc = desired_brightness - display_average_acc;

        // average display method
        unsigned display_ave  = dither_fn(desired_brightness);
        if (  (display_ave + total_displayed_ave) < desired_brightness*frame - 0.5f)
            display_ave++;
        else if (display_ave > 0 && (display_ave + total_displayed_ave) > desired_brightness*frame + 0.5f)
            display_ave--;
        total_displayed_ave += display_ave;
        float display_average_ave = (float)total_displayed_ave/frame;
        float overall_error_ave=  (desired_brightness - display_average_ave)/frame;

        printf("%-5d %-7d %-13.2f %-13.4f | %-10d %-13.2f %-13.3f %-13.2f\n", 
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
    int value = atoi(argv[3]);
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

