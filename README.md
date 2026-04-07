# FLIR Bayer Hit-Frame Pipeline (Scaffold)

This repository contains a Windows/MSVC-oriented C++ scaffold for:

- FLIR camera acquisition at high frame rates (target 170 fps)
- Blob detection pipeline (placeholder now, OpenCV drop-in point prepared)
- Store **only hit frames** + metadata CSV
- Folder-image playback mode for offline testing (temporary replacement for camera)

## Build (CMake)

```bash
cmake -S . -B build
cmake --build build -j
```

## Build (Visual Studio 2022 / MSVC v143)

1. Open `msvc2022/flir_blob_pipeline.sln` in Visual Studio 2022.
2. Select `x64` and `Debug` or `Release`.
3. Build solution (`Ctrl+Shift+B`).
4. Run `flir_blob_pipeline`.

Project file location:

- `msvc2022/flir_blob_pipeline.sln`
- `msvc2022/flir_blob_pipeline/flir_blob_pipeline.vcxproj`

## Run (CMake-built binary)

```bash
./build/flir_blob_pipeline
```

The scaffold runs for 5 seconds and writes output to `output/`.

## Folder-image test mode

The pipeline supports reading test images from a folder instead of camera input:

- Supported formats: `.raw` and `.pgm` (P5)
- Default folder: `test_images/`
- `.raw` uses configured `width x height`
- `.pgm` uses width/height parsed from file header

Set in `src/main.cpp`:

- `config.input.mode = app::InputMode::Folder;`
- `config.input.image_folder = "test_images";`
- `config.input.loop_folder = true;`

## Time statistics

At end of run, program prints:

- `detect_time_avg_us`, `detect_time_max_us`
- `store_time_avg_us`, `store_time_max_us`
- `processed_frames`, `stored_frames`, `dropped_frames`

## Module-level debug order (recommended)

1. **Capture thread**
   - Confirm folder images are loaded in expected order.
2. **Detection thread**
   - Replace `detect_blob()` with OpenCV-based Bayer-domain/gray-domain logic.
3. **Storage thread**
   - Verify only hit frames are written.
   - Verify `hits.csv` columns are complete.
4. **End-to-end timing**
   - Track detect/store latency and dropped-frame count.

## Where to replace placeholder logic

- File: `src/pipeline.cpp`
- Function: `detect_blob(const Frame&, const DetectionConfig&)`

Suggested OpenCV pipeline for real blob detection:

1. Denoise (optional)
2. Background subtraction / frame differencing
3. Threshold + morphology
4. `connectedComponentsWithStats`
5. Area/shape filtering + hit decision

## Spinnaker integration notes

Use `ENABLE_SPINNAKER=ON` (CMake) or add Spinnaker include/lib paths in the VS project.
Replace folder/simulated frame generation in `capture_thread` with real `GetNextImage()` logic.

## Design Document

- Full architecture and class/interface documentation: `docs/DESIGN.md`
