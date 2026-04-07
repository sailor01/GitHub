#pragma once

#include <chrono>
#include <cstdint>
#include <string>

namespace app {

enum class InputMode {
    Simulated,
    Folder
};

struct CameraConfig {
    uint32_t width = 1280;
    uint32_t height = 1024;
    uint32_t fps = 170;
    std::string pixel_format = "BayerRG8";
};

struct InputConfig {
    InputMode mode = InputMode::Folder;
    std::string image_folder = "test_images";
    bool loop_folder = true;
};

struct DetectionConfig {
    uint32_t min_blob_area = 50;
    uint32_t max_blob_area = 200000;
    uint32_t binary_threshold = 24;
    uint32_t min_consecutive_hits = 2;
};

struct StorageConfig {
    std::string output_dir = "output";
    bool store_color = true;
    uint32_t jpeg_quality = 92;
    uint32_t pre_trigger_frames = 10;
    uint32_t post_trigger_frames = 20;
};

struct QueueConfig {
    uint32_t capture_queue_size = 256;
    uint32_t hit_queue_size = 256;
};

struct PipelineConfig {
    CameraConfig camera;
    InputConfig input;
    DetectionConfig detection;
    StorageConfig storage;
    QueueConfig queue;
    std::chrono::seconds run_for = std::chrono::seconds(10);
};

}  // namespace app
