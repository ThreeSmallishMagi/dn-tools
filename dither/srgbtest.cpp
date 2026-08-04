// SPDX-License-Identifier: GPL-3.0-or-later
#include <cstdio>
#include <string>
#include <opencv2/opencv.hpp>
#include "srgb.h"
using namespace cv;

int main(int argc, char **argv)
{
    std::string inPath("examples/ramp_256x256.png");
    Mat image = imread(inPath, IMREAD_COLOR);
    Mat linear(vision::srgb_to_linear(image)); // sRGB[0,255] → linear[0,1]

    for (int x = 0; x < linear.cols; x+=8) {
        std::printf("%d:%.3f ",x, linear.at<cv::Vec3f>(0, x)[0]);
    }
    printf("\n");
    const unsigned px_blk = image.at<cv::Vec3b>(0, 0)[0];
    const unsigned px_wht = image.at<cv::Vec3b>(0, 255)[0];
    const unsigned px_127 = image.at<cv::Vec3b>(127, 127)[0];
    std::printf("blk:%u wht:%u 127:%u\n", px_blk, px_wht, px_127);

    const float px_blk_lin = linear.at<cv::Vec3f>(0, 0)[0];
    const float px_wht_lin = linear.at<cv::Vec3f>(0, 255)[0];
    const float px_127_lin = linear.at<cv::Vec3f>(127, 127)[0];
    std::printf("blk:%f wht:%f 127:%f\n", px_blk_lin, px_wht_lin,px_127_lin);

    const float grey = (px_blk_lin + px_wht_lin) / 2.0f;
    const float grey_srgb_f = vision::linear_to_srgb(grey);
    const float grey_srgb_int = std::round(grey_srgb_f);
    const float grey_srgb_frac = grey_srgb_f - grey_srgb_int;
    printf("grey:%f grey_srgb:%f grey_srgb_int:%f grey_srgb_frac:%f\n", grey, grey_srgb_f, grey_srgb_int, grey_srgb_frac);

    // For each column of the ramp: how many black (0) and white (255) pixels,
    // averaged in linear light over the column's height, reproduce its colour.
    printf("\ncol  srgb  linear  black  white   avg_lin  avg_srgb  err\n");
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

    printf("\n");
    int black=0,white =255;
    float black_lin = vision::srgb_to_linear(black);
    float white_lin = vision::srgb_to_linear(white);
    float grey_lin = (black_lin + white_lin) / 2.0f;
    int grey_srgb = std::roundl(vision::linear_to_srgb(grey_lin));
    printf("black:%d white:%d grey:%d\n",black,white,grey_srgb);
    return 0;
}
