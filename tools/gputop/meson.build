gputop_src = [ 'gputop.c', 'utils.c', 'xe_gputop.c']
executable('gputop', sources : gputop_src,
           install : true,
           install_rpath : bindir_rpathdir,
           dependencies : [igt_deps,lib_igt_perf,lib_igt_drm_clients,lib_igt_drm_fdinfo,lib_igt_profiling,math],
	   install: true)
