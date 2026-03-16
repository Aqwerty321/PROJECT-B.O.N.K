cmake_minimum_required(VERSION 3.14)
project(ProjectBONK)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Find Boost (headers-only usage)
find_package(Boost REQUIRED)

# Find simdjson for JSON parsing
find_package(simdjson REQUIRED)

# Allow overriding jluna path (defaults to the Docker container location)
set(JLUNA_DIR "/opt/jluna" CACHE PATH "Path to the jluna source directory")

# Build jluna from source
add_subdirectory(${JLUNA_DIR} jluna_build)

# Main executable
add_executable(ProjectBONK main.cpp)

target_link_libraries(ProjectBONK
    simdjson::simdjson
    Boost::headers
    jluna
)
