#include <chrono>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include <grpcpp/grpcpp.h>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include "recycling.grpc.pb.h"

using Clock = std::chrono::high_resolution_clock;

static double MsSince(const Clock::time_point& start, const Clock::time_point& end) {
    return std::chrono::duration<double, std::milli>(end - start).count();
}

static std::vector<int> ParseCsvInts(const std::string& value) {
    std::vector<int> out;
    std::stringstream ss(value);
    std::string item;
    while (std::getline(ss, item, ',')) {
        if (!item.empty()) out.push_back(std::stoi(item));
    }
    return out;
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

int main(int argc, char** argv) {
    std::string server = "localhost:50051";
    std::string out_path = "results/benchmark.csv";
    int images = 128;
    int width = 640;
    int height = 480;
    std::vector<int> threads = {1, 2, 4, 8};
    std::vector<int> batches = {8, 16, 32, 64};

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        auto next = [&]() -> std::string {
            if (i + 1 >= argc) throw std::runtime_error("Missing value for " + arg);
            return argv[++i];
        };
        if (arg == "--server") server = next();
        else if (arg == "--out") out_path = next();
        else if (arg == "--images") images = std::stoi(next());
        else if (arg == "--width") width = std::stoi(next());
        else if (arg == "--height") height = std::stoi(next());
        else if (arg == "--threads") threads = ParseCsvInts(next());
        else if (arg == "--batches") batches = ParseCsvInts(next());
    }

    auto channel = grpc::CreateChannel(server, grpc::InsecureChannelCredentials());
    auto stub = recycling::RecyclingInspector::NewStub(channel);

    std::vector<std::vector<std::uint8_t>> encoded;
    encoded.reserve(images);
    for (int i = 0; i < images; ++i) {
        encoded.push_back(MakeSyntheticImage(i, width, height));
    }

    std::ofstream out(out_path);
    if (!out) {
        std::cerr << "Failed to open output file: " << out_path << std::endl;
        return 1;
    }

    out << "thread_count,batch_size,image_count,e2e_latency_ms,server_wall_time_ms,avg_image_latency_ms,throughput_images_per_sec,grpc_overhead_ms,speedup\n";

    double baseline = -1.0;
    for (int batch : batches) {
        int n = std::min(images, batch);
        for (int t : threads) {
            recycling::AnalyzeRequest request;
            request.set_request_id("bench_t" + std::to_string(t) + "_b" + std::to_string(batch));
            request.set_thread_count(t);
            request.set_batch_size(batch);
            for (int i = 0; i < n; ++i) {
                auto* img = request.add_images();
                img->set_image_id("synthetic_" + std::to_string(i));
                img->set_filename("synthetic_" + std::to_string(i) + ".jpg");
                img->set_data(encoded[i].data(), encoded[i].size());
            }

            recycling::AnalyzeResponse response;
            grpc::ClientContext context;
            const auto start = Clock::now();
            grpc::Status status = stub->AnalyzeImages(&context, request, &response);
            const auto end = Clock::now();
            if (!status.ok()) {
                std::cerr << "gRPC error: " << status.error_message() << std::endl;
                return 2;
            }
            const double e2e_ms = MsSince(start, end);
            if (baseline < 0.0) baseline = e2e_ms;
            const double throughput = n / (e2e_ms / 1000.0);
            const double grpc_overhead = e2e_ms - response.server_wall_time_ms();
            const double speedup = baseline / e2e_ms;

            out << t << ',' << batch << ',' << n << ',' << e2e_ms << ','
                << response.server_wall_time_ms() << ',' << e2e_ms / n << ','
                << throughput << ',' << grpc_overhead << ',' << speedup << '\n';
            std::cout << "threads=" << t << " batch=" << batch
                      << " e2e_ms=" << e2e_ms
                      << " throughput=" << throughput << std::endl;
        }
    }
    return 0;
}
