# SPDX-License-Identifier: MIT
# Copyright © 2024 Intel Corporation

import logging
import logging.config

LOG_CONFIG = {
    "version": 1,
    "formatters": {
        "detailed": {
            "format": "%(asctime)s [%(levelname)s]: %(name)s (%(funcName)s:%(lineno)d) - %(message)s"
        },
        "simple": {"format": "%(levelname)s - %(message)s"},
    },
    "handlers": {
        "console": {
            "class": "logging.StreamHandler",
            "formatter": "detailed",
            "level": "WARNING",
            "stream": "ext://sys.stdout",
        },
        "file": {
            "backupCount": 5,
            "class": "logging.handlers.RotatingFileHandler",
            "filename": "logfile.log",
            "formatter": "detailed",
            "maxBytes": 5242880,
        },
    },
    "root": {
        "handlers": ["console", "file"],
        "level": "DEBUG"
    }
}

logging.config.dictConfig(LOG_CONFIG)

logger = logging.getLogger('VmtbInit')

logger.info('###########################################')
logger.info('#              VM Test Bench              #')
logger.info('#    SR-IOV VM-level validation suite     #')
logger.info('###########################################')
