FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

# ── Base toolchain ──────────────────────────────────────────────────
RUN apt-get update && apt-get install -y \
    build-essential \
    g++-14 \
    gcc-14 \
    mingw-w64 \
    make \
    cmake \
    ninja-build \
    git \
    lsb-release \
    wget \
    software-properties-common \
    gnupg \
    && update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-14 100 \
    && update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-14 100 \
    && apt-get clean && rm -rf /var/lib/apt/lists/*

# ── LLVM 21 + Obfuscator-LLVM plugin ───────────────────────────────
RUN wget -qO- https://apt.llvm.org/llvm-snapshot.gpg.key | gpg --dearmor -o /usr/share/keyrings/llvm.gpg \
    && echo "deb [signed-by=/usr/share/keyrings/llvm.gpg] https://apt.llvm.org/noble/ llvm-toolchain-noble-21 main" \
       > /etc/apt/sources.list.d/llvm-21.list \
    && apt-get update && apt-get install -y llvm-21-dev clang-21 \
    && apt-get clean && rm -rf /var/lib/apt/lists/*

RUN git clone --depth 1 https://github.com/eshard/obfuscator-llvm /opt/obfuscator-llvm \
    && cd /opt/obfuscator-llvm \
    && mkdir build && cd build \
    && cmake -G "Ninja" -DLLVM_DIR=/usr/lib/llvm-21/lib/cmake/llvm .. \
    && ninja -j$(nproc) \
    && mkdir -p /opt/llvm \
    && cp libLLVMObfuscator.so /opt/llvm/libLLVMObfuscator.so \
    && cd / && rm -rf /opt/obfuscator-llvm

# ── Build Calypso ──────────────────────────────────────────────────
WORKDIR /opt/calypso
COPY . .

RUN if [ -d tools/donut_ollvm ]; then \
        cd tools/donut_ollvm && make donut && \
        cp donut /opt/calypso/tools/donut; \
    fi

RUN make clean && make

# ── Runtime setup ──────────────────────────────────────────────────
# Output directory for packed binaries (mount from host)
VOLUME ["/payloads", "/output"]
WORKDIR /output

ENTRYPOINT ["/opt/calypso/build/calypso"]
