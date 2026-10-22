# Convenience wrapper around CMake.
#   make            configure and build with development tools
#   make run        build and start the game
#   make run LEVEL=3   start straight on floor 3
#   make release    build an optimized game without debug tools or tests
#   make clean      remove the build directories

BUILD_DIR ?= build
BUILD_TYPE ?= Release
RELEASE_DIR ?= $(BUILD_DIR)/release
RUN_ARGS = $(if $(LEVEL),--level $(LEVEL)) $(ARGS)

.PHONY: all build run release clean

all: build

build:
	cmake -S . -B "$(BUILD_DIR)" -DCMAKE_BUILD_TYPE=$(BUILD_TYPE) -DFORTRESS_DEBUG_TOOLS=ON -DBUILD_TESTING=ON
	cmake --build "$(BUILD_DIR)" --config $(BUILD_TYPE) --parallel

run: build
	"$(BUILD_DIR)/fortress" $(RUN_ARGS)

release:
	cmake -S . -B "$(RELEASE_DIR)" -DCMAKE_BUILD_TYPE=Release -DFORTRESS_DEBUG_TOOLS=OFF -DBUILD_TESTING=OFF
	cmake --build "$(RELEASE_DIR)" --config Release --parallel

clean:
	rm -rf "$(BUILD_DIR)" "$(RELEASE_DIR)"
