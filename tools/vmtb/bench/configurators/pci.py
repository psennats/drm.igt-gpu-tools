# SPDX-License-Identifier: MIT
# Copyright © 2024 Intel Corporation

import enum
import typing


class GpuModel(str, enum.Enum):
    ATSM150 = 'Arctic Sound M150 (ATS-M1)'
    ATSM75 = 'Arctic Sound M75 (ATS-M3)'
    Unknown = 'Unknown'

    def __str__(self) -> str:
        return str.__str__(self)


def get_gpu_model(pci_id: str) -> GpuModel:
    """Return GPU model associated with a given PCI Device ID."""
    return pci_ids.get(pci_id.upper(), GpuModel.Unknown)


def get_vgpu_profiles_file(gpu_model: GpuModel) -> str:
    """Return vGPU profile definition JSON file for a given GPU model."""
    if gpu_model == GpuModel.ATSM150:
        vgpu_device_file = 'Flex170.json'
    elif gpu_model == GpuModel.ATSM75:
        vgpu_device_file = 'Flex140.json'
    else: # GpuModel.Unknown
        vgpu_device_file = 'N/A'

    return vgpu_device_file


# PCI Device IDs: ATS-M150 (M1)
_atsm150_pci_ids = {
    '56C0': GpuModel.ATSM150,
    '56C2': GpuModel.ATSM150
}


# PCI Device IDs: ATS-M75 (M3)
_atsm75_pci_ids = {
    '56C1': GpuModel.ATSM75
}


# All PCI Device IDs to GPU Device Names mapping
pci_ids: typing.Dict[str, GpuModel] = {**_atsm150_pci_ids, **_atsm75_pci_ids}
