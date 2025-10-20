#!/bin/bash
set -e

# ============================================================
# Build Monero for all Android architectures
# ============================================================

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Architecture configurations
declare -A CONFIGS=(
    ["arm32"]="android32.Dockerfile:armeabi-v7a"
    ["arm64"]="android64.Dockerfile:arm64-v8a"
    ["x86_64"]="androidx64.Dockerfile:x86_64"
)

echo "======================================================"
echo "Monero Android Multi-Architecture Build"
echo "======================================================"
echo ""

# Parse command line arguments
ARCH_TO_BUILD="${1:-all}"

if [ "$ARCH_TO_BUILD" != "all" ] && [ -z "${CONFIGS[$ARCH_TO_BUILD]}" ]; then
    echo "❌ Invalid architecture: $ARCH_TO_BUILD"
    echo ""
    echo "Usage: $0 [architecture]"
    echo ""
    echo "Available architectures:"
    for arch in "${!CONFIGS[@]}"; do
        echo "  - $arch"
    done
    echo "  - all (default)"
    exit 1
fi

# Function to build a specific architecture
build_arch() {
    local arch=$1
    local config=${CONFIGS[$arch]}
    local dockerfile=$(echo $config | cut -d: -f1)
    local abi=$(echo $config | cut -d: -f2)
    local image_name="monero-android-${arch}"
    
    echo ""
    echo "======================================================"
    echo "Building $arch ($abi)"
    echo "======================================================"
    
    # Create output directory
    mkdir -p "${SCRIPT_DIR}/output"
    
    # Build Docker image
    echo "🔨 Building Docker image: ${image_name}"
    docker build -t "${image_name}" -f "${dockerfile}" .
    
    if [ $? -ne 0 ]; then
        echo "❌ Docker build failed for $arch"
        return 1
    fi
    
    # Extract artifacts using the built-in extraction script
    echo "📦 Extracting artifacts..."
    docker run --rm \
        -v "${SCRIPT_DIR}/output:/host-output" \
        "${image_name}" \
        /extract-libs.sh /host-output
    
    if [ $? -ne 0 ]; then
        echo "❌ Artifact extraction failed for $arch"
        return 1
    fi
}

# Build requested architecture(s)
BUILD_FAILED=0

if [ "$ARCH_TO_BUILD" = "all" ]; then
    echo "Building all architectures: arm32, arm64, x86_64"
    echo ""
    
    for arch in arm32 arm64 x86_64; do
        if ! build_arch "$arch"; then
            echo "⚠️  $arch build failed, continuing..."
            BUILD_FAILED=1
        fi
    done
else
    if ! build_arch "$ARCH_TO_BUILD"; then
        BUILD_FAILED=1
    fi
fi

echo ""
echo "======================================================"
echo "Build Summary"
echo "======================================================"
echo ""

# Check all ABIs and display summary
SUCCESSFUL_BUILDS=0
TOTAL_BUILDS=0

echo ""
echo "Success rate: $SUCCESSFUL_BUILDS/$TOTAL_BUILDS architectures built"
echo ""

# Display output structure
if [ $SUCCESSFUL_BUILDS -gt 0 ]; then
    echo "Output structure:"
    if command -v tree &> /dev/null; then
        tree -L 2 "${SCRIPT_DIR}/output/" 2>/dev/null || true
    else
        echo ""
        find "${SCRIPT_DIR}/output/" -type f -exec ls -lh {} \;
    fi
    echo ""
    echo "All artifacts in: ${SCRIPT_DIR}/output/"
    echo ""
    echo "To use in Android project, copy the .so files to:"
    echo "  app/src/main/jniLibs/<abi>/libmonerujo.so"
fi

# Exit with error if any build failed
if [ $BUILD_FAILED -ne 0 ]; then
    echo "⚠️  Some builds failed. Check logs above for details."
    exit 1
fi

echo "🎉 All builds completed successfully!"
exit 0
