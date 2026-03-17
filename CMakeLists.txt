cmake_minimum_required(VERSION 3.22)
project(ProjectBONK LANGUAGES CXX C)

# ---------------------------------------------------------------------------
# C++ standard
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---------------------------------------------------------------------------
# Default to Release build (aggressive optimisation, no debug info)
# ---------------------------------------------------------------------------
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# ---------------------------------------------------------------------------
# Install-prefix overrides (avoid needing root in container / local builds)
# ---------------------------------------------------------------------------
set(PROJECTBONK_FORCE_LOCAL_INSTALL_PREFIX ON CACHE BOOL
    "Force build-local install prefix to avoid root permissions")

if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX "${CMAKE_BINARY_DIR}/local" CACHE PATH
        "Install path prefix" FORCE)
endif()

if(PROJECTBONK_FORCE_LOCAL_INSTALL_PREFIX
   AND CMAKE_INSTALL_PREFIX STREQUAL "/usr/local")
    set(CMAKE_INSTALL_PREFIX "${CMAKE_BINARY_DIR}/local" CACHE PATH
        "Install path prefix" FORCE)
endif()

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
option(PROJECTBONK_PREFETCH_ONLY
       "Only fetch external dependencies; skip building the executable" OFF)
option(PROJECTBONK_AUTO_INSTALL_JULIA
       "Automatically download Julia on Linux when not found" ON)

set(PROJECTBONK_JULIA_VERSION "1.10.0" CACHE STRING
    "Julia version for automatic download")
set(PROJECTBONK_JULIA_INSTALL_ROOT "${CMAKE_BINARY_DIR}/_deps/julia" CACHE PATH
    "Directory used for automatic Julia download")

include(FetchContent)
include(CTest)

set(PROJECTBONK_TOPLEVEL_BUILD_TESTING "${BUILD_TESTING}")

# ---------------------------------------------------------------------------
# jluna .src symlink helper (unchanged from previous working version)
# ---------------------------------------------------------------------------
function(projectbonk_prepare_jluna_shim JLUNA_SOURCE_ROOT)
    set(PROJECTBONK_JLUNA_COMPAT_PATH "${CMAKE_SOURCE_DIR}/.src")
    set(PROJECTBONK_JLUNA_EXPECTED_FILE
        "${JLUNA_SOURCE_ROOT}/.src/include_julia.inl.in")

    if(NOT EXISTS "${PROJECTBONK_JLUNA_EXPECTED_FILE}")
        message(FATAL_ERROR
            "jluna source at '${JLUNA_SOURCE_ROOT}' is missing "
            ".src/include_julia.inl.in")
    endif()

    if(EXISTS "${PROJECTBONK_JLUNA_COMPAT_PATH}"
       AND NOT IS_SYMLINK "${PROJECTBONK_JLUNA_COMPAT_PATH}")
        message(FATAL_ERROR
            "${PROJECTBONK_JLUNA_COMPAT_PATH} exists and is not a symlink. "
            "Remove it or set JLUNA_DIR to a working jluna checkout.")
    endif()

    if(IS_SYMLINK "${PROJECTBONK_JLUNA_COMPAT_PATH}")
        file(READ_SYMLINK "${PROJECTBONK_JLUNA_COMPAT_PATH}"
             PROJECTBONK_JLUNA_LINK_TARGET)
        if(PROJECTBONK_JLUNA_LINK_TARGET
           STREQUAL "${JLUNA_SOURCE_ROOT}/.src")
            return()
        endif()
    endif()

    file(REMOVE_RECURSE "${PROJECTBONK_JLUNA_COMPAT_PATH}")
    file(CREATE_LINK
        "${JLUNA_SOURCE_ROOT}/.src"
        "${PROJECTBONK_JLUNA_COMPAT_PATH}"
        SYMBOLIC)
endfunction()

# ===================================================================
# External dependencies (always fetched — even in PREFETCH_ONLY mode)
# ===================================================================

# ---------------------------------------------------------------------------
# 1. Boost (headers-only, system package)
# ---------------------------------------------------------------------------
find_package(Boost REQUIRED)

# ---------------------------------------------------------------------------
# 2. simdjson — always fetch a pinned version for reproducibility
# ---------------------------------------------------------------------------
FetchContent_Declare(
    simdjson
    GIT_REPOSITORY https://github.com/simdjson/simdjson.git
    GIT_TAG        v3.9.4
)
FetchContent_MakeAvailable(simdjson)

if(TARGET simdjson AND NOT TARGET simdjson::simdjson)
    add_library(simdjson::simdjson ALIAS simdjson)
endif()

# ---------------------------------------------------------------------------
# 3. cpp-httplib — single header-only HTTP library
#    Downloaded directly; avoids FetchContent/git issues on CMake 3.22.
# ---------------------------------------------------------------------------
set(HTTPLIB_DIR "${CMAKE_BINARY_DIR}/_deps/httplib")
set(HTTPLIB_HEADER "${HTTPLIB_DIR}/httplib.h")

if(NOT EXISTS "${HTTPLIB_HEADER}")
    message(STATUS "Downloading cpp-httplib v0.15.3 header ...")
    file(MAKE_DIRECTORY "${HTTPLIB_DIR}")
    file(DOWNLOAD
        "https://raw.githubusercontent.com/yhirose/cpp-httplib/v0.15.3/httplib.h"
        "${HTTPLIB_HEADER}"
        TLS_VERIFY ON
        STATUS HTTPLIB_DL_STATUS
    )
    list(GET HTTPLIB_DL_STATUS 0 HTTPLIB_DL_RC)
    if(NOT HTTPLIB_DL_RC EQUAL 0)
        list(GET HTTPLIB_DL_STATUS 1 HTTPLIB_DL_MSG)
        message(FATAL_ERROR "Failed to download httplib.h: ${HTTPLIB_DL_MSG}")
    endif()
endif()

add_library(httplib INTERFACE)
target_include_directories(httplib INTERFACE "${HTTPLIB_DIR}")

# ---------------------------------------------------------------------------
# 4. Julia resolution (auto-download on Linux when not on PATH)
# ---------------------------------------------------------------------------
if(NOT DEFINED JULIA_BINDIR)
    find_program(PROJECTBONK_JULIA_EXECUTABLE julia)

    if(PROJECTBONK_JULIA_EXECUTABLE)
        execute_process(
            COMMAND "${PROJECTBONK_JULIA_EXECUTABLE}" -e "print(Sys.BINDIR)"
            OUTPUT_VARIABLE PROJECTBONK_DETECTED_JULIA_BINDIR
            OUTPUT_STRIP_TRAILING_WHITESPACE
            RESULT_VARIABLE PROJECTBONK_DETECT_JULIA_RC
        )
        if(PROJECTBONK_DETECT_JULIA_RC EQUAL 0
           AND PROJECTBONK_DETECTED_JULIA_BINDIR)
            set(JULIA_BINDIR "${PROJECTBONK_DETECTED_JULIA_BINDIR}"
                CACHE PATH "Path to Julia bin directory" FORCE)
            message(STATUS "Using Julia from PATH at ${JULIA_BINDIR}")
        endif()
    elseif(PROJECTBONK_AUTO_INSTALL_JULIA AND UNIX AND NOT APPLE)
        set(PROJECTBONK_JULIA_ARCHIVE
            "julia-${PROJECTBONK_JULIA_VERSION}-linux-x86_64.tar.gz")
        set(PROJECTBONK_JULIA_URL
            "https://julialang-s3.julialang.org/bin/linux/x64/1.10/${PROJECTBONK_JULIA_ARCHIVE}")
        set(PROJECTBONK_JULIA_ARCHIVE_PATH
            "${PROJECTBONK_JULIA_INSTALL_ROOT}/${PROJECTBONK_JULIA_ARCHIVE}")
        set(PROJECTBONK_JULIA_HOME
            "${PROJECTBONK_JULIA_INSTALL_ROOT}/julia-${PROJECTBONK_JULIA_VERSION}")

        file(MAKE_DIRECTORY "${PROJECTBONK_JULIA_INSTALL_ROOT}")

        if(NOT EXISTS "${PROJECTBONK_JULIA_ARCHIVE_PATH}")
            message(STATUS
                "Julia not found on PATH; downloading ${PROJECTBONK_JULIA_ARCHIVE}")
            file(
                DOWNLOAD
                    "${PROJECTBONK_JULIA_URL}"
                    "${PROJECTBONK_JULIA_ARCHIVE_PATH}"
                STATUS  PROJECTBONK_JULIA_DOWNLOAD_STATUS
                SHOW_PROGRESS
                TLS_VERIFY ON
            )
            list(GET PROJECTBONK_JULIA_DOWNLOAD_STATUS 0
                 PROJECTBONK_JULIA_DOWNLOAD_RC)
            if(NOT PROJECTBONK_JULIA_DOWNLOAD_RC EQUAL 0)
                list(GET PROJECTBONK_JULIA_DOWNLOAD_STATUS 1
                     PROJECTBONK_JULIA_DOWNLOAD_MSG)
                message(FATAL_ERROR
                    "Failed to download Julia from ${PROJECTBONK_JULIA_URL}: "
                    "${PROJECTBONK_JULIA_DOWNLOAD_MSG}")
            endif()
        endif()

        if(NOT EXISTS "${PROJECTBONK_JULIA_HOME}/bin/julia")
            message(STATUS
                "Extracting Julia to ${PROJECTBONK_JULIA_INSTALL_ROOT}")
            execute_process(
                COMMAND "${CMAKE_COMMAND}" -E tar xzf
                        "${PROJECTBONK_JULIA_ARCHIVE_PATH}"
                WORKING_DIRECTORY "${PROJECTBONK_JULIA_INSTALL_ROOT}"
                RESULT_VARIABLE PROJECTBONK_JULIA_EXTRACT_RC
            )
            if(NOT PROJECTBONK_JULIA_EXTRACT_RC EQUAL 0)
                message(FATAL_ERROR
                    "Failed to extract Julia archive "
                    "${PROJECTBONK_JULIA_ARCHIVE_PATH}")
            endif()
        endif()

        set(JULIA_BINDIR "${PROJECTBONK_JULIA_HOME}/bin"
            CACHE PATH "Path to Julia bin directory" FORCE)
        message(STATUS "Using auto-installed Julia at ${JULIA_BINDIR}")
    endif()
endif()

if(DEFINED JULIA_BINDIR)
    set(ENV{JULIA_BINDIR} "${JULIA_BINDIR}")
endif()

# Create an imported Julia::Julia target if Julia is resolved
if(DEFINED JULIA_BINDIR AND NOT TARGET Julia::Julia)
    get_filename_component(PROJECTBONK_JULIA_PREFIX
        "${JULIA_BINDIR}" DIRECTORY)
    find_library(PROJECTBONK_JULIA_LIBRARY julia
        HINTS "${PROJECTBONK_JULIA_PREFIX}/lib")
    find_path(PROJECTBONK_JULIA_INCLUDE_DIR julia.h
        HINTS
            "${PROJECTBONK_JULIA_PREFIX}/include"
            "${PROJECTBONK_JULIA_PREFIX}/include/julia"
    )

    if(PROJECTBONK_JULIA_LIBRARY AND PROJECTBONK_JULIA_INCLUDE_DIR)
        add_library(Julia::Julia UNKNOWN IMPORTED)
        set_target_properties(Julia::Julia PROPERTIES
            IMPORTED_LOCATION "${PROJECTBONK_JULIA_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${PROJECTBONK_JULIA_INCLUDE_DIR}"
            IMPORTED_LINK_INTERFACE_LANGUAGES C
        )
    endif()
endif()

# ---------------------------------------------------------------------------
# 5. jluna — use local checkout or fetch from source
# ---------------------------------------------------------------------------
set(JLUNA_DIR "" CACHE PATH "Path to a local jluna source directory")

find_package(jluna CONFIG QUIET)
if(NOT jluna_FOUND)
    if(NOT DEFINED JULIA_BINDIR)
        message(FATAL_ERROR
            "jluna was not found as an installed package and Julia could not "
            "be resolved. Re-run CMake with -DJULIA_BINDIR=/path/to/julia/bin "
            "or enable PROJECTBONK_AUTO_INSTALL_JULIA on Linux.")
    endif()

    if(JLUNA_DIR)
        if(NOT EXISTS "${JLUNA_DIR}/CMakeLists.txt")
            message(WARNING
                "JLUNA_DIR='${JLUNA_DIR}' does not contain a CMake project. "
                "Falling back to automatic jluna fetch.")
            set(JLUNA_DIR "" CACHE PATH
                "Path to a local jluna source directory" FORCE)
        endif()
    endif()

    # Disable third-party tests while configuring jluna, then restore
    # top-level BUILD_TESTING for ProjectBONK tests.
    set(BUILD_TESTING OFF CACHE BOOL "Disable third-party tests" FORCE)

    if(JLUNA_DIR)
        message(STATUS
            "jluna not found via find_package(); using source directory "
            "${JLUNA_DIR}")
        projectbonk_prepare_jluna_shim("${JLUNA_DIR}")
        add_subdirectory(${JLUNA_DIR} jluna_build)
    else()
        message(STATUS
            "jluna not found via find_package(); fetching from source")
        set(jluna_DEVELOPER_MODE OFF CACHE BOOL
            "Disable jluna developer mode" FORCE)
        FetchContent_Declare(
            jluna
            GIT_REPOSITORY https://github.com/Clemapfel/jluna.git
            GIT_TAG        v1.0.1
        )
        FetchContent_GetProperties(jluna)
        if(NOT jluna_POPULATED)
            FetchContent_Populate(jluna)
        endif()

        projectbonk_prepare_jluna_shim("${jluna_SOURCE_DIR}")
        add_subdirectory("${jluna_SOURCE_DIR}" "${jluna_BINARY_DIR}")
    endif()

    set(BUILD_TESTING "${PROJECTBONK_TOPLEVEL_BUILD_TESTING}" CACHE BOOL
        "Enable project tests" FORCE)

    if(TARGET jluna AND NOT TARGET jluna::jluna)
        add_library(jluna::jluna ALIAS jluna)
    endif()
endif()

# ===================================================================
# PREFETCH_ONLY guard — stop here if we only wanted to cache deps
# ===================================================================
if(PROJECTBONK_PREFETCH_ONLY)
    message(STATUS
        "PROJECTBONK_PREFETCH_ONLY is ON — "
        "dependencies fetched, skipping executable build.")
    return()
endif()

# ===================================================================
# Main executable
# ===================================================================
add_executable(ProjectBONK
    main.cpp
    src/broad_phase.cpp
    src/earth_frame.cpp
    src/state_store.cpp
    src/telemetry.cpp
    src/sim_clock.cpp
    src/orbit_math.cpp
    src/propagator.cpp
    src/simulation_engine.cpp
)

set_target_properties(ProjectBONK PROPERTIES
    BUILD_RPATH "${CMAKE_INSTALL_PREFIX}"
)

# Include src/ so headers can be included without path prefix
target_include_directories(ProjectBONK PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
)

# Aggressive optimisation flags (no -march=native for cross-CPU compat)
target_compile_options(ProjectBONK PRIVATE
    -O3
    -DNDEBUG
)

target_link_libraries(ProjectBONK PRIVATE
    simdjson::simdjson
    Boost::headers
    jluna::jluna
    httplib
)

# ===================================================================
# Phase 2 regression harness (fast propagator vs RK4 reference)
# ===================================================================
add_executable(phase2_regression
    tools/phase2_regression.cpp
    src/orbit_math.cpp
    src/propagator.cpp
)

target_include_directories(phase2_regression PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
)

target_compile_options(phase2_regression PRIVATE
    -O3
    -DNDEBUG
)

add_custom_target(phase2_regression_gate
    COMMAND "$<TARGET_FILE:phase2_regression>" 3000 300 86400 --strict-adaptive
    COMMAND "$<TARGET_FILE:phase2_regression>" 3000 1 120 --strict-adaptive
    DEPENDS phase2_regression
    USES_TERMINAL
)

add_executable(broad_phase_validation
    tools/broad_phase_validation.cpp
    src/broad_phase.cpp
    src/state_store.cpp
    src/sim_clock.cpp
    src/orbit_math.cpp
)

target_include_directories(broad_phase_validation PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
)

target_compile_options(broad_phase_validation PRIVATE
    -O3
    -DNDEBUG
)

add_custom_target(broad_phase_sanity_gate
    COMMAND "$<TARGET_FILE:broad_phase_validation>" 3 50 10000 1
    DEPENDS broad_phase_validation
    USES_TERMINAL
)

# ===================================================================
# Phase 3 tick benchmark harness
# ===================================================================
add_executable(phase3_tick_benchmark
    tools/phase3_tick_benchmark.cpp
    src/broad_phase.cpp
    src/state_store.cpp
    src/sim_clock.cpp
    src/orbit_math.cpp
    src/propagator.cpp
    src/simulation_engine.cpp
)

target_include_directories(phase3_tick_benchmark PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
)

target_compile_options(phase3_tick_benchmark PRIVATE
    -O3
    -DNDEBUG
)

# ===================================================================
# Offline multi-objective tuner scaffold (separate from runtime path)
# ===================================================================
add_executable(offline_multiobjective_tuner
    tuner/offline_multiobjective_tuner.cpp
    src/broad_phase.cpp
    src/state_store.cpp
    src/sim_clock.cpp
    src/orbit_math.cpp
)

target_include_directories(offline_multiobjective_tuner PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
)

target_compile_options(offline_multiobjective_tuner PRIVATE
    -O3
    -DNDEBUG
)

if(BUILD_TESTING)
    add_test(
        NAME phase2_regression_gate_test
        COMMAND "$<TARGET_FILE:phase2_regression>" 3000 300 86400 --strict-adaptive
    )
    add_test(
        NAME phase2_regression_gate_short_step_test
        COMMAND "$<TARGET_FILE:phase2_regression>" 3000 1 120 --strict-adaptive
    )
    add_test(
        NAME broad_phase_sanity_gate_test
        COMMAND "$<TARGET_FILE:broad_phase_validation>" 3 50 10000 1
    )
endif()
