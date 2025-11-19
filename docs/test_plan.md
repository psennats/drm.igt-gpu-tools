# :material-clipboard-text: Test Plan

## :material-information-outline: Overview

IGT GPU Tools implements a comprehensive test plan system to ensure proper documentation
and validation of all tests. The test plan is particularly important for maintaining
quality and consistency across different GPU drivers and test categories.

!!! warning "Build Requirement"
    **Some drivers and test sets require that all tests be properly documented via
    testplan.** By default, the build will fail if documentation is missing or outdated.
    This is currently enabled for:

    - **Xe driver tests**
    - **i915 driver tests**
    - **KMS (Kernel Mode Setting) tests**

For more details, see `docs/test_documentation.md` in the IGT source tree.

## :material-file-document-outline: Test Plan Structure

### Documentation Requirements

Every test that falls under test plan validation must include proper documentation using
the IGT documentation system:

```c
/**
 * TEST: test_name
 * Category: Display|Core|Performance|etc
 * Description: Brief description of what the test validates
 * Sub-category: Specific functional area
 */

/**
 * SUBTEST: subtest_name
 * Description: What this specific subtest validates
 */
```

## :material-cog-outline: Test Plan Validation

### Build-Time Validation

The IGT build system automatically validates test documentation:

```bash
# Build will fail if documentation is missing
meson build
ninja -C build

# Error example:
# ERROR: Test 'kms_example' is missing required documentation
# Please add proper TEST and SUBTEST documentation blocks
```

### Documentation Generation

IGT provides tools to generate and validate test documentation:

```bash
# Generate test documentation
./scripts/igt_doc.py --config=tests/kms_test_config.json

# Filter by functionality
./scripts/igt_doc.py --config=tests/kms_test_config.json \
    --filter-field=Functionality=~'kms_core,synchronization'

# Validate specific test categories
./scripts/igt_doc.py --config=tests/intel_test_config.json \
    --filter-field=Category=Display
```

## :material-script-text-outline: Documentation Examples

### Basic Test Documentation

```c
/**
 * TEST: kms_atomic
 * Category: Display
 * Description: Test atomic modesetting functionality
 * Sub-category: Atomic
 */

/**
 * SUBTEST: plane-cursor-legacy
 * Description: Test interaction between cursor plane and legacy cursor API
 */
igt_subtest("plane-cursor-legacy") {
    igt_describe("Verify cursor plane behavior when mixing atomic and legacy APIs");
    test_cursor_interaction();
}
```

### Advanced Documentation with Dynamic Subtests

```c
/**
 * TEST: kms_pipe_operations
 * Category: Display
 * Description: Validate operations across different display pipes
 * Sub-category: Core
 */

/**
 * SUBTEST: pipe-%s-basic-modeset
 * Description: Basic modeset validation for pipe %arg[1]
 */
igt_subtest_with_dynamic("pipe-basic-modeset") {
    for_each_pipe(display, pipe) {
        igt_dynamic_f("pipe-%s", kmstest_pipe_name(pipe)) {
            igt_describe("Verify basic modeset functionality on a single pipe");
            test_basic_modeset(pipe);
        }
    }
}
```

## :material-check-circle-outline: Test Plan Compliance

### Mandatory Requirements

For Xe, i915, and KMS tests:

1. **Complete Documentation**: Every test and subtest must have proper documentation blocks
2. **Accurate Descriptions**: Descriptions must be implementation-agnostic and focus on
   what is tested
3. **Proper Categorization**: Tests must use established categories and sub-categories
4. **Build Validation**: Documentation is validated at build time

### Best Practices

1. **Descriptive Names**: Use clear, descriptive test and subtest names
2. **Consistent Categories**: Follow established category hierarchies
3. **Implementation-Agnostic**: Avoid describing how tests work, focus on what they
   validate
4. **Regular Updates**: Keep documentation in sync with test changes

### Common Documentation Issues

❌ **Bad Documentation:**
```c
// Too implementation-specific
"spawn 10 threads, each pinning cpu core with a busy loop..."

// Too vague
"test stuff"

// Missing required fields
/**
 * TEST: example
 * Description: Some test
 */
// Missing Category and Sub-category
```

✅ **Good Documentation:**
```c
/**
 * TEST: kms_cursor_legacy
 * Category: Display
 * Description: Test legacy cursor interface functionality and compatibility
 * Sub-category: Cursor
 */

/**
 * SUBTEST: basic-flip-before-cursor
 * Description: Verify cursor updates work correctly after page flips
 */
```


## :material-tools: Validation Tools

### igt_doc.py Script

The main tool for test plan validation and generation:

```bash
# Basic validation
./scripts/igt_doc.py --config=tests/kms_test_config.json

# Generate HTML documentation
./scripts/igt_doc.py --config=tests/kms_test_config.json --output-dir=docs/

# Validate specific functionality
./scripts/igt_doc.py --filter-field=Sub-category=Synchronization

# Check for missing documentation
./scripts/igt_doc.py --validate-only
```

### Configuration Files

Test configurations are stored in JSON files:

- `tests/kms_test_config.json` - KMS test configuration
- `tests/intel_test_config.json` - Intel driver test configuration
- `tests/xe_test_config.json` - Xe driver test configuration

### CI Integration

Test plan validation is integrated into CI systems:

- **Pre-merge testing**: Documentation is validated before patches are merged
- **Post-merge validation**: Ensures documentation stays up-to-date
- **Automated reporting**: Missing documentation triggers build failures


## :material-link: References

- `docs/test_documentation.md` - Detailed documentation requirements
- IGT source tree test files - Examples of proper documentation
- CI system logs - Build validation feedback
- `scripts/igt_doc.py` - Documentation generation tool
