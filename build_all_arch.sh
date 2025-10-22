#!/bin/bash

cd ~/git/xmrwallet/app

# Use the correct NDK path from Android SDK
export ANDROID_NDK_HOME=/mnt/android-sdk/ndk/29.0.13113456
export PATH=$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/linux-x86_64/bin:$PATH

TOOLCHAIN_FILE="$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake"

echo "Using NDK: $ANDROID_NDK_HOME"
echo "Using toolchain: $TOOLCHAIN_FILE"

# Create build directory at project root and jniLibs in app
BUILD_DIR="../build"
JNI_LIBS_DIR="src/main/jniLibs"
mkdir -p $BUILD_DIR
mkdir -p $JNI_LIBS_DIR

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
        
        # Clean previous build (now in project root build directory)
        rm -rf "$BUILD_DIR/build_$abi"
        
        # Create ABI-specific jniLibs directory
        mkdir -p "$JNI_LIBS_DIR/$abi"
        
        # Common size optimization flags for all architectures
        SIZE_OPT_FLAGS="-Os -flto -fvisibility=hidden -ffunction-sections -fdata-sections"
        SIZE_LINK_FLAGS="-Wl,--gc-sections -Wl,--icf=all -Wl,--exclude-libs,ALL"
        
        # For ARM64, use specific linker flags to handle large code sections
        if [ "$abi" = "arm64-v8a" ]; then
            echo "Applying ARM64-specific build configuration..."
            
            cmake -B "$BUILD_DIR/build_$abi" \
                -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN_FILE" \
                -DANDROID_ABI=$abi \
                -DANDROID_PLATFORM=android-33 \
                -DEXTERNAL_LIBS_DIR=../external-libs/output \
                -DCMAKE_BUILD_TYPE=MinSizeRel \
                -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
                -DCMAKE_CXX_FLAGS="$SIZE_OPT_FLAGS -Wl,-Bsymbolic -fPIC" \
                -DCMAKE_SHARED_LINKER_FLAGS="$SIZE_LINK_FLAGS -Wl,--gc-sections -Wl,--icf=all"
        else
            cmake -B "$BUILD_DIR/build_$abi" \
                -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN_FILE" \
                -DANDROID_ABI=$abi \
                -DANDROID_PLATFORM=android-33 \
                -DEXTERNAL_LIBS_DIR=../external-libs/output \
                -DCMAKE_BUILD_TYPE=MinSizeRel \
                -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
                -DCMAKE_CXX_FLAGS="$SIZE_OPT_FLAGS" \
                -DCMAKE_SHARED_LINKER_FLAGS="$SIZE_LINK_FLAGS"
        fi
        
        cmake --build "$BUILD_DIR/build_$abi" --parallel $(nproc)
        
        # Check if the build was successful and copy to jniLibs
        if [ -f "$BUILD_DIR/build_$abi/libmonerujo.so" ]; then
            echo "✅ SUCCESS: Built $BUILD_DIR/build_$abi/libmonerujo.so"
            ls -lh "$BUILD_DIR/build_$abi/libmonerujo.so"
            
            # Strip debug symbols to reduce size
            echo "Stripping debug symbols..."
            $ANDROID_NDK_HOME/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-strip --strip-all "$BUILD_DIR/build_$abi/libmonerujo.so"
            
            echo "Size after stripping:"
            ls -lh "$BUILD_DIR/build_$abi/libmonerujo.so"
            
            # Copy to jniLibs directory
            cp "$BUILD_DIR/build_$abi/libmonerujo.so" "$JNI_LIBS_DIR/$abi/"
            echo "✅ Copied stripped libmonerujo.so to $JNI_LIBS_DIR/$abi/"
            
            # Analyze what's taking space
            echo "Library size analysis:"
            $ANDROID_NDK_HOME/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-size "$JNI_LIBS_DIR/$abi/libmonerujo.so"
        else
            echo "❌ FAILED: $BUILD_DIR/build_$abi/libmonerujo.so not found"
        fi
        
        echo "Completed $abi"
        echo "---"
    else
        echo "No directory found for ABI: $abi in output directory"
    fi
done

# Final check of all built libraries in jniLibs
echo "=== Final build results in jniLibs ==="
find "$JNI_LIBS_DIR" -name "libmonerujo.so" -type f -exec ls -lh {} \;

# Show total size
echo "=== Total sizes ==="
for abi in "${POSSIBLE_ABI_DIRS[@]}"; do
    if [ -f "$JNI_LIBS_DIR/$abi/libmonerujo.so" ]; then
        echo -n "$abi: "
        ls -lh "$JNI_LIBS_DIR/$abi/libmonerujo.so" | awk '{print $5}'
    fi
done
