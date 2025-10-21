#!/bin/bash

cd ~/git/xmrwallet/app

# Use the correct NDK path from Android SDK
export ANDROID_NDK_HOME=/mnt/android-sdk/ndk/29.0.13113456
export PATH=$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/linux-x86_64/bin:$PATH

TOOLCHAIN_FILE="$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake"

echo "Using NDK: $ANDROID_NDK_HOME"
echo "Using toolchain: $TOOLCHAIN_FILE"

# First, let's check what's actually in external-libs
echo "Checking external-libs directory..."
ls -la ../external-libs/

# Common ABI directories to check
POSSIBLE_ABI_DIRS=("arm64-v8a" "armeabi-v7a" "x86_64" "x86" "arm64" "aarch64" "armv7")

for abi in "${POSSIBLE_ABI_DIRS[@]}"; do
    if [ -d "../external-libs/$abi" ]; then
        echo "Found ABI directory: $abi"
        echo "Building for $abi..."
        
        cmake -B build_$abi \
            -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN_FILE" \
            -DANDROID_ABI=$abi \
            -DANDROID_PLATFORM=android-33 \
            -DEXTERNAL_LIBS_DIR=../external-libs
        
        cmake --build build_$abi --parallel $(nproc)
        echo "Completed $abi"
        echo "---"
    else
        echo "No directory found for ABI: $abi"
    fi
done
