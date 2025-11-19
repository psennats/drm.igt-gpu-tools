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
2. **Multiple GPUs with `--device` Selection**: Automatically unbinds non-selected GPUs, runs tests on the selected GPU, then rebinds all GPUs
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
- Which device was selected via `--device`
- Which devices need to be rebound after tests

### Device Scanning

At test startup, the test scans `/sys/bus/pci/drivers/xe/` to enumerate all PCI devices currently bound to the xe driver.

### Unbind/Rebind Operations

- **Before Tests**: Non-selected devices are unbound from the xe driver
- **After Tests**: All unbound devices are rebound in the cleanup fixture
- **Failure Handling**: Cleanup runs even if tests fail, ensuring devices are restored

### Validation Logic

The test enforces these rules:
1. If only one GPU is bound → proceed normally
2. If multiple GPUs and `--device` specified → unbind others, proceed
3. If multiple GPUs and no `--device` → skip all tests with warning

## Benefits

- **Test Isolation**: Fault injection only affects the selected GPU
- **System Stability**: Other GPUs remain functional during testing
- **Predictable Results**: Tests produce consistent results regardless of system configuration
- **Safe Cleanup**: Automatic rebinding ensures system is restored even on test failure

## Technical Notes

- Device unbind/rebind uses the IGT `igt_kmod_unbind()` and `igt_kmod_bind()` functions
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

If devices are not automatically rebound:
- Manually rebind with: `echo "0000:03:00.0" > /sys/bus/pci/drivers/xe/bind`
- Check system logs for errors
- Reboot if necessary to restore driver state
