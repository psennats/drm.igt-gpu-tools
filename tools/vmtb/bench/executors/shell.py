# SPDX-License-Identifier: MIT
# Copyright © 2024 Intel Corporation

import signal

from bench.executors.executor_interface import ExecutorInterface
from bench.machines.machine_interface import (DEFAULT_TIMEOUT,
                                              MachineInterface, ProcessResult)


class ShellExecutor(ExecutorInterface):
    def __init__(self, target: MachineInterface, command: str, timeout: int = DEFAULT_TIMEOUT) -> None:
        self.target = target
        self.timeout = timeout
        self.pid = self.target.execute(command)

    def status(self) -> ProcessResult:
        return self.target.execute_status(self.pid)

    def wait(self) -> ProcessResult:
        return self.target.execute_wait(self.pid, self.timeout)

    def sendsig(self, sig: signal.Signals) -> None:
        self.target.execute_signal(self.pid, sig)

    def terminate(self) -> None:
        self.sendsig(signal.SIGTERM)

    def kill(self) -> None:
        self.sendsig(signal.SIGKILL)
