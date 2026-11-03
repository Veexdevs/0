# NovaWin — núcleo nativo (libnovawin.so)
# Compilado pelo Android NDK (arm64-v8a). O SDL2/SDL2_ttf são construídos
# separadamente na CI (mesmo toolchain) e fornecidos em third_party/sdl2.
cmake_minimum_required(VERSION 3.22.1)
project(NovaWinCore CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(SDL2_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/sdl2")

add_library(SDL2 SHARED IMPORTED)
set_target_properties(SDL2 PROPERTIES
    IMPORTED_LOCATION "${SDL2_DIR}/lib/arm64-v8a/libSDL2.so"
    INTERFACE_INCLUDE_DIRECTORIES "${SDL2_DIR}/include")

add_library(SDL2_ttf SHARED IMPORTED)
set_target_properties(SDL2_ttf PROPERTIES
    IMPORTED_LOCATION "${SDL2_DIR}/lib/arm64-v8a/libSDL2_ttf.so"
    INTERFACE_INCLUDE_DIRECTORIES "${SDL2_DIR}/include")

file(GLOB NOVAWIN_SRC "${CMAKE_CURRENT_SOURCE_DIR}/novawin/*.cpp")

add_library(novawin SHARED ${NOVAWIN_SRC})

target_include_directories(novawin PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/novawin"
    "${SDL2_DIR}/include")

target_link_libraries(novawin
    SDL2
    SDL2_ttf
    android
    log
    z)

set_target_properties(novawin PROPERTIES
    POSITION_INDEPENDENT_CODE ON)

target_compile_options(novawin PRIVATE -O2 -fvisibility=hidden)
