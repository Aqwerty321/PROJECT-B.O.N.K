# ============================================================================
# Stage 1: Builder — compile all targets and run the test suite
# ============================================================================
FROM ubuntu:22.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive
ENV CC=/usr/bin/gcc-12
ENV CXX=/usr/bin/g++-12

# ---------------------------------------------------------------------------
# 1. System packages — A2: libboost-dev (headers-only suffices)
#                       A4: sccache for compiler caching
#                       A5: removed curl, wget (unused)
# ---------------------------------------------------------------------------
RUN --mount=type=cache,target=/var/cache/apt,sharing=locked \
    --mount=type=cache,target=/var/lib/apt/lists,sharing=locked \
    apt-get update && apt-get install -y --no-install-recommends \
        build-essential \
        ca-certificates \
        cmake \
        gcc-12 \
        g++-12 \
        git \
        libboost-dev

# A4: Install sccache — download a static binary
RUN --mount=type=cache,target=/var/cache/apt,sharing=locked \
    --mount=type=cache,target=/var/lib/apt/lists,sharing=locked \
    apt-get update && apt-get install -y --no-install-recommends curl \
    && curl -fsSL https://github.com/mozilla/sccache/releases/download/v0.8.1/sccache-v0.8.1-x86_64-unknown-linux-musl.tar.gz \
       | tar xz -C /usr/local/bin --strip-components=1 sccache-v0.8.1-x86_64-unknown-linux-musl/sccache \
    && chmod +x /usr/local/bin/sccache \
    && apt-get purge -y curl && apt-get autoremove -y

ENV CMAKE_C_COMPILER_LAUNCHER=sccache
ENV CMAKE_CXX_COMPILER_LAUNCHER=sccache

WORKDIR /workspace

# ---------------------------------------------------------------------------
# 2. Layer-cache optimisation: fetch all C++ dependencies BEFORE copying
#    source files.  Only CMakeLists.txt is needed for the configure step.
#    A3: cache mount on _deps so FetchContent downloads survive rebuilds.
# ---------------------------------------------------------------------------
COPY CMakeLists.txt /workspace/CMakeLists.txt

RUN --mount=type=cache,target=/workspace/build/_deps,sharing=locked \
    cmake -S /workspace -B /workspace/build \
        -DPROJECTBONK_PREFETCH_ONLY=ON \
        -DCMAKE_BUILD_TYPE=Release

# ---------------------------------------------------------------------------
# 3. Copy source files and build all targets
#    A3: cache mount on build dir for incremental rebuilds.
#    A4: sccache cache mount.
#    NOTE: Binary is copied out of cache mount before RUN ends.
# ---------------------------------------------------------------------------
COPY main.cpp /workspace/main.cpp
COPY src/ /workspace/src/
COPY tools/ /workspace/tools/
COPY tuner/ /workspace/tuner/
COPY scripts/ /workspace/scripts/
COPY docs/ /workspace/docs/

RUN --mount=type=cache,target=/workspace/build,sharing=locked \
    --mount=type=cache,target=/root/.cache/sccache,sharing=locked \
    cmake -S /workspace -B /workspace/build \
        -DPROJECTBONK_PREFETCH_ONLY=OFF \
        -DCMAKE_BUILD_TYPE=Release \
    && cmake --build /workspace/build -j"$(nproc)" \
    && cp /workspace/build/ProjectBONK /workspace/ProjectBONK

# ---------------------------------------------------------------------------
# 4. Run the CTest safety suite inside the build (fail the image if any
#    gate fails — catch regressions before they ship)
# ---------------------------------------------------------------------------
RUN --mount=type=cache,target=/workspace/build,sharing=locked \
    ctest --test-dir /workspace/build --output-on-failure

# ============================================================================
# Stage 2: Frontend — build the React dashboard
# ============================================================================
FROM node:20-slim AS frontend

WORKDIR /frontend
COPY frontend/package.json frontend/package-lock.json* ./
RUN npm ci --ignore-scripts
COPY frontend/ ./
RUN npm run build

# ============================================================================
# Stage 3: Runtime — ubuntu:22.04 (PS Section 8 requirement)
# ============================================================================
FROM ubuntu:22.04 AS runtime

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
        ca-certificates \
        libgomp1 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# P2.3: Enable CORS by default for frontend integration.
# The allow-origin defaults to http://localhost:5173 when CORS is enabled
# but no specific origin is set.  Override PROJECTBONK_CORS_ALLOW_ORIGIN
# at runtime for production deployments.
ENV PROJECTBONK_CORS_ENABLE=1

# Copy the server binary (extracted from cache mount in builder stage)
COPY --from=builder /workspace/ProjectBONK /app/ProjectBONK

# Copy runtime data files (ground-station catalog, etc.)
COPY --from=builder /workspace/docs/groundstations.csv /app/docs/groundstations.csv

# Copy built frontend assets (served by cpp-httplib set_mount_point)
COPY --from=frontend /frontend/dist /app/static

EXPOSE 8000

CMD ["/app/ProjectBONK"]
