#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
IMAGE_NAME="monero-android-x86_64"
ARCH="x86_64"
ABI="x86_64"

echo "======================================================"
echo "Building Monero Android - ${ARCH} (${ABI})"
echo "======================================================"
echo ""

# Create output directory
mkdir -p "${SCRIPT_DIR}/output"

# Build Docker image
echo "🔨 Building Docker image: ${IMAGE_NAME}"
docker build -t "${IMAGE_NAME}" -f androidx64.Dockerfile .

if [ $? -ne 0 ]; then
    echo "❌ Docker build failed for ${ARCH}"
    exit 1
fi

# Extract artifacts using the built-in extraction script
echo ""
echo "📦 Extracting artifacts..."
docker run --rm \
    -v "${SCRIPT_DIR}/output:/host-output" \
    "${IMAGE_NAME}" \
    /extract-libs.sh /host-output

if [ $? -ne 0 ]; then
    echo "❌ Artifact extraction failed for ${ARCH}"
    exit 1
fi

echo ""
echo "======================================================"
echo "Build Complete"
echo "======================================================"
echo ""
echo "Output: ${SCRIPT_DIR}/output/${ABI}/"
echo ""

if [ -d "${SCRIPT_DIR}/output/${ABI}" ]; then
    ls -lh "${SCRIPT_DIR}/output/${ABI}/"
    echo ""
    echo "To use in Android project, copy to:"
    echo "  app/src/main/jniLibs/${ABI}/libmonerujo.so"
fi

echo ""
echo "🎉 ${ARCH} build completed successfully!"
