# Quick Start Guide - Monero Android Builds

## 📋 Prerequisites Setup

### Step 1: Clone This Repository

```bash
git clone <your-repo-url>
cd monero-android-build
```

### Step 2: Run Setup Script

We provide a setup script to download and prepare all required dependencies:

```bash
chmod +x setup-dependencies.sh
./setup-dependencies.sh
```

This script will:
- Download Android NDK r29
- Download Boost 1.81.0
- Clone Monero source code
- Verify all files are in place

**OR** manually download the required files (see below).

---

## 📁 Manual Setup (Alternative)

If you prefer to set up manually, your directory should contain:

```
monero-android-build/
├── android32.Dockerfile      ← Provided by repo
├── android64.Dockerfile      ← Provided by repo
├── androidx86.Dockerfile     ← Provided by repo
├── androidx64.Dockerfile     ← Provided by repo
├── build-all-architectures.sh ← Provided by repo
├── build-android-*.sh        ← Provided by repo
├── setup-dependencies.sh     ← Provided by repo
│
├── ndk29/                    ← You must download
├── boost_1_81_0.tar.gz       ← You must download
└── monero/                   ← You must clone
```

### Download Android NDK r29

```bash
# Download NDK
wget https://dl.google.com/android/repository/android-ndk-r29-linux.zip

# Extract
unzip android-ndk-r29-linux.zip
mv android-ndk-r29 ndk29

# Clean up
rm android-ndk-r29-linux.zip
```

### Download Boost 1.81.0

```bash
wget https://boostorg.jfrog.io/artifactory/main/release/1.81.0/source/boost_1_81_0.tar.gz
```

### Clone Monero Source

```bash
git clone --recursive https://github.com/monero-project/monero.git
cd monero
git checkout release-v0.18  # or your desired version
git submodule update --init --force --recursive
cd ..
```

---

## 🚀 Building

### Build All Architectures (Recommended)

```bash
chmod +x build-all-architectures.sh
./build-all-architectures.sh
```

### Build Specific Architecture

```bash
# ARM 32-bit (armeabi-v7a)
./build-android-arm32.sh

# ARM 64-bit (arm64-v8a)
./build-android-arm64.sh

# x86 32-bit
./build-android-x86.sh

# x86 64-bit
./build-android-x86_64.sh
```

Or using the all-in-one script:

```bash
./build-all-architectures.sh arm32    # Build only ARM 32-bit
./build-all-architectures.sh arm64    # Build only ARM 64-bit
./build-all-architectures.sh x86      # Build only x86
./build-all-architectures.sh x86_64   # Build only x86_64
```

---

## 📦 Output Files

After successful build, you'll find **two files** per architecture:

```
output/
├── arm32/
│   ├── libwallet_merged.a    (~154MB) - Static library
│   └── libwallet.so          (~25MB)  - Shared library
├── arm64/
│   ├── libwallet_merged.a    (~154MB)
│   └── libwallet.so          (~27MB)
├── x86/
│   ├── libwallet_merged.a    (~160MB)
│   └── libwallet.so          (~26MB)
└── x86_64/
    ├── libwallet_merged.a    (~162MB)
    └── libwallet.so          (~28MB)
```

**Note**: You typically only need the `.so` (shared library) files for your Android app.

---

## ✅ Verify Your Build

```bash
# Check files were created
ls -lh output/arm64/

# Check file types
file output/arm64/libwallet_merged.a
file output/arm64/libwallet.so

# Expected outputs:
# libwallet_merged.a: current ar archive
# libwallet.so: ELF 64-bit LSB shared object, ARM aarch64
```

---

## 📊 Build Time & Resource Requirements

### System Requirements
- **RAM**: 8GB minimum, 16GB recommended
- **Disk Space**: 25GB free space minimum
- **CPU**: Multi-core processor (4+ cores recommended)

### Expected Build Times
On a modern system (8 cores, 16GB RAM):

| Architecture | First Build | With Cache |
|-------------|-------------|------------|
| ARM 32-bit  | 50-70 min   | 10-15 min  |
| ARM 64-bit  | 50-70 min   | 10-15 min  |
| x86         | 50-70 min   | 10-15 min  |
| x86_64      | 50-70 min   | 10-15 min  |
| **All (parallel)** | **3-4 hours** | **30-45 min** |

---

## 🎯 Next Steps

After building, integrate the libraries into your Android project:

### Copy Libraries to Your Project

```bash
# For .so files (recommended for Android apps)
mkdir -p YourApp/app/src/main/jniLibs/{armeabi-v7a,arm64-v8a,x86,x86_64}

cp output/arm32/libwallet.so YourApp/app/src/main/jniLibs/armeabi-v7a/
cp output/arm64/libwallet.so YourApp/app/src/main/jniLibs/arm64-v8a/
cp output/x86/libwallet.so YourApp/app/src/main/jniLibs/x86/
cp output/x86_64/libwallet.so YourApp/app/src/main/jniLibs/x86_64/
```

### Update Your CMakeLists.txt

```cmake
# Add the Monero wallet library
add_library(monero-wallet SHARED IMPORTED)
set_target_properties(monero-wallet PROPERTIES
    IMPORTED_LOCATION ${CMAKE_SOURCE_DIR}/../jniLibs/${ANDROID_ABI}/libwallet.so
)

# Link in your JNI library
target_link_libraries(your-native-lib
    monero-wallet
    log
    android
)
```

See `BUILD_INSTRUCTIONS.md` for detailed integration guide.

---

## 🛠️ Troubleshooting

### "Command not found" errors

```bash
# Make all scripts executable
chmod +x *.sh
```

### Build fails immediately

```bash
# Verify all dependencies are present
ls -la ndk29/ boost_1_81_0.tar.gz monero/

# Run setup script if anything is missing
./setup-dependencies.sh
```

### Out of disk space

```bash
# Clean Docker cache
docker system prune -a

# Remove old images
docker images | grep monero-android | awk '{print $3}' | xargs docker rmi
```

### Build is very slow

```bash
# Check Docker resource allocation
docker info | grep -i cpu
docker info | grep -i memory

# Increase in Docker Desktop: Settings → Resources
```

---

## 📖 Additional Documentation

- `BUILD_INSTRUCTIONS.md` - Comprehensive build documentation
- `SETUP.md` - Detailed dependency setup guide
- `INTEGRATION.md` - Android project integration guide

---

**Important**: First build will take 50-70 minutes per architecture as it compiles all dependencies. Subsequent builds are much faster thanks to Docker layer caching.

═══════════════════════════════════════════════════════════

# setup-dependencies.sh

#!/bin/bash
set -e

# ============================================================
# Monero Android Build - Dependency Setup Script
# ============================================================
# This script downloads and prepares all required dependencies
# for building Monero libraries for Android
# ============================================================

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NDK_VERSION="r29"
NDK_LINUX_URL="https://dl.google.com/android/repository/android-ndk-${NDK_VERSION}-linux.zip"
BOOST_VERSION="1.81.0"
BOOST_VERSION_UNDERSCORE="1_81_0"
BOOST_URL="https://boostorg.jfrog.io/artifactory/main/release/${BOOST_VERSION}/source/boost_${BOOST_VERSION_UNDERSCORE}.tar.gz"
MONERO_REPO="https://github.com/monero-project/monero.git"
MONERO_BRANCH="release-v0.18"

echo "======================================================"
echo "Monero Android Build - Dependency Setup"
echo "======================================================"
echo ""

# Function to check if command exists
command_exists() {
    command -v "$1" >/dev/null 2>&1
}

# Check required tools
echo "📋 Checking required tools..."
MISSING_TOOLS=()

if ! command_exists wget && ! command_exists curl; then
    MISSING_TOOLS+=("wget or curl")
fi

if ! command_exists unzip; then
    MISSING_TOOLS+=("unzip")
fi

if ! command_exists git; then
    MISSING_TOOLS+=("git")
fi

if ! command_exists docker; then
    MISSING_TOOLS+=("docker")
fi

if [ ${#MISSING_TOOLS[@]} -ne 0 ]; then
    echo "❌ Missing required tools: ${MISSING_TOOLS[*]}"
    echo ""
    echo "Install on Ubuntu/Debian:"
    echo "  sudo apt-get update"
    echo "  sudo apt-get install wget unzip git docker.io"
    echo ""
    echo "Install on macOS:"
    echo "  brew install wget unzip git"
    echo "  # Install Docker Desktop from docker.com"
    exit 1
fi

echo "✅ All required tools found"
echo ""

# Download function (tries wget then curl)
download_file() {
    local url=$1
    local output=$2
    
    if command_exists wget; then
        wget -c "$url" -O "$output"
    elif command_exists curl; then
        curl -L "$url" -o "$output"
    fi
}

# ============================================================
# Android NDK
# ============================================================
if [ -d "${SCRIPT_DIR}/ndk29" ]; then
    echo "✅ Android NDK r29 already exists"
else
    echo "📥 Downloading Android NDK r29..."
    echo "   This is ~1GB and may take several minutes..."
    
    download_file "$NDK_LINUX_URL" "android-ndk-${NDK_VERSION}-linux.zip"
    
    echo "📦 Extracting Android NDK..."
    unzip -q "android-ndk-${NDK_VERSION}-linux.zip"
    mv "android-ndk-${NDK_VERSION}" ndk29
    
    echo "🧹 Cleaning up..."
    rm "android-ndk-${NDK_VERSION}-linux.zip"
    
    echo "✅ Android NDK r29 installed"
fi
echo ""

# ============================================================
# Boost
# ============================================================
if [ -f "${SCRIPT_DIR}/boost_${BOOST_VERSION_UNDERSCORE}.tar.gz" ]; then
    echo "✅ Boost ${BOOST_VERSION} already downloaded"
else
    echo "📥 Downloading Boost ${BOOST_VERSION}..."
    echo "   This is ~100MB and may take a few minutes..."
    
    download_file "$BOOST_URL" "boost_${BOOST_VERSION_UNDERSCORE}.tar.gz"
    
    echo "✅ Boost ${BOOST_VERSION} downloaded"
fi
echo ""

# ============================================================
# Monero Source
# ============================================================
if [ -d "${SCRIPT_DIR}/monero" ]; then
    echo "✅ Monero source already exists"
    echo "   To update: cd monero && git pull && cd .."
else
    echo "📥 Cloning Monero source code..."
    echo "   Branch: ${MONERO_BRANCH}"
    echo "   This may take several minutes..."
    
    git clone --recursive --branch "${MONERO_BRANCH}" "${MONERO_REPO}"
    
    echo "🔄 Updating submodules..."
    cd monero
    git submodule update --init --force --recursive
    cd ..
    
    echo "✅ Monero source cloned"
fi
echo ""

# ============================================================
# Verification
# ============================================================
echo "======================================================"
echo "Verification"
echo "======================================================"
echo ""

ALL_OK=true

# Check NDK
if [ -d "${SCRIPT_DIR}/ndk29" ] && [ -f "${SCRIPT_DIR}/ndk29/build/cmake/android.toolchain.cmake" ]; then
    NDK_SIZE=$(du -sh ndk29 2>/dev/null | cut -f1)
    echo "✅ Android NDK r29: ${NDK_SIZE}"
else
    echo "❌ Android NDK r29: NOT FOUND or INCOMPLETE"
    ALL_OK=false
fi

# Check Boost
if [ -f "${SCRIPT_DIR}/boost_${BOOST_VERSION_UNDERSCORE}.tar.gz" ]; then
    BOOST_SIZE=$(du -h "boost_${BOOST_VERSION_UNDERSCORE}.tar.gz" | cut -f1)
    echo "✅ Boost ${BOOST_VERSION}: ${BOOST_SIZE}"
else
    echo "❌ Boost ${BOOST_VERSION}: NOT FOUND"
    ALL_OK=false
fi

# Check Monero
if [ -d "${SCRIPT_DIR}/monero" ] && [ -f "${SCRIPT_DIR}/monero/CMakeLists.txt" ]; then
    MONERO_COMMIT=$(cd monero && git rev-parse --short HEAD)
    echo "✅ Monero source: commit ${MONERO_COMMIT}"
else
    echo "❌ Monero source: NOT FOUND or INCOMPLETE"
    ALL_OK=false
fi

# Check Dockerfiles
DOCKERFILES_PRESENT=0
for df in android32.Dockerfile android64.Dockerfile androidx86.Dockerfile androidx64.Dockerfile; do
    if [ -f "${SCRIPT_DIR}/${df}" ]; then
        ((DOCKERFILES_PRESENT++))
    fi
done

if [ $DOCKERFILES_PRESENT -eq 4 ]; then
    echo "✅ All Dockerfiles present (4/4)"
else
    echo "⚠️  Dockerfiles: ${DOCKERFILES_PRESENT}/4 present"
    if [ $DOCKERFILES_PRESENT -eq 0 ]; then
        ALL_OK=false
    fi
fi

# Check build scripts
BUILD_SCRIPTS_PRESENT=0
for script in build-android-arm32.sh build-android-arm64.sh build-android-x86.sh build-android-x86_64.sh build-all-architectures.sh; do
    if [ -f "${SCRIPT_DIR}/${script}" ]; then
        ((BUILD_SCRIPTS_PRESENT++))
    fi
done

if [ $BUILD_SCRIPTS_PRESENT -eq 5 ]; then
    echo "✅ All build scripts present (5/5)"
else
    echo "⚠️  Build scripts: ${BUILD_SCRIPTS_PRESENT}/5 present"
fi

echo ""

# Final status
if [ "$ALL_OK" = true ]; then
    echo "======================================================"
    echo "✅ Setup Complete!"
    echo "======================================================"
    echo ""
    echo "You can now build Monero libraries:"
    echo ""
    echo "  # Build all architectures:"
    echo "  ./build-all-architectures.sh"
    echo ""
    echo "  # Or build specific architecture:"
    echo "  ./build-android-arm64.sh"
    echo ""
    echo "See QUICK_START.md for more information."
    echo ""
else
    echo "======================================================"
    echo "⚠️  Setup Incomplete"
    echo "======================================================"
    echo ""
    echo "Some dependencies are missing. Please fix the issues"
    echo "above and run this script again."
    echo ""
    exit 1
fi

# Calculate total size
TOTAL_SIZE=$(du -sh . 2>/dev/null | cut -f1)
echo "📊 Total size of dependencies: ${TOTAL_SIZE}"
echo ""

═══════════════════════════════════════════════════════════


