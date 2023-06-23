project('qbootctl', 'cpp', default_options : ['c_std=c11', 'cpp_std=c++17'])

cc = meson.get_compiler('cpp')

deps = [
        dependency('zlib'),
]

if not cc.has_header('linux/bsg.h')
        error('linux-headers not found')
endif

src = [
        'qbootctl.cpp',
        'bootctrl_impl.cpp',
        'bootctrl_test.cpp',
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
