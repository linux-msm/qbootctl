project('qbootctl', 'c', default_options : ['c_std=gnu11'])

cc = meson.get_compiler('c')

if not cc.has_header('linux/bsg.h')
        error('linux-headers not found')
endif

src = [
        'qbootctl.c',
        'bootctrl_impl.c',
        'gpt-utils.c',
        'ufs-bsg.c',
        'crc32.c',
]

inc = [
        include_directories('.'),
]

executable('qbootctl', src,
        include_directories: inc,
        install: true,
        c_args: [],
)
