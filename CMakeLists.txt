cmake_minimum_required(VERSION 3.13.1)
project(app C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_SYSROOT ${CMAKE_SOURCE_DIR}/rpi-sysroot)
set(CMAKE_C_COMPILER aarch64-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER aarch64-linux-gnu-g++)

add_executable(app main_full.cpp)

include_directories(
    # Project includes directories
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SYSROOT}/usr/include

    # Vosk includes directories
    ${CMAKE_SYSROOT}/opt/vosk
    ${CMAKE_SYSROOT}/opt/vosk/lib
    ${CMAKE_SYSROOT}/opt/vosk/include

    # Edge impulse includes directories
    ${CMAKE_SOURCE_DIR}/edge-impulse-sdk
    ${CMAKE_SOURCE_DIR}/edge-impulse-sdk/CMSIS/DSP/Include
    ${CMAKE_SOURCE_DIR}/tflite-model
    ${CMAKE_SOURCE_DIR}/model-parameters

    ${CMAKE_SOURCE_DIR}/edge-impulse-sdk/classifier
    ${CMAKE_SOURCE_DIR}/edge-impulse-sdk/tensorflow
    ${CMAKE_SOURCE_DIR}/edge-impulse-sdk/tensorflow/lite
    ${CMAKE_SOURCE_DIR}/edge-impulse-sdk/tensorflow/lite/c
    ${CMAKE_SOURCE_DIR}/edge-impulse-sdk/tensorflow/lite/micro
)

link_directories(
    ${CMAKE_SYSROOT}/usr/lib/aarch64-linux-gnu
    ${CMAKE_SYSROOT}/lib/aarch64-linux-gnu
    ${CMAKE_SYSROOT}/opt/vosk/lib
)

if(NOT TARGET app)
     message(FATAL_ERROR "Please create a target named 'app' (ex: add_executable(app)) before adding this file")
endif()

include(edge-impulse-sdk/cmake/utils.cmake)
add_subdirectory(edge-impulse-sdk/cmake/zephyr)

RECURSIVE_FIND_FILE_APPEND(MODEL_SOURCE "tflite-model" "*.cpp")
target_include_directories(app PRIVATE .)

# add all sources to the project
target_sources(app PRIVATE 
    ${MODEL_SOURCE}
    tflite-model/tflite_learn_5_compiled.cpp
    can.cpp
    #edge-impulse-sdk/classifier/ei_classifier.cpp
    #edge-impulse-sdk/classifier/ei_run_classifier.cpp
    #edge-impulse-sdk/classifier/ei_run_impulse.cpp
    #edge-impulse-sdk/classifier/ei_run_impulse_with_model_parameters.cpp
)

target_link_libraries(app
    ${CMAKE_SYSROOT}/opt/vosk/lib/libvosk.so
    asound
    jsoncpp
    pthread
    m
)

set_target_properties(app PROPERTIES
    INSTALL_RPATH /opt/vosk/lib
)