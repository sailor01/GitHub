#pragma once

#include <chrono>
#include <cstdint>
#include <vector>

namespace app {

struct Frame {
    uint64_t frame_id = 0;
    std::chrono::steady_clock::time_point timestamp{};
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<uint8_t> bayer;
};

struct BlobResult {
    uint64_t frame_id = 0;
    bool hit = false;
    uint32_t blob_count = 0;
    uint32_t max_blob_area = 0;
};

}  // namespace app
