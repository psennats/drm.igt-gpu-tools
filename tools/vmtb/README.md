VM Test Bench
=============

Description
-----------
VM Test Bench (VMTB) is a tool for testing virtualization (SR-IOV)
supported by the xe driver.
It allows to enable and provision VFs (Virtual Functions) and facilitates
manipulation of VMs (Virtual Machines) running virtual GPUs.
This includes starting and accessing the KVM/QEMU VMs,
running workloads or shell commands (Guest/Host),
handling power states, saving and restoring VF state etc.

Requirements
------------
VMTB is implemented in Python using pytest testing framework.

Host OS is expected to provide:
- xe PF driver with SR-IOV support
- VFIO driver (VF save/restore requires vendor specific driver variant)
- QEMU (VF save/restore requires QEMU 8.1+)
- Python 3.11+ with pytest installed
- VM Test Bench tool deployed
- IGT binaries

Guest OS is expected to contain:
- xe VF driver
- QEMU Guest-Agent service for operating on Guest OS
- IGT binaries to execute workloads on VM

Usual VMTB testing environment bases on Ubuntu 24.04 installed
on Host and Guest, but execution on other distros should be also possible.

Building
--------
Install all VMTB's development and build dependencies using pip:

    $ python3 -m pip install -r dev-requirements.txt

The VMTB source distribution package can be built with:

    $ python3 -m build --sdist

that runs Python's `build` frontend
in an isolated virtual environment (`venv`).

The output tarball is created in the `dist/` subdirectory,
that should be copied and extracted on a host device under test, for example:

    $ scp dist/vmtb-1.0.0.tar.gz vmuser@dut:/home/vmuser
    vmuser@dut:~$ tar -xzvf vmtb-1.0.0.tar.gz

Alternatively, VMTB sources can be installed by the IGT build system
with a `vmtb` option enabled:

    # meson build -Dvmtb=enabled && ninja -C build install

that deploys VMTB in a default libexec directory, like:

    /usr/local/libexec/igt-gpu-tools/vmtb/

Running tests
-------------
Test implemented by VM Test Bench are called VMM Flows and located in
`vmm_flows/` directory. Test files are prefixed with `test_` and encapsulate
related validation scenarios. Each test file can contain multiple test classes
(`TestXYZ`) or functions (`test_xyz`), that can be executed independently.

Ensure a pytest framework is available on a DUT host, install if needed with:

    $ python3 -m pip install pytest

Run the VMM Flows test in the following way (as root):

    # pytest-3 -v ./vmtb/vmm_flows/<test_file_name>.py::<test_class_or_function_name> --vm-image=/path/to/<guest_os.img>

For example, the simplest 1xVF/VM test scenario is executed as:

    # pytest-3 -v ./vmtb/vmm_flows/test_basic.py::TestVmSetup::test_vm_boot[1VF] --vm-image=/home/vmuser/guest_os.img

(in case `pytest-3` command cannot be found, check with just `pytest`)

Name of test class/function can be omitted to execute all tests in file.
File name can also be omitted, then all tests in
`vmm_flows` directory will be executed.

Test log (including VM dmesg) is available in `logfile.log` output file.
Test results are presented as a standard pytest output on a terminal.
VM (Guest OS) can be accessed manually over VNC on [host_IP]:5900
(where port is incremented for the consecutive VMs).

Structure
---------
VMTB is divided into the following components:

#### `bench/`
Contains 'core' part of the tool, including Host, Device, Driver and
Virtual Machine abstractions, means to execute workloads (or other tasks),
various helper and configuration functions etc.
VMTB utilizes QMP (QEMU Machine Protocol) to communicate and operate with VMs
and QGA (QEMU Guest Agent) to interact with the Guest OS.

#### `vmm_flows/`
Contains actual functional VM-level tests (`test_*.py`)
as well as a setup and tear-down fixtures (`conftest.py`).
New test files/scenarios shall be placed in this location.
