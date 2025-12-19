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
        
        # For ARM64, use 16KB page size alignment for Google Play compliance
        if [ "$abi" = "arm64-v8a" ]; then
            echo "Applying ARM64-specific build configuration with 16KB page alignment..."
            
            # Add 16KB page size flags for ARM64
            ARM64_PAGE_FLAGS="-Wl,-z,max-page-size=16384"
            
            cmake -B "$BUILD_DIR/build_$abi" \
                -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN_FILE" \
                -DANDROID_ABI=$abi \
                -DANDROID_PLATFORM=android-33 \
                -DEXTERNAL_LIBS_DIR=../external-libs/output \
                -DCMAKE_BUILD_TYPE=MinSizeRel \
                -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
                -DCMAKE_CXX_FLAGS="$SIZE_OPT_FLAGS -Wl,-Bsymbolic -fPIC" \
                -DCMAKE_SHARED_LINKER_FLAGS="$SIZE_LINK_FLAGS $ARM64_PAGE_FLAGS -Wl,--gc-sections -Wl,--icf=all"
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
            
            # Verify 16KB page alignment for ARM64
            if [ "$abi" = "arm64-v8a" ]; then
                echo "Verifying 16KB page alignment for ARM64..."
                readelf -l "$JNI_LIBS_DIR/$abi/libmonerujo.so" | grep "LOAD" | head -1
            fi
            
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

echo ""
echo "=== Google Play 16KB Compliance Check ==="
if [ -f "$JNI_LIBS_DIR/arm64-v8a/libmonerujo.so" ]; then
    echo "Checking ARM64 library page alignment..."
    echo "Full readelf output:"
    readelf -l "$JNI_LIBS_DIR/arm64-v8a/libmonerujo.so" | grep -A 2 "LOAD"
    
    # Extract alignment values (last column of lines after LOAD)
    ALIGNMENTS=$(readelf -l "$JNI_LIBS_DIR/arm64-v8a/libmonerujo.so" | grep -A 1 "LOAD" | grep "0x" | grep -v "LOAD" | awk '{print $NF}')
    
    echo ""
    echo "Detected alignments:"
    echo "$ALIGNMENTS"
    
    # Check if all alignments are 16KB (0x4000)
    HAS_16KB=$(echo "$ALIGNMENTS" | grep -c "0x4000")
    TOTAL_LOADS=$(echo "$ALIGNMENTS" | wc -l)
    
    if [ "$HAS_16KB" -eq "$TOTAL_LOADS" ] && [ "$HAS_16KB" -gt 0 ]; then
        echo "✅ ARM64 library IS COMPLIANT with 16KB page size requirement"
        echo "   All $TOTAL_LOADS LOAD segments have 0x4000 (16384 bytes) alignment"
    elif [ "$HAS_16KB" -gt 0 ]; then
        echo "⚠️  PARTIAL: $HAS_16KB of $TOTAL_LOADS segments have 16KB alignment"
    else
        echo "❌ WARNING: ARM64 library does NOT meet 16KB page size requirement"
        echo "   Expected: 0x4000 (16384 bytes)"
    fi
else
    echo "No ARM64 library found to verify"
fi
