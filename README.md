# FLIR Bayer Hit-Frame Pipeline (Scaffold)

This repository now contains a Windows/MSVC-oriented C++ scaffold for:

- FLIR camera acquisition at high frame rates (target 170 fps)
- Blob detection pipeline (placeholder now, OpenCV drop-in point prepared)
- Store **only hit frames** + metadata CSV

## Build

```bash
cmake -S . -B build
cmake --build build -j
```

## Run

```bash
./build/flir_blob_pipeline
```

The scaffold runs for 5 seconds and writes output to `output/`.

## Module-level debug order (recommended)

1. **Capture thread**
   - Confirm stable frame id increment and queue occupancy.
2. **Detection thread**
   - Replace `detect_blob()` with OpenCV-based Bayer-domain/gray-domain logic.
3. **Storage thread**
   - Verify only hit frames are written.
   - Verify `hits.csv` columns are complete.
4. **End-to-end timing**
   - Capture latency (`capture -> detect -> store`) and dropped-frame count.

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

Use `ENABLE_SPINNAKER=ON` and then configure include/library path in `CMakeLists.txt`.
Replace simulated frame generation in `capture_thread` with real `GetNextImage()` logic.

