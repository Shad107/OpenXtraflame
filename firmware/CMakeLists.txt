# Top-level CMakeLists for openextraflame firmware
cmake_minimum_required(VERSION 3.16)

# Target selection: external (default) or blacklabel
# We use OPENXFLAME_TARGET because plain "TARGET" is a CMake reserved word.
if(NOT DEFINED OPENXFLAME_TARGET)
    set(OPENXFLAME_TARGET "blacklabel")
endif()

message(STATUS "Building openextraflame for target: ${OPENXFLAME_TARGET}")

if(OPENXFLAME_TARGET STREQUAL "external")
    add_compile_definitions(TARGET_EXTERNAL=1)
elseif(OPENXFLAME_TARGET STREQUAL "blacklabel")
    add_compile_definitions(TARGET_BLACKLABEL=1)
else()
    message(FATAL_ERROR "Unknown OPENXFLAME_TARGET: ${OPENXFLAME_TARGET}. Use 'external' or 'blacklabel'.")
endif()

# Firmware version stamped into esp_app_get_description()->version, shown in
# the Web UI header and the OTA tab. Bump on each release.
set(PROJECT_VER "v0.1.0-beta45")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(openextraflame)
