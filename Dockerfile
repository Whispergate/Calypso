FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y \
    build-essential \
    g++-14 \
    gcc-14 \
    mingw-w64 \
    make \
    && update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-14 100 \
    && update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-14 100 \
    && apt-get clean && rm -rf /var/lib/apt/lists/*

WORKDIR /opt/calypso
COPY . .

# Build donut_ollvm (PE-to-shellcode converter)
RUN if [ -d tools/donut_ollvm/donut_ollvm ]; then \
        cd tools/donut_ollvm/donut_ollvm && make && \
        cp donut /opt/calypso/tools/donut; \
    fi

# Build Calypso packer
RUN make clean && make

ENTRYPOINT ["/opt/calypso/build/calypso"]
