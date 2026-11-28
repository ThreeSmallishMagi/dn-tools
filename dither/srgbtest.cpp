// SPDX-License-Identifier: GPL-3.0-or-later
// srgbtest.cpp — prints what the sRGB curve does, for reading rather than for
// asserting. It checks nothing; the checks live under test/.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <opencv2/opencv.hpp>
#include "srgb.h"

namespace {

// The ramp's linear values across one row, every eighth column.
void print_ramp_samples(const cv::Mat& image, const cv::Mat& linear)
{
    for (int x = 0; x < linear.cols; x += 8)
        std::printf("%d:%.3f ", x, linear.at<cv::Vec3f>(0, x)[0]);
    std::printf("\n");
    std::printf("blk:%u wht:%u 127:%u\n",
                image.at<cv::Vec3b>(0, 0)[0],
                image.at<cv::Vec3b>(0, 255)[0],
                image.at<cv::Vec3b>(127, 127)[0]);
    std::printf("blk:%f wht:%f 127:%f\n",
                linear.at<cv::Vec3f>(0, 0)[0],
                linear.at<cv::Vec3f>(0, 255)[0],
                linear.at<cv::Vec3f>(127, 127)[0]);

    const float grey = (linear.at<cv::Vec3f>(0, 0)[0] + linear.at<cv::Vec3f>(0, 255)[0]) / 2.0f;
    const float grey_srgb_f = vision::linear_to_srgb(grey);
    const float grey_srgb_int = std::round(grey_srgb_f);
    std::printf("grey:%f grey_srgb:%f grey_srgb_int:%f grey_srgb_frac:%f\n",
                grey, grey_srgb_f, grey_srgb_int, grey_srgb_f - grey_srgb_int);
}

// For each column of the ramp: how many black and white pixels, averaged in
// linear light over the column's height, reproduce its colour. This is the
// whole argument for dithering in linear light, on one page.
void print_column_mixes(const cv::Mat& linear)
{
    std::printf("\ncol  srgb  linear  black  white   avg_lin  avg_srgb  err\n");
    const int rows = linear.rows;
    for (int x = 0; x < linear.cols; x += 8) {
        double sum = 0.0;
        for (int y = 0; y < rows; ++y) sum += linear.at<cv::Vec3f>(y, x)[0];
        const float col_lin = (float)(sum / rows);

        const int n_white = std::clamp((int)std::lround(col_lin * rows), 0, rows);
        const int n_black = rows - n_white;

        // Black contributes 0 linear, white contributes 1.0, so the mix is just
        // the white fraction — re-encode it to see how close the column comes.
        const float avg_lin = (float)n_white / (float)rows;
        const float avg_srgb = vision::linear_to_srgb(avg_lin);
        const float col_srgb = vision::linear_to_srgb(col_lin);
        std::printf("%3d  %5.1f  %6.4f  %5d  %5d  %8.4f  %8.1f  %+5.1f\n",
                    x, col_srgb, col_lin, n_black, n_white, avg_lin, avg_srgb,
                    avg_srgb - col_srgb);
    }
}

// Half way between black and white in linear light is not 128.
void print_mid_grey()
{
    const int black = 0, white = 255;
    const float grey_lin = (vision::srgb_to_linear(black) +
                            vision::srgb_to_linear(white)) / 2.0f;
    std::printf("\nblack:%d white:%d grey:%d\n",
                black, white, (int)std::lround(vision::linear_to_srgb(grey_lin)));
}

} // namespace

int main(int argc, char** argv)
{
    const std::string inPath = argc > 1 ? argv[1] : "examples/ramp_256x256.png";
    const cv::Mat image = cv::imread(inPath, cv::IMREAD_COLOR);
    if (image.empty()) {
        std::fprintf(stderr, "cannot read %s\n", inPath.c_str());
        return 1;
    }
    // The probes below read fixed points of the 256x256 ramp — column 255 and
    // row 127 — so anything smaller would read past the end. Checked rather
    // than assumed, since the path takes whatever it is given.
    if (image.cols < 256 || image.rows < 128) {
        std::fprintf(stderr, "%s is %dx%d; these probes need at least 256x128\n",
                     inPath.c_str(), image.cols, image.rows);
        return 1;
    }
    const cv::Mat linear = vision::srgb_to_linear(image);   // sRGB[0,255] → linear[0,1]

    print_ramp_samples(image, linear);
    print_column_mixes(linear);
    print_mid_grey();
    return 0;
}
