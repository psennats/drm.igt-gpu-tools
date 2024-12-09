# SPDX-License-Identifier: MIT
# Copyright © 2024 Intel Corporation

class BenchError(Exception):
    pass


# Host errors:
class HostError(BenchError):
    pass


# Guest errors:
class GuestError(BenchError):
    pass


class GuestAgentError(GuestError):
    pass


class AlarmTimeoutError(GuestError):
    pass


# Generic errors:
class GemWsimError(BenchError):
    pass


class VgpuProfileError(BenchError):
    pass


class NotAvailableError(BenchError):
    pass


class VmtbConfigError(BenchError):
    pass
