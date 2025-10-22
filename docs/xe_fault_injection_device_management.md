# Xe Fault Injection Test - Device Management

## Overview
The `xe_fault_injection` test includes special handling for multi-GPU systems to ensure fault injection only affects the intended GPU device.

## Background
Fault injection tests operate at the driver level. When multiple GPUs are bound to the Xe driver, fault injection affects all of them simultaneously, which can:
- Cause unexpected test failures
- Interfere with other running workloads on non-selected GPUs
- Make the --device filter selection ineffective

## Usage

### Single GPU System
If your system has only one Xe GPU bound to the driver, the test will run normally:
```bash
./xe_fault_injection
```

### Multiple GPU System with Device Selection
If your system has multiple Xe GPUs, you must select one with --device:
```bash
./xe_fault_injection --device pci:slot=0000:03:00.0
```

The test will:
1. Unbind all non-selected Xe GPUs from the driver
2. Run the fault injection tests on the selected GPU
3. Rebind all previously unbound GPUs

### Multiple GPU System without Selection
If you have multiple GPUs but don't specify --device, the test will warn and skip:
```
Multiple Xe devices bound to driver, but no device selected with --device
Fault injection affects all devices bound to the driver.
Please use --device to select exactly one GPU.
```

## Implementation Details

### Device Context Management
The test uses a device context structure to track:
- All Xe GPUs currently bound to the driver
- Which device was selected by the user
- Which devices were unbound for testing

### Automatic Cleanup
The test ensures proper cleanup even if tests fail:
- Unbound devices are tracked in the context
- Cleanup is performed in the test fixture teardown
- Cleanup attempts to rebind all previously unbound devices

### Error Handling
The test handles various error conditions:
- Selected device not found in Xe device list
- Unbinding operation failures (with rollback)
- Rebinding operation failures (with warnings)
- Multiple GPUs without explicit selection

## Technical Notes

### Device Discovery
Devices are discovered by scanning `/sys/bus/pci/drivers/xe/` directory for PCI device symlinks.

### Binding Operations
Device binding/unbinding uses the IGT kmod library functions:
- `igt_kmod_unbind("xe", pci_slot)` - Unbind device from driver
- `igt_kmod_bind("xe", pci_slot)` - Bind device to driver

### Verification
After each binding operation, the test verifies the operation succeeded by checking the sysfs interface.

## Limitations
- Only manages devices currently bound to the Xe driver
- Cannot detect Xe-compatible devices that are bound to other drivers
- Requires root privileges for binding/unbinding operations (like all IGT tests)

## See Also
- `tests/intel/xe_fault_injection.c` - Test implementation
- `lib/igt_kmod.c` - Kernel module management functions
- `lib/igt_device_scan.c` - Device scanning and filtering
