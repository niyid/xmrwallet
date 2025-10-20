#!/bin/bash
# Fix syntax errors in all Dockerfiles

set -e

echo "🔧 Fixing Dockerfile syntax errors..."

# Fix android32.Dockerfile
if [ -f "android32.Dockerfile" ]; then
    echo "Fixing android32.Dockerfile..."
    
    # Remove duplicate zmq.h line
    sed -i '/ls -la ${PREFIX}\/libzmq\/arm\/include\/zmq.h.*zmq.h not found.*;$/d' android32.Dockerfile
    
    # Add missing backslash after zmq.h check line
    sed -i 's/\(ls -la ${PREFIX}\/libzmq\/arm\/include\/zmq.h || echo "\[WARN\] zmq.h not found!";\)$/\1 \\/g' android32.Dockerfile
    
    echo "✓ android32.Dockerfile fixed"
fi

# Fix android64.Dockerfile
if [ -f "android64.Dockerfile" ]; then
    echo "Fixing android64.Dockerfile..."
    
    # Add missing backslash after zmq.h check
    sed -i 's/\(ls -la ${PREFIX}\/libzmq\/arm64\/include\/zmq.h || echo "\[WARN\] zmq.h not found!";\)$/\1 \\/g' android64.Dockerfile
    
    # Add missing backslash after echo line
    sed -i '/echo "============================";$/s/$/    \\/' android64.Dockerfile
    
    # Add missing backslash after FindZeroMQ patch
    sed -i '/echo .message(STATUS "Found ZeroMQ (patched): \/work\/local\/libzmq\/arm64\/lib\/libzmq.a"). >> ${WORK_DIR}\/monero\/cmake\/FindZeroMQ.cmake;$/s/$/    \\/' android64.Dockerfile
    
    # Add missing backslash after translations echo
    sed -i '/echo "Disabling translation header generation for cross-compilation...";$/s/$/    \\/' android64.Dockerfile
    
    # Add missing backslash after ZeroMQ_DIR export
    sed -i '/export ZeroMQ_DIR="${PREFIX}\/libzmq\/arm64";$/s/$/    \\/' android64.Dockerfile
    
    echo "✓ android64.Dockerfile fixed"
fi

# Fix androidx86_64.Dockerfile
if [ -f "androidx86_64.Dockerfile" ] || [ -f "androidx64.Dockerfile" ]; then
    FILENAME=$([ -f "androidx86_64.Dockerfile" ] && echo "androidx86_64.Dockerfile" || echo "androidx64.Dockerfile")
    echo "Fixing $FILENAME..."
    
    # Add missing backslash after zmq.h check (wrong path - should be x86_64)
    sed -i 's/ls -la ${PREFIX}\/libzmq\/arm64\/include\/zmq.h/ls -la ${PREFIX}\/libzmq\/x86_64\/include\/zmq.h/g' "$FILENAME"
    sed -i 's/\(ls -la ${PREFIX}\/libzmq\/x86_64\/include\/zmq.h || echo "\[WARN\] zmq.h not found!";\)$/\1 \\/g' "$FILENAME"
    
    # Fix wrong paths in FindZeroMQ.cmake (arm64 -> x86_64)
    sed -i 's/\/work\/local\/libzmq\/arm64\//\/work\/local\/libzmq\/x86_64\//g' "$FILENAME"
    
    # Add missing backslashes
    sed -i '/echo "============================";$/s/$/    \\/' "$FILENAME"
    sed -i '/echo .message(STATUS "Found ZeroMQ (patched): \/work\/local\/libzmq\/x86_64\/lib\/libzmq.a"). >> ${WORK_DIR}\/monero\/cmake\/FindZeroMQ.cmake;$/s/$/    \\/' "$FILENAME"
    sed -i '/echo "Disabling translation header generation for cross-compilation...";$/s/$/    \\/' "$FILENAME"
    sed -i '/export ZeroMQ_DIR="${PREFIX}\/libzmq\/x86_64";$/s/$/    \\/' "$FILENAME"
    
    # Fix CMAKE_C_FLAGS path
    sed -i 's/-DCMAKE_C_FLAGS="-I${PREFIX}\/libzmq\/arm64\/include"/-DCMAKE_C_FLAGS="-I${PREFIX}\/libzmq\/x86_64\/include"/g' "$FILENAME"
    
    # Fix ZeroMQ CMake variables
    sed -i 's/-DZeroMQ_INCLUDE_DIR=${PREFIX}\/libzmq\/arm64\/include/-DZeroMQ_INCLUDE_DIR=${PREFIX}\/libzmq\/x86_64\/include/g' "$FILENAME"
    sed -i 's/-DZeroMQ_LIBRARY=${PREFIX}\/libzmq\/arm64\/lib\/libzmq.a/-DZeroMQ_LIBRARY=${PREFIX}\/libzmq\/x86_64\/lib\/libzmq.a/g' "$FILENAME"
    
    echo "✓ $FILENAME fixed"
fi

# Fix androidx86.Dockerfile
if [ -f "androidx86.Dockerfile" ]; then
    echo "Fixing androidx86.Dockerfile..."
    
    # Add missing backslash after zmq.h check
    sed -i 's/\(ls -la ${PREFIX}\/libzmq\/x86\/include\/zmq.h || echo "\[WARN\] zmq.h not found!";\)$/\1 \\/g' androidx86.Dockerfile
    
    # Add missing backslashes
    sed -i '/echo "============================";$/s/$/    \\/' androidx86.Dockerfile
    sed -i '/echo .message(STATUS "Found ZeroMQ (patched): \/work\/local\/libzmq\/x86\/lib\/libzmq.a"). >> ${WORK_DIR}\/monero\/cmake\/FindZeroMQ.cmake;$/s/$/    \\/' androidx86.Dockerfile
    sed -i '/echo "Disabling translation header generation for cross-compilation...";$/s/$/    \\/' androidx86.Dockerfile
    sed -i '/export ZeroMQ_DIR="${PREFIX}\/libzmq\/x86";$/s/$/    \\/' androidx86.Dockerfile
    
    echo "✓ androidx86.Dockerfile fixed"
fi

echo ""
echo "✅ All Dockerfile syntax errors fixed!"
echo ""
echo "Summary of fixes:"
echo "  - Removed duplicate lines"
echo "  - Added missing backslashes for line continuation"
echo "  - Fixed incorrect architecture paths (arm64 -> x86_64 where needed)"
echo ""
echo "You can now rebuild your Docker images."
