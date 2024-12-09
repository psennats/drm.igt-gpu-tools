# SPDX-License-Identifier: MIT
# Copyright © 2024 Intel Corporation

import json
import logging
import socket
import typing

from bench import exceptions
from bench.machines.virtual.backends.backend_interface import BackendInterface

logger = logging.getLogger('GuestAgent')


class GuestAgentBackend(BackendInterface):
    def __init__(self, socket_path: str, socket_timeout: int) -> None:
        self.sockpath = socket_path
        self.timeout = socket_timeout
        self.sock: socket.socket = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.sock.connect(self.sockpath)
        self.sockf: typing.TextIO = self.sock.makefile(mode='rw', errors='strict')

    def __send(self, command: str, arguments: typing.Optional[typing.Dict] = None) -> typing.Dict:
        if arguments is None:
            arguments = {}

        data = {'execute': command, 'arguments': arguments}
        json.dump(data, self.sockf)
        self.sockf.flush()
        try:
            out: typing.Optional[str] = self.sockf.readline()
        except socket.timeout as soc_to_exc:
            logger.error('Socket readline timeout on command %s', command)
            self.sock.close()
            self.sockf.close()
            raise exceptions.GuestAgentError(f'Socket timed out on {command}') from soc_to_exc
        if out is None:
            logger.error('Command %s, args %s returned with no output')
            raise exceptions.GuestAgentError(f'Command {command} did not retunrned output')
            # Only logging errors for now
        ret: typing.Dict = json.loads(out)
        if 'error' in ret.keys():
            logger.error('Command: %s got error %s', command, ret)

        return ret

    def sync(self, idnum: int) -> typing.Dict:
        return self.__send('guest-sync', {'id': idnum})

    def ping(self) -> typing.Optional[typing.Dict]:
        return self.__send('guest-ping')

    def execute(self, command: str, args: typing.Optional[typing.List[str]] = None) -> typing.Dict:
        if args is None:
            args = []
        arguments = {'path': command, 'arg': args, 'capture-output': True}
        return self.__send('guest-exec', arguments)

    def execute_status(self, pid: int) -> typing.Dict:
        return self.__send('guest-exec-status', {'pid': pid})

    # TODO add qmp-query mechanism for all powerstate changes
    def suspend_disk(self) -> None:
        # self.__send('guest-suspend-disk')
        raise NotImplementedError

    def suspend_ram(self) -> None:
        self.ping()
        # guest-suspend-ram does not return anything, thats why no __send
        data = {'execute': 'guest-suspend-ram'}
        json.dump(data, self.sockf)
        self.sockf.flush()

    def reboot(self) -> None:
        self.ping()
        # guest-shutdown does not return anything, thats why no __send
        data = {'execute': 'guest-shutdown', 'arguments': {'mode': 'reboot'}}
        json.dump(data, self.sockf)
        self.sockf.flush()

    def poweroff(self) -> None:
        self.ping()
        # guest-shutdown does not return anything, thats why no __send
        data = {'execute': 'guest-shutdown', 'arguments': {'mode': 'powerdown'}}
        json.dump(data, self.sockf)
        self.sockf.flush()
        # self.sockf.readline()

    def guest_file_open(self, path: str, mode: str) -> typing.Dict:
        return self.__send('guest-file-open', {'path': path, 'mode': mode})

    def guest_file_close(self, handle: int) -> typing.Dict:
        return self.__send('guest-file-close', {'handle': handle})

    def guest_file_write(self, handle: int, content: str) -> typing.Dict:
        return self.__send('guest-file-write', {'handle': handle, 'buf-b64': content})

    def guest_file_read(self, handle: int) -> typing.Dict:
        return self.__send('guest-file-read', {'handle': handle})
