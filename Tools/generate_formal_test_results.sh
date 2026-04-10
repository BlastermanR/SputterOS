#!/bin/bash
#
# generate_formal_test_results.sh
#
# Orchestrates all test suites (unit, system, examples) using the unified
# tests/ build tree and generates a comprehensive FormalTestResults.md document.
#
# Usage:
#   bash generate_formal_test_results.sh [CMAKE_PATH] [NINJA_PATH] [CTEST_PATH]
#
# Exit code: 0 if all tests pass, 1 if any fail.
#

set -e  # Exit on first error

CMAKE="${1:-cmake}"
NINJA="${2:-ninja}"
CTEST="${3:-ctest}"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
RESULTS_FILE="$PROJECT_ROOT/FormalTestResults.md"
TEXT_RESULTS_FILE="$PROJECT_ROOT/FormalTestResults.txt"
TEST_BUILD_DIR="$PROJECT_ROOT/tests/build"

# Detect environment
OS_NAME=$(uname -s)
case "$OS_NAME" in
    MINGW*|MSYS*)
        OS_STRING="Windows (MSYS2)"
        ;;
    Linux)
        OS_STRING="Linux"
        ;;
    Darwin)
        OS_STRING="macOS"
        ;;
    *)
        OS_STRING="Unknown"
        ;;
esac

CXXCOMPILER=$(c++ --version 2>&1 | head -1)
CMAKE_VERSION=$("$CMAKE" --version 2>&1 | head -1)
UNAME_INFO=$(uname -m)
TIMESTAMP=$(date "+%Y-%m-%d %H:%M:%S")

echo "Timestamp: $TIMESTAMP"
echo "OS: $OS_STRING"
echo "Compiler: $CXXCOMPILER"
echo "CMake: $CMAKE_VERSION"
echo "Arch: $UNAME_INFO"
echo ""

# =============================================================================
# Phase 1: Build unified test tree (unit + system)
# =============================================================================

echo "[1/5] Building unified test tree..."
cd "$PROJECT_ROOT"
BUILD_PASS=1

mkdir -p "$TEST_BUILD_DIR"
cd "$TEST_BUILD_DIR"

if "$CMAKE" "$PROJECT_ROOT/tests" -G Ninja -DCMAKE_MAKE_PROGRAM="$NINJA" > /tmp/sputteros_cmake.log 2>&1; then
    :
else
    BUILD_PASS=0
fi

if "$NINJA" >> /tmp/sputteros_cmake.log 2>&1; then
    :
else
    BUILD_PASS=0
fi

# =============================================================================
# Phase 2: Run unit tests
# =============================================================================

echo "[2/5] Running unit tests..."
UNIT_TEST_PASS=1
UNIT_TEST_OUTPUT=""

if [ $BUILD_PASS -eq 1 ]; then
    UNIT_TEST_OUTPUT=$("$CTEST" --output-on-failure --label-exclude "memory|system" 2>&1 || echo "FAILED")
else
    UNIT_TEST_OUTPUT="Build failed (see CMake log)"
fi
UNIT_TEST_SUMMARY=$(printf '%s\n' "$UNIT_TEST_OUTPUT" | tail -n 15)
UNIT_TEST_COUNT=$(printf '%s\n' "$UNIT_TEST_OUTPUT" | grep -oP 'out of \K[0-9]+' | head -1)
if [ -z "$UNIT_TEST_COUNT" ]; then
    UNIT_TEST_COUNT="N/A"
fi

# =============================================================================
# Phase 3: Run system tests
# =============================================================================

echo "[3/5] Running system tests..."
SYSTEM_TEST_OUTPUT=""

if [ $BUILD_PASS -eq 1 ]; then
    SYSTEM_TEST_OUTPUT=$("$CTEST" --output-on-failure --label-regex system 2>&1 || echo "FAILED")
else
    SYSTEM_TEST_OUTPUT="Build failed (see CMake log)"
fi
SYSTEM_TEST_SUMMARY=$(printf '%s\n' "$SYSTEM_TEST_OUTPUT" | tail -n 15)
SYSTEM_TEST_COUNT=$(printf '%s\n' "$SYSTEM_TEST_OUTPUT" | grep -oP 'out of \K[0-9]+' | head -1)
if [ -z "$SYSTEM_TEST_COUNT" ]; then
    SYSTEM_TEST_COUNT="N/A"
fi

# =============================================================================
# Phase 3b: Run example projects
# =============================================================================

echo "[4/5] Running example projects..."
cd "$PROJECT_ROOT"
EXAMPLE_TEST_PASS=1

mkdir -p exampleProjects/build
cd exampleProjects/build

if "$CMAKE" .. -G Ninja -DCMAKE_MAKE_PROGRAM="$NINJA" > /tmp/sputteros_example_cmake.log 2>&1; then
    :
else
    EXAMPLE_TEST_PASS=0
fi

if "$NINJA" >> /tmp/sputteros_example_cmake.log 2>&1; then
    :
else
    EXAMPLE_TEST_PASS=0
fi

if [ $EXAMPLE_TEST_PASS -eq 1 ]; then
    EXAMPLE_TEST_OUTPUT=$("$CTEST" --output-on-failure 2>&1 || echo "FAILED")
else
    EXAMPLE_TEST_OUTPUT="Build failed (see CMake log)"
fi
EXAMPLE_TEST_SUMMARY=$(printf '%s\n' "$EXAMPLE_TEST_OUTPUT" | tail -n 8)
EXAMPLE_TEST_COUNT=$(printf '%s\n' "$EXAMPLE_TEST_OUTPUT" | grep -oP 'out of \K[0-9]+' | head -1)
if [ -z "$EXAMPLE_TEST_COUNT" ]; then
    EXAMPLE_TEST_COUNT="N/A"
fi

# Library size
LIBRARY_FILE="$TEST_BUILD_DIR/SputterOS_lib/libSputterOS.a"
if [ -f "$LIBRARY_FILE" ]; then
    LIBRARY_BYTES=$(stat -c%s "$LIBRARY_FILE" 2>/dev/null || stat -f%z "$LIBRARY_FILE")
    if command -v numfmt >/dev/null 2>&1; then
        LIBRARY_SIZE=$(numfmt --to=iec --suffix=B "$LIBRARY_BYTES")
    else
        LIBRARY_SIZE="$LIBRARY_BYTES bytes"
    fi
else
    LIBRARY_SIZE="Unknown"
fi

# =============================================================================
# Phase 4: Generate Report
# =============================================================================

cd "$PROJECT_ROOT"
echo ""
echo "[5/5] Generating formal test report: $RESULTS_FILE"

# Determine statuses
if echo "$UNIT_TEST_OUTPUT" | grep -q "100% tests passed" 2>/dev/null; then
    UNIT_STATUS="✓ PASS"
    UNIT_PASS=1
else
    UNIT_STATUS="✗ FAIL"
    UNIT_PASS=0
fi

if echo "$SYSTEM_TEST_OUTPUT" | grep -q "100% tests passed" 2>/dev/null; then
    SYSTEM_STATUS="✓ PASS"
    SYSTEM_PASS=1
else
    SYSTEM_STATUS="✗ FAIL"
    SYSTEM_PASS=0
fi

if echo "$EXAMPLE_TEST_OUTPUT" | grep -q "100% tests passed" 2>/dev/null; then
    EXAMPLE_STATUS="✓ PASS"
else
    EXAMPLE_STATUS="✗ FAIL"
    EXAMPLE_TEST_PASS=0
fi

ALL_PASS=0
if [ "$BUILD_PASS" -eq 1 ] && [ "$UNIT_PASS" -eq 1 ] && [ "$SYSTEM_PASS" -eq 1 ] && [ "$EXAMPLE_TEST_PASS" -eq 1 ]; then
    ALL_PASS=1
fi

cat > "$RESULTS_FILE" << EOF
# SputterOS Formal Test Results

**Generated:** $TIMESTAMP  
**OS:** $OS_STRING  
**Architecture:** $UNAME_INFO  
**Compiler:** $CXXCOMPILER  
**CMake:** $CMAKE_VERSION  

---

## Summary

EOF

if [ "$ALL_PASS" -eq 1 ]; then
    cat >> "$RESULTS_FILE" << 'EOF'
**Overall Result: ✓ PASS**

All test suites completed successfully. This build is safe for integration.

EOF
else
    cat >> "$RESULTS_FILE" << 'EOF'
**Overall Result: ✗ FAIL**

One or more test suites failed. See details below.

EOF
fi

cat >> "$RESULTS_FILE" << 'EOF'
| Suite | Status | Count | Details |
|---|---|---|---|
EOF

echo "| Unit Tests (GoogleTest) | $UNIT_STATUS | $UNIT_TEST_COUNT | Core logic, HAL/OSAL interfaces |" >> "$RESULTS_FILE"
echo "| System Tests (GoogleTest) | $SYSTEM_STATUS | $SYSTEM_TEST_COUNT | End-to-end kernel integration |" >> "$RESULTS_FILE"
echo "| Example Projects | $EXAMPLE_STATUS | $EXAMPLE_TEST_COUNT | Standalone SputterOS executables |" >> "$RESULTS_FILE"
echo "| SputterOS Library | ✓ SIZE | $LIBRARY_SIZE | Static archive size |" >> "$RESULTS_FILE"

# =============================================================================
# Unit Test Details
# =============================================================================

cat >> "$RESULTS_FILE" << 'EOF'

---

## Unit Tests (GoogleTest Suite)

EOF

if [ "$UNIT_PASS" -eq 1 ]; then
    cat >> "$RESULTS_FILE" << 'EOF'
**Status:** ✓ PASS

Output:
\`\`\`
EOF
    echo "$UNIT_TEST_SUMMARY" >> "$RESULTS_FILE" 2>/dev/null || true
    cat >> "$RESULTS_FILE" << 'EOF'
```

EOF
else
    cat >> "$RESULTS_FILE" << 'EOF'
**Status:** ✗ FAIL

Output:
\`\`\`
EOF
    echo "$UNIT_TEST_OUTPUT" >> "$RESULTS_FILE" 2>/dev/null || true
    cat >> "$RESULTS_FILE" << 'EOF'
```

EOF
fi

# =============================================================================
# System Test Details
# =============================================================================

cat >> "$RESULTS_FILE" << 'EOF'

---

## System Tests (GoogleTest Integration)

EOF

if [ "$SYSTEM_PASS" -eq 1 ]; then
    cat >> "$RESULTS_FILE" << 'EOF'
**Status:** ✓ PASS

Output:
\`\`\`
EOF
    echo "$SYSTEM_TEST_SUMMARY" >> "$RESULTS_FILE" 2>/dev/null || true
    cat >> "$RESULTS_FILE" << 'EOF'
```

EOF
else
    cat >> "$RESULTS_FILE" << 'EOF'
**Status:** ✗ FAIL

Output:
\`\`\`
EOF
    echo "$SYSTEM_TEST_OUTPUT" >> "$RESULTS_FILE" 2>/dev/null || true
    cat >> "$RESULTS_FILE" << 'EOF'
```

EOF
fi

# =============================================================================
# Example Projects Details
# =============================================================================

cat >> "$RESULTS_FILE" << 'EOF'

---

## Example Projects

EOF

if [ "$EXAMPLE_TEST_PASS" -eq 1 ]; then
    cat >> "$RESULTS_FILE" << 'EOF'
**Status:** ✓ PASS

Output:
\`\`\`
EOF
    echo "$EXAMPLE_TEST_SUMMARY" >> "$RESULTS_FILE" 2>/dev/null || true
    cat >> "$RESULTS_FILE" << 'EOF'
```

EOF
else
    cat >> "$RESULTS_FILE" << 'EOF'
**Status:** ✗ FAIL

Output:
\`\`\`
EOF
    echo "$EXAMPLE_TEST_OUTPUT" >> "$RESULTS_FILE" 2>/dev/null || true
    cat >> "$RESULTS_FILE" << 'EOF'
```

EOF
fi

# =============================================================================
# Build Log (if failed)
# =============================================================================

if [ "$BUILD_PASS" -eq 0 ]; then
    cat >> "$RESULTS_FILE" << 'EOF'

---

## Build Log (FAILED)

\`\`\`
EOF
    cat /tmp/sputteros_cmake.log >> "$RESULTS_FILE" 2>/dev/null || echo "Log unavailable" >> "$RESULTS_FILE"
    cat >> "$RESULTS_FILE" << 'EOF'
```

EOF
fi

# =============================================================================
# Cleanup & Text Report
# =============================================================================

rm -f /tmp/sputteros_cmake.log /tmp/sputteros_example_cmake.log

OVERALL_STATUS="✓ PASS"
if [ "$ALL_PASS" -eq 0 ]; then
    OVERALL_STATUS="✗ FAIL"
fi

cat > "$TEXT_RESULTS_FILE" << EOF
SputterOS Formal Test Results

Generated: $TIMESTAMP
OS: $OS_STRING
Architecture: $UNAME_INFO
Compiler: $CXXCOMPILER
CMake: $CMAKE_VERSION

Overall Result: $OVERALL_STATUS

Suites:
- Unit Tests (GoogleTest): $UNIT_STATUS, $UNIT_TEST_COUNT tests
- System Tests (GoogleTest): $SYSTEM_STATUS, $SYSTEM_TEST_COUNT tests
- Example Projects: $EXAMPLE_STATUS, $EXAMPLE_TEST_COUNT tests
- SputterOS Library: $LIBRARY_SIZE

Unit Tests Summary:
$UNIT_TEST_SUMMARY

System Tests Summary:
$SYSTEM_TEST_SUMMARY

Example Projects Summary:
$EXAMPLE_TEST_SUMMARY
EOF

echo "Report written to: $RESULTS_FILE"
echo "Editor-friendly report written to: $TEXT_RESULTS_FILE"
echo "===================================="

if [ "$ALL_PASS" -eq 1 ]; then
    echo "✓ All tests PASSED"
    exit 0
else
    echo "✗ Some tests FAILED"
    exit 1
fi

