#include "inspector_engine.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <numeric>
#include <sstream>

#include <omp.h>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

namespace smart_recycling {
namespace {

using Clock = std::chrono::high_resolution_clock;

static double MsSince(const Clock::time_point& start, const Clock::time_point& end) {
    return std::chrono::duration<double, std::milli>(end - start).count();
}

static double Clamp(double v, double lo = 0.0, double hi = 1.0) {
    return std::max(lo, std::min(hi, v));
}

static double CountRatio(const cv::Mat& mask) {
    if (mask.empty()) return 0.0;
    return static_cast<double>(cv::countNonZero(mask)) / static_cast<double>(mask.rows * mask.cols);
}

static std::vector<std::string> DisposalStepsFor(const std::string& category,
                                                 const std::string& contamination) {
    if (category == "plastic") {
        if (contamination == "label_not_removed") {
            return {"Remove the label or sticker.", "Rinse the container.", "Flatten it if possible.", "Place it in the plastic recycling bin."};
        }
        if (contamination == "food_residue") {
            return {"Rinse food residue thoroughly.", "Dry the item briefly.", "Recycle only after residue is removed."};
        }
        return {"Empty the container.", "Rinse if needed.", "Recycle as plastic."};
    }
    if (category == "paper") {
        return {"Remove plastic coating if separable.", "Keep paper dry.", "Recycle as paper."};
    }
    if (category == "can") {
        return {"Empty remaining liquid.", "Rinse if contaminated.", "Recycle as metal can."};
    }
    if (category == "glass") {
        return {"Remove cap if possible.", "Rinse the bottle or jar.", "Recycle as glass."};
    }
    if (category == "food_waste") {
        return {"Remove packaging.", "Drain excess liquid.", "Dispose as food waste if local rules allow."};
    }
    return {"When material is uncertain, use the general waste bin.", "Check local recycling rules."};
}

struct FeaturePack {
    double brightness = 0.0;
    double saturation = 0.0;
    double edge_density = 0.0;
    double dark_ratio = 0.0;
    double bright_ratio = 0.0;
    double brown_ratio = 0.0;
    double green_ratio = 0.0;
    double blue_ratio = 0.0;
    double color_variance = 0.0;
};

static void Classify(const FeaturePack& f,
                     std::string& category,
                     double& confidence,
                     std::string& explanation) {
    double paper_score = Clamp((f.brightness - 0.55) * 1.7 + (0.32 - f.saturation) + (0.18 - f.edge_density));
    double can_score = Clamp((0.35 - f.saturation) * 1.2 + f.bright_ratio * 1.8 + f.edge_density * 0.8);
    double glass_score = Clamp(f.green_ratio * 2.5 + f.brightness * 0.35 + (0.4 - f.saturation) * 0.5);
    double food_score = Clamp(f.brown_ratio * 2.2 + f.dark_ratio * 0.8 + (0.55 - f.brightness) * 0.4);
    double plastic_score = Clamp(f.saturation * 1.1 + f.edge_density * 0.9 + f.blue_ratio * 1.2 + f.color_variance * 0.6);
    double general_score = 0.35;

    std::vector<std::pair<std::string, double>> scores = {
        {"plastic", plastic_score},
        {"paper", paper_score},
        {"can", can_score},
        {"glass", glass_score},
        {"food_waste", food_score},
        {"general_waste", general_score}
    };

    auto best = std::max_element(scores.begin(), scores.end(), [](const auto& a, const auto& b) {
        return a.second < b.second;
    });

    category = best->first;
    confidence = Clamp(0.55 + best->second * 0.40, 0.50, 0.96);

    std::ostringstream oss;
    oss << "The classifier selected " << category
        << " using brightness=" << f.brightness
        << ", saturation=" << f.saturation
        << ", edge density=" << f.edge_density
        << ", brown residue ratio=" << f.brown_ratio
        << ", and color variance=" << f.color_variance << ".";
    explanation = oss.str();
}

static void Contamination(const std::string& category,
                          const FeaturePack& f,
                          std::string& status,
                          double& score,
                          std::string& recommendation) {
    double residue_score = Clamp(f.brown_ratio * 2.4 + f.dark_ratio * 0.8);
    double label_score = Clamp(f.edge_density * 1.6 + f.saturation * 0.7 + f.color_variance * 0.8);
    double mixed_score = Clamp(f.color_variance * 1.5 + f.edge_density * 0.5);

    if (category != "food_waste" && residue_score > 0.32) {
        status = "food_residue";
        score = residue_score;
        recommendation = "Rinse or wipe food residue before recycling.";
    } else if (category == "plastic" && label_score > 0.55) {
        status = "label_not_removed";
        score = label_score;
        recommendation = "Remove labels or stickers before recycling this plastic item.";
    } else if (mixed_score > 0.68) {
        status = "mixed_material_risk";
        score = mixed_score;
        recommendation = "Separate mixed materials if possible; otherwise dispose according to local rules.";
    } else {
        status = "clean_or_low_risk";
        score = std::max({residue_score, label_score, mixed_score}) * 0.45;
        recommendation = "Item appears ready for the predicted disposal category.";
    }
    score = Clamp(score);
}

}  // namespace

std::vector<ImageResult> SmartWasteInspector::AnalyzeBatch(const std::vector<ImageJob>& jobs,
                                                           int thread_count) const {
    const int safe_threads = std::max(1, thread_count);
    omp_set_num_threads(safe_threads);

    std::vector<ImageResult> results(jobs.size());

#pragma omp parallel for schedule(dynamic)
    for (int i = 0; i < static_cast<int>(jobs.size()); ++i) {
        results[static_cast<std::size_t>(i)] = AnalyzeOne(jobs[static_cast<std::size_t>(i)]);
    }

    return results;
}

ImageResult SmartWasteInspector::AnalyzeOne(const ImageJob& job) const {
    ImageResult result;
    result.image_id = job.image_id;
    result.filename = job.filename;

    const auto total_start = Clock::now();

    try {
        const auto decode_start = Clock::now();
        cv::Mat raw(1, static_cast<int>(job.data.size()), CV_8UC1, const_cast<std::uint8_t*>(job.data.data()));
        cv::Mat image = cv::imdecode(raw, cv::IMREAD_COLOR);
        const auto decode_end = Clock::now();
        result.breakdown.decode_ms = MsSince(decode_start, decode_end);

        if (image.empty()) {
            result.ok = false;
            result.error_message = "OpenCV could not decode this image.";
            result.category = "unknown";
            result.contamination_status = "unknown";
            result.recommendation = "Upload a valid JPEG or PNG image.";
            result.breakdown.total_ms = MsSince(total_start, Clock::now());
            return result;
        }

        const auto prep_start = Clock::now();
        cv::Mat resized, blurred, hsv, gray;
        cv::resize(image, resized, cv::Size(224, 224), 0, 0, cv::INTER_AREA);
        cv::GaussianBlur(resized, blurred, cv::Size(3, 3), 0.0);
        cv::cvtColor(blurred, hsv, cv::COLOR_BGR2HSV);
        cv::cvtColor(blurred, gray, cv::COLOR_BGR2GRAY);
        const auto prep_end = Clock::now();
        result.breakdown.preprocess_ms = MsSince(prep_start, prep_end);

        const auto feature_start = Clock::now();
        cv::Scalar mean_bgr, std_bgr;
        cv::meanStdDev(blurred, mean_bgr, std_bgr);
        cv::Scalar mean_hsv = cv::mean(hsv);
        cv::Mat edges;
        cv::Canny(gray, edges, 60, 140);

        cv::Mat dark_mask, bright_mask, brown_mask, green_mask, blue_mask;
        cv::threshold(gray, dark_mask, 55, 255, cv::THRESH_BINARY_INV);
        cv::threshold(gray, bright_mask, 210, 255, cv::THRESH_BINARY);
        cv::inRange(hsv, cv::Scalar(5, 35, 30), cv::Scalar(28, 255, 210), brown_mask);
        cv::inRange(hsv, cv::Scalar(35, 25, 35), cv::Scalar(90, 255, 230), green_mask);
        cv::inRange(hsv, cv::Scalar(90, 35, 35), cv::Scalar(135, 255, 255), blue_mask);

        FeaturePack f;
        f.brightness = Clamp(cv::mean(gray)[0] / 255.0);
        f.saturation = Clamp(mean_hsv[1] / 255.0);
        f.edge_density = CountRatio(edges);
        f.dark_ratio = CountRatio(dark_mask);
        f.bright_ratio = CountRatio(bright_mask);
        f.brown_ratio = CountRatio(brown_mask);
        f.green_ratio = CountRatio(green_mask);
        f.blue_ratio = CountRatio(blue_mask);
        f.color_variance = Clamp((std_bgr[0] + std_bgr[1] + std_bgr[2]) / (3.0 * 80.0));
        const auto feature_end = Clock::now();
        result.breakdown.feature_ms = MsSince(feature_start, feature_end);

        const auto infer_start = Clock::now();
        Classify(f, result.category, result.confidence, result.explanation);
        const auto infer_end = Clock::now();
        result.breakdown.inference_ms = MsSince(infer_start, infer_end);

        const auto post_start = Clock::now();
        Contamination(result.category, f, result.contamination_status, result.contamination_score,
                      result.recommendation);
        result.disposal_steps = DisposalStepsFor(result.category, result.contamination_status);
        result.features = {
            {"brightness", f.brightness},
            {"saturation", f.saturation},
            {"edge_density", f.edge_density},
            {"dark_area_ratio", f.dark_ratio},
            {"brown_residue_ratio", f.brown_ratio},
            {"green_glass_ratio", f.green_ratio},
            {"blue_plastic_ratio", f.blue_ratio},
            {"color_variance", f.color_variance}
        };
        const auto post_end = Clock::now();
        result.breakdown.postprocess_ms = MsSince(post_start, post_end);
        result.breakdown.total_ms = MsSince(total_start, Clock::now());
    } catch (const std::exception& e) {
        result.ok = false;
        result.error_message = e.what();
        result.category = "unknown";
        result.contamination_status = "unknown";
        result.recommendation = "The server failed to analyze this image.";
        result.breakdown.total_ms = MsSince(total_start, Clock::now());
    }

    return result;
}

}  // namespace smart_recycling
