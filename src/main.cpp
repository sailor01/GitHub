#include "app/pipeline.hpp"

#include <chrono>
#include <iostream>

int main() {
    app::PipelineConfig config;
    config.camera.width = 1280;
    config.camera.height = 1024;
    config.camera.fps = 170;
    config.camera.pixel_format = "BayerRG8";

    config.input.mode = app::InputMode::Folder;
    config.input.image_folder = "test_images";
    config.input.loop_folder = true;

    config.detection.min_blob_area = 50;
    config.detection.max_blob_area = 200000;
    config.detection.binary_threshold = 24;
    config.detection.min_consecutive_hits = 2;

    config.storage.output_dir = "output";
    config.storage.store_color = false;

    config.run_for = std::chrono::seconds(5);

    std::cout << "Starting FLIR Bayer hit-frame pipeline scaffold...\n";
    std::cout << "Input mode: folder = " << config.input.image_folder << '\n';

    app::Pipeline pipeline(config);
    pipeline.run();
    return 0;
}
