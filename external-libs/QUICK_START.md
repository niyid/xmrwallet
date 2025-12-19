# Quick Start Guide

<img src="https://github.com/niyid/xmrwallet/raw/master/monero_wallet.png" alt="Monero Wallet" width="200" height="200">

Get Monero Android libraries built in under 5 minutes.

## Prerequisites Check

Ensure you have:
- ✅ Docker installed and running
- ✅ Bash shell available
- ✅ 20GB+ free disk space
- ✅ Stable internet connection

Test Docker:
```bash
docker --version
```

## Build Commands

### Option 1: Build Everything (Recommended)

```bash
./build-all-architectures.sh
```

**Time**: 2-4 hours for first build  
**Output**: All three architectures (arm32, arm64, x86_64)

### Option 2: Build Single Architecture

Most modern devices use **arm64**:

```bash
./build-android-arm64.sh
```

**Time**: 45-90 minutes  
**Output**: arm64-v8a libraries only

Other options:
```bash
./build-android-arm32.sh    # For older devices
./build-android-x86_64.sh   # For emulators
```

### Option 3: Build Specific Architecture via Main Script

```bash
./build-all-architectures.sh arm64
```

## What You'll Get

After building, check your `output/` directory:

```
output/
├── armeabi-v7a/
│   └── lib/
│       ├── libwallet_api.a      (~28 MB)
│       ├── libwallet.a          (~94 MB)
│       ├── libcryptonote_core.a (~26 MB)
│       ├── libcrypto.a          (OpenSSL)
│       ├── libssl.a             (OpenSSL)
│       ├── libboost_*.a         (Boost libraries)
│       └── ... (25+ more .a files)
├── arm64-v8a/
│   └── lib/
│       └── (same files)
└── x86_64/
    └── lib/
        └── (same files)
```

**Important**: These are **static libraries** (`.a` files), not ready-to-use Android libraries. You need to link them into a shared library (`.so`) for Android.

## Next Steps: Integration

### What These Libraries Are

The build produces **static libraries** that contain:
- Monero wallet functionality
- Cryptography (OpenSSL, Sodium)
- Network libraries (ZMQ)
- Boost C++ libraries

### What You Need to Do

To use these in Android, you need to:

1. **Create a JNI wrapper** (C/C++ code)
2. **Link all the `.a` files** together
3. **Build a shared library** (`.so` file)
4. **Load in your Android app**

### Quick Example

#### 1. Copy libraries to your project:

```bash
mkdir -p YourApp/src/main/cpp/libs
cp -r output/* YourApp/src/main/cpp/libs/
```

#### 2. Create CMakeLists.txt:

```cmake
cmake_minimum_required(VERSION 3.10)
project(monerujo)

set(MONERO_LIBS ${CMAKE_SOURCE_DIR}/libs/${ANDROID_ABI}/lib)

add_library(monerujo SHARED
    monerujo.cpp  # Your JNI wrapper
)

# Link all Monero static libraries
target_link_libraries(monerujo
    ${MONERO_LIBS}/libwallet_api.a
    ${MONERO_LIBS}/libwallet.a
    ${MONERO_LIBS}/libcryptonote_core.a
    ${MONERO_LIBS}/libcryptonote_basic.a
    ${MONERO_LIBS}/libblockchain_db.a
    ${MONERO_LIBS}/libringct.a
    ${MONERO_LIBS}/libringct_basic.a
    ${MONERO_LIBS}/libcommon.a
    ${MONERO_LIBS}/libepee.a
    ${MONERO_LIBS}/libmnemonics.a
    ${MONERO_LIBS}/libcncrypto.a
    ${MONERO_LIBS}/libeasylogging.a
    ${MONERO_LIBS}/libdevice.a
    ${MONERO_LIBS}/libcheckpoints.a
    ${MONERO_LIBS}/libcrypto.a
    ${MONERO_LIBS}/libssl.a
    ${MONERO_LIBS}/libboost_serialization.a
    ${MONERO_LIBS}/libboost_filesystem.a
    ${MONERO_LIBS}/libboost_thread.a
    ${MONERO_LIBS}/libboost_program_options.a
    ${MONERO_LIBS}/libboost_regex.a
    ${MONERO_LIBS}/libboost_chrono.a
    ${MONERO_LIBS}/libboost_system.a
    ${MONERO_LIBS}/libboost_date_time.a
    ${MONERO_LIBS}/libsodium.a
    ${MONERO_LIBS}/libzmq.a
    ${MONERO_LIBS}/libunbound.a
    ${MONERO_LIBS}/libexpat.a
    log android
)
```

#### 3. Create JNI wrapper (example):

```cpp
// monerujo.cpp
#include <jni.h>
#include "wallet/api/wallet2_api.h"

extern "C" JNIEXPORT jstring JNICALL
Java_com_example_WalletManager_createWallet(
    JNIEnv* env,
    jobject /* this */,
    jstring path) {
    
    const char* walletPath = env->GetStringUTFChars(path, nullptr);
    
    Monero::WalletManager* wmgr = Monero::WalletManagerFactory::getWalletManager();
    Monero::Wallet* wallet = wmgr->createWallet(
        walletPath,
        "password",
        "English",
        Monero::MAINNET
    );
    
    env->ReleaseStringUTFChars(path, walletPath);
    
    return env->NewStringUTF(wallet ? "success" : "failed");
}
```

#### 4. In your Android app:

```kotlin
class WalletManager {
    companion object {
        init {
            System.loadLibrary("monerujo")
        }
    }
    
    external fun createWallet(path: String): String
}
```

## Common Issues & Solutions

### ❌ "I don't see .so files, only .a files"

This is correct! The build produces static libraries (`.a`). You need to:
1. Create a JNI wrapper
2. Link the `.a` files using CMake
3. Build a `.so` file yourself

### ❌ Docker not found
```bash
# Install Docker first
# Linux: sudo apt install docker.io
# Mac: Download Docker Desktop
# Windows: Use WSL2 + Docker Desktop
```

### ❌ Permission denied
```bash
sudo chmod +x build-*.sh
# Or add your user to docker group:
sudo usermod -aG docker $USER
# Then logout and login again
```

### ❌ Out of memory during build
- Increase Docker memory to 6GB+
- Docker Desktop: Settings → Resources → Memory

### ❌ Linking errors when building Android app

Common solutions:
- Link libraries in correct order (dependencies first)
- Include ALL `.a` files from the list above
- Make sure ABI matches (arm64 device needs arm64-v8a libraries)

## Verify Your Build

Check if all libraries were built:

```bash
ls -lh output/arm64-v8a/lib/
```

You should see ~28 `.a` files. Key ones to check:

```bash
# Should be ~94 MB
ls -lh output/arm64-v8a/lib/libwallet.a

# Should be ~28 MB
ls -lh output/arm64-v8a/lib/libwallet_api.a

# Should be ~26 MB
ls -lh output/arm64-v8a/lib/libcryptonote_core.a
```

## Testing in Android Emulator

1. Build x86_64 libraries:
   ```bash
   ./build-android-x86_64.sh
   ```

2. Use x86_64 libraries when linking for emulator

3. Run on x86_64 emulator

## Build Time Reference

| Architecture | First Build | Cached Build |
|-------------|-------------|--------------|
| arm32       | ~60 min     | ~10 min      |
| arm64       | ~60 min     | ~10 min      |
| x86_64      | ~60 min     | ~10 min      |
| **All**     | **2-4 hours** | **~30 min** |

*Times vary based on CPU and internet speed*

## Pro Tips

💡 **Build overnight**: First build takes hours - start it before bed

💡 **Build arm64 first**: Most Android devices use this architecture

💡 **Use Docker cache**: Subsequent builds are much faster

💡 **Clean Docker regularly**: Each image is ~5GB
```bash
docker system prune -a
```

💡 **Study Monerujo**: Look at the [Monerujo project](https://github.com/m2049r/xmrwallet) for a complete example of JNI integration

💡 **Start simple**: Test basic wallet creation before implementing complex features

## Real-World Example

For a complete working example, check out **Monerujo** wallet:
- GitHub: https://github.com/m2049r/xmrwallet
- Shows complete JNI wrapper implementation
- Demonstrates proper library linking
- Production-ready Android integration

## Next Steps

- ✅ Build complete? → Create JNI wrapper and link libraries
- 📖 Need linking help? → Read [BUILD_INSTRUCTIONS.md](BUILD_INSTRUCTIONS.md)
- 🔍 See working example? → Study Monerujo source code
- 🛠 Issues? → Check troubleshooting in BUILD_INSTRUCTIONS.md
