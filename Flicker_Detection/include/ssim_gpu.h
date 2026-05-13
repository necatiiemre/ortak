#ifndef SSIM_GPU_H
#define SSIM_GPU_H

#include <opencv2/opencv.hpp>

// Calculates SSIM (Structural Similarity Index) between two images using GPU acceleration
// Used for comparing similarity between the current and previous video frames
// @param img1: reference image (previous frame)
// @param img2: target image (current frame)
// @param windowSize: size of the local window for statistics (typically 11)
// @param maxVal: maximum pixel value in the image (e.g., 255.0)
// @return a float value representing the similarity (1.0 = identical, 0.0 = completely different)
float ssimGPU(const cv::Mat& img1, const cv::Mat& img2, int windowSize = 11, float maxVal = 255.0);

#endif // SSIM_GPU_H
