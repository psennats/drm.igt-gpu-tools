# :material-clipboard-check-outline: General



!!! info "IGT GPU Tools"

    **IGT GPU Tools** is a collection of low-level tools and tests for developing and testing
    DRM(Direct Rendering Manager) drivers.
    IGT focuses on targeted, low-level testing that's easier to build,
    debug, and trace to the kernel.

:material-book-outline: Latest API documentation:
[API Reference](https://drm.pages.freedesktop.org/igt-gpu-tools/api_reference/index.html)

## :material-cog-outline: Requirements

- Fedora users: see `Dockerfile.build-fedora` for package dependencies.
- Debian-based systems: check `Dockerfile.build-debian` or `Dockerfile.build-debian-minimal`.

If IGT is available in your distro's package manager:

```bash
dnf builddep igt-gpu-tools
```

:material-alert-circle-outline: **Note:** Package dependencies may be outdated compared to
`master` branch requirements.

## :material-folder-outline: Directory Overview

This section explains the purpose of each major directory in the IGT GPU Tools project.
Understanding the structure will help new contributors and users navigate the codebase
more effectively.

### :material-file-tree-outline: `tests/`

Contains test cases and subtests – the core of IGT validation.

- Each test is a standalone executable built from a `.c` file.
- Tests follow naming conventions and are managed by `igt_runner`.
- Subtests are organized using `igt_subtest` and described using `igt_describe()`.
- Certain tests may be specific to `i915` or `Xe` drivers and follow respective
  documentation requirements.

### :material-puzzle-outline: `lib/`

Shared helper libraries used across tests.

- Provides wrappers for kernel interactions, memory management, and display configuration.
- Contains useful macros like `igt_assert`, `igt_require`, and `igt_info`.
- Some helpers are hardware-specific (e.g., Intel platform tools).

### :material-tools: `tools/`

Standalone utilities that use IGT libraries but aren't part of the test suite.

- Often used for debugging or GPU state inspection.
- May require root access depending on functionality.

### :material-database-outline: `data/`

Contains test data files and reference materials used by IGT tests.

- **Reference images and frames**: Used for visual comparison tests and CRC validation
- **Test configuration files**: Standard test data and expected outputs for validation
- **Binary test data**: GPU-specific test patterns, EDID data, and hardware configurations
- **Frame dump data**: Reference frames for display testing and Chamelium validation

**Key features:**

- Accessed via `igt_fopen_data(filename)` helper function
- Automatic fallback search: installation directory → build directory → current directory
- Used for frame comparison tests, display validation, and hardware-specific test data
- Essential for tests that require known-good reference data or specific input patterns

**Common use cases:**

```c
// Open a reference frame for comparison
FILE *ref_file = igt_fopen_data("reference_frame_1080p.raw");

// Load test pattern data
FILE *pattern = igt_fopen_data("test_patterns/gradient.bin");
```

### :material-book-open-outline: `docs/`

Contains developer documentation, test plan tools, and API references.

- Some content is generated via `gtk-doc`.
- Tests for `i915` and `Xe` drivers are subject to test plan validation.
  Documentation must be kept in sync – see `docs/test_documentation.md`.

To regenerate:

```bash
ninja -C build igt-gpu-tools-doc
```

### :material-play-circle-outline: `runner/`

The `igt_runner` test harness manages batch execution and filtering.

- Supports result summaries, filtering by subtest, and dry-run modes.
- Used heavily in CI and automation.

### :material-file-document-outline: `include/`

Public headers and UAPI headers:

- Includes types and APIs exposed by IGT.
- Subdirectories such as `drm-uapi/` and `linux-uapi/` are synced from upstream kernel
  headers (e.g., `drm-next`).

### :material-chart-box-outline: `benchmarks/`

Microbenchmarks for evaluating GPU performance.

- Focus on throughput, memory speed, and latency.
- Useful for low-level tuning and regression checks.

### :material-script-text-outline: `scripts/`

Helper scripts for CI, patch formatting, and test orchestration.

- Not always cross-platform.
- Includes the `run-tests.sh` script to simplify test execution.

## :material-console-line: Build Files

- `meson.build`, `configure.ac`, `Makefile.am`: Build system support.
- Autotools and Meson support may coexist but Meson is preferred for modern development.

## :material-lan: Special Testing Scenarios

- **Multi-GPU testing:** Useful for scenarios involving iGPU + dGPU switching.
- **Display testing:** EDID dongles or connected screens required.
- **Power management:** Laptops or devices with sensors give better PM insights.
- **Virtualization:** Run in KVM/QEMU to emulate certain configurations.

## :material-wrench-clock: Optional Tools

- **EDID dongles** – simulate display hotplug events.
- **Serial/UART debug cables** – capture early boot logs.
- **Power meters (RAPL, Watts Up Pro)** – validate runtime PM and energy use.
- **CI hardware** – watchdog-capable systems with remote boot and serial access.
- **Chamelium** - automates external display testing across VGA, HDMI, and DisplayPort
  (DP). More: [External Tools -> Chamelium](chamelium.md) or [The Chromium Projects - Chamelium](https://www.chromium.org/chromium-os/developer-library/guides/hardware-schematics/chamelium/)

## :material-cloud-outline: CI and Development Tips

- Ensure **firmware can be updated** easily on your test hardware.
- Be prepared to **compile and switch kernels** frequently.
- Run tests on a **known-good setup** to rule out config or version issues.

And refer to [ci_infrastructure.md](ci_infrastructure.md), [platforms.md](platforms.md) and to the platform-specific driver documentation for more details.

:material-email-outline: Questions? Reach out via
[igt-dev@lists.freedesktop.org](mailto:igt-dev@lists.freedesktop.org)
