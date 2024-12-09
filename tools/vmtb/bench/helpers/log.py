# SPDX-License-Identifier: MIT
# Copyright © 2024 Intel Corporation

import errno
import fcntl
import functools
import logging
import os
import typing
from pathlib import Path

from bench import exceptions

logger = logging.getLogger('Host-kmsg')

HOST_DMESG_FILE = Path("/tmp/vm-test-bench-host_dmesg.log.tmp")


class LogDecorators():
    """Read and parse kernel log buffer.
    https://www.kernel.org/doc/Documentation/ABI/testing/dev-kmsg
    """
    @staticmethod
    def read_messages(fd: int) -> typing.List[str]:
        buf_size = 4096
        kmsgs = []
        while True:
            try:
                kmsg = os.read(fd, buf_size)
                kmsgs.append(kmsg.decode())
            except OSError as exc:
                if exc.errno == errno.EAGAIN:
                    break

                if exc.errno == errno.EPIPE:
                    pass
                else:
                    raise
        return kmsgs

    @staticmethod
    def parse_messages(kmsgs: typing.List[str]) -> None:
        for msg in kmsgs:
            header, human = msg.split(';', 1)
            # Get priority/facility field (seq, time, other unused for now)
            prio_fac, _, _, _ = header.split(',', 3)
            level = int(prio_fac) & 0x7 # Syslog priority

            if level <= 2: # KERN_CRIT/ALERT/EMERG
                logger.error("[Error: %s]: %s", level, human.strip())
                raise exceptions.HostError(f'Error in dmesg: {human.strip()}')

            logger.debug("%s", human.strip())

    @classmethod
    def parse_kmsg(cls, func: typing.Callable) -> typing.Callable:
        @functools.wraps(func)
        def parse_wrapper(*args: typing.Any, **kwargs: typing.Optional[typing.Any]) -> typing.Any:
            with open('/dev/kmsg', 'r', encoding='utf-8') as f, \
                 open(HOST_DMESG_FILE, 'a', encoding='utf-8') as dmesg_file:

                fd = f.fileno()
                os.lseek(fd, os.SEEK_SET, os.SEEK_END)
                flags = fcntl.fcntl(fd, fcntl.F_GETFL)
                fcntl.fcntl(fd, fcntl.F_SETFL, flags | os.O_NONBLOCK)

                # Execute actual function
                result = func(*args, **kwargs)

                kmsgs = cls.read_messages(fd)
                dmesg_file.writelines(kmsgs)
                cls.parse_messages(kmsgs)

                return result
        return parse_wrapper
