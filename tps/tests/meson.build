test_files = files([
  'close_check.py',
  'execve_check.py',
  'ps_compare.py',
])

foreach file : test_files
  test(
    '@0@'.format(file),
    file,
    args : [exe.full_path()],
    is_parallel : false,
  )
endforeach
