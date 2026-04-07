cmake_minimum_required(VERSION 3.20)
project(flir_blob_pipeline LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(ENABLE_SPINNAKER "Enable FLIR Spinnaker camera backend" OFF)

add_executable(flir_blob_pipeline
    src/main.cpp
    src/pipeline.cpp
)

target_include_directories(flir_blob_pipeline PRIVATE include)

if (MSVC)
    target_compile_options(flir_blob_pipeline PRIVATE /W4 /permissive-)
else()
    target_compile_options(flir_blob_pipeline PRIVATE -Wall -Wextra -Wpedantic)
endif()

if (ENABLE_SPINNAKER)
    target_compile_definitions(flir_blob_pipeline PRIVATE ENABLE_SPINNAKER=1)
    # Example only: users should configure include/library paths for local SDK install.
    # target_include_directories(flir_blob_pipeline PRIVATE "C:/Program Files/FLIR Systems/Spinnaker/include")
    # target_link_libraries(flir_blob_pipeline PRIVATE Spinnaker_v140)
endif()
