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

// The curve itself, in the unit domain: a normalised code in [0,1] to linear
// [0,1] and back.
//
// Unit rather than [0,255] because the code is what the file carries and 255 is
// only one file format's idea of white. An 8-bit 255 and a 16-bit 65535 are the
// same colour, and saying so here is what lets one decode path serve both
// depths. The [0,255] overloads below remain for callers that genuinely hold an
// 8-bit pixel.
inline float srgb_to_linear_unit(float code) {
	return code <= 0.04045f ? code / 12.92f
	                        : std::pow((code + 0.055f) / 1.055f, 2.4f);
}

inline float linear_to_srgb_unit(float linear) {
	return linear <= 0.0031308f ? linear * 12.92f
	                            : 1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f;
}

// Converts an sRGB value in the range [0, 255] to a linear value in
// the range [0, 1].
inline float srgb_to_linear(float srgb) { return srgb_to_linear_unit(srgb / 255.0f); }

// Converts a linear value in the range [0, 1] to an sRGB value in
// the range [0, 255].
inline float linear_to_srgb(float linear) { return linear_to_srgb_unit(linear) * 255.0f; }

// Decode a normalised pixel code in [0,1] to linear [0,1]. srgb=true applies
// the sRGB curve; srgb=false takes the code as linear already. This is the one
// place the input's bit depth stops mattering: whatever the file's white is,
// it arrives here as 1.0.
inline float decode_code(float code, bool srgb) {
	return srgb ? srgb_to_linear_unit(code) : code;
}

// Decode an 8-bit gray pixel to linear [0,1]. srgb=true uses the sRGB curve;
// srgb=false treats the pixel as raw linear (p/255).
inline float decode_pixel(uint8_t p, bool srgb) {
	return decode_code(p / 255.0f, srgb);
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

// Where the ladder's rungs sit, which is a statement about the device the
// output is going to.
//
//   Srgb    even steps of 8-bit pixel value. The file *is* the display signal,
//           so a rung emits whatever that pixel value decodes to.
//   Linear  even steps of light.
//   Device  even steps of the display's own `levels` codes, the file being only
//           a carrier for them. A rung emits what the display gives for code k
//           of levels-1, which is not the same as what its 8-bit stand-in
//           decodes to, because 255/(levels-1) is not a whole number.
//
// Srgb and Device differ only by that rounding, and only for 2 < levels < 256.
// It is worth up to 2% of a rung's light: a 3-bit panel's lowest rung emits
// sRGB(1/7) = 0.0180, while pixel 36 decodes to 0.0176.
enum class Spacing { Srgb, Linear, Device };

LevelSet make_levels(int levels, bool srgb, Spacing spacing);

// Position a linear [0,1] value on the ladder, returning display units in
// [0, count-1]: an integer part naming the level below and a fraction giving
// how far to the next one. The fraction interpolates in LINEAR light, which is
// what makes dithering between two levels average back to `linear`.
float to_display(const LevelSet& levels, float linear);

// Coerce any image to single-channel, *keeping its bit depth*: CV_8U stays
// CV_8U and CV_16U stays CV_16U, so a 16-bit input reaches the dither with its
// precision intact. Float depths are taken as [0,1] and widened to CV_16U.
//
// This used to be to_gray8, which min-max normalised anything that was not
// 8-bit. That was not merely lossy: the scale came from the image's own extremes,
// so the same scene with a different brightest pixel decoded to different tone,
// and every density guarantee in the program was void for such an input.
cv::Mat to_gray(const cv::Mat& img);

// Decode any image to CV_32F linear grayscale in [0,1]. One path for every
// depth: the depth decides only how many distinct codes there are, and so how
// big the lookup table is.
cv::Mat decode_gray(const cv::Mat& img, bool srgb);

// Decode any image straight to CV_32F display units on the given ladder.
cv::Mat decode_display(const cv::Mat& img, const LevelSet& levels, bool srgb);

// Encode a CV_8U mat of level indices (values in [0, count-1]) to pixel values.
cv::Mat encode_levels(const cv::Mat& levels_mat, const LevelSet& levels);

// Shrink a CV_8U grayscale image by an integer factor, averaging each block in
// linear light — the correct way to resolve a supersampled dither.
cv::Mat downsample_gray(const cv::Mat& img, int factor, bool srgb);

// The same rule for an arbitrary target: decode, resample, encode. Averaging
// encoded pixel values would be perceptually wrong, which is why callers do not
// simply call cv::resize.
//
// INTER_AREA shrinking, INTER_LINEAR growing -- area averaging is what makes a
// downscale a mean over the pixels it replaces, and is a poor interpolator in
// the other direction.
cv::Mat resize_gray(const cv::Mat& img, cv::Size size, bool srgb);

// Average CV_8U grayscale frames in linear light: decode → mean → encode.
cv::Mat average_gray(const std::vector<cv::Mat>& frames, bool srgb);

// 3-channel Mat converters (used by srgbtest and downstream code).
cv::Mat srgb_to_linear(const cv::Mat& mat);
cv::Mat linear_to_srgb(const cv::Mat& linear);

} // namespace vision
