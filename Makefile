# Makefile for SputterOS
#
# Requires: cmake, ninja, g++ or clang, clang-format, clang-tidy, doxygen, graphviz
# See DEPENDENCIES.md for full install instructions.


# --- Project Paths ---
BUILD_DIR ?= build
TEST_BUILD_DIR ?= tests/build
TEST_COV_BUILD_DIR ?= tests/build_coverage
CMAKE ?= cmake
CTEST ?= ctest
NINJA ?= ninja
DOXYGEN ?= doxygen
CLANG_TIDY ?= clang-tidy

# ────────────────────────────────────────────────────────────────────────────
# LIBRARYONLY BUILD (for use as a submodule in parent projects)
# ────────────────────────────────────────────────────────────────────────────
# Build SputterOS library without tests. Used when SputterOS is included via
# add_subdirectory() in another project (e.g., CMU_HackerFab_Sputtering_Control).
.PHONY: libOnly
libOnly:
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
	@echo "    make formalTest       Generate formal test report (CI/CD)"
	@echo ""
	@echo "  Code Coverage:"
	@echo "    make coverage         Build all tests with Clang coverage, generate reports (llvm-cov)"
	@echo ""
	@echo "  Static Analysis:"
	@echo "    make tidy             Run clang-tidy on library sources"
	@echo ""
	@echo "  Documentation:"
	@echo "    make docs             Generate Doxygen API docs (HTML + graphs)"
	@echo "    make cleanDocs        Remove generated documentation"
	@echo ""
	@echo "  Utilities:"
	@echo "    make format           Format all source files (clang-format)"
	@echo "    make memoryUsage TARGET=<path>  Report text/data/bss size of any executable"
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
	@bash tools/scripts/generate_formal_test_results.sh "$(CMAKE)" "$(NINJA)" "$(CTEST)"

# Memory usage: Reports text/data/bss section sizes for any compiled executable.
# Usage: make memoryUsage TARGET=<path/to/executable>
TARGET ?=
.PHONY: memoryUsage
memoryUsage:
	@if [ -z "$(TARGET)" ]; then \
		echo "Usage: make memoryUsage TARGET=<path/to/executable>"; \
		exit 1; \
	fi
	@if [ ! -f "$(TARGET)" ]; then \
		echo "Error: file not found: $(TARGET)"; \
		exit 1; \
	fi
	@echo "Memory usage for: $(TARGET)"
	@size "$(TARGET)"

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
	@find $(TEST_COV_BUILD_DIR) -maxdepth 1 -name 'coverage-*.profraw' -delete
	@cd $(TEST_COV_BUILD_DIR) && \
		LLVM_PROFILE_FILE="$(abspath $(CURDIR)/$(TEST_COV_BUILD_DIR))/coverage-%p.profraw" \
		$(CTEST) --output-on-failure || true
	@echo ""
	@echo "[4/6] Merging profile data..."
	@find $(TEST_COV_BUILD_DIR) -maxdepth 1 -name 'coverage-*.profraw' \
		> $(TEST_COV_BUILD_DIR)/profraw_list.txt
	@$(LLVM_PROFDATA) merge -sparse \
		-f $(TEST_COV_BUILD_DIR)/profraw_list.txt \
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
		2>&1 | grep -v 'functions have mismatched data' | tee coverage_report.txt | cat
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
			-output-dir=coverage_html \
		2>&1 | grep -v 'functions have mismatched data' >&2 || true
	@echo ""
	@echo "HTML report: $(TEST_COV_BUILD_DIR)/coverage_html/index.html"

# ────────────────────────────────────────────────────────────────────────────
# UTILITIES
# ────────────────────────────────────────────────────────────────────────────

# ────────────────────────────────────────────────────────────────────────────
# STATIC ANALYSIS (clang-tidy)
# ────────────────────────────────────────────────────────────────────────────
# Runs clang-tidy on all library sources against the unified test build's
# compile_commands.json. Requires a prior test build (auto-triggered).

.PHONY: tidy
tidy: _testBuild
	@echo "Running clang-tidy on SputterOS library sources..."
	@find include/sputteros -type f -name '*.h' | sort | xargs -I{} \
		$(CLANG_TIDY) -p $(TEST_BUILD_DIR) {} -- -std=c++17 \
		-I$(abspath include) 2>&1 | tee $(TEST_BUILD_DIR)/clang-tidy-report.txt
	@find src -type f -name '*.cpp' | sort | xargs -I{} \
		$(CLANG_TIDY) -p $(TEST_BUILD_DIR) {} -- -std=c++17 \
		-I$(abspath include) 2>&1 | tee -a $(TEST_BUILD_DIR)/clang-tidy-report.txt
	@echo ""
	@echo "clang-tidy report: $(TEST_BUILD_DIR)/clang-tidy-report.txt"
	@echo "clang-tidy complete."

# ────────────────────────────────────────────────────────────────────────────
# DOCUMENTATION (Doxygen + Graphviz)
# ────────────────────────────────────────────────────────────────────────────

.PHONY: docs
docs:
	@echo "Generating SputterOS API documentation..."
	@$(DOXYGEN) Doxyfile
	@echo ""
	@echo "Documentation generated: docs/api/html/index.html"

.PHONY: cleanDocs
cleanDocs:
	@echo "Removing generated documentation..."
	@rm -rf docs/api
	@echo "Documentation removed."

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
	@rm -rf docs/api
	@echo "Clean complete."