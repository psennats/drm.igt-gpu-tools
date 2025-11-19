# IGT GPU Tools API

**IGT GPU Tools** provides a set of C libraries and helper macros for writing and
running test cases targeting the Linux graphics stack.

Generated documentation is available at:
[API Reference](https://drm.pages.freedesktop.org/igt-gpu-tools/api_reference/index.html)

## Purpose

The API is designed to:

 - Simplify interaction with kernel graphics drivers (e.g., via DRM ioctls)
 - Provide reusable infrastructure for writing test cases
 - Offer abstractions for managing displays, buffers, events, and execution contexts

## Key Components

 - `igt_assert`, `igt_skip`, `igt_require`: Macros for managing test flow
 - `igt_fixture`, `igt_subtest`: Infrastructure for setup/teardown and subtest isolation

Helper modules for:
  - Buffer objects (e.g., `intel_buf`)
  - Display configuration (e.g., `kmstest`, `igt_display`)
  - Memory mapping, fences, command streams, and performance counters

##  Examples

For code examples, browse the `tests/` and `lib/` directories.
