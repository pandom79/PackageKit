#Uncomment in prod
#libxbps_dep = dependency('libxbps', version: '>=0.59.1')

#Remove in prod from here
#    libxbps_lib = 'xbps'
#    libxbps_lib_dir = '/home/domenico/Programmi/xbps-packagekit/usr/lib'
#    libxbps_header_dir = '/home/domenico/Programmi/xbps-packagekit/usr/include'

#    libxbps_lib = 'xbps'
#    libxbps_lib_dir = '/home/domenico/Programmi/new-xbps-pkgkit/usr/lib'
#    libxbps_header_dir = '/home/domenico/Programmi/new-xbps-pkgkit/usr/include'

    libxbps_lib = 'xbps'
    libxbps_lib_dir = '/home/domenico/Programmi/new-xbps-pkgkit-2.0/usr/lib'
    libxbps_header_dir = '/home/domenico/Programmi/new-xbps-pkgkit-2.0/usr/include'


    libxbps_dep = declare_dependency(
        link_args : ['-L' + libxbps_lib_dir, '-l' + libxbps_lib],
        include_directories : include_directories(libxbps_header_dir)
    )
#To here

shared_module(
    'pk_backend_xbps',
    'pk-backend-xbps.h',
    'pk-backend-xbps.c',
    'pk-xbps-repo.c',
    'pk-xbps-packages.c',
    'pk-xbps-dependencies.c',
    'pk-xbps-updates.c',
    'pk-xbps-search.c',
    'pk-xbps-transaction.c',
    include_directories: packagekit_src_include,
    dependencies: [
        packagekit_glib2_dep,
        libxbps_dep,
        gmodule_dep,
    ],
    c_args: [
        '-DPK_LOG_PREFIX="PACKAGEKIT"',
        '-DG_LOG_DOMAIN="PackageKit-Xbps"',
        '-DPK_BACKEND_DEFAULT_PATH="/bin:/usr/bin:/sbin:/usr/sbin"',
    ],
    install: true,
    install_dir: pk_plugin_dir,
)

install_data(
    'repos.list',
    install_dir: join_paths(get_option('sysconfdir'), 'PackageKit')
)

install_data(
    'xbps.d/repos.conf',
    'xbps.d/xbps.conf',
    'xbps.d/xbps-arch.conf',
    install_dir: join_paths(get_option('sysconfdir'), 'PackageKit', 'xbps.d')
)
