#ifndef PREPROCESSINGPIPELINE_H
#define PREPROCESSINGPIPELINE_H

#include <opencv2/opencv.hpp>

class PreprocessingPipeline
{
public:
    PreprocessingPipeline() = default;

    cv::Mat preprocess(const cv::Mat &inputImage,
                       int blurStrength,
                       bool normalizeEnabled,
                       bool resizeEnabled,
                       double scale,
                       bool grayscaleEnabled,
                       bool claheEnabled) const;

private:
    cv::Mat convertToGrayscale(const cv::Mat &inputImage) const;
    cv::Mat applyGaussianBlur(const cv::Mat &inputImage, int blurStrength) const;
    cv::Mat normalizeIntensity(const cv::Mat &inputImage) const;
    cv::Mat resizeImage(const cv::Mat &inputImage, double scale) const;
    cv::Mat applyCLAHE(const cv::Mat &inputImage) const;
};

#endif // PREPROCESSINGPIPELINE_H