#!/bin/bash
docker run --rm --user root \
    -v /Users/libo/Berry/NanoHatOLED/src:/work \
    -w /work \
    openwrt-sdk-sunxi \
    sh compile.sh