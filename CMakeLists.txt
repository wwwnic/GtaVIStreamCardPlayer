cmake_minimum_required(VERSION 3.20)
project(GamePlayer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# Static runtime: the Viewer ships as one plain executable, no redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_compile_options(/W4 /permissive- /utf-8)
add_compile_definitions(UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)

# Pure logic: no window, device or GPU. Everything here is unit-tested.
add_library(gameplayer_core STATIC
    src/core/picture_fit.cpp
    src/core/capture_choice.cpp
    src/core/colour.cpp
    src/core/frame.cpp
    src/core/processor.cpp
    src/core/sound_ring.cpp
    src/core/drift_correction.cpp
    src/core/resampler.cpp
    src/core/pcm.cpp
    src/core/sound_playout.cpp
)
target_include_directories(gameplayer_core PUBLIC src)

# The Viewer. Executable name must stay "GamePlayer" (Lossless Scaling profile).
add_executable(GamePlayer WIN32
    src/app/main.cpp
    src/platform/log.cpp
    src/platform/mf_source.cpp
    src/platform/presenter.cpp
    src/platform/sound.cpp
)
target_link_libraries(GamePlayer PRIVATE gameplayer_core
    d3d11 dxgi d3dcompiler mfplat mf mfreadwrite mfuuid ole32 user32 gdi32 shell32 avrt)

enable_testing()
add_executable(gameplayer_tests
    tests/test_main.cpp
    tests/picture_fit_test.cpp
    tests/capture_choice_test.cpp
    tests/colour_test.cpp
    tests/latest_slot_test.cpp
    tests/frame_test.cpp
    tests/processor_test.cpp
    tests/sound_ring_test.cpp
    tests/drift_correction_test.cpp
    tests/resampler_test.cpp
    tests/pcm_test.cpp
    tests/sound_playout_test.cpp
)
target_include_directories(gameplayer_tests PRIVATE third_party/doctest)
target_link_libraries(gameplayer_tests PRIVATE gameplayer_core)
add_test(NAME unit COMMAND gameplayer_tests)
