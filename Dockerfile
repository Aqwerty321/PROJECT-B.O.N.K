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
    nlohmann-json3-dev \
    && rm -rf /var/lib/apt/lists/*

# Install Julia 1.10.0 (official Linux x64 binary) with SHA256 verification
RUN wget https://julialang-s3.julialang.org/bin/linux/x64/1.10/julia-1.10.0-linux-x86_64.tar.gz \
    && echo "a7299b73d49145827e6daa9d5f75073b3b72dc17d6b8a73e7d2e8d28f31523bb  julia-1.10.0-linux-x86_64.tar.gz" | sha256sum -c - \
    && tar -xzf julia-1.10.0-linux-x86_64.tar.gz -C /opt/ \
    && ln -s /opt/julia-1.10.0/bin/julia /usr/local/bin/julia \
    && rm julia-1.10.0-linux-x86_64.tar.gz

# Install ObjectBox C/C++ SDK into /opt/objectbox
# Download script to a temporary file before executing for auditability
RUN curl -fsSL https://raw.githubusercontent.com/objectbox/objectbox-c/main/download.sh \
        -o /tmp/objectbox-download.sh \
    && bash /tmp/objectbox-download.sh --install /opt/objectbox \
    && rm /tmp/objectbox-download.sh

# Clone jluna repository
RUN git clone https://github.com/Clemapfel/jluna.git /opt/jluna

# Add ObjectBox library path to LD_LIBRARY_PATH
ENV LD_LIBRARY_PATH=/opt/objectbox/lib:${LD_LIBRARY_PATH}

EXPOSE 8000

WORKDIR /workspace
