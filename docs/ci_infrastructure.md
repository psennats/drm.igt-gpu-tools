# :material-cloud-cog: CI Infrastructure

## :material-information-outline: Overview

IGT GPU Tools employs a comprehensive Continuous Integration (CI) infrastructure to ensure
code quality, prevent regressions, and maintain driver stability. The CI system operates
across multiple platforms and architectures, providing automated testing for both pre-merge
and post-merge scenarios.

The infrastructure consists of a main component:

- **GitLab CI** (Freedesktop.org) - Build validation and cross-platform testing

There are CI infrastructures which focus on specific HW platforms:

- **Intel GFX CI** - Comprehensive hardware testing on real Intel GPU systems

##
## :material-gitlab: GitLab CI (Freedesktop.org)

### Infrastructure Overview

IGT uses GitLab CI hosted at `gitlab.freedesktop.org` for:

- **Build validation** across multiple architectures
- **Container-based testing** for reproducible environments
- **Cross-platform compatibility** checks
- **Documentation generation** and validation

### Container Infrastructure

IGT leverages containerized builds for consistency and reliability:

```bash
# Pre-built containers available at:
registry.freedesktop.org/drm/igt-gpu-tools/igt:master

# Run IGT in container:
podman run --rm --privileged registry.freedesktop.org/drm/igt-gpu-tools/igt:master
```

### Build Matrix

| Platform | Architecture | Purpose |
|-|-||
| **Debian** | x86_64 | Primary build and test platform |
| **Fedora** | x86_64 | Alternative distro validation |
| **Debian Minimal** | x86_64 | Minimal dependency testing |
| **Multi-arch** | arm64, mips | Cross-platform compatibility |

## :material-cpu-64-bit: Intel GFX CI

### Mission-Critical Testing

Intel GFX CI provides the backbone for Intel GPU driver validation:

!!! quote "Intel GFX CI Mission"
    "Test each patch that goes into i915, Intel Xe, or IGT GPU Tools **before** it lands
    in the repository, comparing results with post-merge baselines to catch regressions
    early."

### Hardware Infrastructure

**Diverse GPU Coverage:**

- Multiple Intel GPU generations (Gen7 through latest)
- Various form factors (desktop, mobile, server)
- Different display configurations (HDMI, DP, eDP, VGA)
- Specialized hardware for specific features

**Sample Hardware Types:**

- `bat-apl-1` - Apollo Lake platform
- `bat-jsl-3` - Jasper Lake system
- `bat-rpls-1` - Raptor Lake S
- `bat-mtlp-8` - Meteor Lake P
- `shard-tglb1` - Tiger Lake (sharded testing)

### Test Execution Tiers

#### 1. Basic Acceptance Tests (BAT)
**Purpose:** Fast feedback and gating mechanism

**Duration:** ~1 hour

**Test List:** `tests/intel-ci/fast-feedback.testlist`

```bash
# Example BAT tests:
igt@core_auth@getclient-simple
igt@i915_module_load@load
igt@kms_busy@basic@modeset
igt@debugfs_test@read_all_entries
```

**Key Characteristics:**

- Ensures testing configuration is working
- Gates all further testing (sharded runs)
- Fast-feedback tests for quick validation
- Must pass before proceeding to full testing

#### 2. Full IGT (Sharded Runs)
**Purpose:** Comprehensive validation
**Duration:** ~6 hours
**Scope:** All IGT tests (filtered by blocklists)

**Driver-Specific Execution:**

- **i915:** All IGT tests filtered by `blocklist.txt`
- **Xe:** All IGT tests filtered by `xe.blocklist.txt`

**Sharding Strategy:**
Tests are distributed across multiple machines for parallel execution:

- `shard-tglb1`, `shard-tglb2` - Tiger Lake systems
- `shard-dg2` - DG2/Arc GPU systems
- Multiple concurrent executions for faster results

#### 3. Specialized Runs

| Run Type | Purpose | Characteristics |
|-||--|
| **Idle Runs** | Extra coverage during quiet periods | Run when CI is idle |
| **KASAN Runs** | Memory safety validation | Kernel Address Sanitizer enabled |
| **100 Re-runs** | Flaky test detection | Same test 100 times for statistics |
| **Resume Runs** | Suspend/resume validation | Single machine, resume capability |
| **drmtip Runs** | Extended coverage | Full IGT on BAT hardware |

## :material-email-fast: Pre-Merge Testing Workflow

### Patch Submission Process

 1. **Mailing List Submission**
   ```bash
   # IGT patches should include i-g-t tag:
   git config --local format.subjectPrefix "PATCH i-g-t"
   # Example subject:
   [PATCH i-g-t] tests/kms_atomic: Add new subtest for cursor planes
   ```
 2. **Automatic CI Triggering**
    - CI automatically detects patches on mailing lists
    - Both BAT and Full IGT are scheduled
    - Results sent as email replies to original patch

 3. **Result Timeline**
    - BAT Results: ~1 hour
    - Full IGT Results: ~6 hours
    - All results emailed even on success

### Forcing Custom Test Configurations

For specific testing needs, developers can override CI behavior:

```bash
# Force specific tests in BAT (mark with HAX):
[PATCH i-g-t HAX] tests/intel-ci: Add kms_example to fast-feedback

# Change kernel configuration:
[PATCH HAX] kernel: Enable CONFIG_EXAMPLE for testing

# Force IGT test list changes:
# Modify tests/intel-ci/fast-feedback.testlist in a HAX patch
```

### Trybot System

!!! info "Works for i915 only"
**Purpose:** Test changes before formal review

**Usage:**

- Submit to trybot mailing list
- Get CI feedback without formal patch review
- Useful for experimental changes

## :material-bug: Results and Bug Tracking

### Result Categories

| Result | Description | Action Required |
|--|-|--|
| **PASS** | Test completed successfully | None |
| **SKIP** | Test skipped (missing hardware/feature) | Generally acceptable |
| **FAIL** | Test failed | Investigation required |
| **Dmesg-Warn** | Kernel warnings detected | Review warnings |
| **Incomplete** | Test didn't complete | Infrastructure issue |

### Bug Tracking Integration

**Automated Bug Filtering:**

- Known issues are filtered out using pattern matching
- New failures automatically trigger investigation
- Results tied to specific dmesg patterns and hardware

**Bug Trackers:**

- **freedesktop GitLab** - Kernel, Xe, and IGT issues
- **Hardware-specific filters** - Machine type patterns
- **Pattern-based matching** - dmesg output analysis

## :material-git: Repository Integration

### CI-Tagged Repositories

**IGT CI Tags:** `https://gitlab.freedesktop.org/gfx-ci/igt-ci-tags.git`

- Contains CI-specific tags and metadata
- Used for tracking tested versions
- Integration with kernel development

**Kernel Integration:** `git://anongit.freedesktop.org/gfx-ci/linux`

- Kernel tree with CI integration
- Automated testing of kernel + IGT combinations
- Firmware coordination

### Firmware Management

**Firmware Repositories:**

- **intel-staging** - Upcoming firmware blobs ready for merge
- **intel-for-ci** - CI-specific firmware with intel-ci directory
- **Automatic deployment** - Firmware updated on test machines

**Firmware Deployment Process:**

1. New pushes detected on firmware branches
2. Extract i915 and xe directories from intel-staging
3. Extract intel-ci from intel-for-ci
4. Deploy all three directories to test machines
5. Integrate with base OS firmware tree (Ubuntu)

## :material-docker: Container Registry

### Available Images

**Primary IGT Container:**
```bash
registry.freedesktop.org/drm/igt-gpu-tools/igt:master
```

**Build Images:**

- Debian-based development environment
- Fedora-based testing environment
- Multi-architecture build support
- Documentation generation environment

### Usage Examples

```bash
# Run tests in container:
podman run --rm --privileged \
  registry.freedesktop.org/drm/igt-gpu-tools/igt:master \
  /usr/libexec/igt-gpu-tools/core_auth

# Build IGT in container:
podman run --rm -v $(pwd):/workspace \
  registry.freedesktop.org/drm/igt-gpu-tools/igt:master \
  bash -c "cd /workspace && meson build && ninja -C build"
```

## :material-chart-timeline-variant: CI Configuration Files

### GitLab CI Configuration

**Primary Configuration:** `.gitlab-ci.yml`

- Multi-stage pipeline definition
- Container build and test stages
- Cross-architecture build matrix
- Documentation generation jobs

**Key Configuration Sections:**
```yaml
# Build stage example:
build:
  stage: build
  script:
    - meson build
    - ninja -C build
  artifacts:
    paths:
      - build/

# Test stage example:
test:
  stage: test
  script:
    - cd build && meson test
```

### Intel CI Configuration

**Test Lists:**

- `tests/intel-ci/fast-feedback.testlist` - BAT test definitions
- `tests/intel-ci/xe-fast-feedback.testlist` - Xe-specific BAT tests
- `tests/intel-ci/blocklist.txt` - i915 test exclusions
- `tests/intel-ci/xe.blocklist.txt` - Xe test exclusions

**Hardware Configuration:**

- Platform-specific test routing
- Display configuration requirements
- GPU generation compatibility matrices

## :material-monitor-dashboard: Monitoring and Maintenance

### Performance Metrics

**CI Health Indicators:**

- Queue depth and processing time
- Success/failure rates by platform
- Hardware utilization statistics
- Container build performance

**Continuous Monitoring:**

- Real-time queue status
- Historical trend analysis
- Capacity planning metrics
- Infrastructure reliability tracking

### Maintenance Operations

**Regular Tasks:**

- Container image updates
- Hardware firmware updates
- Test list maintenance
- Bug filter updates

**Scaling Operations:**

- Hardware addition/retirement
- Load balancing adjustments
- Performance optimization
- Capacity expansion

## :material-account-group: Development Integration

### Developer Workflow

1. **Local Development**
   ```bash
   # Test locally with containers:
   podman run --rm --privileged \
     -v $(pwd):/workspace \
     registry.freedesktop.org/drm/igt-gpu-tools/igt:master
   ```

2. **Patch Submission**:

    - Email patches to mailing lists
    - CI automatically triggered
    - Results delivered via email

3. **Result Analysis**

    - Review BAT results first (~1 hour)
    - Full results available later (~6 hours)
    - Investigate any failures or warnings

### Best Practices

**For Contributors:**

- Test patches locally before submission
- Use appropriate subject line tags ([PATCH i-g-t])
- Mark experimental patches with HAX
- Monitor CI results and respond to failures

**For Maintainers:**

- Review CI results before merging
- Update test lists as needed
- Maintain bug filters
- Coordinate with hardware teams

## :material-link-variant: Resources and References

### CI Infrastructure Links

- **[Freedesktop Gitlab CI Pipelines](https://gitlab.freedesktop.org/drm/igt-gpu-tools/-/pipelines)**
- **[Container Registry](https://gitlab.freedesktop.org/drm/igt-gpu-tools/container_registry)**
- **[CI Tags Repository](https://gitlab.freedesktop.org/gfx-ci/igt-ci-tags)**
