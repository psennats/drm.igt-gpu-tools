# IGT GPU Tools - Copilot Coding Agent Instructions

## Repository Overview

**IGT GPU Tools** is a collection of tools for development and testing of DRM (Direct Rendering Manager) drivers. This is a large C codebase (~772 C files, ~304 headers) primarily focused on low-level GPU driver testing for Linux.

- **Primary Language**: C (gnu11 standard)
- **Build System**: Meson + Ninja
- **Main Components**: 
  - `tests/` - 429 test files including 250 Intel-specific tests
  - `lib/` - Core IGT library (~200+ files)
  - `tools/` - Debugging utilities
  - `benchmarks/` - Performance benchmarks
  - `runner/` - Test execution framework

## Critical Build Information

### Build Requirements

**IMPORTANT**: Building requires specific system dependencies. Reference these Dockerfiles for complete dependency lists:
- Fedora: `Dockerfile.build-fedora`
- Debian: `Dockerfile.build-debian` or `Dockerfile.build-debian-minimal`

Key dependencies: libdrm, pciaccess, libkmod, libproc2 (or libprocps), libdw, pixman, cairo, libudev, glib-2.0

### Build Commands

**ALWAYS use this exact sequence:**

```bash
# Setup (first time only)
meson setup build

# Build (use one of these)
ninja -C build                           # Standard build
ninja -C build -j4                       # Parallel build with 4 jobs
ninja -C build -j1                       # Single-threaded (fallback if parallel fails)

# The CI uses this pattern - try parallel first, fallback to single-threaded:
ninja -C build -j${FDO_CI_CONCURRENT:-4} || ninja -C build -j 1
```

**Common build options:**
```bash
meson setup -Dtests=disabled build       # Disable tests
meson setup -Ddocs=disabled build        # Disable docs
meson setup -Drunner=disabled build      # Disable test runner
meson setup -Dchamelium=disabled build   # Disable Chamelium tests
meson setup -Dlibdrm_drivers= build      # Minimal libdrm support
```

**Reconfigure after meson_options.txt changes:**
```bash
ninja -C build reconfigure
```

### Running Tests

**Unit tests (lib/tests/ and tests/):**
```bash
ninja -C build test
# Or with parallelism:
meson test -C build --num-processes 4
```

**Integration tests (requires root, no X/Wayland):**
```bash
# Run single test:
sudo build/tests/core_auth

# Run with subtest filter:
sudo build/tests/core_auth --run-subtest getclient-simple

# List available subtests:
build/tests/core_auth --list-subtests

# Run full test suite:
meson -Drunner=enabled build && ninja -C build
./scripts/run-tests.sh -T my.testlist
```

### Documentation Building

```bash
ninja -C build igt-gpu-tools-doc
```

**CRITICAL**: Documentation building enforces test documentation requirements for i915, Xe drivers, and KMS tests. Build will FAIL if tests are undocumented. See `docs/test_documentation.md` for details.

## Code Style and Validation

### Mandatory Code Style

- **Follow Linux Kernel coding style**: https://www.kernel.org/doc/html/latest/process/coding-style.html
- **Indentation**: Tabs (width 8) for C files
- **Line length**: Max 100 characters (enforced by checkpatch)
- **Subtests naming**: Use minus signs (-) as word separators

### Pre-commit Validation

**ALWAYS run checkpatch.pl before submitting:**

```bash
# Download checkpatch.pl from Linux kernel if not available
# Then run with IGT-specific options (from .checkpatch.conf):
checkpatch.pl --emacs --strict --show-types --max-line-length=100 \
  --ignore=BIT_MACRO,SPLIT_STRING,LONG_LINE_STRING,BOOL_MEMBER,PREFER_KERNEL_TYPES \
  <your-patch-file>
```

**Test your changes match existing patterns:**
```bash
# Use semantic patch to verify IGT idioms:
spatch --sp-file lib/igt.cocci --in-place tests/<your_test>.c
```

### Required Test Documentation

**ALL new tests must use `igt_describe()` or testplan markup:**

```c
/**
 * TEST: Test name
 * Description: What this test does
 *
 * SUBTEST: subtest-name
 * Description: What this subtest does
 */
```

For i915/Xe/KMS tests, use full testplan format (see `docs/test_documentation.md`).

## Common Pitfalls and Workarounds

### Build Errors

**"Dependency libdrm not found"**: Install libdrm development packages or use minimal build:
```bash
meson setup -Dtests=disabled -Dlibdrm_drivers="" build
```

**"Either libprocps or libproc2 is required"**: Install `libproc2-dev` (Debian) or `libproc2-devel` (Fedora).

**Parallel build failures**: The CI automatically falls back to single-threaded:
```bash
ninja -C build -j4 || ninja -C build -j 1
```

**Documentation build failures**: Missing test documentation. Check which tests need docs:
```bash
.gitlab-ci/list_undocumented_tests.py
```

### Test-Related Issues

**Tests must run as root**: Most driver tests require root privileges and no X/Wayland running.

**Cross-compilation**: Test list generation and documentation are disabled. Use provided cross-files:
- `meson-cross-arm64.txt`
- `meson-cross-armhf.txt`  
- `meson-cross-mips.txt`

Example: `meson setup --cross-file meson-cross-arm64.txt build`

## Project Structure

### Key Directories
- `tests/` - Test suite (core tests, driver-specific subdirs)
  - `tests/intel/` - Intel GPU tests
  - `tests/amdgpu/` - AMD GPU tests
  - `tests/chamelium/` - Chamelium display tests
- `lib/` - Core IGT library
  - `lib/igt_*.c` - IGT framework functions
  - `lib/i915/` - Intel-specific library code
  - `lib/amdgpu/` - AMD-specific library code
- `tools/` - Debugging tools (must run as root)
- `benchmarks/` - Performance microbenchmarks
- `runner/` - Test execution framework (`igt_runner`)
- `scripts/` - Helper scripts
  - `scripts/run-tests.sh` - Main test runner script
  - `scripts/code_cov_*` - Code coverage scripts
- `docs/` - Documentation source
- `include/drm-uapi/` - Imported DRM kernel headers
- `man/` - Manual pages

### Configuration Files
- `meson.build` - Main build configuration
- `meson_options.txt` - Build options (85 lines defining features)
- `.checkpatch.conf` - Checkpatch configuration
- `.editorconfig` - Editor settings (tabs, width 8)
- `.gitlab-ci.yml` - CI/CD pipeline (primary validation)

### Important Build Artifacts
- `build/` - Build output directory (created by meson)
- `build/tests/` - Compiled test binaries
- `build/tests/test-list.txt` - Generated list of all tests

## CI/CD Pipeline

GitLab CI runs multiple validation stages:

1. **Build containers**: Debian/Fedora/ARM container builds
2. **Build stage**: 
   - Fedora build with full options
   - Debian builds (standard + minimal)
   - ARM cross-builds (armhf, arm64)
   - Clang build
3. **Test stage**: 
   - `meson test` with 4 parallel jobs
   - Multiple configurations tested

**Key CI patterns to replicate locally:**
- Parallel build with fallback: `ninja -C build -j4 || ninja -C build -j 1`
- Test with parallelism: `meson test -C build --num-processes 4`
- Build docs: `ninja -C build igt-gpu-tools-doc`

## Development Workflow Best Practices

1. **Start with minimal build** if dependencies are missing:
   ```bash
   meson setup -Dtests=disabled -Ddocs=disabled build
   ```

2. **Always validate with checkpatch** before committing.

3. **Document new tests** using `igt_describe()` or testplan format.

4. **Test incrementally**: Build after small changes, don't accumulate.

5. **For library changes**: Verify at least 2 users exist (or likely future users).

6. **For test changes**: Avoid `igt_assert/igt_require` in library functions; create `__function()` variants without them.

7. **Check test list**: After adding tests, verify they appear in `build/tests/test-list.txt`.

## Useful Commands Reference

```bash
# Quick build check
./meson.sh

# Clean build
ninja -C build clean

# Install locally
ninja -C build install

# Cross-compilation
meson setup --cross-file meson-cross-arm64.txt build

# Run specific test pattern
./scripts/run-tests.sh -t "pattern"

# Code coverage (requires gcov-enabled kernel)
./scripts/run-tests.sh -c code_cov_capture -k ~/linux -P

# Generate documentation
ninja -C build igt-gpu-tools-doc
```

## Trust These Instructions

These instructions are comprehensive and validated. Only search for additional information if:
- Instructions are incomplete for your specific task
- You encounter an error not documented here
- You need driver-specific details not covered

For build/test issues, check the Dockerfiles and `.gitlab-ci.yml` first before extensive searching.
