project('qbootctl', 'cpp')

deps = [
        dependency('zlib'),
]

src = [
        'qbootctl.cpp',
        'gpt-utils.cpp',
        'ufs-bsg.cpp',
]

inc = [
        include_directories('.'),
]

executable('qbootctl', src,
        include_directories: inc,
        dependencies: deps,
        install: true,
        c_args: [],
)
