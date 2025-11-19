## :material-play-circle-outline: General

Tests are located in the `tests/` directory.

- List available subtests:

```bash
build/tests/core_auth --list-subtests
```

- Run a specific subtest:

```bash
build/tests/core_auth --run-subtest getclient-simple
```

- If `--run-subtest` is not used, all subtests will be executed.

!!! info "Most tests must be run as **root** and with **no X or Wayland compositor** running."

Example output:

```
# build/tests/core_auth
IGT-Version: 1.24 (x86_64) (Linux: 5.3.0 x86_64)
Starting subtest: getclient-simple
Subtest getclient-simple: SUCCESS (0.001s)
...

```

### Dynamic Subtests

IGT supports dynamic subtests using `igt_subtest_with_dynamic` for cases where the full
set of possible subtests is too large or impossible to know beforehand. Dynamic subtests
are useful for operations like testing each KMS pipe separately.

**Key characteristics of dynamic subtests:**

- Used when reporting several aspects separately is desired
- Useful when the full possible set is too big or impossible to know beforehand
- Each dynamic subtest runs within a parent subtest block
- Results are aggregated: SKIP if no dynamic subtests run, PASS if all pass, FAIL if
  any fail

**Example use case:** Testing KMS pipes where the number of available pipes varies by
hardware:
```c
igt_subtest_with_dynamic("pipe-tests") {
    igt_require(is_kms_available(fd));
    for_each_pipe(display, pipe) {
        igt_dynamic_f("pipe-%s", kmstest_pipe_name(pipe)) {
            test_pipe_operation(pipe);
        }
    }
}
```

**Running dynamic subtests:**

- List dynamic subtests: `--list-subtests` shows both parent and dynamic subtests
- Run specific dynamic subtest: `--run-subtest parent-subtest@dynamic-name`
- Run all dynamic subtests under a parent: `--run-subtest parent-subtest`

**Dynamic subtest naming:**

- Parent subtest name followed by `@` and dynamic name
- Example: `pipe-tests@pipe-A`, `pipe-tests@pipe-B`
- Dynamic names are generated at runtime based on available hardware

**Example listing dynamic subtests:**
```bash
# List all subtests including dynamic ones
build/tests/kms_atomic --list-subtests
atomic-commits
atomic-commits@pipe-A-HDMI-1
atomic-commits@pipe-B-DP-1
atomic-commits@pipe-C-eDP-1
```

**Example running dynamic subtests:**
```bash
# Run all dynamic subtests under atomic-commits
build/tests/kms_atomic --run-subtest atomic-commits

# Run specific dynamic subtest
build/tests/kms_atomic --run-subtest atomic-commits@pipe-A-HDMI-1
```

### Using `run-tests.sh`

You can also use the `scripts/run-tests.sh` wrapper script:

```bash
meson -Drunner=enabled build && ninja -C build
```

Run tests with filters:

```bash
scripts/run-tests.sh -t <regex>       # Include tests matching regex
scripts/run-tests.sh -x <regex>       # Exclude tests matching regex
```

- List all tests and subtests:

```bash
scripts/run-tests.sh -l
```

- Get help on options:

```bash
scripts/run-tests.sh -h
```

Test results are saved as a JSON file.

For the tests API reference please look at: [API Reference Documentation](api_reference/index.html)


## :material-docker: Running via Container

You can run IGT in a container using `podman` or `docker`:

```bash
podman run --rm --privileged registry.freedesktop.org/drm/igt-gpu-tools/igt:master
```

This avoids installing build dependencies locally.
