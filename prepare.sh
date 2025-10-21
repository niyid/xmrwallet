cd ~/git/xmrwallet/external-libs

# Create the exact structure that CMakeLists.txt expects
for abi in arm64-v8a armeabi-v7a x86_64 x86; do
    echo "Setting up $abi..."
    
    # Create the ABI directory
    mkdir -p $abi
    
    # Copy/link libraries from output directory to the expected location
    if [ -d "output/$abi/lib" ]; then
        # Copy all .a files to the root ABI directory (where CMake expects them)
        cp output/$abi/lib/*.a $abi/ 2>/dev/null || true
        
        # Also create the output subdirectory that CMake expects for some libraries
        mkdir -p $abi/output
        cp output/$abi/lib/*.a $abi/output/ 2>/dev/null || true
    fi
    
    # Also check build directories for additional libraries
    if [ -d "build-$abi/lib" ]; then
        cp build-$abi/lib/*.a $abi/ 2>/dev/null || true
        cp build-$abi/lib/*.a $abi/output/ 2>/dev/null || true
    fi
done

# Verify the structure
echo "=== Created structure ==="
ls -la arm64-v8a/ | head -10
ls -la arm64-v8a/output/ | head -10
