cmake_minimum_required(VERSION 3.13.1)
project(app C CXX)

set(CMAKE_CXX_STANDARD 17)

# ----------------------------------------------------------------------
#  Detect architecture from toolchain and configure sysroot paths
# ----------------------------------------------------------------------

if(NOT DEFINED CMAKE_SYSROOT)
    message(FATAL_ERROR "CMAKE_SYSROOT not defined! Use -DCMAKE_TOOLCHAIN_FILE=<toolchain.cmake>")
endif()

message(STATUS "Using SYSROOT = ${CMAKE_SYSROOT}")
message(STATUS "Compiler = ${CMAKE_C_COMPILER}")

# Paths that may exist depending on arch
set(SYSROOT_LIB_DIRS
    ${CMAKE_SYSROOT}/usr/lib/arm-linux-gnueabihf
    ${CMAKE_SYSROOT}/lib/arm-linux-gnueabihf
    ${CMAKE_SYSROOT}/usr/lib/aarch64-linux-gnu
    ${CMAKE_SYSROOT}/lib/aarch64-linux-gnu
    ${CMAKE_SYSROOT}/usr/lib
    ${CMAKE_SYSROOT}/lib
    ${CMAKE_SYSROOT}/opt/vosk/lib
)

foreach(dir ${SYSROOT_LIB_DIRS})
    if(EXISTS ${dir})
        message(STATUS "Adding library search path: ${dir}")
        link_directories(${dir})
    endif()
endforeach()

# ----------------------------------------------------------------------
#  Include paths
# ----------------------------------------------------------------------

include_directories(
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SYSROOT}/usr/include

    # Vosk
    ${CMAKE_SYSROOT}/opt/vosk
    ${CMAKE_SYSROOT}/opt/vosk/include

    # Edge Impulse
    ${CMAKE_SOURCE_DIR}/edge-impulse-sdk
    ${CMAKE_SOURCE_DIR}/edge-impulse-sdk/CMSIS/DSP/Include
    ${CMAKE_SOURCE_DIR}/edge-impulse-sdk/classifier
    ${CMAKE_SOURCE_DIR}/edge-impulse-sdk/tensorflow
    ${CMAKE_SOURCE_DIR}/edge-impulse-sdk/tensorflow/lite
    ${CMAKE_SOURCE_DIR}/edge-impulse-sdk/tensorflow/lite/micro
    ${CMAKE_SOURCE_DIR}/tflite-model
    ${CMAKE_SOURCE_DIR}/model-parameters
)

add_executable(app
    src/main.cpp
    src/can.cpp
    src/audio.cpp
    src/vosk.cpp
    src/edge_impulse.cpp
)

# ----------------------------------------------------------------------
#  Edge Impulse model source discovery
# ----------------------------------------------------------------------

include(edge-impulse-sdk/cmake/utils.cmake)
add_subdirectory(edge-impulse-sdk/cmake/zephyr)

RECURSIVE_FIND_FILE_APPEND(MODEL_SOURCE "tflite-model" "*.cpp")
target_sources(app PRIVATE ${MODEL_SOURCE})

# Modelo principal (compilado)
target_sources(app PRIVATE
    tflite-model/tflite_learn_5_compiled.cpp
)

# ----------------------------------------------------------------------
#  Libraries
# ----------------------------------------------------------------------

target_link_libraries(app
    PRIVATE
        jsoncpp
        asound
        sndfile
        pthread
        m
        ${CMAKE_SYSROOT}/opt/vosk/libvosk.so
)

# ----------------------------------------------------------------------
#  Runtime path for Vosk on target
# ----------------------------------------------------------------------

set_target_properties(app PROPERTIES
    INSTALL_RPATH "/opt/vosk"
)