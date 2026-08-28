cmake_minimum_required(VERSION 3.20)

project(SanHands LANGUAGES CXX)

set(PLUGIN_SDK_DIR "C:/Users/Digon/Documents/Fuentes/plugin-sdk-master" CACHE PATH "Ruta a plugin-sdk")

add_library(SanHands SHARED src/SanHands.cpp)
set_target_properties(SanHands PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS YES
    PREFIX ""
    OUTPUT_NAME "SanHands"
    SUFFIX ".asi"
)

target_compile_definitions(SanHands PRIVATE
    GTASA
    PLUGIN_SGV_10US
    RW
    _CRT_SECURE_NO_WARNINGS
    TARGET_NAME="SanHands"
)

target_include_directories(SanHands SYSTEM PRIVATE
    "${PLUGIN_SDK_DIR}/plugin_sa"
    "${PLUGIN_SDK_DIR}/plugin_sa/game_sa"
    "${PLUGIN_SDK_DIR}/plugin_sa/game_sa/enums"
    "${PLUGIN_SDK_DIR}/plugin_sa/game_sa/rw"
    "${PLUGIN_SDK_DIR}/shared"
    "${PLUGIN_SDK_DIR}/shared/game"
    "${PLUGIN_SDK_DIR}/injector"
    "${PLUGIN_SDK_DIR}/hooking"
    "${PLUGIN_SDK_DIR}/safetyhook"
)

target_compile_options(SanHands PRIVATE
    -m32
    -Wall
    -Wextra
    -Wpedantic
    -Wconversion
    -Wshadow
    -fpermissive
)

target_link_directories(SanHands PRIVATE "${PLUGIN_SDK_DIR}/output/mingw/lib")
target_link_libraries(SanHands PRIVATE Plugin)
target_link_options(SanHands PRIVATE
    -m32
    -static-libgcc
    -static-libstdc++
    -Wl,--whole-archive
    -Wl,-Bstatic
    -lwinpthread
    -Wl,-Bdynamic
    -Wl,--no-whole-archive
    -Wl,--subsystem,windows
    -Wl,--exclude-all-symbols
    -Wl,--gc-sections
    -s
)
