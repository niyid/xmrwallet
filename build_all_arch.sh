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

# Check if headers exist in output directory
echo "Checking for headers in output directory..."
find ../external-libs/output -name "*.h" | head -10

# Common ABI directories to check
POSSIBLE_ABI_DIRS=("arm64-v8a" "armeabi-v7a" "x86_64" "x86" "arm64" "aarch64" "armv7")

for abi in "${POSSIBLE_ABI_DIRS[@]}"; do
    if [ -d "../external-libs/$abi" ]; then
        echo "Found ABI directory: $abi"
        echo "Contents of ../external-libs/$abi:"
        ls -la ../external-libs/$abi/
        
        # Check if the library exists
        if [ ! -f "../external-libs/$abi/lib/libwallet_api.a" ] && [ ! -f "../external-libs/$abi/libwallet_api.a" ]; then
            echo "WARNING: libwallet_api.a not found in ../external-libs/$abi"
            echo "Skipping $abi..."
            continue
        fi
        
        echo "Building for $abi..."
        
        # Clean previous build
        rm -rf build_$abi
        
        # Set ABI variable for conditional flag handling
        ABI="$abi"
        
        # For ARM64 only, add these linker flags
        if [ "$ABI" = "arm64-v8a" ]; then
            EXTRA_CMAKE_FLAGS="-DCMAKE_CXX_FLAGS=\"-Wl,-z,notext\""
        else
            EXTRA_CMAKE_FLAGS=""
        fi
        
        # Point to the output directory structure with proper include paths
        cmake -B build_$abi \
            -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN_FILE" \
            -DANDROID_ABI=$abi \
            -DANDROID_PLATFORM=android-33 \
            -DEXTERNAL_LIBS_DIR=../external-libs/output \
            -DEXTERNAL_INCLUDE_DIR=../external-libs/output/include \
            $EXTRA_CMAKE_FLAGS
        
        cmake --build build_$abi --parallel $(nproc)
        echo "Completed $abi"
        echo "---"
    else
        echo "No directory found for ABI: $abi"
    fi
done
