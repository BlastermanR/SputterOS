# Makefile for SputteringOS Project (Raspberry Pi Pico 2)
#
# make format
#     Recursively runs clang-format -i on all .cpp, .h, and .pio files across 
#     your workspace while ignoring the build folder entirely. 


# --- Project Paths ---
BUILD_DIR ?= build
TEST_BUILD_DIR ?= tests/build
TEST_COV_BUILD_DIR ?= tests/build_coverage
PICO_SDK_ROOT ?= /c/Users/ryanb/.pico-sdk
PICOTOOL ?= $(PICO_SDK_ROOT)/picotool/2.2.0-a4/picotool/picotool.exe
OPENOCD ?= $(PICO_SDK_ROOT)/openocd/0.12.0+dev/openocd.exe
OPENOCD_SCRIPTS ?= $(PICO_SDK_ROOT)/openocd/0.12.0+dev/scripts
CMAKE ?= $(PICO_SDK_ROOT)/cmake/v3.31.5/bin/cmake.exe
CTEST ?= $(PICO_SDK_ROOT)/cmake/v3.31.5/bin/ctest.exe
NINJA ?= $(PICO_SDK_ROOT)/ninja/v1.12.1/ninja.exe
BIN_TARGET = SputteringACS.elf

# ────────────────────────────────────────────────────────────────────────────
# LIBRARY-ONLY BUILD (for use as a submodule in parent projects)
# ────────────────────────────────────────────────────────────────────────────
# Build SputterOS library without tests. Used when SputterOS is included via
# add_subdirectory() in another project (e.g., CMU_HackerFab_Sputtering_Control).
.PHONY: lib-only
lib-only:
	@echo "Building SputterOS library (tests disabled)..."
	@mkdir -p $(BUILD_DIR)
	@cd $(BUILD_DIR) && $(CMAKE) .. -G Ninja -DCMAKE_MAKE_PROGRAM=$(NINJA) -DENABLE_TESTS=OFF && $(NINJA)
	@echo "Library build complete. Output in $(BUILD_DIR)/"

# ────────────────────────────────────────────────────────────────────────────
# STANDALONE TESTING (top-level build)
# ────────────────────────────────────────────────────────────────────────────

# --- Default Goal ---
.PHONY: all
all: help

.PHONY: help
help:
	@echo "SputterOS Build Targets:"
	@echo ""
	@echo "  Library (for use as submodule):"
	@echo "    make lib-only         Build SputterOS library only (no tests)"
	@echo ""
	@echo "  Standalone Testing:"
	@echo "    make test             Run all tests (unit + system + examples)"
	@echo "    make unitTest         Run unit tests (single build tree)"
	@echo "    make systemTest       Run system tests (single build tree)"
	@echo "    make exampleProjects  Run example projects (heartbeat, etc.)"
	@echo "    make testFormal       Generate formal test report (CI/CD)"
	@echo ""
	@echo "  Code Coverage:"
	@echo "    make coverage         Build all tests with Clang coverage, generate reports (llvm-cov)"
	@echo ""
	@echo "  Utilities:"
	@echo "    make format           Format all source files (clang-format)"
	@echo "    make clean            Remove build artifacts"
	@echo "    make help             Show this help message"

# ── Unified test build ───────────────────────────────────────────────────────
# Both unit and system tests share a single CMake build tree under tests/build/.
# GoogleTest is fetched once; SputterOS is compiled once.

# Internal: configure + build the unified test tree (idempotent).
.PHONY: _testBuild
_testBuild:
	@mkdir -p $(TEST_BUILD_DIR)
	@cd $(TEST_BUILD_DIR) && $(CMAKE) $(abspath $(CURDIR)/tests) -G Ninja \
		-DCMAKE_MAKE_PROGRAM=$(NINJA) && $(NINJA)

# Build and run the host-native GoogleTest/CTest unit test suite
.PHONY: unitTest
unitTest: _testBuild
	@echo "Running SputterOS unit tests..."
	@cd $(TEST_BUILD_DIR) && $(CTEST) --output-on-failure --label-exclude "memory|system"
	@echo "Unit tests complete."

# Build and run the host-native system test suite (GoogleTest integration tests)
.PHONY: systemTest
systemTest: _testBuild
	@echo "Running SputterOS system tests..."
	@cd $(TEST_BUILD_DIR) && $(CTEST) --output-on-failure --label-regex system
	@echo "System tests complete."

# Build and run example projects (standalone SputterOS executables)
.PHONY: exampleProjects
exampleProjects:
	@echo "Building SputterOS example projects..."
	@mkdir -p exampleProjects/build
	@cd exampleProjects/build && $(CMAKE) .. -G Ninja -DCMAKE_MAKE_PROGRAM=$(NINJA) && $(NINJA)
	@echo "Running SputterOS example projects..."
	@cd exampleProjects/build && $(CTEST) --output-on-failure
	@echo "Example projects complete."

# Quick test: Build and run all core tests (fast feedback cycle for development)
.PHONY: test
test: unitTest systemTest exampleProjects
	@echo ""
	@echo "All core tests passed!"

# Formal test: Run all tests and generate comprehensive report (ideal for CI/CD)
.PHONY: formalTest
formalTest:
	@echo "===================================="
	@echo "SputterOS Formal Test Generation"
	@echo "===================================="
	@bash Tools/generate_formal_test_results.sh "$(CMAKE)" "$(NINJA)" "$(CTEST)"

# Memory usage: Runs the memory usage script against the Pico build executable.
# Run 'make pico_build' instead of 'make testBuild' for an ELF target.
.PHONY: memoryUsage
memoryUsage:
	@echo "memoryUsage requires a Pico ELF target. Run 'make pico_build' first."
	@echo "Example: python Tools/pico_memory_usage.py <path/to/target.elf>"

# ────────────────────────────────────────────────────────────────────────────
# CODE COVERAGE (LLVM source-based via llvm-cov)
# ────────────────────────────────────────────────────────────────────────────
# Single build tree with Clang instrumentation. GoogleTest is fetched once;
# both unit and system tests produce .profraw files that are merged together.

LLVM_PROFDATA ?= llvm-profdata
LLVM_COV ?= llvm-cov

# Re-use cached GoogleTest from the regular test build when available.
GTEST_SRC_TEST := $(abspath $(CURDIR)/$(TEST_BUILD_DIR)/_deps/googletest-src)
GTEST_SRC_COV  := $(abspath $(CURDIR)/$(TEST_COV_BUILD_DIR)/_deps/googletest-src)

.PHONY: coverage
coverage:
	@echo "========================================"
	@echo "  SputterOS Code Coverage (llvm-cov)"
	@echo "========================================"
	@echo ""
	@echo "[1/6] Configuring tests with Clang + coverage flags..."
	@mkdir -p $(TEST_COV_BUILD_DIR)
	@if [ -d "$(GTEST_SRC_COV)" ]; then \
		echo "  Re-using cached GoogleTest: $(GTEST_SRC_COV)"; \
		cd $(TEST_COV_BUILD_DIR) && $(CMAKE) $(abspath $(CURDIR)/tests) -G Ninja \
			-DCMAKE_MAKE_PROGRAM=$(NINJA) \
			-DCMAKE_CXX_COMPILER=clang++ \
			-DCMAKE_C_COMPILER=clang \
			-DSPUTTEROS_COVERAGE=ON \
			-DFETCHCONTENT_SOURCE_DIR_GOOGLETEST=$(GTEST_SRC_COV) \
			-DFETCHCONTENT_UPDATES_DISCONNECTED=ON; \
	elif [ -d "$(GTEST_SRC_TEST)" ]; then \
		echo "  Re-using cached GoogleTest from test build: $(GTEST_SRC_TEST)"; \
		cd $(TEST_COV_BUILD_DIR) && $(CMAKE) $(abspath $(CURDIR)/tests) -G Ninja \
			-DCMAKE_MAKE_PROGRAM=$(NINJA) \
			-DCMAKE_CXX_COMPILER=clang++ \
			-DCMAKE_C_COMPILER=clang \
			-DSPUTTEROS_COVERAGE=ON \
			-DFETCHCONTENT_SOURCE_DIR_GOOGLETEST=$(GTEST_SRC_TEST) \
			-DFETCHCONTENT_UPDATES_DISCONNECTED=ON; \
	else \
		echo "  No cached GoogleTest found — downloading from GitHub..."; \
		cd $(TEST_COV_BUILD_DIR) && $(CMAKE) $(abspath $(CURDIR)/tests) -G Ninja \
			-DCMAKE_MAKE_PROGRAM=$(NINJA) \
			-DCMAKE_CXX_COMPILER=clang++ \
			-DCMAKE_C_COMPILER=clang \
			-DSPUTTEROS_COVERAGE=ON; \
	fi
	@echo ""
	@echo "[2/6] Building all tests..."
	@cd $(TEST_COV_BUILD_DIR) && $(NINJA)
	@echo ""
	@echo "[3/6] Running all tests (collecting profiles)..."
	@cd $(TEST_COV_BUILD_DIR) && \
		LLVM_PROFILE_FILE="$(abspath $(CURDIR)/$(TEST_COV_BUILD_DIR))/coverage-%p.profraw" \
		$(CTEST) --output-on-failure || true
	@echo ""
	@echo "[4/6] Merging profile data..."
	@$(LLVM_PROFDATA) merge -sparse \
		$(TEST_COV_BUILD_DIR)/coverage-*.profraw \
		-o $(TEST_COV_BUILD_DIR)/coverage.profdata
	@echo ""
	@echo "[5/6] Generating text coverage report..."
	@cd $(TEST_COV_BUILD_DIR) && \
		OBJS=""; \
		for bin in $$(find . -maxdepth 3 -name 'test_*' -type f -executable); do \
			if [ -z "$$OBJS" ]; then OBJS="$$bin"; else OBJS="$$OBJS -object=$$bin"; fi; \
		done && \
		$(LLVM_COV) report \
			$$OBJS \
			-instr-profile=coverage.profdata \
			-ignore-filename-regex='(_deps|googletest|googlemock|tests/)' \
		| tee coverage_report.txt | cat
	@echo ""
	@echo "Text report: $(TEST_COV_BUILD_DIR)/coverage_report.txt"
	@echo ""
	@echo "[6/6] Generating HTML coverage report..."
	@mkdir -p $(TEST_COV_BUILD_DIR)/coverage_html
	@cd $(TEST_COV_BUILD_DIR) && \
		OBJS=""; \
		for bin in $$(find . -maxdepth 3 -name 'test_*' -type f -executable); do \
			if [ -z "$$OBJS" ]; then OBJS="$$bin"; else OBJS="$$OBJS -object=$$bin"; fi; \
		done && \
		$(LLVM_COV) show \
			$$OBJS \
			-instr-profile=coverage.profdata \
			-ignore-filename-regex='(_deps|googletest|googlemock|tests/)' \
			-format=html \
			-output-dir=coverage_html
	@echo ""
	@echo "HTML report: $(TEST_COV_BUILD_DIR)/coverage_html/index.html"

# ────────────────────────────────────────────────────────────────────────────
# UTILITIES
# ────────────────────────────────────────────────────────────────────────────

# Format: Runs clang-format on all relevant sources
.PHONY: format
format:
	@echo "Formatting project files..."
	@find . -type f \( -name "*.cpp" -o -name "*.h" \) -not -path "*/$(BUILD_DIR)/*" -not -path "*/lib/*" -exec clang-format -i {} +
	@echo "Formatting complete."

# Clean: Removes all build directories
.PHONY: clean
clean:
	@echo "Cleaning build artifacts..."
	@rm -rf $(BUILD_DIR)
	@rm -rf $(TEST_BUILD_DIR)
	@rm -rf $(TEST_COV_BUILD_DIR)
	@rm -rf exampleProjects/build
	@echo "Clean complete."