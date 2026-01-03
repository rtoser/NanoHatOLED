# OpenWrt SDK for NanoPi NEO2 Plus (sunxi/cortexa53)
# Usage:
#   docker build -t openwrt-sdk-sunxi .
#   docker run -it --rm -v $(pwd)/src:/src openwrt-sdk-sunxi

FROM debian:bookworm-slim

# Install dependencies
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    ccache \
    curl \
    file \
    gawk \
    gettext \
    git \
    libncurses-dev \
    libssl-dev \
    python3 \
    python3-dev \
    python3-distutils \
    python3-setuptools \
    rsync \
    swig \
    unzip \
    wget \
    zstd \
    ca-certificates \
    # Additional deps for host python3 build
    libffi-dev \
    libexpat1-dev \
    libbz2-dev \
    liblzma-dev \
    libsqlite3-dev \
    libreadline-dev \
    uuid-dev \
    && rm -rf /var/lib/apt/lists/*

# Download and extract OpenWrt SDK
ARG SDK_URL=https://mirrors.aliyun.com/openwrt/releases/24.10.5/targets/sunxi/cortexa53/openwrt-sdk-24.10.5-sunxi-cortexa53_gcc-13.3.0_musl.Linux-x86_64.tar.zst
ARG SDK_DIR=openwrt-sdk-24.10.5-sunxi-cortexa53_gcc-13.3.0_musl.Linux-x86_64

WORKDIR /opt

RUN curl -L ${SDK_URL} -o sdk.tar.zst \
    && tar --zstd -xf sdk.tar.zst \
    && rm sdk.tar.zst \
    && mv ${SDK_DIR} sdk

WORKDIR /opt/sdk

# Use SDK's built-in feeds.conf.default, only enable base+packages feeds
RUN grep -E '^src-git.*(base|packages)' feeds.conf.default > feeds.conf && \
    cat feeds.conf && \
    ./scripts/feeds update -a && \
    ./scripts/feeds install libubox libubus libjson-c

# Configure: minimal package selection
# Disable auto-selection to avoid pulling in unnecessary packages
ENV TERM=xterm
RUN echo "# Disable auto-selection of packages" > .config && \
    echo "# CONFIG_ALL_KMODS is not set" >> .config && \
    echo "# CONFIG_ALL_NONSHARED is not set" >> .config && \
    echo "# CONFIG_DEVEL is not set" >> .config && \
    echo "# Only enable the specific packages we need" >> .config && \
    echo "CONFIG_PACKAGE_libubox=y" >> .config && \
    echo "CONFIG_PACKAGE_libubus=y" >> .config && \
    echo "CONFIG_PACKAGE_libjson-c=y" >> .config && \
    echo "CONFIG_PACKAGE_libblobmsg-json=y" >> .config && \
    make defconfig && \
    grep -E "^CONFIG_PACKAGE_(lib|ubus)" .config && \
    echo "=== Compiling libubox ===" && \
    make package/libubox/compile V=s -j$(nproc) && \
    echo "=== Compiling ubus ===" && \
    make package/ubus/compile V=s -j$(nproc)

# Verify libraries are built
RUN ls -la staging_dir/target-*/usr/lib/libubus* staging_dir/target-*/usr/lib/libjson-c* && \
    ls -la staging_dir/target-*/usr/include/libubus.h

# Set up environment
ENV PATH="/opt/sdk/staging_dir/toolchain-aarch64_cortex-a53_gcc-13.3.0_musl/bin:${PATH}"
ENV STAGING_DIR="/opt/sdk/staging_dir"

# Create symlink for easier access
RUN ln -s /opt/sdk/staging_dir/target-aarch64_cortex-a53_musl /opt/target

WORKDIR /src

# Default command
CMD ["/bin/bash"]
