FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive
ENV JULIA_VERSION=1.10.0
ENV JULIA_BINDIR=/opt/julia-1.10.0/bin
ENV PATH="${JULIA_BINDIR}:${PATH}"
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

# ---------------------------------------------------------------------------
# 2. Julia 1.10.0 — manual install with SHA-256 verification
# ---------------------------------------------------------------------------
RUN set -eux; \
    JULIA_TARBALL="julia-${JULIA_VERSION}-linux-x86_64.tar.gz"; \
    JULIA_BASE_URL="https://julialang-s3.julialang.org/bin/linux/x64/1.10"; \
    wget -q -O "${JULIA_TARBALL}" "${JULIA_BASE_URL}/${JULIA_TARBALL}"; \
    EXPECTED_SHA="a7298207f72f2b27b2ab1ce392a6ea37afbd1e1a9d157239e26be44c77384d3a"; \
    echo "${EXPECTED_SHA}  ${JULIA_TARBALL}" | sha256sum -c -; \
    tar -xzf "${JULIA_TARBALL}" -C /opt/; \
    ln -sf /opt/julia-${JULIA_VERSION}/bin/julia /usr/local/bin/julia; \
    rm -f "${JULIA_TARBALL}"

WORKDIR /workspace

# ---------------------------------------------------------------------------
# 3. Layer-cache optimisation: fetch all C++ dependencies BEFORE copying
#    source files.  Only CMakeLists.txt is needed for the configure step.
# ---------------------------------------------------------------------------
COPY CMakeLists.txt /workspace/CMakeLists.txt

RUN cmake -S /workspace -B /workspace/build \
        -DPROJECTBONK_PREFETCH_ONLY=ON \
        -DCMAKE_BUILD_TYPE=Release

# ---------------------------------------------------------------------------
# 4. Copy source files and build the real executable
# ---------------------------------------------------------------------------
COPY main.cpp /workspace/main.cpp
# Future: COPY src/ /workspace/src/

RUN cmake -S /workspace -B /workspace/build \
        -DPROJECTBONK_PREFETCH_ONLY=OFF \
        -DCMAKE_BUILD_TYPE=Release \
    && cmake --build /workspace/build -j"$(nproc)"

EXPOSE 8000

CMD ["/workspace/build/ProjectBONK"]
