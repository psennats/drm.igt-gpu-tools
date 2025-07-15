#!/usr/bin/env python3
# pylint: disable=C0301,R0914,R0915,R1702
# SPDX-License-Identifier: (GPL-2.0 OR MIT)

## Copyright (C) 2023    Intel Corporation                 ##
## Author: Mauro Carvalho Chehab <mchehab@kernel.org>      ##
##                                                         ##
## Allow keeping inlined test documentation and validate   ##
## if the documentation is kept updated.                   ##

"""Maintain test plan and test implementation documentation on IGT."""

import argparse
import os
import re
import sys

from test_list import TestList

class IgtTestList(TestList):
    def __init__(self, *args, **kwargs):
        self.split_regex = re.compile(r",\s*")
        self.default_gpu = "default"

        super().__init__(*args, **kwargs)

    def _get_run_type_drivers(self, run_types):
        """
            Ancillary routine to return drivers and run type set from
            a run type string.
        """

        run_type_dict = {}
        for run_type in set(self.split_regex.split(run_types)):
            driver_set = set()
            if not run_type:
                run_type = "other"
            else:
                for driver in self.drivers:
                    result = re.sub(r"^" + driver + r"[\W_]*", "", run_type, flags = re.IGNORECASE)
                    if result != run_type:
                        driver_set = set([driver])
                        run_type = result
                        break

            if not driver_set:
                driver_set = set(self.drivers)

            if run_type in run_type_dict:
                run_type_dict[run_type].update(driver_set)
            else:
                run_type_dict[run_type] = driver_set

        return run_type_dict

    """
        This class implements testlist generation as expected by Intel CI.
        It does that by handling test lists split by "Run type" and
        using GPU (or configuration) specific fields, being "GPU" for a
        permit list of tests, and "GPU excluded platform" for a block
        list of tests.

        The logic below has "priority zero" rules, which are:

        - if the test is not on any block lists nor it contains
          "GPU" or "GPU excluded platform", it won't be blocked;
        - if the test is in "all" block list, it will be blocked for all
          GPUs. Values from "GPU" and "GPU excluded platform" will be ignored.

        If none of the above rules apply, it will handle GPU positive
        and negative rules:

        - if "GPU" field is present on such test, the default is
          is to block the test (default_gpu_value = False). If not
          present, the default is to not block (default_gpu_value = True).

        Now, it will check for "GPU" and "GPU excluded platform":

        - it sets the default according to default_gpu_value.

        Then:

        - if "GPU" exists, for each GPU listed on the list, it will
          unblock the test;
        - if "GPU excluded platform" exists, for each GPU listed on
          the list, it will block the test.
    """
    def gen_intelci_testlist(self):
        """Return a list of gpu configs and testlists."""

        subtest_dict = self.expand_dictionary(True)

        # Create a tests_per_list dict
        gpu_set = set()
        tests_per_list = {}

        for driver in set(self.drivers):
            tests_per_list[driver] = {}

        for subname, subtest in subtest_dict.items():
            subname = subname.lower()
            run_types = subtest.get("Run type", "other").lower()

            run_type_dict = self._get_run_type_drivers(run_types)

            for run_type, drivers in run_type_dict.items():
                for driver in drivers:
                    if run_type not in tests_per_list[driver]:
                        tests_per_list[driver][run_type] = {}

                    if subname not in tests_per_list[driver][run_type]:
                        tests_per_list[driver][run_type][subname] = {}

                    if "GPU" in subtest:
                        for gpu in self.split_regex.split(subtest["GPU"]):
                            gpu_set.add(gpu)
                            tests_per_list[driver][run_type][subname][gpu] = True

                    if "GPU excluded platform" in subtest:
                        for gpu in self.split_regex.split(subtest["GPU excluded platform"]):
                            gpu_set.add(gpu)
                            tests_per_list[driver][run_type][subname][gpu] = False

        # Create a testlist dictionary

        testlists = {}

        for driver, run_types in tests_per_list.items():
            testlists[driver] = {}
            testlists[driver][self.default_gpu] = {}

            for run_type, subnames in run_types.items():
                if not run_type:
                    run_type = "other"

                if run_type not in testlists[driver][self.default_gpu]:
                    testlists[driver][self.default_gpu][run_type] = set()

                for subname, gpus in subnames.items():
                    # Globally blocklisted values: ignore subtest
                    if "all" in tests_per_list[driver][run_type][subname]:
                        continue

                    # If GPU field is used, default is to block list
                    default_gpu_value = True
                    for gpu, value in gpus.items():
                        if value:
                            default_gpu_value = False
                            break

                    # Fill default values
                    if default_gpu_value:
                        testlists[driver][self.default_gpu][run_type].add(subname)

                    if not gpus:
                        for gpu in gpu_set:
                            # blocked on all GPUs: ignore subtest
                            if gpu == "all":
                                continue

                            # Nothing blocked: add subtest
                            if gpu not in testlists[driver]:
                                testlists[driver][gpu] = {}

                            if run_type not in testlists[driver][gpu]:
                                testlists[driver][gpu][run_type] = set()

                            testlists[driver][gpu][run_type].add(subname)
                        continue

                    for gpu in gpu_set:
                        value = tests_per_list[driver][run_type][subname].get(gpu, default_gpu_value)

                        if not value:
                            continue

                        if gpu not in testlists[driver]:
                            testlists[driver][gpu] = {}

                        if run_type not in testlists[driver][gpu]:
                            testlists[driver][gpu][run_type] = set()

                        testlists[driver][gpu][run_type].add(subname)

        # Always create a default GPU
        gpu_set.add(self.default_gpu)

        return (testlists, gpu_set)

class IntelciTestlist:
    def __init__(self):
        self.testlists = {}
        self.gpu_set = set()
        self.default_gpu = "default"

    def add(self, testlist, gpu_set):
        # Handle GPUs found at the set to be added
        for driver, gpus in testlist.items():
            if driver not in self.testlists:
                self.testlists[driver] = {}

            for gpu in gpus:
                if gpu not in self.testlists[driver]:
                    self.testlists[driver][gpu] = {}

                for run_type in testlist[driver][gpu]:
                    if run_type not in self.testlists[driver][gpu]:
                        self.testlists[driver][gpu][run_type] = set()

                    self.testlists[driver][gpu][run_type].update(testlist[driver][gpu][run_type])

        # Apply default values to gpus that aren't in common
        if self.gpu_set:
            not_intersecting_gpus = self.gpu_set.symmetric_difference(gpu_set)

            for driver in self.testlists.keys():
                for gpu in not_intersecting_gpus:
                    if gpu not in self.testlists[driver]:
                        self.testlists[driver][gpu] = {}

                    for run_type in self.testlists[driver][gpu].keys():
                        if run_type not in self.testlists[driver][self.default_gpu]:
                            continue

                        default_list = self.testlists[driver][self.default_gpu][run_type]

                        self.testlists[driver][gpu][run_type].update(default_list)

        self.gpu_set.update(gpu_set)


    def write(self, directory):
        '''Create testlist directory (if needed) and files'''

        if not os.path.exists(directory):
            os.makedirs(directory)

        files_written = False

        for driver, gpus in self.testlists.items():
            driver_path = os.path.join(directory, driver)
            try:
                os.makedirs(driver_path)
            except FileExistsError:
                pass

            for gpu, names in gpus.items():
                gpu = re.sub(r"[\W_]+", "-", gpu).lower()

                # "all" is used only as a default value for unlisted GPUs
                if gpu == "all":
                    continue

                dname = os.path.join(driver_path, gpu)
                try:
                    os.makedirs(dname)
                except FileExistsError:
                    pass

                for testlist, subtests in names.items():
                    testlist = re.sub(r"[\W_]+", "-", testlist).lower()
                    testlist = re.sub(r"_+", "_", testlist)

                    if not subtests:
                        if testlist != "other":
                            sys.stderr.write(f"Not creating empty testlist: {dname}/{testlist}\n")
                        continue

                    fname = os.path.join(dname, testlist) + ".testlist"
                    with open(fname, 'w', encoding='utf8') as handler:
                        for sub in sorted(subtests):
                            handler.write (f"{sub}\n")

                    files_written = True

        if not files_written:
            sys.stderr.write("Warning: No Intel CI test files created!")

def main():
    """
    Main logic
    """

    igt_build_path = 'build'

    parser = argparse.ArgumentParser(description = "Print formatted kernel documentation to stdout.",
                                    formatter_class = argparse.ArgumentDefaultsHelpFormatter,
                                    epilog = 'If no action specified, assume --rest.')
    parser.add_argument("--config", required = True, nargs='+',
                        help="JSON file describing the test plan template")
    parser.add_argument("--rest",
                        help="Output documentation from the source files in REST file.")
    parser.add_argument("--per-test", action="store_true",
                        help="Modifies ReST output to print subtests per test.")
    parser.add_argument("--to-json",
                        help="Output test documentation in JSON format as TO_JSON file")
    parser.add_argument("--show-subtests", action="store_true",
                        help="Shows the name of the documented subtests in alphabetical order.")
    parser.add_argument("--sort-field",
                        help="modify --show-subtests to sort output based on SORT_FIELD value")
    parser.add_argument("--filter-field", nargs='*',
                        help="filter subtests based on regular expressions given by FILTER_FIELD=~'regex'")
    parser.add_argument("--check-testlist", action="store_true",
                        help="Compare documentation against IGT built tests.")
    parser.add_argument("--include-plan", action="store_true",
                        help="Include test plans, if any.")
    parser.add_argument("--igt-build-path",
                        help="Path to the IGT build directory. Used by --check-testlist.",
                        default=igt_build_path)
    parser.add_argument("--gen-testlist",
                        help="Generate documentation at the GEN_TESTLIST directory, using SORT_FIELD to split the tests. Requires --sort-field.")
    parser.add_argument("--intelci-testlist",
                        help="Generate testlists for Intel CI integration at the INTELCI_TESTLIST directory.")
    parser.add_argument('--files', nargs='+',
                        help="File name(s) to be processed")

    parse_args = parser.parse_args()

    test_array = []

    # Except for intelci_testlist, all options are handled per config
    # file, as the typical usage is to pass just one config file.

    for config in parse_args.config:
        tests = IgtTestList(config_fname = config,
                            include_plan = parse_args.include_plan,
                            file_list = parse_args.files,
                            igt_build_path = parse_args.igt_build_path)

        if parse_args.filter_field:
            for filter_expr in parse_args.filter_field:
                tests.add_filter(filter_expr)

        run = False
        if parse_args.show_subtests:
            run = True
            tests.show_subtests(parse_args.sort_field)

        if parse_args.check_testlist:
            run = True
            tests.check_tests()

        if parse_args.gen_testlist:
            run = True
            if not parse_args.sort_field:
                sys.exit("Need a field to split the testlists")
            if len(config) > 1:
                sys.exit("Only one config file is supported with --json option")
            tests.gen_testlist(parse_args.gen_testlist, parse_args.sort_field)

        if parse_args.intelci_testlist:
            run = True
            test_array.append(tests)

        if parse_args.to_json:
            run = True
            if len(parse_args.config) > 1:
                sys.exit(f"Only one config file is supported with --json option, but {len(parse_args.config)} specified")

            tests.print_json(parse_args.to_json)

        if not run or parse_args.rest:
            if len(parse_args.config) > 1:
                if parse_args.rest:
                    sys.exit(f"Only one config file is supported with --rest option, but {len(parse_args.config)} specified")

            if parse_args.per_test:
                tests.print_rest_flat(parse_args.rest)
            else:
                tests.print_nested_rest(parse_args.rest)

    if not run and len(parse_args.config) > 1:
        print("Warning: output was shown per-config file")

    # Group testlists altogether when generating intel-ci output
    if parse_args.intelci_testlist:
        intel_testlists = IntelciTestlist()

        for tests in test_array:
            (testlist, gpu_set) = tests.gen_intelci_testlist()
            intel_testlists.add(testlist, gpu_set)

        intel_testlists.write(parse_args.intelci_testlist)

if __name__ == '__main__':
    main()
