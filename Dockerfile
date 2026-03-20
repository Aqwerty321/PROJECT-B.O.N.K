# ============================================================================
# Stage 1: Builder — compile all targets and run the test suite
# ============================================================================
FROM ubuntu:22.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive
ENV CC=/usr/bin/gcc-12
ENV CXX=/usr/bin/g++-12

# ---------------------------------------------------------------------------
# 1. System packages (no libsimdjson-dev — we FetchContent a pinned version)
# ---------------------------------------------------------------------------
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential \
        ca-certificates \
        cmake \
        gcc-12 \
        g++-12 \
        git \
        curl \
        wget \
        libboost-all-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /workspace

# ---------------------------------------------------------------------------
# 2. Layer-cache optimisation: fetch all C++ dependencies BEFORE copying
#    source files.  Only CMakeLists.txt is needed for the configure step.
# ---------------------------------------------------------------------------
COPY CMakeLists.txt /workspace/CMakeLists.txt

RUN cmake -S /workspace -B /workspace/build \
        -DPROJECTBONK_PREFETCH_ONLY=ON \
        -DCMAKE_BUILD_TYPE=Release

# ---------------------------------------------------------------------------
# 3. Copy source files and build all targets
# ---------------------------------------------------------------------------
COPY main.cpp /workspace/main.cpp
COPY src/ /workspace/src/
COPY tools/ /workspace/tools/
COPY tuner/ /workspace/tuner/
COPY scripts/ /workspace/scripts/
COPY docs/ /workspace/docs/

RUN cmake -S /workspace -B /workspace/build \
        -DPROJECTBONK_PREFETCH_ONLY=OFF \
        -DCMAKE_BUILD_TYPE=Release \
    && cmake --build /workspace/build -j"$(nproc)"

# ---------------------------------------------------------------------------
# 4. Run the CTest safety suite inside the build (fail the image if any
#    gate fails — catch regressions before they ship)
# ---------------------------------------------------------------------------
RUN ctest --test-dir /workspace/build --output-on-failure

# ============================================================================
# Stage 2: Runtime — minimal image with only the server binary + data files
# ============================================================================
FROM ubuntu:22.04 AS runtime

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
        ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# P2.3: Enable CORS by default for frontend integration.
# The allow-origin defaults to http://localhost:5173 when CORS is enabled
# but no specific origin is set.  Override PROJECTBONK_CORS_ALLOW_ORIGIN
# at runtime for production deployments.
ENV PROJECTBONK_CORS_ENABLE=1

# Copy the server binary from the builder stage
COPY --from=builder /workspace/build/ProjectBONK /app/ProjectBONK

# Copy runtime data files (ground-station catalog, etc.)
COPY --from=builder /workspace/docs/groundstations.csv /app/docs/groundstations.csv

EXPOSE 8000

CMD ["/app/ProjectBONK"]
