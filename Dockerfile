FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive

# Install core build tools and libraries
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    git \
    curl \
    wget \
    libboost-all-dev \
    libsimdjson-dev \
    && rm -rf /var/lib/apt/lists/*

# Install Julia 1.10.0 (official Linux x64 binary) with SHA256 verification
RUN set -eux; \
    JULIA_VERSION="1.10.0"; \
    JULIA_TARBALL="julia-${JULIA_VERSION}-linux-x86_64.tar.gz"; \
    JULIA_BASE_URL="https://julialang-s3.julialang.org/bin/linux/x64/1.10"; \
    JULIA_CHECKSUM_URL="https://julialang-s3.julialang.org/bin/checksums/julia-${JULIA_VERSION}.sha256"; \
    wget -O "${JULIA_TARBALL}" "${JULIA_BASE_URL}/${JULIA_TARBALL}"; \
    wget -O SHA256SUMS "${JULIA_BASE_URL}/sha256sums.txt" || wget -O SHA256SUMS "${JULIA_CHECKSUM_URL}"; \
    grep "${JULIA_TARBALL}\$" SHA256SUMS | sha256sum -c -; \
    tar -xzf "${JULIA_TARBALL}" -C /opt/; \
    ln -s /opt/julia-${JULIA_VERSION}/bin/julia /usr/local/bin/julia; \
    rm -f "${JULIA_TARBALL}" SHA256SUMS


# Clone jluna repository
RUN git clone https://github.com/Clemapfel/jluna.git /opt/jluna

 

EXPOSE 8000

WORKDIR /workspace

# Copy project sources and build the executable at image build time
COPY . /workspace

RUN set -eux; \
    rm -rf /workspace/build; \
    ln -sfn /opt/jluna/.src /workspace/.src; \
    cmake -S /workspace -B /workspace/build -DJLUNA_DIR=/opt/jluna; \
    cmake --build /workspace/build -j"$(nproc)"

# Run the app by default when the container starts
CMD ["/workspace/build/ProjectBONK"]
