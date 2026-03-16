# C.A.S.C.A.D.E

## Collision Avoidance System for Constellation Analysis and Debris Elimination

C++20 project that integrates:

- `Boost` (headers)
- `simdjson`
- `jluna` + `Julia 1.10.0`

The repository includes a Docker build that compiles the executable and runs it on container start.

## Quick Start (Docker)

### 1. Build image

```bash
docker build --no-cache -t space-engine .
```

### 2. Run container

```bash
docker run --rm -it --name space-engine space-engine
```

Expected output:

```text
CASCADE (Project BONK) SYSTEM ONLINE
Boost version: 1_74
```

## Local Build (Ubuntu 22.04)

Docker is the recommended path.  
If you want to build locally on Linux, install equivalent dependencies and layouts used by the Dockerfile.

### 1. Install toolchain and base libs

```bash
sudo apt-get update
sudo apt-get install -y build-essential cmake git curl wget libboost-all-dev libsimdjson-dev
```

### 2. Install Julia 1.10.0

```bash
wget https://julialang-s3.julialang.org/bin/linux/x64/1.10/julia-1.10.0-linux-x86_64.tar.gz
tar -xzf julia-1.10.0-linux-x86_64.tar.gz
sudo mv julia-1.10.0 /opt/julia-1.10.0
sudo ln -sf /opt/julia-1.10.0/bin/julia /usr/local/bin/julia
rm julia-1.10.0-linux-x86_64.tar.gz
```

### 4. Clone jluna

```bash
git clone https://github.com/Clemapfel/jluna.git /opt/jluna
```

### 5. Configure and build project

From repo root:

```bash
rm -rf build
ln -sfn /opt/jluna/.src .src
cmake -S . -B build -DJLUNA_DIR=/opt/jluna
cmake --build build -j"$(nproc)"
```

### 6. Run executable

```bash
./build/ProjectBONK
```

## Notes

- `main.cpp` calls `jluna::initialize()` at startup.
- `.dockerignore` excludes host build artifacts to prevent CMake cache conflicts in container builds.
- If you see CMake cache path mismatch errors, remove the local `build/` directory and rebuild.
