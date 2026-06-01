# TODO(S3.1): ARM-Linux cross-compilation toolchain
#
# This file will be populated in Slice 3.1 with:
#   - CMAKE_SYSTEM_NAME / CMAKE_SYSTEM_PROCESSOR
#   - Cross-compiler paths (aarch64-linux-gnu-gcc etc.)
#   - Qt6 sysroot / cross-compiled Qt paths
#
# Usage (future):
#   cmake -B build-arm -G Ninja \
#     -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/aarch64-linux.cmake
