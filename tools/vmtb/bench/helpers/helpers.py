# SPDX-License-Identifier: MIT
# Copyright © 2024 Intel Corporation

import logging

from bench.executors.igt import IgtExecutor
from bench.executors.shell import ShellExecutor
from bench.machines.machine_interface import MachineInterface

logger = logging.getLogger('Helpers')


def driver_check(machine: MachineInterface, card: int = 0) -> bool:
    drm_driver = machine.get_drm_driver_name()
    if not machine.dir_exists(f'/sys/module/{drm_driver}/drivers/pci:{drm_driver}/'):
        logger.error(f'{drm_driver} module not loaded on card %s', card)
        return False

    return True


def igt_check(igt_test: IgtExecutor) -> bool:
    ''' Helper/wrapper for wait and check for igt test '''
    igt_out = igt_test.wait()
    if igt_out.exit_code == 0 and igt_test.did_pass():
        return True
    logger.error('IGT failed with %s', igt_out)
    return False


def igt_run_check(machine: MachineInterface, test: str) -> bool:
    ''' Helper/wrapper for quick run and check for igt test '''
    igt_test = IgtExecutor(machine, test)
    return igt_check(igt_test)


def cmd_check(cmd: ShellExecutor) -> bool:
    ''' Helper/wrapper for wait and check for shell command '''
    cmd_out = cmd.wait()
    if cmd_out.exit_code == 0:
        return True
    logger.error('%s failed with %s', cmd, cmd_out)
    return False


def cmd_run_check(machine: MachineInterface, cmd: str) -> bool:
    ''' Helper/wrapper for quick run and check for shell command '''
    cmd_run = ShellExecutor(machine, cmd)
    return cmd_check(cmd_run)


def modprobe_driver(machine: MachineInterface, parameters: str = '', options: str = '') -> ShellExecutor:
    """Load driver (modprobe [driver_module]) and return ShellExecutor instance (do not check a result)."""
    drm_driver = machine.get_drm_driver_name()
    modprobe_cmd = ShellExecutor(machine, f'modprobe {drm_driver} {options} {parameters}')
    return modprobe_cmd


def modprobe_driver_check(machine: MachineInterface, cmd: ShellExecutor) -> bool:
    """Check result of a driver load (modprobe) based on a given ShellExecutor instance."""
    modprobe_success = cmd_check(cmd)
    if modprobe_success:
        return driver_check(machine)

    logger.error('Modprobe failed')
    return False


def modprobe_driver_run_check(machine: MachineInterface, parameters: str = '', options: str = '') -> bool:
    """Load (modprobe) a driver and check a result (waits until operation ends)."""
    modprobe_cmd = modprobe_driver(machine, parameters, options)
    modprobe_success = modprobe_driver_check(machine, modprobe_cmd)
    if modprobe_success:
        return driver_check(machine)

    logger.error('Modprobe failed')
    return False
