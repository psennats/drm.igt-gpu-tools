# SPDX-License-Identifier: MIT
# Copyright © 2024 Intel Corporation

import abc
import typing


class BackendInterface(metaclass=abc.ABCMeta):

    @abc.abstractmethod
    def sync(self, idnum: int) -> typing.Optional[typing.Dict]:
        raise NotImplementedError

    @abc.abstractmethod
    def ping(self) -> typing.Optional[typing.Dict]:
        raise NotImplementedError

    @abc.abstractmethod
    def execute(self, command: str, args: typing.List[str]) -> typing.Optional[typing.Dict]:
        raise NotImplementedError

    @abc.abstractmethod
    def execute_status(self, pid: int) -> typing.Optional[typing.Dict]:
        raise NotImplementedError

    @abc.abstractmethod
    def suspend_disk(self) -> None:
        raise NotImplementedError

    @abc.abstractmethod
    def suspend_ram(self) -> None:
        raise NotImplementedError

    @abc.abstractmethod
    def reboot(self) -> None:
        raise NotImplementedError

    @abc.abstractmethod
    def poweroff(self) -> None:
        raise NotImplementedError
