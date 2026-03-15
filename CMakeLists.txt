cmake_minimum_required(VERSION 3.14)
project(SpaceEngine)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Find Boost (headers-only usage)
find_package(Boost REQUIRED)

# Find nlohmann_json
find_package(nlohmann_json REQUIRED)

# Allow overriding jluna path (defaults to the Docker container location)
set(JLUNA_DIR "/opt/jluna" CACHE PATH "Path to the jluna source directory")

# Build jluna from source
add_subdirectory(${JLUNA_DIR} jluna_build)

# Main executable
add_executable(SpaceEngine main.cpp)

# ObjectBox include and library directories (target-scoped)
target_include_directories(SpaceEngine PRIVATE /opt/objectbox/include)
target_link_directories(SpaceEngine PRIVATE /opt/objectbox/lib)

target_link_libraries(SpaceEngine
    nlohmann_json::nlohmann_json
    Boost::headers
    objectbox
    jluna
)
