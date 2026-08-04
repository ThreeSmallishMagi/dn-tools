// SPDX-License-Identifier: GPL-3.0-or-later
// srgb.cpp — Mat-level implementations of the vision:: helpers declared in srgb.h.
#include "srgb.h"

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

namespace vision {

cv::Mat to_gray8(const cv::Mat& img) {
	cv::Mat g;
	if      (img.channels() == 3) cv::cvtColor(img, g, cv::COLOR_BGR2GRAY);
	else if (img.channels() == 4) cv::cvtColor(img, g, cv::COLOR_BGRA2GRAY);
	else                          g = img;
	if (g.depth() != CV_8U) {
		double mn, mx; cv::minMaxLoc(g, &mn, &mx);
		double s = (mx > mn) ? 255.0 / (mx - mn) : 1.0;
		g.convertTo(g, CV_8U, s, -mn * s);
	}
	return g;
}

cv::Mat decode_gray(const cv::Mat& img, bool srgb) {
	cv::Mat gray = to_gray8(img);
	cv::Mat out(gray.size(), CV_32F);
	gray.forEach<uint8_t>([&](uint8_t pixel, const int* pos) {
		out.at<float>(pos[0], pos[1]) = decode_pixel(pixel, srgb);
	});
	return out;
}

LevelSet make_levels(int levels, bool srgb, bool srgb_spaced) {
	LevelSet out;
	out.uniform = !srgb_spaced;
	for (int k = 0; k < levels; k++) {
		if (srgb_spaced) {
			// Equal steps in pixel value; the level's linear value is whatever
			// that pixel decodes to, so the ladder describes what is displayed.
			uint8_t px = (uint8_t)std::lround(255.0 * k / (levels - 1));
			out.px.push_back(px);
			out.lin.push_back(decode_pixel(px, srgb));
		} else {
			float lin = (float)k / (float)(levels - 1);
			out.lin.push_back(lin);
			out.px.push_back(encode_pixel(lin, srgb));
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

cv::Mat average_gray(const std::vector<cv::Mat>& frames, bool srgb) {
	cv::Mat acc = cv::Mat::zeros(frames[0].size(), CV_32F);
	for (const auto& f : frames) {
		f.forEach<uint8_t>([&](uint8_t pixel, const int* pos) {
			acc.at<float>(pos[0], pos[1]) += decode_pixel(pixel, srgb);
		});
	}
	acc /= (float)frames.size();
	cv::Mat out(frames[0].size(), CV_8U);
	acc.forEach<float>([&](float val, const int* pos) {
		out.at<uint8_t>(pos[0], pos[1]) = encode_pixel(val, srgb);
	});
	return out;
}

cv::Mat srgb_to_linear(const cv::Mat& mat) {
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
	cv::Mat srgb(linear.rows, linear.cols, CV_8UC3);
	for (int r = 0; r < linear.rows; ++r) {
		for (int c = 0; c < linear.cols; ++c) {
			auto& dest = srgb.at<cv::Vec<unsigned char, 3>>(r, c);
			auto& source = linear.at<cv::Vec<float, 3>>(r, c);
			for (int channel = 0; channel < 3; ++channel) {
				dest[channel] = cv::saturate_cast<unsigned char>(linear_to_srgb(source[channel]) + 0.5f);
			}
		}
	}
	return srgb;
}

} // namespace vision
