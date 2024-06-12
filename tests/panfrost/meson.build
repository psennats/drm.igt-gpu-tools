panfrost_progs = [
	'panfrost_get_param',
	'panfrost_gem_new',
	'panfrost_prime',
	'panfrost_submit',
]

foreach prog : panfrost_progs
	test_executables += executable(prog, prog + '.c',
				       dependencies : test_deps,
				       install_dir : panfrostdir,
				       install_rpath : panfrost_rpathdir,
				       install : true)
	test_list += join_paths('panfrost', prog)
endforeach
