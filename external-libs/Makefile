.PHONY: all arm64-v8a armeabi-v7a x86_64 x86 clean extract-libs

NPROC ?= 8

all: arm64-v8a armeabi-v7a x86_64 x86

arm64-v8a:
	@echo "Building for arm64-v8a..."
	docker build -f android64.Dockerfile -t android64-builder --build-arg NPROC=$(NPROC) .
	docker create --name temp-arm64 android64-builder
	docker cp temp-arm64:/opt/android/prefix ./build-arm64-v8a
	docker rm temp-arm64
	@echo "✓ arm64-v8a build complete: ./build-arm64-v8a"

armeabi-v7a:
	@echo "Building for armeabi-v7a..."
	docker build -f android32.Dockerfile -t android32-builder --build-arg NPROC=$(NPROC) .
	docker create --name temp-arm32 android32-builder
	docker cp temp-arm32:/opt/android/prefix ./build-armeabi-v7a
	docker rm temp-arm32
	@echo "✓ armeabi-v7a build complete: ./build-armeabi-v7a"

x86_64:
	@echo "Building for x86_64..."
	docker build -f androidx64.Dockerfile -t androidx64-builder --build-arg NPROC=$(NPROC) .
	docker create --name temp-x86_64 androidx64-builder
	docker cp temp-x86_64:/opt/android/prefix ./build-x86_64
	docker rm temp-x86_64
	@echo "✓ x86_64 build complete: ./build-x86_64"

x86:
	@echo "Building for x86..."
	docker build -f androidx86.Dockerfile -t androidx86-builder --build-arg NPROC=$(NPROC) .
	docker create --name temp-x86 androidx86-builder
	docker cp temp-x86:/opt/android/prefix ./build-x86
	docker rm temp-x86
	@echo "✓ x86 build complete: ./build-x86"

extract-libs:
	@echo "Extracting libraries from all architectures..."
	@mkdir -p libs/arm64-v8a libs/armeabi-v7a libs/x86_64 libs/x86
	@mkdir -p include
	@if [ -d build-arm64-v8a ]; then \
		cp build-arm64-v8a/lib/*.a libs/arm64-v8a/ 2>/dev/null || true; \
		cp -r build-arm64-v8a/include/* include/ 2>/dev/null || true; \
	fi
	@if [ -d build-armeabi-v7a ]; then \
		cp build-armeabi-v7a/lib/*.a libs/armeabi-v7a/ 2>/dev/null || true; \
	fi
	@if [ -d build-x86_64 ]; then \
		cp build-x86_64/lib/*.a libs/x86_64/ 2>/dev/null || true; \
	fi
	@if [ -d build-x86 ]; then \
		cp build-x86/lib/*.a libs/x86/ 2>/dev/null || true; \
	fi
	@echo "✓ Libraries extracted to ./libs/ and headers to ./include/"

clean:
	@echo "Cleaning up..."
	docker rmi -f android64-builder android32-builder androidx64-builder androidx86-builder 2>/dev/null || true
	docker rm -f temp-arm64 temp-arm32 temp-x86_64 temp-x86 2>/dev/null || true
	rm -rf build-arm64-v8a build-armeabi-v7a build-x86_64 build-x86
	rm -rf libs include
	@echo "✓ Clean complete"

help:
	@echo "Available targets:"
	@echo "  make all           - Build all architectures"
	@echo "  make arm64-v8a     - Build ARM 64-bit"
	@echo "  make armeabi-v7a   - Build ARM 32-bit"
	@echo "  make x86_64        - Build x86 64-bit"
	@echo "  make x86           - Build x86 32-bit"
	@echo "  make extract-libs  - Extract libraries to libs/ folder"
	@echo "  make clean         - Remove all build artifacts"
	@echo ""
	@echo "Options:"
	@echo "  NPROC=N           - Number of parallel jobs (default: 8)"
	@echo ""
	@echo "Example: make arm64-v8a NPROC=4"
