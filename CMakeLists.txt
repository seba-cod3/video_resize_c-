cmake_minimum_required(VERSION 3.15)
project(VideoProcessor)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add FetchContent for downloading cpp-httplib
include(FetchContent)
FetchContent_Declare(
    httplib
    GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
    GIT_TAG v0.14.1
)
FetchContent_MakeAvailable(httplib)

find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET
    libavcodec
    libavformat
    libavutil
    libswscale
)

# Main video processor library
add_library(video_processor_lib
    src/video_processor.cpp
)

target_include_directories(video_processor_lib 
    PUBLIC include
    PRIVATE ${FFMPEG_INCLUDE_DIRS}
)

target_link_libraries(video_processor_lib 
    PRIVATE PkgConfig::FFMPEG
)

# CLI executable
add_executable(video_processor_cli
    src/main.cpp
)

target_link_libraries(video_processor_cli
    PRIVATE video_processor_lib
)

# Server executable
add_executable(video_processor_server
    src/server.cpp
)

target_link_libraries(video_processor_server
    PRIVATE video_processor_lib
    PRIVATE httplib::httplib
)