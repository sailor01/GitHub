#include "app/pipeline.hpp"

#include "app/frame.hpp"
#include "app/ring_queue.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace app {

namespace {

struct RuntimeStats {
    std::atomic<uint64_t> dropped_frames{0};
    std::atomic<uint64_t> processed_frames{0};
    std::atomic<uint64_t> stored_frames{0};

    std::atomic<uint64_t> detect_time_us_total{0};
    std::atomic<uint64_t> detect_time_us_max{0};

    std::atomic<uint64_t> store_time_us_total{0};
    std::atomic<uint64_t> store_time_us_max{0};
};

void update_max(std::atomic<uint64_t>& target, uint64_t candidate) {
    uint64_t current = target.load();
    while (candidate > current && !target.compare_exchange_weak(current, candidate)) {
    }
}

BlobResult detect_blob(const Frame& frame, const DetectionConfig& config) {
    // Placeholder detector for module-by-module bring-up.
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

bool load_raw_file(const std::filesystem::path& path, uint32_t width, uint32_t height, std::vector<uint8_t>& out) {
    const auto expected = static_cast<std::size_t>(width) * height;
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs) {
        return false;
    }

    out.assign(std::istreambuf_iterator<char>(ifs), std::istreambuf_iterator<char>());
    if (out.size() != expected) {
        return false;
    }
    return true;
}

bool load_pgm_p5_file(const std::filesystem::path& path, std::vector<uint8_t>& out, uint32_t& width, uint32_t& height) {
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs) {
        return false;
    }

    std::string magic;
    ifs >> magic;
    if (magic != "P5") {
        return false;
    }

    auto skip_comments = [&ifs]() {
        while (ifs.peek() == '#') {
            std::string comment;
            std::getline(ifs, comment);
        }
    };

    skip_comments();
    ifs >> width;
    skip_comments();
    ifs >> height;
    skip_comments();

    int maxval = 0;
    ifs >> maxval;
    if (maxval != 255) {
        return false;
    }

    ifs.get();

    out.resize(static_cast<std::size_t>(width) * height);
    ifs.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(out.size()));
    return static_cast<std::size_t>(ifs.gcount()) == out.size();
}

std::optional<uint16_t> read_u16_le(std::ifstream& ifs) {
    uint8_t b0 = 0;
    uint8_t b1 = 0;
    if (!ifs.read(reinterpret_cast<char*>(&b0), 1)) {
        return std::nullopt;
    }
    if (!ifs.read(reinterpret_cast<char*>(&b1), 1)) {
        return std::nullopt;
    }
    return static_cast<uint16_t>(b0 | (static_cast<uint16_t>(b1) << 8U));
}

std::optional<uint32_t> read_u32_le(std::ifstream& ifs) {
    uint8_t b0 = 0;
    uint8_t b1 = 0;
    uint8_t b2 = 0;
    uint8_t b3 = 0;
    if (!ifs.read(reinterpret_cast<char*>(&b0), 1)) {
        return std::nullopt;
    }
    if (!ifs.read(reinterpret_cast<char*>(&b1), 1)) {
        return std::nullopt;
    }
    if (!ifs.read(reinterpret_cast<char*>(&b2), 1)) {
        return std::nullopt;
    }
    if (!ifs.read(reinterpret_cast<char*>(&b3), 1)) {
        return std::nullopt;
    }
    return static_cast<uint32_t>(b0 | (static_cast<uint32_t>(b1) << 8U) | (static_cast<uint32_t>(b2) << 16U) |
                                 (static_cast<uint32_t>(b3) << 24U));
}

bool load_bmp_file(const std::filesystem::path& path, std::vector<uint8_t>& out, uint32_t& width, uint32_t& height) {
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs) {
        return false;
    }

    const auto signature = read_u16_le(ifs);
    if (!signature.has_value() || signature.value() != 0x4D42U) {
        return false;
    }

    const auto file_size = read_u32_le(ifs);
    const auto reserved1 = read_u16_le(ifs);
    const auto reserved2 = read_u16_le(ifs);
    const auto data_offset = read_u32_le(ifs);
    const auto dib_size = read_u32_le(ifs);
    if (!file_size.has_value() || !reserved1.has_value() || !reserved2.has_value() || !data_offset.has_value() ||
        !dib_size.has_value()) {
        return false;
    }
    if (dib_size.value() < 40U) {
        return false;
    }

    const auto w = read_u32_le(ifs);
    const auto h = read_u32_le(ifs);
    const auto planes = read_u16_le(ifs);
    const auto bit_count = read_u16_le(ifs);
    const auto compression = read_u32_le(ifs);
    const auto image_size = read_u32_le(ifs);
    const auto xppm = read_u32_le(ifs);
    const auto yppm = read_u32_le(ifs);
    const auto clr_used = read_u32_le(ifs);
    const auto clr_important = read_u32_le(ifs);
    if (!w.has_value() || !h.has_value() || !planes.has_value() || !bit_count.has_value() || !compression.has_value() ||
        !image_size.has_value() || !xppm.has_value() || !yppm.has_value() || !clr_used.has_value() ||
        !clr_important.has_value()) {
        return false;
    }

    if (planes.value() != 1U || compression.value() != 0U) {
        return false;
    }
    if (bit_count.value() != 8U && bit_count.value() != 24U) {
        return false;
    }

    width = w.value();
    height = h.value();
    if (width == 0U || height == 0U) {
        return false;
    }

    ifs.seekg(static_cast<std::streamoff>(data_offset.value()), std::ios::beg);
    if (!ifs.good()) {
        return false;
    }

    const uint32_t bytes_per_pixel = (bit_count.value() == 24U) ? 3U : 1U;
    const uint32_t stride = ((width * bytes_per_pixel) + 3U) & ~3U;
    std::vector<uint8_t> row(stride, 0U);
    out.resize(static_cast<std::size_t>(width) * height);

    for (uint32_t row_idx = 0; row_idx < height; ++row_idx) {
        if (!ifs.read(reinterpret_cast<char*>(row.data()), static_cast<std::streamsize>(row.size()))) {
            return false;
        }

        const uint32_t dst_y = height - 1U - row_idx;  // BMP bottom-up
        auto* dst = out.data() + static_cast<std::size_t>(dst_y) * width;

        if (bit_count.value() == 8U) {
            std::copy_n(row.data(), width, dst);
        } else {
            for (uint32_t x = 0; x < width; ++x) {
                const uint8_t b = row[static_cast<std::size_t>(x) * 3U + 0U];
                const uint8_t g = row[static_cast<std::size_t>(x) * 3U + 1U];
                const uint8_t r = row[static_cast<std::size_t>(x) * 3U + 2U];
                dst[x] = static_cast<uint8_t>((static_cast<uint16_t>(r) + g + b) / 3U);
            }
        }
    }

    return true;
}

std::vector<std::filesystem::path> collect_input_images(const std::string& folder) {
    std::vector<std::filesystem::path> files;
    if (!std::filesystem::exists(folder)) {
        return files;
    }

    for (const auto& entry : std::filesystem::directory_iterator(folder)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        const auto ext = entry.path().extension().string();
        if (ext == ".raw" || ext == ".pgm" || ext == ".bmp") {
            files.push_back(entry.path());
        }
    }

    std::sort(files.begin(), files.end());
    return files;
}

void store_hit_frame(const Frame& frame,
                     const BlobResult& hit,
                     const StorageConfig& storage,
                     RuntimeStats& stats) {
    const auto t0 = std::chrono::steady_clock::now();

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

    const auto t1 = std::chrono::steady_clock::now();
    const auto cost_us = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count());

    stats.store_time_us_total.fetch_add(cost_us);
    update_max(stats.store_time_us_max, cost_us);
    stats.stored_frames.fetch_add(1);
}

void print_stats(const RuntimeStats& stats) {
    const uint64_t processed = stats.processed_frames.load();
    const uint64_t stored = stats.stored_frames.load();

    const double detect_avg_us =
        (processed == 0) ? 0.0 : static_cast<double>(stats.detect_time_us_total.load()) / processed;
    const double store_avg_us =
        (stored == 0) ? 0.0 : static_cast<double>(stats.store_time_us_total.load()) / stored;

    std::cout << "Run complete.\n"
              << "  dropped_frames: " << stats.dropped_frames.load() << '\n'
              << "  processed_frames: " << processed << '\n'
              << "  stored_frames: " << stored << '\n'
              << "  detect_time_avg_us: " << detect_avg_us << '\n'
              << "  detect_time_max_us: " << stats.detect_time_us_max.load() << '\n'
              << "  store_time_avg_us: " << store_avg_us << '\n'
              << "  store_time_max_us: " << stats.store_time_us_max.load() << '\n';
}

}  // namespace

Pipeline::Pipeline(PipelineConfig config) : config_(std::move(config)) {}

void Pipeline::run() {
    RingQueue<std::shared_ptr<Frame>> capture_queue(config_.queue.capture_queue_size);
    RingQueue<std::pair<std::shared_ptr<Frame>, BlobResult>> hit_queue(config_.queue.hit_queue_size);

    std::atomic<bool> stop{false};
    RuntimeStats stats;

    std::thread capture_thread([&]() {
        uint64_t frame_id = 0;
        const auto frame_period = std::chrono::microseconds(1'000'000 / config_.camera.fps);

        const auto input_files = collect_input_images(config_.input.image_folder);
        std::size_t file_index = 0;

        if (config_.input.mode == InputMode::Folder && input_files.empty()) {
            std::cerr << "[WARN] folder mode enabled but no .raw/.pgm/.bmp files found in: "
                      << config_.input.image_folder << '\n';
        }

        while (!stop.load()) {
            auto frame = std::make_shared<Frame>();
            frame->frame_id = frame_id++;
            frame->timestamp = std::chrono::steady_clock::now();
            frame->width = config_.camera.width;
            frame->height = config_.camera.height;

            bool loaded_ok = false;
            if (config_.input.mode == InputMode::Folder && !input_files.empty()) {
                const auto& file = input_files[file_index];
                if (file.extension() == ".raw") {
                    loaded_ok = load_raw_file(file, config_.camera.width, config_.camera.height, frame->bayer);
                } else if (file.extension() == ".pgm") {
                    uint32_t w = 0;
                    uint32_t h = 0;
                    loaded_ok = load_pgm_p5_file(file, frame->bayer, w, h);
                    frame->width = w;
                    frame->height = h;
                } else if (file.extension() == ".bmp") {
                    uint32_t w = 0;
                    uint32_t h = 0;
                    loaded_ok = load_bmp_file(file, frame->bayer, w, h);
                    frame->width = w;
                    frame->height = h;
                }

                ++file_index;
                if (file_index >= input_files.size()) {
                    if (config_.input.loop_folder) {
                        file_index = 0;
                    } else {
                        stop.store(true);
                    }
                }
            }

            if (!loaded_ok) {
                frame->bayer.resize(static_cast<std::size_t>(frame->width) * frame->height, 0U);
            }

            if (!capture_queue.push(frame)) {
                stats.dropped_frames.fetch_add(1);
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

            const auto t0 = std::chrono::steady_clock::now();
            const BlobResult result = detect_blob(*item.value(), config_.detection);
            const auto t1 = std::chrono::steady_clock::now();

            const auto cost_us = static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count());
            stats.detect_time_us_total.fetch_add(cost_us);
            update_max(stats.detect_time_us_max, cost_us);
            stats.processed_frames.fetch_add(1);

            if (result.hit) {
                ++consecutive_hits;
            } else {
                consecutive_hits = 0;
            }

            if (consecutive_hits >= config_.detection.min_consecutive_hits) {
                if (!hit_queue.push({item.value(), result})) {
                    stats.dropped_frames.fetch_add(1);
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
            store_hit_frame(*item->first, item->second, config_.storage, stats);
        }
    });

    std::this_thread::sleep_for(config_.run_for);
    stop.store(true);

    capture_thread.join();
    detection_thread.join();
    storage_thread.join();

    print_stats(stats);
    std::cout << "Output folder: " << config_.storage.output_dir << '\n';
}

}  // namespace app
