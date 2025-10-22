#!/bin/bash

cd ~/git/xmrwallet/app

# Use the correct NDK path from Android SDK
export ANDROID_NDK_HOME=/mnt/android-sdk/ndk/29.0.13113456
export PATH=$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/linux-x86_64/bin:$PATH

TOOLCHAIN_FILE="$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake"

echo "Using NDK: $ANDROID_NDK_HOME"
echo "Using toolchain: $TOOLCHAIN_FILE"

# Common ABI directories to check
POSSIBLE_ABI_DIRS=("arm64-v8a" "armeabi-v7a" "x86_64")

for abi in "${POSSIBLE_ABI_DIRS[@]}"; do
    if [ -d "../external-libs/output/$abi" ]; then
        echo "Found ABI directory: $abi"
        
        # Check if the library exists in the correct output directory
        if [ ! -f "../external-libs/output/$abi/lib/libwallet_api.a" ]; then
            echo "WARNING: libwallet_api.a not found in ../external-libs/output/$abi/lib/"
            echo "Skipping $abi..."
            continue
        fi
        
        echo "Building for $abi..."
        
        # Clean previous build
        rm -rf build_$abi
        
        # For ARM64, use specific linker flags to handle large code sections
        if [ "$abi" = "arm64-v8a" ]; then
            echo "Applying ARM64-specific build configuration..."
            
            cmake -B build_$abi \
                -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN_FILE" \
                -DANDROID_ABI=$abi \
                -DANDROID_PLATFORM=android-33 \
                -DEXTERNAL_LIBS_DIR=../external-libs/output \
                -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
                -DCMAKE_CXX_FLAGS="-Wl,-Bsymbolic -fPIC" \
                -DCMAKE_SHARED_LINKER_FLAGS="-Wl,--gc-sections -Wl,--icf=all"
        else
            cmake -B build_$abi \
                -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN_FILE" \
                -DANDROID_ABI=$abi \
                -DANDROID_PLATFORM=android-33 \
                -DEXTERNAL_LIBS_DIR=../external-libs/output
        fi
        
        cmake --build build_$abi --parallel $(nproc)
        
        # Check if the build was successful
        if [ -f "build_$abi/libmonerujo.so" ]; then
            echo "✅ SUCCESS: Built build_$abi/libmonerujo.so"
            ls -lh "build_$abi/libmonerujo.so"
        else
            echo "❌ FAILED: build_$abi/libmonerujo.so not found"
        fi
        
        echo "Completed $abi"
        echo "---"
    else
        echo "No directory found for ABI: $abi in output directory"
    fi
done
