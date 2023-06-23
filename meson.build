project('qbootctl', 'c', default_options : ['c_std=c11'])

cc = meson.get_compiler('c')

deps = [
        dependency('zlib'),
]

if not cc.has_header('linux/bsg.h')
        error('linux-headers not found')
endif

src = [
        'qbootctl.c',
        'bootctrl_impl.c',
        'bootctrl_test.c',
        'gpt-utils.c',
        'ufs-bsg.c',
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
