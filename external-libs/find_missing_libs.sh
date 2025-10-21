#!/bin/bash
# find_missing_libs_complete.sh

set -euo pipefail

EXTERNAL_LIBS_DIR="/home/niyid/git/xmrwallet/external-libs"
cd "$EXTERNAL_LIBS_DIR"

echo "=== Comprehensive Library Analysis ==="
echo "External libs directory: $(pwd)"
echo ""

# Write output to both terminal and file
exec > >(tee -a library_analysis.log) 2>&1

# Define all expected libraries
EXPECTED_LIBS=(
    "libblockchain_db.a"
    "libcheckpoints.a" 
    "libcncrypto.a"
    "libcommon.a"
    "libcrypto.a"
    "libcryptonote_basic.a"
    "libcryptonote_core.a"
    "libdevice.a"
    "libeasylogging.a"
    "libepee.a"
    "libmnemonics.a"
    "libringct.a"
    "libringct_basic.a"
    "libunbound.a"
    "libwallet.a"
    "libwallet_api.a"
    "libsodium.a"
    "libssl.a"
    "librandomx.a"
    "libboost_chrono.a"
    "libboost_date_time.a"
    "libboost_filesystem.a"
    "libboost_program_options.a"
    "libboost_regex.a"
    "libboost_serialization.a"
    "libboost_system.a"
    "libboost_thread.a"
    "libexpat.a"
    "libzmq.a"
    "libwallet-crypto.a"
    "libcharset.a"
)

# Function to analyze an architecture with better error handling
analyze_architecture() {
    local arch=$1
    local arch_dir="$EXTERNAL_LIBS_DIR/$arch"
    
    echo "================================================================="
    echo "ARCHITECTURE: $arch"
    echo "================================================================="
    
    if [ ! -d "$arch_dir" ]; then
        echo "❌ Directory does not exist: $arch_dir"
        echo ""
        return 1
    fi
    
    # Count total libraries safely
    local total_libs=0
    if command -v find >/dev/null 2>&1; then
        total_libs=$(find "$arch_dir" -name "*.a" 2>/dev/null | wc -l || echo "0")
    else
        total_libs=$(ls "$arch_dir"/*.a 2>/dev/null | wc -l || echo "0")
    fi
    
    echo "Total .a files found: $total_libs"
    echo ""
    
    if [ "$total_libs" -eq 0 ]; then
        echo "⚠️  No libraries found in $arch_dir"
        echo ""
        return 1
    fi
    
    # Check expected libraries
    echo "--- REQUIRED LIBRARIES ---"
    local missing_count=0
    local found_count=0
    
    for lib in "${EXPECTED_LIBS[@]}"; do
        if [[ -f "$arch_dir/$lib" || -L "$arch_dir/$lib" ]]; then
            echo "✅ $lib"
            ((found_count++)) || true
        else
            echo "❌ $lib - MISSING"
            ((missing_count++)) || true
        fi
    done
    
    echo ""
    echo "Required libraries: $found_count found, $missing_count missing"
    echo ""
    
    # Show actual libraries present
    echo "--- ALL LIBRARIES PRESENT (first 30) ---"
    if command -v find >/dev/null 2>&1; then
        find "$arch_dir" -name "*.a" -printf "  %f\n" 2>/dev/null | sort | head -30
    else
        ls -1 "$arch_dir"/*.a 2>/dev/null | xargs -n1 basename | sort | head -30
    fi
    echo ""
    
    return 0
}

# Main execution with better error handling
echo "Scanning architectures: arm64-v8a, armeabi-v7a, x86_64"
echo ""

for arch in arm64-v8a armeabi-v7a x86_64; do
    if analyze_architecture "$arch"; then
        echo "✓ Completed analysis for $arch"
    else
        echo "✗ Failed to analyze $arch"
    fi
    echo ""
done

echo "=== Generating Summary ==="
echo ""

# Create a summary table
echo "LIBRARY SUMMARY:"
echo "Architecture    Total Libs   Status"
echo "-------------   ----------   ------"

for arch in arm64-v8a armeabi-v7a x86_64; do
    local total_libs=0
    local status=""
    
    if [ -d "$arch" ]; then
        if command -v find >/dev/null 2>&1; then
            total_libs=$(find "$arch" -name "*.a" 2>/dev/null | wc -l || echo "0")
        else
            total_libs=$(ls "$arch"/*.a 2>/dev/null | wc -l || echo "0")
        fi
        
        if [ "$total_libs" -eq 0 ]; then
            status="❌ EMPTY"
        elif [ "$total_libs" -lt 40 ]; then
            status="⚠️  INCOMPLETE"
        else
            status="✅ COMPLETE"
        fi
    else
        total_libs="0"
        status="❌ MISSING"
    fi
    
    printf "%-15s %-12s %s\n" "$arch" "$total_libs" "$status"
done

echo ""
echo "=== Checking Critical Dependencies ==="
echo ""

# Check for critical libraries
CRITICAL_LIBS=("libwallet.a" "libwallet_api.a" "librandomx.a" "libboost_filesystem.a")

for arch in arm64-v8a armeabi-v7a x86_64; do
    echo "--- $arch Critical Libraries ---"
    for lib in "${CRITICAL_LIBS[@]}"; do
        if [ -f "$arch/$lib" ]; then
            size=$(du -h "$arch/$lib" 2>/dev/null | cut -f1 || echo "unknown")
            echo "✅ $lib ($size)"
        elif [ -L "$arch/$lib" ]; then
            echo "🔗 $lib (symlink)"
        else
            echo "❌ $lib"
        fi
    done
    echo ""
done

echo "=== Analysis Complete ==="
echo "Results saved to: $(pwd)/library_analysis.log"
echo ""
echo "RECOMMENDATIONS:"
echo "1. Check architectures with ❌ EMPTY or ⚠️ INCOMPLETE status"
echo "2. Look for missing critical libraries (marked with ❌)"
echo "3. Rebuild incomplete architectures using build scripts"
