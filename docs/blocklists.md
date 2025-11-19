# :material-block-helper: Blocklists

## :material-information-outline: Overview

Blocklists in IGT GPU Tools are essential mechanisms for
excluding problematic, unstable, or inappropriate tests from automated CI runs. They help
maintain CI stability by filtering out tests that are known to cause issues, while still
preserving the tests in the codebase for manual execution or future fixing.

!!! info "Purpose"
    Blocklists allow CI systems to run comprehensive test suites while automatically
    excluding tests that:

    - Are known to be flaky or unstable
    - Require specific hardware configurations not available in CI
    - Are under development and not ready for production testing
    - Cause system hangs or crashes
    - Coverage for older generations—not in active use but required to maintain
      historical continuity.

## :material-file-outline: Blocklist Files

### Driver-Specific Blocklists

IGT maintains separate blocklists for different GPU drivers and testing scenarios:

| File | Purpose | Usage |
|||--|
| **`blocklist.txt`** | i915 driver test exclusions | Full IGT runs on i915 hardware |
| **`xe.blocklist.txt`** | Xe driver test exclusions | Full IGT runs on Xe hardware |
| **Test lists** | Positive inclusion lists | BAT and fast-feedback testing |

### File Locations

```
tests/intel-ci/
├── blocklist.txt              # i915 driver blocklist
├── xe.blocklist.txt          # Xe driver blocklist
├── fast-feedback.testlist    # i915 BAT test list
└── xe-fast-feedback.testlist # Xe BAT test list
```

## :material-format-text: Blocklist Format and Syntax

### Basic Pattern Matching

Blocklists use pattern matching to exclude tests flexibly:

```bash
# Exact test match
igt@i915_module_load@load

# Wildcard patterns
igt@gem_caching@.*              # All gem_caching subtests
igt@kms_prime@.*               # All kms_prime subtests
igt@prime_vgem@coherency-.*    # All coherency subtests

# Binary-level exclusions
igt@gem_workarounds@.*         # Entire gem_workarounds test binary
```

### Pattern Types

| Pattern | Description | Example |
||-||
| **Exact Match** | Specific test/subtest | `igt@core_auth@getclient-simple` |
| **Wildcard (.*)**| All subtests in a test | `igt@gem_exec_nop@.*` |
| **Partial Match** | Tests matching prefix | `igt@kms_.*@basic` |
| **Regex Support** | Advanced pattern matching | `igt@.*@.*-suspend` |

### Comment Syntax

```bash
# Full line comments start with #
igt@problematic_test@.*

# Inline comments after patterns
igt@flaky_test@.* # Known to be unstable

############################################
# Section separators for organization
############################################
```

## :material-cog: Driver-Specific Blocklists

### i915 Driver Blocklist (`blocklist.txt`)

**Purpose:** Exclude tests from Full IGT runs on i915 hardware

**Common Exclusion Categories:**

```bash
# Hardware-specific exclusions
igt@gem_caching@.*              # Requires specific cache coherency
igt@gem_userptr_blits@.*        # Userptr functionality issues

# Display-specific exclusions
igt@kms_cursor_legacy@.*        # Legacy cursor compatibility
igt@kms_scaling_modes@.*        # Scaling mode limitations

# Performance/stability exclusions
igt@gem_exec_whisper@.*         # Resource-intensive stress tests
igt@gem_concurrent_blit@.*      # Concurrent operation stability
```

### Xe Driver Blocklist (`xe.blocklist.txt`)

**Purpose:** Exclude tests from Full IGT runs on Xe hardware

**Xe-Specific Exclusions:**

```bash
# KMS tests requiring i915-specific features
igt@kms_prime@.*               # Prime buffer sharing limitations
igt@kms_cursor_legacy@.*       # Legacy API compatibility

# Memory management differences
igt@gem_caching@.*             # Different memory architecture
igt@gem_mmap_gtt@.*           # GTT mapping not applicable

# SR-IOV test management
igt@sriov_basic@.*            # Controlled SR-IOV test execution
```

## :material-lightning-bolt: Dynamic Blocklist Management

### Adding Tests to Blocklists

**For systematic exclusions:**
```bash
# Include issue tracking and reasoning
# In blocklist file:
############################################
# Blocked due to hardware incompatibility
# See: https://gitlab.freedesktop.org/drm/intel/-/issues/XXXX
############################################
igt@hardware_specific_test@.*
```

!!! warning "Use this for exceptional use-cases only"

**For urgent CI stability:**
```bash
# Add to appropriate blocklist file
echo "igt@problematic_test@.*" >> tests/intel-ci/blocklist.txt

# Commit with clear justification
git commit -m "intel-ci: Blocklist problematic_test due to [issue]"
```


### Removing Tests from Blocklists

**When issues are resolved:**

1. **Verify fix** - Ensure the underlying issue is resolved
2. **Test locally** - Run the test on affected hardware
3. **Remove from blocklist** - Delete the line from appropriate file
4. **Monitor CI** - Watch for regressions after removal

## :material-timeline-check: Blocklist Categories

### By Test Stability

| Category | Description | Examples |
|-|-|-|
| **Flaky Tests** | Intermittent failures | Tests with timing dependencies |
| **Hardware-Specific** | Requires unavailable hardware | Specific GPU generations only |
| **Environment-Dependent** | Needs special configuration | Display connection requirements |
| **Under Development** | Work-in-progress features | New functionality testing |

### By Impact Level

| Level | Description | Action |
|-|-|--|
| **Critical** | Causes system hangs/crashes | Immediate blocklisting |
| **High** | Consistent CI failures | Blocklist until fixed |
| **Medium** | Intermittent issues | Monitor and consider blocklisting |
| **Low** | Minor failures | Keep in CI with issue tracking |

## :material-chart-timeline: CI Integration

### How Blocklists Are Applied

**During CI Execution:**

1. **Test Discovery** - CI enumerates all available IGT tests
2. **Blocklist Filtering** - Excludes tests matching blocklist patterns
3. **Test Execution** - Runs remaining tests in filtered set
4. **Result Reporting** - Reports only on tests that were attempted

**CI Run Types:**

| Run Type | Blocklist Used | Test Scope |
|-|-||
| **BAT (Basic Acceptance)** | Test lists (positive) | Curated fast tests |
| **Full IGT** | Blocklists (negative) | All tests minus blocked |
| **Idle Runs** | Blocklists (negative) | Extended coverage |

### Integration with Intel GFX CI

**Automatic Application:**

- **i915 Full IGT:** All IGT tests filtered by `blocklist.txt`
- **Xe Full IGT:** All IGT tests filtered by `xe.blocklist.txt`
- **Cross-platform:** Same blocklist patterns across different hardware

**Result Filtering:**

- Blocked tests don't appear in CI results
- No false positives from known problematic tests
- Focus on real regressions and new issues

## :material-bug: Issue Tracking Integration

### Linking Blocklists to Issues

**Best Practices:**
```bash
# Include issue references in blocklist comments
############################################
# Blocked due to regression in foo feature
# See: https://gitlab.freedesktop.org/drm/intel/-/issues/1234
# Remove when fixed
############################################
igt@affected_test@.*
```

**Issue Lifecycle Management:**

1. **Issue Reported** - Problem identified in CI or manual testing
2. **Temporary Blocklist** - Add to blocklist to stabilize CI
3. **Issue Investigation** - Development team investigates root cause
4. **Fix Implementation** - Code changes to resolve issue
5. **Blocklist Removal** - Remove from blocklist after verification
6. **Monitoring** - Watch for regressions

### Automated Issue Tracking

**CI Bug Log Integration:**

- Links blocklist entries to GitLab issues
- Tracks issue lifecycle and resolution
- Automatic notifications when issues are resolved
- Historical data on blocklist effectiveness

## :material-wrench: Maintenance and Best Practices

### Regular Maintenance Tasks

**Periodic Review:**
```bash
# Review blocklist contents quarterly
# Check for resolved issues
# Remove outdated entries
# Update issue references
```

**Validation Procedures:**

1. **Test Removal Candidates** - Try removing old entries
2. **Hardware Updates** - Review when new hardware is added
3. **Driver Updates** - Check blocklists after major driver changes
4. **CI Infrastructure Changes** - Verify patterns after CI updates

### Best Practices for Contributors

**Adding Blocklist Entries:**

- ✅ Include clear justification and issue references
- ✅ Use minimal patterns (specific as possible)
- ✅ Add comments explaining the exclusion reason
- ✅ Link to tracking issues when available

**Pattern Guidelines:**

- Use `.*` for all subtests in a test binary
- Use specific subtest names when only some subtests fail
- Avoid overly broad patterns that exclude working tests
- Test patterns locally before committing

**Documentation Requirements:**
```bash
# Good blocklist entry:
############################################
# gem_caching tests require coherent memory
# Not supported on DG2 hardware - see issue #1234
############################################
igt@gem_caching@.*

# Poor blocklist entry:
igt@gem_caching@.*  # broken
```

## :material-file-code: Example Blocklist Configurations

### Complete i915 Blocklist Example

```bash
############################################
# i915 Driver Test Blocklist
# Last Updated: 2024-XX-XX
############################################

# Memory management tests requiring specific hardware
igt@gem_caching@.*              # Cache coherency not universal
igt@gem_userptr_blits@.*        # Userptr stability issues

# Display tests with hardware dependencies
igt@kms_cursor_legacy@.*        # Legacy cursor compatibility
igt@kms_scaling_modes@.*        # Hardware scaling limitations

# Resource-intensive stress tests
igt@gem_exec_whisper@.*         # CPU/memory intensive
igt@gem_concurrent_blit@.*      # Concurrent stability

############################################
# Module loading tests - BAT only
# See: https://gitlab.freedesktop.org/drm/i915/kernel/-/issues/6227
############################################
igt@i915_module_load@load       # Requires unloaded i915 module
```

### Complete Xe Blocklist Example

```bash
############################################
# Xe Driver Test Blocklist
# Last Updated: 2025-XX-XX
############################################

# KMS tests requiring i915-specific features
igt@kms_prime@.*               # Prime implementation differences
igt@kms_cursor_legacy@.*       # Legacy API not supported

# Memory architecture differences
igt@gem_caching@.*             # Different memory subsystem
igt@gem_mmap_gtt@.*           # GTT not applicable to Xe

############################################
# SR-IOV test management
# Only specific subtests should run in CI
############################################
igt@sriov_basic@enable-vfs-autoprobe-off.*
igt@sriov_basic@enable-vfs-bind-unbind-each.*
igt@sriov_basic@bind-unbind-vf.*

############################################
# Platform-specific exclusions
############################################
igt@xe_wedged@.*              # Controlled error injection
```

