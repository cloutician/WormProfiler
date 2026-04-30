#include "PreprocessingPipeline.h"

cv::Mat PreprocessingPipeline::preprocess(const cv::Mat &inputImage,
                                          int blurStrength,
                                          bool normalizeEnabled) const
{
    if (inputImage.empty()) {
        return cv::Mat();
    }

    cv::Mat gray = convertToGrayscale(inputImage);
    cv::Mat blurred = applyGaussianBlur(gray, blurStrength);

    if (normalizeEnabled) {
        return normalizeIntensity(blurred);
    }

    return blurred;
}

cv::Mat PreprocessingPipeline::convertToGrayscale(const cv::Mat &inputImage) const
{
    if (inputImage.empty()) {
        return cv::Mat();
    }

    if (inputImage.channels() == 1) {
        return inputImage.clone();
    }

    cv::Mat gray;
    cv::cvtColor(inputImage, gray, cv::COLOR_BGR2GRAY);
    return gray;
}

cv::Mat PreprocessingPipeline::applyGaussianBlur(const cv::Mat &inputImage, int blurStrength) const
{
    if (inputImage.empty() || blurStrength <= 0) {
        return inputImage.clone();
    }

    int kernelSize = blurStrength * 2 + 1;

    cv::Mat blurred;
    cv::GaussianBlur(inputImage, blurred, cv::Size(kernelSize, kernelSize), 0);
    return blurred;
}

cv::Mat PreprocessingPipeline::normalizeIntensity(const cv::Mat &inputImage) const
{
    if (inputImage.empty()) {
        return cv::Mat();
    }

    cv::Mat normalized;
    cv::normalize(inputImage, normalized, 0, 255, cv::NORM_MINMAX);
    return normalized;
}