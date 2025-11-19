# IGT GPU Tools Test Documentation Categories

This document explains the different types of test categorization and documentation markup
used in the IGT GPU Tools project for organizing and documenting test cases.

## Overview

IGT GPU Tools uses a structured documentation system with various categories and
subcategories to organize tests. The project has been evolving its documentation markup
to make it more precise and maintainable, with recent changes removing some deprecated
fields like 'Functionality' and 'Test category' that were dependent on developer
interpretation.

!!! warning "Deprecated fields"
    Some tests (e.g. for Core sub-category) have still deprecated fields like
    'Functionality', 'Test category'. There is a work in progress to remove them.

## Primary Documentation Fields

### 1. **Sub-category**
The Sub-category field is the primary organizational unit for grouping related tests.
This field has been actively maintained and expanded with new subcategories being
introduced regularly.

Recent Sub-categories include:

- **Core** : Basic DRM functionality tests
- **Synchronization** : Tests for synchronization mechanisms
- **CMD submission** : Command submission related tests
- **Blitter** : Blitter engine tests (unified for i915 and Xe)
- **GPGPU** : General Purpose GPU compute tests
- **Compute** : Computational workload tests
- **Performance** : Performance measurement tests
- **Memory management** : Memory allocation and management tests
- **Power management** : Power-related functionality tests
- **Display** : Display/KMS related tests
- **Firmware** : Firmware interaction tests
- **Media** : Media processing tests
- **Render copy** : Render copy operation tests
- **Debugging** : Debug and diagnostic tests
- **Workarounds** : Hardware workaround tests
- **Obsolete** : Deprecated or legacy tests
- **SysMan** : System management tests
- **FDinfo** : File descriptor information tests
- **Flat-ccs** : Flat CCS (Color Compression Surface) tests

### 2. **Mega-feature**
While not explicitly detailed in the current documentation, mega-feature appears to be a
higher-level categorization that groups multiple related sub-categories under broader
functional areas.

### 3. **Test Description**
IGT uses the `igt_describe()` macro to attach descriptions to subtests. The description
should complement the test/subtest name and provide context on what is being tested. It
should explain the idea of the test without mentioning implementation details.

**Best practices for descriptions:**

- Focus on the userspace perspective
- Capture the reason for the test's existence
- Be brief and avoid implementation details
- Don't translate code into English

**Good examples:**

- "make sure that legacy cursor updates do not stall atomic commits"
- "check that atomic updates of many planes are indeed atomic and take effect
  immediately after the commit"
- "make sure that the meta-data exposed by the kernel to the userspace is correct and
  matches the used EDID"

## Deprecated Fields

### **Test Category**
The 'Test category' field has been removed from KMS tests documentation as it was not
being used consistently.

### **Functionality**
The 'Functionality' field has also been removed as it "solely depends upon developer's
interpretation" and was making documentation maintenance difficult.

### **Run Type**
The 'Run type' documentation field has been removed from recent versions.

## Documentation Structure

### **Test Organization**
Tests can be organized using `igt_subtest_group` blocks, where the resulting subtest
documentation is a concatenation of its own description and all parenting subtest group
descriptions, starting from the outermost one.

### **Dynamic Subtests**
IGT supports dynamic subtests using `igt_subtest_with_dynamic` for cases where the full
set of possible subtests is too large or impossible to know beforehand. Dynamic subtests
are useful for operations like testing each KMS pipe separately.

## Driver-Specific Categories

### **Intel Tests**
The Intel-specific tests have their own subcategory system:

- **i915** driver tests
- **Xe** driver tests (newer Intel GPU architecture)
- **DRM** general Direct Rendering Manager tests

### **Vendor-Specific**

- **AMDGPU** - AMD GPU specific tests
- **intel-nouveau** - Intel and Nouveau driver interaction tests

## Documentation Generation

The test documentation is automatically generated and can be accessed via the `--describe`
command line switch. The IGT framework also supports `--list-subtests` to enumerate
available subtests and `--run-subtest` to execute specific subtests.

## Recent Changes and Evolution

The IGT project has been actively refining its documentation system, with version 1.29
introducing numerous subcategory additions and removing inconsistent documentation fields.
The focus has shifted toward more precise categorization that is easier to maintain.

### Key Improvements:

- Unification of Blitter subcategory across i915 and Xe
- Standardization of Sub-category/Functionality naming
- Removal of developer-dependent interpretation fields
- Addition of missing documentation fields for SRIOV and Display tests

## Usage

When adding new tests to IGT, developers should:

1. Use appropriate Sub-category classification
2. Provide clear, implementation-agnostic descriptions using `igt_describe()`
3. Follow the established naming conventions
4. Avoid using deprecated documentation fields
5. Ensure consistency with existing test organization

This categorization system helps maintain the large IGT test suite by providing clear
organization and making it easier for developers to find relevant tests and understand
their purpose.

## Documentation Fields and Values Table

Based on the current IGT GPU Tools codebase, here are the documented fields and their
possible values:

### Primary Documentation Fields

| Field | Status | Description | Possible Values |
|-------|--------|-------------|-----------------|
| **Sub-category** | Active | Primary organizational unit for grouping related tests | See detailed list below |
| **Test Description** | Active | Human-readable description using `igt_describe()` | Free text (implementation-agnostic) |
| **Mega-feature** | Active | High-level categorization grouping multiple sub-categories | Driver-specific groupings |

### Main Test Categories (Top-level)

| Category | Description | Examples |
|----------|-------------|----------|
| **Core Tests** | Tests for core DRM ioctls and behaviour | Authentication, basic operations |
| **KMS Tests** | Tests for kernel mode setting | Display functionality, connectors |
| **GEM Tests** | Tests for graphics execution manager | Memory management, buffer operations |
| **i915 Tests** | Tests for overall i915 driver behaviour | Intel-specific functionality |
| **Display Tests** | Tests for display validation | Panel fitting, CRC validation |
| **Perf Tests** | Tests for performance metrics | GPU performance counters |
| **PM Tests** | Tests for power management features | Suspend/resume, power states |
| **Prime Tests** | Tests for buffer sharing | Inter-device buffer sharing |
| **Debugfs Tests** | Tests for debugfs behaviour | Debug filesystem functionality |
| **DRM Tests** | Tests for libdrm behaviour | Library functionality |
| **Meta Tests** | Tests for the CI system itself | Test infrastructure validation |
| **SW Sync Tests** | Tests for software sync (fencing) | Synchronization primitives |
| **vGEM Tests** | Tests for virtual graphics execution manager | Virtual GPU functionality |
| **Tools Tests** | Tests for IGT tools behaviour | Tool validation |

### Sub-category Values (Detailed)

#### Core Infrastructure
| Sub-category | Description |
|--------------|-------------|
| **Core** | Basic DRM functionality tests |
| **Synchronization** | Tests for synchronization mechanisms |
| **Device** | Device enumeration and management |
| **Authentication** | DRM authentication mechanisms |

#### Intel-Specific Sub-categories
| Sub-category | Description |
|--------------|-------------|
| **CMD submission** | Command submission related tests |
| **Blitter** | Blitter engine tests (unified for i915 and Xe) |
| **GPGPU** | General Purpose GPU compute tests |
| **Compute** | Computational workload tests |
| **Performance** | Performance measurement tests |
| **Memory management** | Memory allocation and management tests |
| **Power management** | Power-related functionality tests |
| **Firmware** | Firmware interaction tests |
| **Media** | Media processing tests |
| **Render copy** | Render copy operation tests |
| **Debugging** | Debug and diagnostic tests |
| **Workarounds** | Hardware workaround tests |
| **SysMan** | System management tests |
| **FDinfo** | File descriptor information tests |
| **Flat-ccs** | Flat CCS (Color Compression Surface) tests |

#### Display Sub-categories
| Sub-category | Description |
|--------------|-------------|
| **Display** | General display functionality |
| **KMS** | Kernel Mode Setting tests |
| **Chamelium** | External test device integration |
| **Panel** | Panel-specific functionality |
| **HDMI** | HDMI interface tests |
| **DisplayPort** | DisplayPort interface tests |
| **VGA** | VGA interface tests |

#### Vendor-Specific Sub-categories
| Sub-category | Description |
|--------------|-------------|
| **AMDGPU** | AMD GPU specific tests |
| **intel-nouveau** | Intel and Nouveau driver interaction |
| **i915** | Intel i915 driver tests |
| **Xe** | Intel Xe driver tests |

#### Legacy/Deprecated Sub-categories
| Sub-category | Description |
|--------------|-------------|
| **Obsolete** | Deprecated or legacy tests |

### Deprecated Fields

| Field | Status | Reason for Deprecation |
|-------|--------|----------------------|
| **Test Category** | WIP | Not used consistently |
| **Functionality** | WIP | Developer-dependent interpretation |
| **Run Type** | WIP | Unclear usage patterns |

### Driver-Specific Mega-features

| Driver | Mega-feature Examples |
|--------|---------------------|
| **i915** | Display, GEM, Performance, Power Management |
| **Xe** | Compute, CMD Submission, Memory Management |
| **AMDGPU** | Display, Compute, Performance |

### Documentation Markup Examples

```c
/**
 * SUBTEST: %s
 * Description: %s
 * Sub-category: %s
 */

// Example usage:
igt_describe("check that atomic updates are indeed atomic");
igt_subtest_with_dynamic("atomic-plane-updates") {
    // Test implementation
}
```

### Field Usage Guidelines

1. **Sub-category**: Always required, must match established categories
2. **Test Description**: Should be implementation-agnostic and focus on what is tested
3. **Mega-feature**: Used for high-level organization, typically driver-specific
4. **Avoid deprecated fields**: Do not use Test Category, Functionality, or Run Type
