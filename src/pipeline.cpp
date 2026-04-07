#include "app/pipeline.hpp"

#include "app/frame.hpp"
#include "app/ring_queue.hpp"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <random>
#include <sstream>
#include <thread>

namespace app {

namespace {

BlobResult detect_blob(const Frame& frame, const DetectionConfig& config) {
    // Placeholder detector for module-by-module bring-up.
    // Replace with OpenCV pipeline:
    // 1) optional denoise
    // 2) absdiff(background, frame)
    // 3) threshold + morphology
    // 4) connectedComponentsWithStats
    BlobResult result;
    result.frame_id = frame.frame_id;

    const uint32_t pseudo_area = static_cast<uint32_t>((frame.frame_id * 37U) % 500U);
    result.max_blob_area = pseudo_area;
    result.blob_count = (pseudo_area > config.min_blob_area) ? 1U : 0U;
    result.hit = result.blob_count > 0U && pseudo_area < config.max_blob_area;
    return result;
}

std::string to_filename(uint64_t frame_id) {
    std::ostringstream oss;
    oss << "frame_" << std::setw(8) << std::setfill('0') << frame_id << ".raw";
    return oss.str();
}

void store_hit_frame(const Frame& frame, const BlobResult& hit, const StorageConfig& storage) {
    std::filesystem::create_directories(storage.output_dir);

    const std::filesystem::path image_path = std::filesystem::path(storage.output_dir) / to_filename(frame.frame_id);
    std::ofstream ofs(image_path, std::ios::binary);
    ofs.write(reinterpret_cast<const char*>(frame.bayer.data()), static_cast<std::streamsize>(frame.bayer.size()));

    const std::filesystem::path csv_path = std::filesystem::path(storage.output_dir) / "hits.csv";
    const bool exists = std::filesystem::exists(csv_path);
    std::ofstream log(csv_path, std::ios::app);
    if (!exists) {
        log << "frame_id,blob_count,max_blob_area,width,height,pixel_format\n";
    }
    log << hit.frame_id << ',' << hit.blob_count << ',' << hit.max_blob_area << ',' << frame.width << ',' << frame.height
        << ",Bayer\n";
}

}  // namespace

Pipeline::Pipeline(PipelineConfig config) : config_(std::move(config)) {}

void Pipeline::run() {
    RingQueue<std::shared_ptr<Frame>> capture_queue(config_.queue.capture_queue_size);
    RingQueue<std::pair<std::shared_ptr<Frame>, BlobResult>> hit_queue(config_.queue.hit_queue_size);

    std::atomic<bool> stop{false};
    std::atomic<uint64_t> dropped_frames{0};

    std::thread capture_thread([&]() {
        uint64_t frame_id = 0;
        const auto frame_period = std::chrono::microseconds(1'000'000 / config_.camera.fps);

        while (!stop.load()) {
            auto frame = std::make_shared<Frame>();
            frame->frame_id = frame_id++;
            frame->timestamp = std::chrono::steady_clock::now();
            frame->width = config_.camera.width;
            frame->height = config_.camera.height;
            frame->bayer.resize(static_cast<std::size_t>(frame->width) * frame->height, 0U);

            if (!capture_queue.push(frame)) {
                ++dropped_frames;
            }
            std::this_thread::sleep_for(frame_period);
        }

        capture_queue.close();
    });

    std::thread detection_thread([&]() {
        uint32_t consecutive_hits = 0;
        while (true) {
            auto item = capture_queue.pop();
            if (!item.has_value()) {
                break;
            }

            const BlobResult result = detect_blob(*item.value(), config_.detection);
            if (result.hit) {
                ++consecutive_hits;
            } else {
                consecutive_hits = 0;
            }

            if (consecutive_hits >= config_.detection.min_consecutive_hits) {
                if (!hit_queue.push({item.value(), result})) {
                    ++dropped_frames;
                }
            }
        }

        hit_queue.close();
    });

    std::thread storage_thread([&]() {
        while (true) {
            auto item = hit_queue.pop();
            if (!item.has_value()) {
                break;
            }
            store_hit_frame(*item->first, item->second, config_.storage);
        }
    });

    std::this_thread::sleep_for(config_.run_for);
    stop.store(true);

    capture_thread.join();
    detection_thread.join();
    storage_thread.join();

    std::cout << "Run complete. Dropped frames: " << dropped_frames.load() << '\n';
    std::cout << "Output folder: " << config_.storage.output_dir << '\n';
}

}  // namespace app
