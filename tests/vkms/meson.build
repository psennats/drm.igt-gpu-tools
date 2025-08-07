vkms_progs = [
	'vkms_configfs',
]
vkms_deps = test_deps

foreach prog : vkms_progs
	test_executables += executable(prog, prog + '.c',
				       dependencies : vkms_deps,
				       install_dir : vkmsdir,
				       install_rpath : vkms_rpathdir,
				       install : true)
	test_list += join_paths('vkms', prog)
endforeach
