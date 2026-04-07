#include "app/pipeline.hpp"

#include <iostream>

int main() {
    app::PipelineConfig config;
    config.camera.width = 1280;
    config.camera.height = 1024;
    config.camera.fps = 170;
    config.camera.pixel_format = "BayerRG8";

    config.detection.min_blob_area = 50;
    config.detection.max_blob_area = 200000;
    config.detection.binary_threshold = 24;
    config.detection.min_consecutive_hits = 2;

    config.storage.output_dir = "output";
    config.storage.store_color = false;  // Store raw Bayer in this scaffold.

    config.run_for = std::chrono::seconds(5);

    std::cout << "Starting FLIR Bayer hit-frame pipeline scaffold...\n";
    app::Pipeline pipeline(config);
    pipeline.run();
    return 0;
}
