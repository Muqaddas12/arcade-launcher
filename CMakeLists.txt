cmake_minimum_required(VERSION 3.16)

project(MalikGameOS)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
find_package(Vulkan REQUIRED)
pkg_check_modules(SDL2 REQUIRED sdl2)
pkg_check_modules(SDL2_TTF REQUIRED SDL2_ttf)

add_executable(malik-game-os
    src/main.cpp
    src/GameList.cpp
    src/Controller.cpp
    src/Settings.cpp
    src/DuckStationSettings.cpp
    src/HardwareOptimizer.cpp
)

target_include_directories(malik-game-os PRIVATE
    ${SDL2_INCLUDE_DIRS}
    ${SDL2_TTF_INCLUDE_DIRS}
)

target_link_directories(malik-game-os PRIVATE
    ${SDL2_LIBRARY_DIRS}
    ${SDL2_TTF_LIBRARY_DIRS}
)

target_link_libraries(malik-game-os
    ${SDL2_LIBRARIES}
    ${SDL2_TTF_LIBRARIES}
    Vulkan::Vulkan
)