# Dockerfile for manual build from debian base (V16 logic)

# --- Stage 1: The Builder ---
FROM debian:bookworm-slim AS builder

# 1. Install dependencies
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential ccache curl git file gawk python3-dev rsync unzip zstd wget \
    vim tree ca-certificates libncurses-dev \
    && rm -rf /var/lib/apt/lists/*

# 2. Create user and copy SDK
RUN useradd -ms /bin/bash buildbot
ARG SDK_FILE
ARG SDK_DIR
COPY dl/${SDK_FILE} /tmp/
RUN tar --zstd -xf /tmp/${SDK_FILE} -C / && \
    rm /tmp/${SDK_FILE} && \
    mv /${SDK_DIR} /builder && \
    chown -R buildbot:buildbot /builder

# 3. Switch to build user and build dependencies
USER buildbot
WORKDIR /builder
COPY --chown=buildbot:buildbot patch_libgpiod_makefile.sh .
RUN chmod +x patch_libgpiod_makefile.sh
RUN ./scripts/feeds update base packages
RUN ./scripts/feeds install ubus libgpiod
RUN ./patch_libgpiod_makefile.sh
RUN make defconfig
RUN make package/feeds/base/ubus/compile V=s -j1 && \
    make package/feeds/packages/libgpiod/compile V=s -j$(nproc)

# --- Stage 2: The Final Image ---
FROM debian:bookworm-slim

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential ccache curl gawk python3-dev unzip wget \
    ca-certificates vim tree wget curl git vim tree file zstd \
    rsync cmake libncurses-dev \
    && rm -rf /var/lib/apt/lists/*

RUN useradd -ms /bin/bash buildbot
WORKDIR /builder

COPY --from=builder --chown=buildbot:buildbot /builder /builder

USER buildbot
ENV STAGING_DIR="/builder/staging_dir"
CMD ["/bin/bash"]
