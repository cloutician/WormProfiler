#include "WormCropper.h"

#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

bool WormCropper::loadModel(const std::string &modelPath, std::string *errorMessage)
{
    try {
        m_net = cv::dnn::readNetFromONNX(modelPath);

        if (m_net.empty()) {
            if (errorMessage != nullptr) {
                *errorMessage = "OpenCV loaded an empty network.";
            }

            return false;
        }

        m_net.setPreferableBackend(cv::dnn::DNN_BACKEND_OPENCV);
        m_net.setPreferableTarget(cv::dnn::DNN_TARGET_CPU);
        return true;
    } catch (const cv::Exception &exception) {
        if (errorMessage != nullptr) {
            *errorMessage = exception.what();
        }

        return false;
    }
}

bool WormCropper::isLoaded() const
{
    return !m_net.empty();
}

WormCropPrediction WormCropper::predictCrop(const cv::Mat &inputImage,
                                            int marginPixels,
                                            float threshold)
{
    WormCropPrediction prediction;

    if (inputImage.empty()) {
        prediction.message = "Image could not be loaded.";
        return prediction;
    }

    prediction.cropRect = cv::Rect(0, 0, inputImage.cols, inputImage.rows);

    if (!isLoaded()) {
        prediction.message = "Crop model is not loaded.";
        return prediction;
    }

    try {
        cv::Mat networkInput = prepareNetworkInput(inputImage);

        if (networkInput.empty()) {
            prediction.message = "Image format is not supported by the crop model.";
            return prediction;
        }

        m_net.setInput(networkInput);
        cv::Mat networkOutput = m_net.forward();
        cv::Mat probabilityMask = extractProbabilityMask(networkOutput);

        if (probabilityMask.empty()) {
            prediction.message = "Crop model returned an unexpected output shape.";
            return prediction;
        }

        cv::Mat binaryMask;
        cv::threshold(probabilityMask, binaryMask, threshold, 1.0, cv::THRESH_BINARY);
        binaryMask.convertTo(binaryMask, CV_8U);

        double componentArea = 0.0;
        cv::Rect componentRect = largestComponentRect(binaryMask, &componentArea);

        if (componentRect.empty()) {
            prediction.message = "No confident worm region found; using the full image.";
            return prediction;
        }

        const double xScale = static_cast<double>(inputImage.cols) / probabilityMask.cols;
        const double yScale = static_cast<double>(inputImage.rows) / probabilityMask.rows;
        const int margin = std::max(0, marginPixels);

        int x0 = static_cast<int>(std::floor(componentRect.x * xScale)) - margin;
        int y0 = static_cast<int>(std::floor(componentRect.y * yScale)) - margin;
        int x1 = static_cast<int>(std::ceil((componentRect.x + componentRect.width) * xScale)) + margin;
        int y1 = static_cast<int>(std::ceil((componentRect.y + componentRect.height) * yScale)) + margin;

        x0 = std::clamp(x0, 0, inputImage.cols - 1);
        y0 = std::clamp(y0, 0, inputImage.rows - 1);
        x1 = std::clamp(x1, x0 + 1, inputImage.cols);
        y1 = std::clamp(y1, y0 + 1, inputImage.rows);

        prediction.cropRect = cv::Rect(x0, y0, x1 - x0, y1 - y0);
        prediction.found = !prediction.cropRect.empty();
        prediction.componentArea = componentArea;
        prediction.message = prediction.found
                                 ? "Model crop ready."
                                 : "Model crop was empty; using the full image.";
        return prediction;
    } catch (const cv::Exception &exception) {
        prediction.message = exception.what();
        return prediction;
    }
}

cv::Mat WormCropper::prepareNetworkInput(const cv::Mat &inputImage) const
{
    if (inputImage.empty()) {
        return cv::Mat();
    }

    cv::Mat image8;

    if (inputImage.depth() == CV_8U) {
        image8 = inputImage;
    } else {
        double minValue = 0.0;
        double maxValue = 0.0;
        cv::minMaxLoc(inputImage.reshape(1), &minValue, &maxValue);

        if (maxValue > minValue) {
            inputImage.convertTo(image8, CV_8U, 255.0 / (maxValue - minValue), -minValue * 255.0 / (maxValue - minValue));
        } else {
            inputImage.convertTo(image8, CV_8U);
        }
    }

    cv::Mat rgb;

    if (image8.channels() == 1) {
        cv::cvtColor(image8, rgb, cv::COLOR_GRAY2RGB);
    } else if (image8.channels() == 3) {
        cv::cvtColor(image8, rgb, cv::COLOR_BGR2RGB);
    } else if (image8.channels() == 4) {
        cv::cvtColor(image8, rgb, cv::COLOR_BGRA2RGB);
    } else {
        return cv::Mat();
    }

    cv::Mat resized;
    cv::resize(rgb, resized, cv::Size(m_inputWidth, m_inputHeight), 0.0, 0.0, cv::INTER_AREA);

    cv::Mat resizedFloat;
    resized.convertTo(resizedFloat, CV_32F, 1.0 / 255.0);

    if (!resizedFloat.isContinuous()) {
        resizedFloat = resizedFloat.clone();
    }

    const int inputShape[] = {1, m_inputHeight, m_inputWidth, 3};
    cv::Mat networkInput(4, inputShape, CV_32F);
    std::memcpy(networkInput.ptr<float>(),
                resizedFloat.ptr<float>(),
                static_cast<size_t>(m_inputHeight) * m_inputWidth * 3 * sizeof(float));

    return networkInput;
}

cv::Mat WormCropper::extractProbabilityMask(const cv::Mat &networkOutput) const
{
    if (networkOutput.empty()) {
        return cv::Mat();
    }

    cv::Mat outputFloat;

    if (networkOutput.depth() == CV_32F) {
        outputFloat = networkOutput;
    } else {
        networkOutput.convertTo(outputFloat, CV_32F);
    }

    if (outputFloat.dims == 4) {
        const int batch = outputFloat.size[0];
        const int dim1 = outputFloat.size[1];
        const int dim2 = outputFloat.size[2];
        const int dim3 = outputFloat.size[3];

        if (batch != 1) {
            return cv::Mat();
        }

        if (dim3 == 1) {
            return cv::Mat(dim1, dim2, CV_32F, outputFloat.ptr<float>()).clone();
        }

        if (dim1 == 1) {
            return cv::Mat(dim2, dim3, CV_32F, outputFloat.ptr<float>()).clone();
        }
    }

    if (outputFloat.dims == 3 && outputFloat.size[0] == 1) {
        return cv::Mat(outputFloat.size[1], outputFloat.size[2], CV_32F, outputFloat.ptr<float>()).clone();
    }

    if (outputFloat.dims == 2) {
        return outputFloat.clone();
    }

    return cv::Mat();
}

cv::Rect WormCropper::largestComponentRect(const cv::Mat &binaryMask, double *componentArea) const
{
    if (binaryMask.empty()) {
        return cv::Rect();
    }

    cv::Mat labels;
    cv::Mat stats;
    cv::Mat centroids;
    const int labelCount = cv::connectedComponentsWithStats(binaryMask, labels, stats, centroids, 8);

    int bestLabel = -1;
    int bestArea = 0;

    for (int label = 1; label < labelCount; ++label) {
        const int area = stats.at<int>(label, cv::CC_STAT_AREA);

        if (area > bestArea) {
            bestArea = area;
            bestLabel = label;
        }
    }

    if (bestLabel < 0) {
        if (componentArea != nullptr) {
            *componentArea = 0.0;
        }

        return cv::Rect();
    }

    if (componentArea != nullptr) {
        *componentArea = bestArea;
    }

    return cv::Rect(stats.at<int>(bestLabel, cv::CC_STAT_LEFT),
                    stats.at<int>(bestLabel, cv::CC_STAT_TOP),
                    stats.at<int>(bestLabel, cv::CC_STAT_WIDTH),
                    stats.at<int>(bestLabel, cv::CC_STAT_HEIGHT));
}
