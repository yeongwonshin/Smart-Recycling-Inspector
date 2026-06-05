#include <chrono>
#include <iostream>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include <grpcpp/grpcpp.h>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include "inspector_engine.h"
#include "recycling.grpc.pb.h"

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;

namespace {

using Clock = std::chrono::high_resolution_clock;

static double MsSince(const Clock::time_point& start, const Clock::time_point& end) {
    return std::chrono::duration<double, std::milli>(end - start).count();
}

static void FillBreakdown(const smart_recycling::ProcessingBreakdown& src,
                          recycling::ProcessingBreakdown* dst) {
    dst->set_decode_ms(src.decode_ms);
    dst->set_preprocess_ms(src.preprocess_ms);
    dst->set_feature_ms(src.feature_ms);
    dst->set_inference_ms(src.inference_ms);
    dst->set_postprocess_ms(src.postprocess_ms);
    dst->set_total_ms(src.total_ms);
}

static void FillResult(const smart_recycling::ImageResult& src,
                       recycling::ImageAnalysisResult* dst) {
    dst->set_image_id(src.image_id);
    dst->set_filename(src.filename);
    dst->set_category(src.category);
    dst->set_confidence(src.confidence);
    dst->set_contamination_status(src.contamination_status);
    dst->set_contamination_score(src.contamination_score);
    dst->set_recommendation(src.recommendation);
    dst->set_explanation(src.explanation);
    dst->set_ok(src.ok);
    dst->set_error_message(src.error_message);
    for (const auto& step : src.disposal_steps) {
        dst->add_disposal_steps(step);
    }
    for (const auto& feature : src.features) {
        auto* f = dst->add_features();
        f->set_name(feature.name);
        f->set_value(feature.value);
    }
    FillBreakdown(src.breakdown, dst->mutable_breakdown());
}

static std::vector<std::uint8_t> MakeSyntheticImage(int index, int width, int height) {
    cv::Mat img(height, width, CV_8UC3, cv::Scalar(235, 235, 235));
    const int mode = index % 5;
    if (mode == 0) {
        cv::rectangle(img, cv::Rect(width / 4, height / 5, width / 2, height * 3 / 5), cv::Scalar(230, 80, 80), -1);
        cv::putText(img, "LABEL", cv::Point(width / 3, height / 2), cv::FONT_HERSHEY_SIMPLEX, 1.0, cv::Scalar(255, 255, 255), 2);
    } else if (mode == 1) {
        cv::rectangle(img, cv::Rect(width / 5, height / 5, width * 3 / 5, height * 3 / 5), cv::Scalar(250, 250, 245), -1);
        cv::line(img, cv::Point(width / 4, height / 3), cv::Point(width * 3 / 4, height / 3), cv::Scalar(180, 180, 180), 2);
    } else if (mode == 2) {
        cv::circle(img, cv::Point(width / 2, height / 2), std::min(width, height) / 4, cv::Scalar(180, 180, 180), -1);
        cv::circle(img, cv::Point(width / 2, height / 2), std::min(width, height) / 6, cv::Scalar(245, 245, 245), 2);
    } else if (mode == 3) {
        cv::rectangle(img, cv::Rect(width / 3, height / 5, width / 3, height * 3 / 5), cv::Scalar(40, 145, 90), -1);
        cv::rectangle(img, cv::Rect(width / 3 + 15, height / 5 + 20, width / 3 - 30, height * 3 / 5 - 40), cv::Scalar(90, 190, 130), 2);
    } else {
        cv::ellipse(img, cv::Point(width / 2, height / 2), cv::Size(width / 4, height / 5), 0, 0, 360, cv::Scalar(60, 100, 150), -1);
        cv::circle(img, cv::Point(width / 2 + 25, height / 2 - 15), 20, cv::Scalar(30, 60, 90), -1);
    }
    std::vector<std::uint8_t> bytes;
    cv::imencode(".jpg", img, bytes);
    return bytes;
}

}  // namespace

class RecyclingInspectorService final : public recycling::RecyclingInspector::Service {
public:
    Status AnalyzeImages(ServerContext* context,
                         const recycling::AnalyzeRequest* request,
                         recycling::AnalyzeResponse* response) override {
        (void)context;
        const auto start = Clock::now();

        std::vector<smart_recycling::ImageJob> jobs;
        jobs.reserve(request->images_size());
        for (const auto& img : request->images()) {
            smart_recycling::ImageJob job;
            job.image_id = img.image_id();
            job.filename = img.filename();
            const std::string& data = img.data();
            job.data.assign(data.begin(), data.end());
            jobs.push_back(std::move(job));
        }

        const int requested_threads = request->thread_count() > 0 ? request->thread_count() : 1;
        auto results = inspector_.AnalyzeBatch(jobs, requested_threads);

        const auto end = Clock::now();
        const double wall_ms = MsSince(start, end);

        response->set_request_id(request->request_id());
        response->set_image_count(static_cast<int>(jobs.size()));
        response->set_thread_count(requested_threads);
        response->set_batch_size(request->batch_size());
        response->set_server_wall_time_ms(wall_ms);
        response->set_throughput_images_per_sec(jobs.empty() ? 0.0 : jobs.size() / (wall_ms / 1000.0));
        response->set_summary("Analyzed " + std::to_string(jobs.size()) +
                              " images using OpenMP thread_count=" + std::to_string(requested_threads) + ".");

        for (const auto& result : results) {
            FillResult(result, response->add_results());
        }
        return Status::OK;
    }

    Status Benchmark(ServerContext* context,
                     const recycling::BenchmarkRequest* request,
                     recycling::BenchmarkResponse* response) override {
        (void)context;
        std::vector<int> thread_counts(request->thread_counts().begin(), request->thread_counts().end());
        std::vector<int> batch_sizes(request->batch_sizes().begin(), request->batch_sizes().end());
        if (thread_counts.empty()) thread_counts = {1, 2, 4, 8};
        if (batch_sizes.empty()) batch_sizes = {8, 16, 32, 64};

        const int image_count = request->synthetic_image_count() > 0 ? request->synthetic_image_count() : 128;
        const int width = request->width() > 0 ? request->width() : 640;
        const int height = request->height() > 0 ? request->height() : 480;

        std::vector<smart_recycling::ImageJob> all_jobs;
        all_jobs.reserve(image_count);
        for (int i = 0; i < image_count; ++i) {
            smart_recycling::ImageJob job;
            job.image_id = "synthetic_" + std::to_string(i);
            job.filename = job.image_id + ".jpg";
            job.data = MakeSyntheticImage(i, width, height);
            all_jobs.push_back(std::move(job));
        }

        double base_ms = -1.0;
        for (int batch_size : batch_sizes) {
            const int n = std::min(image_count, batch_size);
            std::vector<smart_recycling::ImageJob> jobs(all_jobs.begin(), all_jobs.begin() + n);
            for (int threads : thread_counts) {
                const auto start = Clock::now();
                auto results = inspector_.AnalyzeBatch(jobs, threads);
                (void)results;
                const auto end = Clock::now();
                const double wall_ms = MsSince(start, end);
                if (base_ms < 0.0 && threads == thread_counts.front() && batch_size == batch_sizes.front()) {
                    base_ms = wall_ms;
                }
                auto* point = response->add_points();
                point->set_thread_count(threads);
                point->set_batch_size(batch_size);
                point->set_image_count(n);
                point->set_avg_latency_ms(wall_ms / static_cast<double>(n));
                point->set_throughput_images_per_sec(n / (wall_ms / 1000.0));
                point->set_speedup(base_ms > 0.0 ? base_ms / wall_ms : 1.0);
                point->set_server_wall_time_ms(wall_ms);
            }
        }
        return Status::OK;
    }

private:
    smart_recycling::SmartWasteInspector inspector_;
};

int main(int argc, char** argv) {
    std::string address = "0.0.0.0:50051";
    if (argc >= 2) {
        address = argv[1];
    }

    RecyclingInspectorService service;
    ServerBuilder builder;
    builder.AddListeningPort(address, grpc::InsecureServerCredentials());
    builder.RegisterService(&service);
    builder.SetMaxReceiveMessageSize(64 * 1024 * 1024);
    builder.SetMaxSendMessageSize(64 * 1024 * 1024);

    std::unique_ptr<Server> server(builder.BuildAndStart());
    std::cout << "Smart Recycling gRPC server listening on " << address << std::endl;
    server->Wait();
    return 0;
}
