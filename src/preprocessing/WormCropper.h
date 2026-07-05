#ifndef WORMCROPPER_H
#define WORMCROPPER_H

#include <opencv2/core.hpp>
#include <opencv2/dnn.hpp>

#include <string>

struct WormCropPrediction
{
    cv::Rect cropRect;
    bool found = false;
    double componentArea = 0.0;
    std::string message;
};

class WormCropper
{
public:
    WormCropper() = default;

    bool loadModel(const std::string &modelPath, std::string *errorMessage = nullptr);
    bool isLoaded() const;

    WormCropPrediction predictCrop(const cv::Mat &inputImage,
                                   int marginPixels,
                                   float threshold = 0.65f);

private:
    cv::Mat prepareNetworkInput(const cv::Mat &inputImage) const;
    cv::Mat extractProbabilityMask(const cv::Mat &networkOutput) const;
    cv::Rect largestComponentRect(const cv::Mat &binaryMask, double *componentArea) const;

    cv::dnn::Net m_net;
    int m_inputWidth = 256;
    int m_inputHeight = 256;
};

#endif // WORMCROPPER_H
