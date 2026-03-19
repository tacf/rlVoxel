BUILD_DIR ?= build
CMAKE ?= cmake

.PHONY: fetch build run debug clean docs docs-serve docs-clean

fetch:
	$(CMAKE) -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=Release

build: fetch
	$(CMAKE) --build $(BUILD_DIR) --config Release -j

run: build
	./$(BUILD_DIR)/rlVoxel

debug:
	$(CMAKE) -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=Debug
	$(CMAKE) --build $(BUILD_DIR) --config Debug -j
	./$(BUILD_DIR)/rlVoxel

clean:
	rm -rf $(BUILD_DIR)