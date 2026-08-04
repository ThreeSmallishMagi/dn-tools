// SPDX-License-Identifier: GPL-3.0-or-later
// The scalar srgb_to_linear/linear_to_srgb float overloads are from
// https://github.com/PetterS/opencv_srgb_gamma; the rest of this header
// and the Mat-level helpers in srgb.cpp are local to this project.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include <opencv2/core.hpp>

namespace vision {

// Converts an sRGB value in the range [0, 255] to a linear value in
// the range [0, 1].
inline float srgb_to_linear(float srgb) {
	auto linear = srgb / 255.0f;
	if (linear <= 0.04045f) {
		linear = linear / 12.92f;
	} else {
		linear = std::pow((linear + 0.055f) / 1.055f, 2.4f);
	}
	return linear;
}

// Converts a linear value in the range [0, 1] to an sRGB value in
// the range [0, 255].
inline float linear_to_srgb(float linear) {
	float srgb;
	if (linear <= 0.0031308f) {
		srgb = linear * 12.92f;
	} else {
		srgb = 1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f;
	}
	return srgb * 255.f;
}

// Decode an 8-bit gray pixel to linear [0,1]. srgb=true uses the sRGB curve;
// srgb=false treats the pixel as raw linear (p/255).
inline float decode_pixel(uint8_t p, bool srgb) {
	return srgb ? srgb_to_linear((float)p) : p / 255.0f;
}

// Encode a linear [0,1] value to an 8-bit gray pixel, clamped to [0,255].
inline uint8_t encode_pixel(float lin, bool srgb) {
	float px = srgb ? linear_to_srgb(lin) : lin * 255.0f;
	return (uint8_t)std::clamp((int)std::lround(px), 0, 255);
}

// Convert a display level in [0, levels-1] to an 8-bit pixel value.
inline uint8_t level_to_pixel(int level, int levels, bool srgb) {
	return encode_pixel((float)level / (float)(levels - 1), srgb);
}

// The ladder of display levels: the pixel each level emits and the linear-light
// value that pixel actually has. Where the rungs sit is a choice:
//   linear spacing — equal steps in linear light. 3 levels give pixels 0/188/255.
//   sRGB spacing   — equal steps in pixel value.  3 levels give pixels 0/128/255,
//                    which puts the middle level at perceptual half and leaves
//                    fewer tones to be dithered against black in the shadows.
// Either way `lin` holds each level's true linear value, so the dither can be
// density-correct on a ladder whose rungs are unevenly spaced in linear light.
struct LevelSet {
	std::vector<float>   lin;   // linear value of each level, ascending
	std::vector<uint8_t> px;    // pixel value of each level
	bool uniform = false;       // lin[k] == k/(count-1) exactly, so to_display()
	                            // is a plain scale rather than a ladder search
	int count() const { return (int)lin.size(); }
};

// Mat-level helpers — defined in srgb.cpp.

// Build the level ladder. srgb_spaced=false reproduces level_to_pixel().
LevelSet make_levels(int levels, bool srgb, bool srgb_spaced);

// Position a linear [0,1] value on the ladder, returning display units in
// [0, count-1]: an integer part naming the level below and a fraction giving
// how far to the next one. The fraction interpolates in LINEAR light, which is
// what makes dithering between two levels average back to `linear`.
float to_display(const LevelSet& levels, float linear);

// Coerce any image (color/gray, any depth) to CV_8U single-channel.
// Non-CV_8U depths are min-max normalized to fit the 8-bit range.
cv::Mat to_gray8(const cv::Mat& img);

// Decode any image to CV_32F linear grayscale in [0,1].
cv::Mat decode_gray(const cv::Mat& img, bool srgb);

// Decode any image straight to CV_32F display units on the given ladder.
cv::Mat decode_display(const cv::Mat& img, const LevelSet& levels, bool srgb);

// Encode a CV_8U mat of level indices (values in [0, count-1]) to pixel values.
cv::Mat encode_levels(const cv::Mat& levels_mat, const LevelSet& levels);

// Average CV_8U grayscale frames in linear light: decode → mean → encode.
cv::Mat average_gray(const std::vector<cv::Mat>& frames, bool srgb);

// 3-channel Mat converters (used by srgbtest and downstream code).
cv::Mat srgb_to_linear(const cv::Mat& mat);
cv::Mat linear_to_srgb(const cv::Mat& linear);

} // namespace vision
