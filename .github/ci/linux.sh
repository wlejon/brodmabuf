#!/usr/bin/env bash
# The Linux build and tests inside debian:trixie (Mesa 25: GBM's kms_swrast
# for vkms; lavapipe, which offers DMA-BUF only when udmabuf exists). Called by CI
# as the container's command with the workspace mounted at /w.
#
# Environment: CC / CXX (gcc|clang), CONFIG (Release|Debug), COVERAGE (ON|OFF),
# BRODMABUF_DRM_DEVICE (the vkms card the host loaded, or empty).
#
# Builds as root, then runs ctest as an unprivileged user in the video group,
# as a desktop process would. Nothing else holds the vkms card, so that user's
# first open of it is DRM master and test_kms modesets on it.
set -euo pipefail

: "${CC:=gcc}" "${CXX:=g++}" "${CONFIG:=Release}" "${COVERAGE:=OFF}"
export LANG=C.UTF-8
cd /w

export DEBIAN_FRONTEND=noninteractive
apt-get update -qq
packages=(
    ca-certificates cmake ninja-build pkg-config g++ clang
    libdrm-dev libgbm-dev libvulkan-dev
    libgbm1 libgl1-mesa-dri mesa-vulkan-drivers vulkan-tools
)
[ "$COVERAGE" = "ON" ] && packages+=(gcovr)
apt-get install -y --no-install-recommends "${packages[@]}"

configure_args=(-S . -B build -G Ninja -DCMAKE_BUILD_TYPE="$CONFIG")
[ "$COVERAGE" = "ON" ] && configure_args+=(-DBRODMABUF_COVERAGE=ON)
cmake "${configure_args[@]}"
cmake --build build --parallel "$(nproc)"

id ci >/dev/null 2>&1 || useradd -m -G video ci
chown -R ci /w

# What the tests have to work with.
ls -l /dev/dri /dev/udmabuf 2>&1 || true
runuser -u ci -- vulkaninfo --summary 2>&1 | grep -E 'deviceName|driverName|apiVersion' || true

set +e
runuser -u ci -- env BRODMABUF_DRM_DEVICE="${BRODMABUF_DRM_DEVICE:-}" \
    /w/.github/ci/ctest.sh --test-dir build
rc=$?
set -e

# Scoped to the library's own src/ and include/; the tests are not the subject.
if [ "$COVERAGE" = "ON" ]; then
    mkdir -p coverage-html
    gcovr --root . \
        --filter 'src/' --filter 'include/brodmabuf/' \
        --exclude-unreachable-branches \
        --print-summary \
        --html-details coverage-html/index.html \
        | tee coverage-summary.txt
    test -s coverage-html/index.html
fi

chmod -R a+rwX /w
exit "$rc"
