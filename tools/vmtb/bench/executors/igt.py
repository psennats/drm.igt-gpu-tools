# SPDX-License-Identifier: MIT
# Copyright © 2024 Intel Corporation

import enum
import json
import logging
import posixpath
import signal
import typing

from bench.executors.executor_interface import ExecutorInterface
from bench.executors.shell import ShellExecutor
from bench.machines.machine_interface import (DEFAULT_TIMEOUT,
                                              MachineInterface, ProcessResult)

logger = logging.getLogger('IgtExecutor')


class IgtType(enum.Enum):
    EXEC_BASIC = 1
    EXEC_STORE = 2
    SPIN_BATCH = 3


# Mappings of driver specific (i915/xe) IGT instances:
# {IGT type: (i915 IGT name, xe IGT name)}
igt_tests: typing.Dict[IgtType, typing.Tuple[str, str]] = {
    IgtType.EXEC_BASIC: ('igt@gem_exec_basic@basic', 'igt@xe_exec_basic@once-basic'),
    IgtType.EXEC_STORE: ('igt@gem_exec_store@dword', 'igt@xe_exec_store@basic-store'),
    IgtType.SPIN_BATCH: ('igt@gem_spin_batch@legacy', 'igt@xe_spin_batch@spin-basic')
    }


class IgtExecutor(ExecutorInterface):
    def __init__(self, target: MachineInterface,
                 test: typing.Union[str, IgtType],
                 timeout: int = DEFAULT_TIMEOUT) -> None:
        self.igt_config = target.get_igt_config()

        # TODO ld_library_path not used now, need a way to pass this to guest
        #ld_library_path = f'LD_LIBRARY_PATH={igt_config.lib_dir}'
        runner = posixpath.join(self.igt_config.tool_dir, 'igt_runner')
        testlist = '/tmp/igt_executor.testlist'
        command = f'{runner} {self.igt_config.options} ' \
                  f'--test-list {testlist} {self.igt_config.test_dir} {self.igt_config.result_dir}'
        self.results: typing.Dict[str, typing.Any] = {}
        self.target: MachineInterface = target
        self.igt: str = test if isinstance(test, str) else self.select_igt_variant(target.get_drm_driver_name(), test)
        self.target.write_file_content(testlist, self.igt)
        self.timeout: int = timeout

        logger.info("[%s] Execute IGT test: %s", target, self.igt)
        self.pid: int = self.target.execute(command)

    # Executor interface implementation
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

    # IGT specific methods
    def get_results_log(self) -> typing.Dict:
        # Results are cached
        if self.results:
            return self.results
        path = posixpath.join(self.igt_config.result_dir, 'results.json')
        result = self.target.read_file_content(path)
        self.results = json.loads(result)
        return self.results

    def did_pass(self) -> bool:
        results = self.get_results_log()
        totals = results.get('totals')
        if not totals:
            return False
        aggregate = totals.get('root')
        if not aggregate:
            return False

        pass_case = 0
        fail_case = 0
        for key in aggregate:
            if key in ['pass', 'warn', 'dmesg-warn']:
                pass_case = pass_case + aggregate[key]
                continue
            fail_case = fail_case + aggregate[key]

        logger.debug('Full IGT test results:\n%s', json.dumps(results, indent=4))

        if fail_case > 0:
            logger.error('Test failed!')
            return False

        return True

    def select_igt_variant(self, driver: str, igt_type: IgtType) -> str:
        # Select IGT variant dedicated for a given drm driver: xe or i915
        igt = igt_tests[igt_type]
        return igt[1] if driver == 'xe' else igt[0]


def igt_list_subtests(target: MachineInterface, test_name: str) -> typing.List[str]:
    command = f'{target.get_igt_config().test_dir}{test_name} --list-subtests'
    proc_result = ShellExecutor(target, command).wait()
    if proc_result.exit_code == 0:
        return proc_result.stdout.split("\n")
    return []
