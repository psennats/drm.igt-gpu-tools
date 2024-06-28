executable('xe-perf-configs',
           [ 'xe_perf_configs.c' ],
           include_directories: inc,
           dependencies: [lib_igt_chipset, lib_igt_xe_oa],
           install: true)

executable('xe-perf-recorder',
           [ 'xe_perf_recorder.c' ],
           include_directories: inc,
           dependencies: [lib_igt, lib_igt_xe_oa],
           install: true)

executable('xe-perf-control',
           [ 'xe_perf_control.c' ],
           include_directories: inc,
           install: true)

executable('xe-perf-reader',
           [ 'xe_perf_reader.c' ],
           include_directories: inc,
           dependencies: [lib_igt, lib_igt_xe_oa],
           install: true)
