image_files = [
  '1080p-left.png',
  '1080p-right.png',
  'pass.png',
]

foreach img : image_files
      configure_file(output:img, input:img, copy:true)
endforeach

install_data(sources : image_files, install_dir : datadir)
