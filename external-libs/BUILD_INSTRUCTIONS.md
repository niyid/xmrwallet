# Build Instructions

<img src="https://github.com/niyid/xmrwallet/raw/master/monero_wallet.png" alt="Monero Wallet" width="200" height="200">

Complete guide for building Monero native libraries for Android.

## Prerequisites

- Docker installed and running
- Bash shell (Linux/macOS/WSL)
- 20+ GB free disk space
- 4+ GB RAM

## Quick Start

### Build All Architectures (Recommended)

```bash
./build-all-architectures.sh
```

This builds all three Android architectures (arm32, arm64, x86_64) sequentially.

### Build Single Architecture

Build only what you need:

```bash
# ARM 32-bit (armeabi-v7a)
./build-android-arm32.sh

# ARM 64-bit (arm64-v8a) - Most common for modern devices
./build-android-arm64.sh

# x86_64 - For emulators and x86 devices
./build-android-x86_64.sh
```

Or use the multi-arch script with an argument:

```bash
./build-all-architectures.sh arm64
./build-all-architectures.sh arm32
./build-all-architectures.sh x86_64
```

## Output Structure

Build artifacts are organized by Android ABI:

```
output/
├── armeabi-v7a/
│   └── lib/
│       ├── libwallet.a
│       ├── libwallet_api.a
│       ├── libcryptonote_core.a
│       ├── libcryptonote_basic.a
│       ├── libblockchain_db.a
│       ├── libringct.a
│       ├── libringct_basic.a
│       ├── libcommon.a
│       ├── libepee.a
│       ├── libmnemonics.a
│       ├── libcncrypto.a
│       ├── libeasylogging.a
│       ├── libdevice.a
│       ├── libcheckpoints.a
│       ├── libcrypto.a (OpenSSL)
│       ├── libssl.a (OpenSSL)
│       ├── libboost_*.a (multiple Boost libraries)
│       ├── libsodium.a
│       ├── libzmq.a
│       ├── libunbound.a
│       └── libexpat.a
├── arm64-v8a/
│   └── lib/
│       └── (same structure)
└── x86_64/
    └── lib/
        └── (same structure)
```

### Key Library Files

- **libwallet_api.a** (~28 MB) - Main wallet API library
- **libwallet.a** (~94 MB) - Core wallet implementation
- **libcryptonote_core.a** (~26 MB) - Monero protocol implementation
- **libcrypto.a / libssl.a** - OpenSSL cryptography
- **libboost_*.a** - Boost C++ libraries
- **libsodium.a** - Crypto library
- **libzmq.a** - ZeroMQ messaging

All files are static libraries (`.a`) that need to be linked into your application or converted to shared libraries (`.so`).

## Integration with Android Project

### Option 1: Create Shared Library (Recommended)

You'll need to create a JNI wrapper that links these static libraries into a shared library:

1. Create a JNI wrapper (e.g., `monerujo.cpp`)
2. Use CMake or ndk-build to link the static libraries
3. Build `libmonerujo.so` (or your chosen name)

Example `CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.10)
project(monerujo)

set(MONERO_LIBS_DIR ${CMAKE_SOURCE_DIR}/libs/${ANDROID_ABI}/lib)

add_library(monerujo SHARED
    monerujo.cpp
)

target_link_libraries(monerujo
    ${MONERO_LIBS_DIR}/libwallet_api.a
    ${MONERO_LIBS_DIR}/libwallet.a
    ${MONERO_LIBS_DIR}/libcryptonote_core.a
    ${MONERO_LIBS_DIR}/libcryptonote_basic.a
    ${MONERO_LIBS_DIR}/libblockchain_db.a
    ${MONERO_LIBS_DIR}/libringct.a
    ${MONERO_LIBS_DIR}/libringct_basic.a
    ${MONERO_LIBS_DIR}/libcommon.a
    ${MONERO_LIBS_DIR}/libepee.a
    ${MONERO_LIBS_DIR}/libmnemonics.a
    ${MONERO_LIBS_DIR}/libcncrypto.a
    ${MONERO_LIBS_DIR}/libeasylogging.a
    ${MONERO_LIBS_DIR}/libdevice.a
    ${MONERO_LIBS_DIR}/libcheckpoints.a
    ${MONERO_LIBS_DIR}/libcrypto.a
    ${MONERO_LIBS_DIR}/libssl.a
    ${MONERO_LIBS_DIR}/libboost_serialization.a
    ${MONERO_LIBS_DIR}/libboost_filesystem.a
    ${MONERO_LIBS_DIR}/libboost_thread.a
    ${MONERO_LIBS_DIR}/libboost_program_options.a
    ${MONERO_LIBS_DIR}/libboost_regex.a
    ${MONERO_LIBS_DIR}/libboost_chrono.a
    ${MONERO_LIBS_DIR}/libboost_system.a
    ${MONERO_LIBS_DIR}/libboost_date_time.a
    ${MONERO_LIBS_DIR}/libsodium.a
    ${MONERO_LIBS_DIR}/libzmq.a
    ${MONERO_LIBS_DIR}/libunbound.a
    ${MONERO_LIBS_DIR}/libexpat.a
    log
    android
)
```

### Option 2: Direct Static Linking

If you have a native component in your Android app, link these libraries directly in your build configuration.

### Copying Libraries to Your Project

```bash
# Copy all architectures
cp -r output/* your-app/src/main/cpp/libs/

# Or specific architecture
cp -r output/arm64-v8a your-app/src/main/cpp/libs/
```

## Build Process Details

### What Happens During Build

1. **Docker Image Creation**
   - Installs Android NDK r29
   - Sets up build dependencies
   - Configures toolchain for target architecture

2. **Monero Compilation**
   - Clones Monero repository (release-v0.18 branch)
   - Builds dependencies (Boost, OpenSSL, Sodium, ZMQ, etc.)
   - Compiles all Monero components as static libraries
   - Builds wallet API layer

3. **Artifact Extraction**
   - Runs extraction script in container
   - Copies all `.a` files to host output directory
   - Organizes by Android ABI structure

### Build Time Estimates

- **First build**: 45-90 minutes per architecture
- **Subsequent builds**: Uses Docker cache, much faster
- **All architectures**: 2-4 hours (first time)

## Architecture Details

### ARM 32-bit (armeabi-v7a)
- **Dockerfile**: `android32.Dockerfile`
- **Target ABI**: armeabi-v7a
- **Use case**: Older Android devices
- **Min API**: 21 (Android 5.0)
- **Output**: `output/armeabi-v7a/lib/*.a`

### ARM 64-bit (arm64-v8a)
- **Dockerfile**: `android64.Dockerfile`
- **Target ABI**: arm64-v8a
- **Use case**: Modern Android devices (most common)
- **Min API**: 21 (Android 5.0)
- **Output**: `output/arm64-v8a/lib/*.a`

### x86_64
- **Dockerfile**: `androidx64.Dockerfile`
- **Target ABI**: x86_64
- **Use case**: Android emulators, x86 tablets
- **Min API**: 21 (Android 5.0)
- **Output**: `output/x86_64/lib/*.a`

## Troubleshooting

### Build Fails with "Out of Memory"

Increase Docker memory allocation:
- Docker Desktop: Settings → Resources → Memory (set to 6GB+)
- Linux: Adjust Docker daemon configuration

### Build Fails with "No space left on device"

Clean Docker cache:

```bash
docker system prune -a
```

### Extraction Script Not Found

Make sure the Dockerfile includes the extraction script. Check that `/extract-libs.sh` exists in the container:

```bash
docker run --rm monero-android-arm64 ls -la /extract-libs.sh
```

### Missing Libraries in Output

If some `.a` files are missing, check the build logs:

```bash
docker run --rm monero-android-arm64 ls -la /work/out/lib/
```

### Build Succeeds but Output is Empty

The extraction script may have failed silently. Run manually:

```bash
docker run --rm -v "$(pwd)/output:/host-output" \
    monero-android-arm64 \
    /extract-libs.sh /host-output
```

### Linking Errors When Building Android App

Common issues:
- **Undefined symbols**: Make sure to link libraries in correct order (dependencies first)
- **Missing libraries**: Verify all required `.a` files are included
- **ABI mismatch**: Ensure library ABI matches your target device/emulator

Order matters when linking. Generally link in this order:
1. Wallet API libraries (libwallet_api.a, libwallet.a)
2. Core libraries (libcryptonote_core.a, etc.)
3. Supporting libraries (libringct.a, libcommon.a, etc.)
4. Crypto libraries (libcrypto.a, libssl.a, libsodium.a)
5. Utility libraries (Boost, libzmq.a, libunbound.a, libexpat.a)

## Advanced Usage

### Custom Monero Version

Edit the Dockerfile to change the branch:

```dockerfile
RUN git clone --recursive --branch release-v0.18 \
    https://github.com/monero-project/monero.git
```

### Debug Build

Add debug flags in the Dockerfile:

```dockerfile
ENV CFLAGS="-g -O0"
ENV CXXFLAGS="-g -O0"
```

### Clean Build (No Cache)

```bash
docker build --no-cache -t monero-android-arm64 -f android64.Dockerfile .
```

### Inspect Build Container

```bash
docker run -it --rm monero-android-arm64 bash
```

## Testing

### Verify Libraries

Check libraries were built:

```bash
ls -lh output/arm64-v8a/lib/
```

Check for the key files:
```bash
file output/arm64-v8a/lib/libwallet_api.a
```

Should show: "current ar archive"

### Library Sizes (Approximate)

- libwallet.a: ~90-100 MB
- libwallet_api.a: ~25-30 MB
- libcryptonote_core.a: ~25-30 MB
- libepee.a: ~25-30 MB
- Others: 1-15 MB each

If sizes are significantly different, there may be a build issue.

## Performance Optimization

### Parallel Builds

Build multiple architectures in parallel (requires sufficient RAM):

```bash
./build-android-arm32.sh &
./build-android-arm64.sh &
./build-android-x86_64.sh &
wait
```

### Disk Space Management

Each Docker image is ~5GB. Clean up old images:

```bash
docker images | grep monero-android
docker rmi <image-id>
```

## Security Considerations

- Always verify Monero source from official repository
- Check commit signatures when possible
- Review Dockerfiles before building
- Use official Android NDK from Google
- Keep build environment isolated
- Verify checksums of built libraries if distributing

## Next Steps After Building

1. **Create JNI wrapper** - Write C/C++ code to interface with the Monero wallet API
2. **Link static libraries** - Configure CMake or ndk-build to link all `.a` files
3. **Build shared library** - Compile your JNI wrapper into a `.so` file
4. **Integrate into Android** - Load the `.so` library in your Android app
5. **Test thoroughly** - Verify wallet operations work correctly

## Contributing

When modifying build scripts:

1. Test all architectures
2. Verify all expected `.a` files are produced
3. Check file sizes are reasonable
4. Update documentation
5. Test integration with sample Android app

## Support

For build issues:
- Check this documentation first
- Review Docker logs for specific errors
- Verify prerequisites are met
- Test with a clean Docker environment

For Monero-specific issues:
- Refer to official Monero documentation
- Check Monero GitHub issues
- Review wallet API documentation at https://github.com/monero-project/monero
