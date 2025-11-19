# Xe Fault Injection Device Management

## Overview

The `xe_fault_injection` test performs fault injection at the driver level, which affects all GPUs bound to the Xe driver simultaneously. To ensure fault injection only affects the intended GPU in multi-GPU systems, the test implements automatic device management.

## Multi-GPU Handling

### Problem

When multiple Xe GPUs are present in a system:
- Fault injection tests impact all GPUs bound to the driver
- This causes unpredictable test failures
- The `--device` filter is ignored at the driver level
- Other workloads on non-selected GPUs may be interfered with

### Solution

The test automatically detects and manages multiple Xe GPUs:

1. **Single GPU System**: Tests run normally without any modifications
2. **Multiple GPUs with `--device` Selection**: Automatically unbinds non-selected GPUs, runs tests on the selected GPU, then reloads the xe module
3. **Multiple GPUs without `--device`**: Skips all tests with a warning message

## Usage

### Single GPU System

Run the test normally:
```bash
./xe_fault_injection
```

### Multi-GPU System - Correct Usage

Specify which GPU to test using the `--device` filter:
```bash
./xe_fault_injection --device pci:slot=0000:03:00.0
```

You can find your device's PCI slot using:
```bash
ls /sys/bus/pci/drivers/xe/
```

### Multi-GPU System - Incorrect Usage

Running without `--device` will skip all tests:
```bash
./xe_fault_injection
# Output:
# Multiple Xe devices bound to driver, but no device selected with --device
# Fault injection affects all devices bound to the driver.
# Please use --device to select exactly one GPU.
```

## Implementation Details

### Device Context Structure

The test maintains a `struct xe_device_context` that tracks:
- All Xe devices bound to the driver
- Whether `--device` filter was explicitly used
- Which devices need to be managed after tests

### Device Scanning

At test startup, the test scans `/sys/bus/pci/drivers/xe/` to enumerate all PCI devices currently bound to the xe driver. It also checks `igt_device_filter_count()` to determine if the `--device` flag was actually used.

### Unbind/Module Reload Operations

- **Before Tests**: Non-selected devices are unbound from the xe driver
- **After Tests**: If any devices were unbound, the xe module is reloaded to restore all devices
- **Failure Handling**: Cleanup runs even if tests fail, ensuring the system is restored
- **Why Module Reload**: Reloading the module is safer than rebinding individual devices as it avoids issues with module dependencies (e.g., audio modules that depend on the GPU driver)

### Validation Logic

The test enforces these rules:
1. If only one GPU is bound → proceed normally
2. If multiple GPUs and `--device` explicitly specified → unbind others, proceed
3. If multiple GPUs and no `--device` → skip all tests with warning

## Benefits

- **Test Isolation**: Fault injection only affects the selected GPU
- **System Stability**: Other GPUs remain functional during testing
- **Predictable Results**: Tests produce consistent results regardless of system configuration
- **Safe Cleanup**: Module reload ensures system is restored even on test failure
- **Dependency Safety**: Module reload avoids issues with interdependent modules (e.g., audio)

## Technical Notes

- Device unbind uses `igt_kmod_unbind()` function
- Module reload uses `igt_xe_driver_unload()` and `igt_xe_driver_load()` functions
- The `--device` filter detection uses `igt_device_filter_count()` to check if filter was explicitly provided
- PCI slot names are matched against the device opened with `drm_open_driver()`
- The device context is cleaned up in the test fixture teardown, guaranteeing cleanup
- Maximum of 16 Xe devices are supported (defined by `MAX_XE_DEVICES`)

## Troubleshooting

### Device Selection

If you're unsure which device to select, list all Xe devices:
```bash
ls -l /sys/bus/pci/drivers/xe/
```

Each PCI slot (e.g., `0000:03:00.0`) represents a device.

### Bind/Unbind Failures

If device unbind or rebind fails:
- Check that you have sufficient permissions (may need root)
- Verify no other processes are using the device
- Check dmesg for kernel errors

### Cleanup Issues

If the xe module is not automatically reloaded:
- Manually reload with: `sudo rmmod xe && sudo modprobe xe`
- Check system logs for errors with: `dmesg | tail -50`
- Reboot if necessary to restore driver state
