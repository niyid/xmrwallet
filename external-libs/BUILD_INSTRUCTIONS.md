# Monero Android Build Instructions

Comprehensive guide for building Monero wallet libraries for all Android architectures.

## 📋 Table of Contents

1. [Prerequisites](#prerequisites)
2. [Quick Setup](#quick-setup)
3. [Architecture Support](#architecture-support)
4. [Building](#building)
5. [Output Files](#output-files)
6. [Integration](#integration)
7. [Customization](#customization)
8. [Troubleshooting](#troubleshooting)
9. [Advanced Topics](#advanced-topics)

---

## Prerequisites

### System Requirements

- **Operating System**: Linux (Ubuntu 20.04+ recommended) or macOS
- **Docker**: Version 20.10 or later
- **RAM**: 8GB minimum, 16GB recommended
- **Disk Space**: 25GB free space minimum
- **CPU**: Multi-core processor (4+ cores recommended)

### Required Tools

```bash
# Ubuntu/Debian
sudo apt-get update
sudo apt-get install wget unzip git docker.io

# macOS
brew install wget unzip git
# Install Docker Desktop from docker.com
```

### Starting Docker

```bash
# Linux - start Docker service
sudo systemctl start docker
sudo usermod -aG docker $USER  # Add yourself to docker group
newgrp docker  # Refresh group membership

# macOS - start Docker Desktop application
```

---

## Quick Setup

### Step 1: Clone Repository

```bash
git clone <your-repo-url>
cd monero-android-build
```

### Step 2: Download Dependencies

Run the automated setup script:

```bash
chmod +x setup-dependencies.sh
./setup-dependencies.sh
```

This downloads (~1.2GB total):
- Android NDK r29 (~1GB)
- Boost 1.81.0 (~100MB)
- Monero source code (~100MB)

**Note**: First-time setup takes 10-20 minutes depending on your internet speed.

---

## Architecture Support

This build system supports all Android ABIs:

| Architecture | Android ABI | Dockerfile | Typical Devices |
|-------------|-------------|------------|-----------------|
| ARM 32-bit  | armeabi-v7a | android32.Dockerfile | Older phones/tablets |
| ARM 64-bit  | arm64-v8a   | android64.Dockerfile | Modern phones/tablets |
| x86 32-bit  | x86         | androidx86.Dockerfile | Emulators, tablets |
| x86 64-bit  | x86_64      | androidx64.Dockerfile | Emulators, tablets |

**Recommendation**: Build arm64-v8a and armeabi-v7a for widest device support.

---

## Building

### Build All Architectures

```bash
./build-all-architectures.sh
```

Builds all 4 architectures sequentially. Takes 3-4 hours on first run.

### Build Specific Architecture

```bash
# Using individual scripts
./build-android-arm64.sh     # ARM 64-bit
./build-android-arm32.sh     # ARM 32-bit
./build-android-x86_64.sh    # x86 64-bit
./build-android-x86.sh       # x86 32-bit

# Or using the all-in-one script
./build-all-architectures.sh arm64
```

### Build Process Overview

Each build performs these steps:

1. **Setup Environment** (1 min)
   - Configure Android NDK toolchain
   - Set up cross-compilation environment

2. **Build Boost** (15-20 min)
   - Cross-compile Boost libraries for Android
   - Builds: system, filesystem, thread, chrono, date_time, regex, serialization, program_options

3. **Build OpenSSL** (5-10 min)
   - Cross-compile crypto and ssl libraries

4. **Build Dependencies** (10-15 min)
   - Expat (XML parser)
   - Unbound (DNS resolver)
   - libsodium (cryptographic library)
   - libzmq (ZeroMQ messaging)

5. **Build Monero** (20-30 min)
   - Configure with CMake for Android
   - Build wallet_api and related components
   - Merge all libraries into single files

6. **Package Output** (< 1 min)
   - Create libwallet_merged.a (static library)
   - Create libwallet.so (shared library)

---

## Output Files

### File Locations

After successful build:

```
output/
├── arm32/
│   ├── libwallet_merged.a    # Static library (~154MB)
│   └── libwallet.so          # Shared library (~25MB)
├── arm64/
│   ├── libwallet_merged.a    # Static library (~154MB)
│   └── libwallet.so          # Shared library (~27MB)
├── x86/
│   ├── libwallet_merged.a    # Static library (~160MB)
│   └── libwallet.so          # Shared library (~26MB)
└── x86_64/
    ├── libwallet_merged.a    # Static library (~162MB)
    └── libwallet.so          # Shared library (~28MB)
```

### File Types

**libwallet_merged.a** (Static Library)
- Contains all Monero wallet code and dependencies
- Use when you want to statically link everything
- Larger app size but no external dependencies

**libwallet.so** (Shared Library)
- Contains all Monero wallet code and dependencies
- Recommended for Android apps
- Smaller app size, loaded at runtime

### Verification

```bash
# Check architecture
file output/arm64/libwallet.so
# Expected: ELF 64-bit LSB shared object, ARM aarch64

# Check size
ls -lh output/arm64/

# List symbols
nm output/arm64/libwallet.so | grep -i wallet | head -10
```

---

## Integration

### Copy to Android Project

```bash
# Create jniLibs directories
mkdir -p YourApp/app/src/main/jniLibs/{armeabi-v7a,arm64-v8a}

# Copy shared libraries (recommended)
cp output/arm32/libwallet.so YourApp/app/src/main/jniLibs/armeabi-v7a/
cp output/arm64/libwallet.so YourApp/app/src/main/jniLibs/arm64-v8a/

# Optional: x86 for emulator testing
cp output/x86_64/libwallet.so YourApp/app/src/main/jniLibs/x86_64/
```

### CMakeLists.txt Configuration

```cmake
cmake_minimum_required(VERSION 3.18.1)
project("your-app")

# Import Monero wallet library
add_library(monero-wallet SHARED IMPORTED)
set_target_properties(monero-wallet PROPERTIES
    IMPORTED_LOCATION ${CMAKE_SOURCE_DIR}/../jniLibs/${ANDROID_ABI}/libwallet.so
)

# Your JNI wrapper library
add_library(your-native-lib SHARED
    native-lib.cpp
    wallet_jni.cpp
)

# Link libraries
target_link_libraries(your-native-lib
    monero-wallet
    log      # Android logging
    android  # Android framework
)
```

### Java/Kotlin Integration

```kotlin
class MoneroWallet {
    companion object {
        init {
            System.loadLibrary("monero-wallet")
            System.loadLibrary("your-native-lib")
        }
    }
    
    // Your JNI methods
    external fun createWallet(path: String, password: String): Boolean
    external fun getBalance(): Long
    // ... more methods
}
```

---

## Customization

### Change API Level

Edit Dockerfiles and modify:

```dockerfile
ENV API_LEVEL=33    # Change to your desired API level
```

Minimum supported: API 21 (Android 5.0)

### Enable/Disable Features

In the CMake configuration section:

```dockerfile
-DUSE_DEVICE_TREZOR=OFF    # Hardware wallet support
-DUSE_DEVICE_LEDGER=OFF    # Hardware wallet support
-DUSE_UNBOUND=ON           # DNS resolution
-DTRANSLATIONS=OFF         # Internationalization
```

### Modify Monero Version

Edit `setup-dependencies.sh`:

```bash
MONERO_BRANCH="release-v0.18"  # Change version here
```

Then re-run setup:

```bash
rm -rf monero
./setup-dependencies.sh
```

### Custom Boost Version

1. Download different Boost version
2. Update Dockerfile:
```dockerfile
COPY boost_1_XX_0.tar.gz ${WORK_DIR}/boost/
```

---

## Troubleshooting

### Build Fails Immediately

**Symptom**: Build fails within first few minutes

**Solutions**:
```bash
# Verify all dependencies
ls -la ndk29/ boost_1_81_0.tar.gz monero/

# Re-run setup
./setup-dependencies.sh

# Check Docker is running
docker ps
```

### Out of Disk Space

**Symptom**: "No space left on device"

**Solutions**:
```bash
# Clean Docker cache
docker system prune -a

# Remove old build images
docker images | grep monero-android | awk '{print $3}' | xargs docker rmi

# Check available space
df -h
```

### Build Very Slow

**Symptom**: Build taking much longer than expected

**Solutions**:
```bash
# Check Docker resources
docker info | grep -i "cpus\|memory"

# Increase in Docker Desktop:
# Settings → Resources → Increase CPU/Memory

# Check system load
top
htop  # if installed
```

### Boost Build Fails

**Symptom**: Error during Boost compilation

**Solutions**:
```bash
# Verify Boost archive
tar -tzf boost_1_81_0.tar.gz | head

# Re-download if corrupted
rm boost_1_81_0.tar.gz
./setup-dependencies.sh
```

### Library Not in Output

**Symptom**: `output/` directory empty after build

**Solutions**:
```bash
# Check if library exists in container
docker run --rm monero-android-arm64 ls -lh /work/out/

# Manual extraction
docker create --name temp monero-android-arm64
docker cp temp:/work/out/libwallet.so ./output/arm64/
docker rm temp
```

### CMake Configuration Errors

**Symptom**: Errors during Monero CMake configuration

**Solutions**:
```bash
# Save build log
docker build -t monero-android-arm64 -f android64.Dockerfile . 2>&1 | tee build.log

# Search for specific errors
grep -i "error\|failed" build.log

# Check dependency versions
docker run --rm monero-android-arm64 bash -c "
    ${TOOLCHAIN}/bin/aarch64-linux-android33-clang++ --version
"
```

---

## Advanced Topics

### Parallel Builds

Build multiple architectures simultaneously:

```bash
# Terminal 1
./build-android-arm64.sh &

# Terminal 2
./build-android-arm32.sh &

# Wait for both
wait
```

### Incremental Builds

Docker caches each build layer. To rebuild only changed components:

```bash
# Modify Monero source
# Then rebuild (uses cache for dependencies)
docker build -t monero-android-arm64 -f android64.Dockerfile .
```

To force complete rebuild:

```bash
docker build --no-cache -t monero-android-arm64 -f android64.Dockerfile .
```

### Custom Build Flags

Add custom compiler flags by modifying Dockerfile:

```dockerfile
-DCMAKE_CXX_FLAGS="-fPIC -O2 -DANDROID -DYOUR_CUSTOM_FLAG"
```

### Build with Different NDK

```bash
# Download different NDK version
wget https://dl.google.com/android/repository/android-ndk-rXX-linux.zip
unzip android-ndk-rXX-linux.zip
mv android-ndk-rXX ndk-custom

# Modify Dockerfile
COPY ndk-custom ${ANDROID_NDK_HOME}
```

---

## Build Time Reference

On Intel i7-10700K (8 cores/16 threads), 32GB RAM, SSD:

| Stage | ARM64 | ARM32 | x86_64 | x86 |
|-------|-------|-------|--------|-----|
| Boost | 18min | 17min | 16min  | 15min |
| OpenSSL | 8min | 7min | 7min | 7min |
| Dependencies | 12min | 11min | 11min | 10min |
| Monero | 22min | 23min | 24min | 25min |
| **Total (first)** | **60min** | **58min** | **58min** | **57min** |
| **Total (cached)** | **12min** | **11min** | **12min** | **11min** |

---

## Library Contents

`libwallet_merged.a` and `libwallet.so` include:

**Core Wallet**
- `libwallet.a` - Wallet functionality
- `libwallet_api.a` - Public API

**Cryptonote**
- `libcryptonote_core.a` - Protocol implementation
- `libcryptonote_basic.a` - Basic types
- `libcryptonote_protocol.a` - Network protocol

**Cryptography**
- `libcncrypto.a` - Cryptographic primitives
- `libringct.a` - Ring confidential transactions
- `libringct_basic.a` - Basic RingCT

**Supporting**
- `libmnemonics.a` - Seed words
- `libcommon.a` - Utilities
- `libdevice.a` - Hardware wallets
- `libcheckpoints.a` - Blockchain checkpoints
- `libblockchain_db.a` - Database layer
- `libeasylogging.a` - Logging
- `libepee.a` - Network layer

**External Dependencies**
- Boost libraries (statically linked)
- OpenSSL (statically linked)
- libsodium (statically linked)
- libzmq (statically linked)
- Expat (statically linked)
- Unbound (statically linked)

---

## Security Considerations

1. **Verify Downloads**: Always verify checksums of downloaded dependencies
2. **Use Release Versions**: Don't use debug builds in production
3. **Code Signing**: Sign your Android app properly
4. **Secure Storage**: Use Android Keystore for sensitive data
5. **Network Security**: Use HTTPS/SSL for all connections

---

## License

This build system is provided under MIT License. Monero is licensed under BSD 3-Clause License.

---

## Support & Contributing

- **Issues**: Open an issue on GitHub
- **Contributions**: Pull requests welcome
- **Documentation**: Help improve these docs

---

**Note**: These are cross-compiled libraries for Android devices. They cannot run on your build host (Linux/macOS).
