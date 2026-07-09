# Convenience wrapper around CMake.
#   make            configure (first time) and build
#   make run        build and start the game
#   make run LEVEL=3   start straight on floor 3
#   make clean      remove the build directory

BUILD_DIR ?= build
BUILD_TYPE ?= Release
RUN_ARGS = $(if $(LEVEL),--level $(LEVEL)) $(ARGS)

.PHONY: all build run clean

all: build

$(BUILD_DIR)/CMakeCache.txt:
	cmake -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=$(BUILD_TYPE)

build: $(BUILD_DIR)/CMakeCache.txt
	cmake --build $(BUILD_DIR) --parallel

run: build
	$(BUILD_DIR)/fortress $(RUN_ARGS)

clean:
	rm -rf $(BUILD_DIR)
