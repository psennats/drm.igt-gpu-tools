# :material-hammer-screwdriver: Cross-Building IGT

## :material-information-outline: Overview

Producing an IGT build for another architecture requires setting up a cross-compilation
environment with the appropriate toolchain, system root directory, and Meson
configuration. This guide covers the essential steps for cross-building IGT GPU Tools
for different target architectures.

## :material-wrench-cog: Cross-Build Toolchain

### Required Components

Cross-building requires installing a toolchain with support for the target architecture,
or a toolchain built specifically for the target, plus an emulator (like QEMU). For IGT,
the minimal toolchain consists of GCC and binutils.

**Example: ARM64 on Fedora**

For cross-building with ARM64 as the target architecture, install these packages:

```bash
# Fedora packages for ARM64 cross-compilation
binutils-aarch64-linux-gnu
gcc-aarch64-linux-gnu
```

### Alternative Toolchain Sources

Pre-built cross-compiler toolchain tarballs are also available:

- **Bootlin Toolchains**: https://toolchains.bootlin.com/
- Various distribution-specific cross-compilation packages
- Custom-built toolchains for specific targets

## :material-folder-cog: System Root Directory (sysroot)

### Purpose

Besides a toolchain, a system root directory containing the libraries used by IGT
pre-compiled to the target architecture is required.

### Obtaining a Sysroot

The sysroot can be obtained through several methods:

1. **Cross-building a distribution** using Yocto, buildroot, or similar tools
2. **Copying the system root** from an existing installation for the desired architecture
3. **Building dependencies** manually for the target architecture

The sysroot must contain all IGT build-time and runtime library dependencies compiled
for the target architecture.

### Important Considerations

!!! warning "Toolchain Dependencies"
    Cross-build toolchains may require some dependent object files and libraries to
    also be copied to the system root directory. For instance, with Fedora, files
    located under `/usr/aarch64-linux-gnu/sys-root/` (for aarch64 architecture) should
    also be stored in the sysroot directory used by Meson, otherwise the preparation
    step will fail.

## :material-cog-outline: Meson Configuration

### Cross-File Requirement

Meson requires an extra configuration file for non-native builds, passed via the
`--cross-file` parameter. This file contains details about:

- Target OS/architecture specifications
- Host (native) OS/architecture information
- Sysroot location and compiler configurations
- Binary locations and execution wrappers

### Configuration Sections

#### `[host_machine]` Section
Defines the system and architecture of the native OS.

**Example:** Native OS is Linux x86_64 architecture
```ini
[host_machine]
system = 'linux'
cpu_family = 'x86_64'
cpu = 'x86_64'
endian = 'little'
```

#### `[target_machine]` Section
Contains details about the target OS/architecture.

**Example:** Target is aarch64 (ARM 64-bit architecture)
```ini
[target_machine]
system = 'linux'
cpu_family = 'aarch64'
cpu = 'aarch64'
endian = 'little'
```

#### `[constants]` Section (Optional)
Helps define reusable paths and arguments for use in other sections.

```ini
[constants]
sysroot = '/aarch64-sysroot'
common_args = ['--sysroot=' + sysroot]
```

#### `[properties]` Section
Contains arguments to be used by the build binaries.

```ini
[properties]
sys_root = sysroot
c_args = common_args
c_link_args = common_args
pkg_config_libdir = [
    sysroot + '/usr/lib64/pkgconfig',
    sysroot + '/usr/share/pkgconfig',
    sysroot + '/usr/local/lib/pkgconfig'
]
```

#### `[binaries]` Section
Contains the binaries to be used during the build.

- Can use either native or target toolchain
- If using target toolchain, requires `exe_wrapper` pointing to an architecture
  emulator like `qemu-arm`

```ini
[binaries]
c = '/usr/bin/aarch64-linux-gnu-gcc'
ar = '/usr/bin/aarch64-linux-gnu-gcc-ar'
ld = '/usr/bin/aarch64-linux-gnu-ld'
strip = '/usr/bin/aarch64-linux-gnu-strip'
pkgconfig = 'pkg-config'
```

## :material-play-circle-outline: Build Process

### Preparation

Prepare for cross-compilation by calling Meson with the cross-compilation config file
and build directory:

```bash
meson --cross-file arm64_cross.txt build
```

### Compilation

Execute the actual compilation using Ninja:

```bash
ninja -C build
```

### Build Limitations

!!! info "Cross-Compilation Limitations"
    Some parts of the IGT build are disabled during cross-compilation, including:

    - Testlist file creation
    - Documentation generation
    - Other steps that depend on running generated code on the native machine

## :material-file-code: Pre-configured Examples

The IGT root directory contains pre-configured cross-compilation examples using QEMU
to run target-OS machine toolchains:

- `meson-cross-arm64.txt` - ARM64 architecture
- `meson-cross-armhf.txt` - ARM hard-float architecture
- `meson-cross-mips.txt` - MIPS architecture

## :material-script-text: Complete Example Configuration

### Native Cross-Builder Toolchain: `arm64_cross.txt`

```ini
[constants]
sysroot = '/aarch64-sysroot'
common_args = ['--sysroot=' + sysroot]

[properties]
sys_root = sysroot
c_args = common_args
c_link_args = common_args
pkg_config_libdir = [
    sysroot + '/usr/lib64/pkgconfig',
    sysroot + '/usr/share/pkgconfig',
    sysroot + '/usr/local/lib/pkgconfig'
]

[binaries]
c = '/usr/bin/aarch64-linux-gnu-gcc'
ar = '/usr/bin/aarch64-linux-gnu-gcc-ar'
ld = '/usr/bin/aarch64-linux-gnu-ld'
strip = '/usr/bin/aarch64-linux-gnu-strip'
pkgconfig = 'pkg-config'

[host_machine]
system = 'linux'
cpu_family = 'x86_64'
cpu = 'x86_64'
endian = 'little'

[target_machine]
system = 'linux'
cpu_family = 'aarch64'
cpu = 'aarch64'
endian = 'little'
```

## :material-lightbulb-outline: Best Practices

### Sysroot Management

- Ensure all required dependencies are present in the sysroot
- Verify library paths match the target architecture
- Include both runtime and development packages

### Toolchain Verification

- Test the cross-compilation toolchain independently before building IGT
- Verify that the toolchain can produce working binaries for the target
- Check that all required tools (gcc, binutils, etc.) are available

### Build Validation

- Use emulation (QEMU) to test cross-compiled binaries when possible
- Validate that the cross-compiled IGT works on actual target hardware
- Compare functionality with native builds to ensure completeness

## :material-link: References

- [Meson Cross-compilation Documentation](https://mesonbuild.com/Cross-compilation.html)
- [Bootlin Toolchains](https://toolchains.bootlin.com/)
