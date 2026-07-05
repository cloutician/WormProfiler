#include "PreprocessingPipeline.h"

#include <algorithm>
#include <vector>

cv::Mat PreprocessingPipeline::preprocess(const cv::Mat &inputImage,
                                          int blurStrength,
                                          bool normalizeEnabled,
                                          bool resizeEnabled,
                                          double scale,
                                          bool grayscaleEnabled,
                                          bool claheEnabled,
                                          bool autoCropEnabled,
                                          int cropMarginPixels) const
{
    if (inputImage.empty()) {
        return cv::Mat();
    }

    cv::Mat working = inputImage;

    if (autoCropEnabled) {
        working = cropToWormArea(working, cropMarginPixels);
    }

    if (resizeEnabled && scale > 0.0 && scale < 1.0) {
        working = resizeImage(working, scale);
    }

    if (grayscaleEnabled) {
        working = convertToGrayscale(working);
    }

    cv::Mat blurred = applyGaussianBlur(working, blurStrength);

    if (claheEnabled) {
        blurred = applyCLAHE(blurred);
    }

    if (normalizeEnabled) {
        return normalizeIntensity(blurred);
    }

    return blurred;
}

cv::Mat PreprocessingPipeline::cropToWormArea(const cv::Mat &inputImage, int marginPixels) const
{
    if (inputImage.empty()) {
        return cv::Mat();
    }

    cv::Rect wormBounds = detectWormBounds(inputImage);

    if (wormBounds.empty()) {
        return inputImage.clone();
    }

    int margin = std::max(0, marginPixels);
    cv::Rect imageBounds(0, 0, inputImage.cols, inputImage.rows);
    cv::Rect cropRect(wormBounds.x - margin,
                      wormBounds.y - margin,
                      wormBounds.width + (margin * 2),
                      wormBounds.height + (margin * 2));

    cropRect &= imageBounds;

    if (cropRect.empty()) {
        return inputImage.clone();
    }

    return inputImage(cropRect).clone();
}

cv::Rect PreprocessingPipeline::detectWormBounds(const cv::Mat &inputImage) const
{
    cv::Mat gray = convertToGrayscale(inputImage);

    if (gray.empty() || gray.channels() != 1) {
        return cv::Rect();
    }

    cv::Mat gray8;

    if (gray.depth() == CV_8U) {
        gray8 = gray;
    } else {
        cv::normalize(gray, gray8, 0, 255, cv::NORM_MINMAX, CV_8U);
    }

    cv::Mat blurred;
    cv::GaussianBlur(gray8, blurred, cv::Size(5, 5), 0);

    std::vector<cv::Mat> masks;

    cv::Mat binary;
    cv::threshold(blurred, binary, 0, 255, cv::THRESH_BINARY | cv::THRESH_OTSU);
    masks.push_back(binary);

    cv::Mat invertedBinary;
    cv::bitwise_not(binary, invertedBinary);
    masks.push_back(invertedBinary);

    const double imageArea = static_cast<double>(inputImage.cols) * inputImage.rows;
    const double minContourArea = std::max(25.0, imageArea * 0.0005);
    const double maxContourArea = imageArea * 0.90;

    cv::Rect bestRect;
    double bestScore = 0.0;

    for (cv::Mat mask : masks) {
        cv::Mat cleaned;
        cv::Mat smallKernel = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(3, 3));
        cv::Mat largeKernel = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(9, 9));

        cv::morphologyEx(mask, cleaned, cv::MORPH_OPEN, smallKernel);
        cv::morphologyEx(cleaned, cleaned, cv::MORPH_CLOSE, largeKernel);

        std::vector<std::vector<cv::Point>> contours;
        cv::findContours(cleaned, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

        for (const std::vector<cv::Point> &contour : contours) {
            double contourArea = cv::contourArea(contour);

            if (contourArea < minContourArea || contourArea > maxContourArea) {
                continue;
            }

            cv::Rect rect = cv::boundingRect(contour);
            double rectArea = static_cast<double>(rect.width) * rect.height;
            bool coversMostImage = rectArea > imageArea * 0.95;
            bool touchesAllEdges = rect.x <= 1
                                   && rect.y <= 1
                                   && rect.x + rect.width >= inputImage.cols - 1
                                   && rect.y + rect.height >= inputImage.rows - 1;

            if (coversMostImage || touchesAllEdges) {
                continue;
            }

            if (contourArea > bestScore) {
                bestScore = contourArea;
                bestRect = rect;
            }
        }
    }

    return bestRect;
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

    if (inputImage.channels() == 3) {
        cv::cvtColor(inputImage, gray, cv::COLOR_BGR2GRAY);
    } else if (inputImage.channels() == 4) {
        cv::cvtColor(inputImage, gray, cv::COLOR_BGRA2GRAY);
    } else {
        return inputImage.clone();
    }

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

cv::Mat PreprocessingPipeline::resizeImage(const cv::Mat &inputImage, double scale) const
{
    if (inputImage.empty()){
        return cv::Mat();
    }

    cv::Mat resized;
    cv::resize(inputImage, resized, cv::Size(), scale, scale, cv::INTER_AREA);
    return resized;
}

cv::Mat PreprocessingPipeline::applyCLAHE(const cv::Mat &inputImage) const
{
    if (inputImage.empty()) {
        return cv::Mat();
    }

    cv::Mat gray = convertToGrayscale(inputImage);

    if (gray.empty() || gray.channels() != 1) {
        return inputImage.clone();
    }

    cv::Mat output;
    cv::Ptr<cv::CLAHE> clahe = cv::createCLAHE();
    clahe->setClipLimit(2.0);
    clahe->setTilesGridSize(cv::Size(8, 8));
    clahe->apply(gray, output);

    return output;
}
