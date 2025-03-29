.. SPDX-License-Identifier: MIT

=====
lsgpu
=====

-------------------------------------------------------------
List and inspect GPUs by scanning the PCI bus, DRM, and sysfs
-------------------------------------------------------------
.. include:: defs.rst
:Author: IGT Developers <igt-dev@lists.freedesktop.org>
:Date: 2025-03-27
:Version: |PACKAGE_STRING|
:Copyright: 2009,2011,2012,2016,2018,2019,2020,2023,2024,2025 Intel Corporation
:Manual section: |MANUAL_SECTION|
:Manual group: |MANUAL_GROUP|

SYNOPSIS
========

**lsgpu** [*OPTIONS*]

DESCRIPTION
===========

**lsgpu** is part of the **igt-gpu-tools** suite, and it is designed to assist in GPU enumeration and debugging. **lsgpu**  is a command-line tool for listing and inspecting GPUs available on the system. It scans the PCI bus, DRM subsystem, and sysfs to gather information about detected GPU devices and their properties.

By default, **lsgpu** displays a list of GPUs along with basic details. The tool supports additional options to print detailed properties and sysfs attributes, apply filters to select specific devices, and verify access permissions to GPU nodes.

Filtering can be performed using the -d or --device option, allowing users to match GPUs based on vendor, PCI attributes, or other criteria. When a filter is applied, only the first matching device is displayed.

Additionally, **lsgpu** attempts to open the corresponding DRM device nodes (/dev/dri/cardX, /dev/dri/renderDX) to check for access permissions. It follows the IGT variable search order for selecting the target device:

1. The **--device** option, if provided
2. The **IGT_DEVICE** environment variable, if set
3. The **.igtrc** configuration file (Common::Device setting), if neither of the above is specified

OPTIONS
=======

-h, --help
    Show help text.

-n, --numeric
    Print vendor/device as hex

-c, --codename
    Print codename instead pretty device name

-s, --print-simple
    Print simple (legacy) device details

-p, --print-details
    Print devices with details

-P, --pci-scan
    Print PCI GPU devices

-v, --list-vendors
    List recognized vendors

-l, --list-filter-types
    List registered device filters types

-d, --device <filter>
    Apply a device filter (e.g. 'pci:vendor=Intel,device=discrete,card=0'). Can be used multiple times.

-V, --version
    Print version information and exit

Default print mode options
--------------------------

These options are only valid when using the default printout mode:

--drm
    Default: Print DRM filters for each device

--sysfs
    Print sysfs filters for each device

--pci
    Print PCI filters for each device


REPORTING BUGS
==============

Report bugs on fd.o GitLab: https://gitlab.freedesktop.org/drm/igt-gpu-tools/-/issues
