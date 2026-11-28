// SPDX-License-Identifier: GPL-3.0-or-later
// srgb.cpp — Mat-level implementations of the vision:: helpers declared in srgb.h.
#include "srgb.h"

#include <array>
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

namespace vision {

cv::Mat to_gray(const cv::Mat& img) {
	cv::Mat g;
	if      (img.channels() == 3) cv::cvtColor(img, g, cv::COLOR_BGR2GRAY);
	else if (img.channels() == 4) cv::cvtColor(img, g, cv::COLOR_BGRA2GRAY);
	else                          g = img;
	// The two integer depths a file can carry are kept exactly as they are.
	if (g.depth() == CV_8U || g.depth() == CV_16U) return g;
	// Anything else is float, which by convention holds [0,1] already. Scaled
	// by a *constant*, never by the image's own extremes: a scale read off the
	// content would make the same scene decode differently depending on its
	// brightest pixel.
	cv::Mat wide;
	g.convertTo(wide, CV_16U, 65535.0);
	return wide;
}

// Every distinct pixel code decodes to one linear value, so the curve is
// evaluated once per code rather than once per pixel. Exact, not an
// approximation: the table holds decode_code()'s own answers.
//
// `max_code` is the file's white — 255 for an 8-bit image, 65535 for a 16-bit
// one — which is the only thing that differs between the depths. Built once per
// (depth, curve) rather than once per call: this runs for every frame of a
// multi-frame average and every supersampled downsample, so rebuilding it was
// the larger part of the work for a small image.
static std::vector<float> build_decode_table(int max_code, bool srgb) {
	std::vector<float> t((size_t)max_code + 1);
	for (int i = 0; i <= max_code; i++)
		t[(size_t)i] = decode_code((float)i / (float)max_code, srgb);
	return t;
}

static const float* decode_table(int max_code, bool srgb) {
	// Lazily, and per depth: a run that never sees a 16-bit file should not pay
	// 65536 pow() calls for the table it would have used.
	if (max_code == 255) {
		static const std::vector<float> lin  = build_decode_table(255, false);
		static const std::vector<float> srgb_ = build_decode_table(255, true);
		return (srgb ? srgb_ : lin).data();
	}
	static const std::vector<float> lin  = build_decode_table(65535, false);
	static const std::vector<float> srgb_ = build_decode_table(65535, true);
	return (srgb ? srgb_ : lin).data();
}

// One loop, instantiated per pixel type. The depth changes the pointer and the
// table; it does not change the work, so it must not change the source.
template <typename Code>
static void decode_into(const cv::Mat& gray, cv::Mat& out, const float* table) {
	for (int y = 0; y < gray.rows; y++) {
		const Code* in = gray.ptr<Code>(y);
		float* dst = out.ptr<float>(y);
		for (int x = 0; x < gray.cols; x++) dst[x] = table[in[x]];
	}
}

// The way back. No table here — the input is a float, so there is nothing
// finite to tabulate.
static cv::Mat encode_gray8(const cv::Mat& lin, bool srgb) {
	cv::Mat out(lin.size(), CV_8U);
	for (int y = 0; y < lin.rows; y++) {
		const float* in = lin.ptr<float>(y);
		uint8_t* dst = out.ptr<uint8_t>(y);
		for (int x = 0; x < lin.cols; x++) dst[x] = encode_pixel(in[x], srgb);
	}
	return out;
}

cv::Mat decode_gray(const cv::Mat& img, bool srgb) {
	const cv::Mat gray = to_gray(img);
	cv::Mat out(gray.size(), CV_32F);
	if (gray.depth() == CV_16U)
		decode_into<uint16_t>(gray, out, decode_table(65535, srgb));
	else
		decode_into<uint8_t>(gray, out, decode_table(255, srgb));
	return out;
}

LevelSet make_levels(int levels, bool srgb, Spacing spacing) {
	LevelSet out;
	// `uniform` means lin[k] is exactly k/(count-1), which lets to_display()
	// short-circuit to a plain scale. Even steps of light are that by
	// definition; device codes are too when no curve is being applied.
	out.uniform = spacing == Spacing::Linear ||
	              (spacing == Spacing::Device && !srgb);
	out.lin.reserve(levels);
	out.px.reserve(levels);
	for (int k = 0; k < levels; k++) {
		const float code = (float)k / (float)(levels - 1);
		switch (spacing) {
		case Spacing::Srgb: {
			// Equal steps in pixel value; the level's linear value is whatever
			// that pixel decodes to, so the ladder describes what is displayed.
			const uint8_t px = (uint8_t)std::lround(255.0 * code);
			out.px.push_back(px);
			out.lin.push_back(decode_pixel(px, srgb));
			break;
		}
		case Spacing::Linear:
			out.lin.push_back(code);
			out.px.push_back(encode_pixel(code, srgb));
			break;
		case Spacing::Device:
			// The display's own code, not its 8-bit stand-in. Rounding the code
			// to 8 bits and decoding *that* is what leaves a 3-bit panel 2% dark
			// on its lowest rung; the pixel value is still the carrier, and
			// still recovers the code under a top-bits truncation.
			out.px.push_back((uint8_t)std::lround(255.0 * code));
			out.lin.push_back(srgb ? srgb_to_linear(255.0f * code) : code);
			break;
		}
	}
	return out;
}

float to_display(const LevelSet& levels, float linear) {
	const int top = levels.count() - 1;
	// Evenly spaced rungs invert to a plain scale — the same arithmetic the
	// pipeline used before ladders existed, kept bit-for-bit.
	if (levels.uniform) return linear * (float)top;
	if (linear <= levels.lin.front()) return 0.0f;
	if (linear >= levels.lin.back())  return (float)top;
	int k = (int)(std::upper_bound(levels.lin.begin(), levels.lin.end(), linear)
	              - levels.lin.begin()) - 1;
	float span = levels.lin[k + 1] - levels.lin[k];
	return (float)k + (span > 0.0f ? (linear - levels.lin[k]) / span : 0.0f);
}

cv::Mat decode_display(const cv::Mat& img, const LevelSet& levels, bool srgb) {
	cv::Mat out = decode_gray(img, srgb);
	out.forEach<float>([&](float& v, const int*) { v = to_display(levels, v); });
	return out;
}

cv::Mat encode_levels(const cv::Mat& levels_mat, const LevelSet& levels) {
	cv::Mat lut(1, 256, CV_8U);
	for (int i = 0; i < 256; i++)
		lut.at<uint8_t>(i) = (i < levels.count()) ? levels.px[i] : 0;
	cv::Mat out;
	cv::LUT(levels_mat, lut, out);
	return out;
}

// Both of these are the same domain rule: decode to linear, combine there,
// encode back. Combining encoded pixel values instead would be perceptually
// wrong, which is the whole reason these functions exist rather than a resize
// and a mean at the call site.
cv::Mat resize_gray(const cv::Mat& img, cv::Size size, bool srgb) {
	if (size == img.size()) return img;
	const bool shrinking = size.width <= img.cols && size.height <= img.rows;
	cv::Mat lin = decode_gray(img, srgb), out;
	cv::resize(lin, out, size, 0, 0, shrinking ? cv::INTER_AREA : cv::INTER_LINEAR);
	return encode_gray8(out, srgb);
}

cv::Mat downsample_gray(const cv::Mat& img, int factor, bool srgb) {
	if (factor <= 1) return img;
	return resize_gray(img, cv::Size(img.cols / factor, img.rows / factor), srgb);
}

cv::Mat average_gray(const std::vector<cv::Mat>& frames, bool srgb) {
	cv::Mat acc = cv::Mat::zeros(frames[0].size(), CV_32F);
	for (const auto& f : frames) acc += decode_gray(f, srgb);
	acc /= (float)frames.size();
	return encode_gray8(acc, srgb);
}

// Three-channel only, unlike the grayscale helpers above; srgbtest is the only
// caller. The assert says so rather than leaving the name to imply otherwise.
cv::Mat srgb_to_linear(const cv::Mat& mat) {
	CV_Assert(mat.channels() == 3);
	cv::Mat linear;
	mat.convertTo(linear, CV_32FC3);
	for (auto itr = linear.begin<cv::Vec<float, 3>>(); itr != linear.end<cv::Vec<float, 3>>();
	     ++itr) {
		for (int channel = 0; channel < 3; ++channel) {
			(*itr)[channel] = srgb_to_linear((*itr)[channel]);
		}
	}
	return linear;
}

cv::Mat linear_to_srgb(const cv::Mat& linear) {
	CV_Assert(linear.channels() == 3);
	cv::Mat srgb(linear.rows, linear.cols, CV_8UC3);
	for (int r = 0; r < linear.rows; ++r) {
		for (int c = 0; c < linear.cols; ++c) {
			auto& dest = srgb.at<cv::Vec<unsigned char, 3>>(r, c);
			auto& source = linear.at<cv::Vec<float, 3>>(r, c);
			for (int channel = 0; channel < 3; ++channel) {
				// saturate_cast rounds; adding 0.5 first rounded twice and biased
				// every odd code upward. encode_pixel's lround is the one answer.
				dest[channel] = cv::saturate_cast<unsigned char>(linear_to_srgb(source[channel]));
			}
		}
	}
	return srgb;
}

} // namespace vision
