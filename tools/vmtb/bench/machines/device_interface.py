# SPDX-License-Identifier: MIT
# Copyright © 2024 Intel Corporation

import abc


class DeviceInterface(abc.ABC):

    @abc.abstractmethod
    def create_vf(self, num: int) -> int:
        raise NotImplementedError

    @abc.abstractmethod
    def remove_vfs(self) -> int:
        raise NotImplementedError

    @abc.abstractmethod
    def bind_driver(self) -> None:
        raise NotImplementedError

    @abc.abstractmethod
    def unbind_driver(self) -> None:
        raise NotImplementedError
