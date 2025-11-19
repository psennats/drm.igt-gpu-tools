## :material-hammer-wrench: Tests

To get started quickly and build tests:

```bash
meson setup build && ninja -C build
```

!!! info "Meson requires that builds be done in a separate directory from the source tree!"

To run self-tests from `lib/tests` and `tests/`:

```bash
ninja -C build test
```
## :material-book: Documentation

To build the documentation:

```bash
ninja -C build igt-gpu-tools-doc
```

!!! info "Missing documentation for a new test?"
    Some drivers (e.g., Xe, i915) and KMS tests require proper documentation in a
    **test plan**. The build will fail if documentation is missing! See
    [`test_documentation.md`](test_documentation.md) for details.
