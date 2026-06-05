#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace smart_recycling {

struct ImageJob {
    std::string image_id;
    std::string filename;
    std::vector<std::uint8_t> data;
};

struct FeatureValue {
    std::string name;
    double value = 0.0;
};

struct ProcessingBreakdown {
    double decode_ms = 0.0;
    double preprocess_ms = 0.0;
    double feature_ms = 0.0;
    double inference_ms = 0.0;
    double postprocess_ms = 0.0;
    double total_ms = 0.0;
};

struct ImageResult {
    std::string image_id;
    std::string filename;
    std::string category;
    double confidence = 0.0;
    std::string contamination_status;
    double contamination_score = 0.0;
    std::string recommendation;
    std::string explanation;
    std::vector<std::string> disposal_steps;
    std::vector<FeatureValue> features;
    ProcessingBreakdown breakdown;
    bool ok = true;
    std::string error_message;
};

class SmartWasteInspector {
public:
    std::vector<ImageResult> AnalyzeBatch(const std::vector<ImageJob>& jobs, int thread_count) const;

private:
    ImageResult AnalyzeOne(const ImageJob& job) const;
};

}  // namespace smart_recycling
