# SPDX-License-Identifier: MIT
# Copyright © 2024 Intel Corporation

import abc
import signal

from bench.machines.machine_interface import ProcessResult


class ExecutorInterface(metaclass=abc.ABCMeta):

    @abc.abstractmethod
    def status(self) -> ProcessResult:
        raise NotImplementedError

    @abc.abstractmethod
    def wait(self) -> ProcessResult:
        raise NotImplementedError

    @abc.abstractmethod
    def sendsig(self, sig: signal.Signals) -> None:
        raise NotImplementedError
