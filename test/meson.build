gtest_dep = dependency('gtest', main: true, disabler: true, required: false)
gmock_dep = dependency('gmock', disabler: true, required: false)

if not gtest_dep.found() or not gmock_dep.found()
    gtest_proj = import('cmake').subproject('googletest', required: false)
    if gtest_proj.found()
        gtest_dep = declare_dependency(
            dependencies: [
                dependency('threads'),
                gtest_proj.dependency('gtest'),
                gtest_proj.dependency('gtest_main'),
            ],
        )
        gmock_dep = gtest_proj.dependency('gmock')
    else
        assert(
            not get_option('tests').enabled(),
            'Googletest is required if tests are enabled',
        )
    endif
endif

if gtest_dep.found() and gmock_dep.found()
    root_inc = include_directories('../include')
    test_inc = include_directories('.')

    cereal = dependency('cereal', required: false)
    cpp = meson.get_compiler('cpp')
    has_cereal = cpp.has_header_symbol(
        'cereal/cereal.hpp',
        'cereal::specialize',
        dependencies: cereal,
        required: false,
    )
    if not has_cereal
        cereal_opts = import('cmake').subproject_options()
        cereal_opts.add_cmake_defines(
            {'BUILD_TESTS' : 'OFF', 'SKIP_PERFORMANCE_COMPARISON' : 'ON'},
        )
        cereal_proj = import('cmake').subproject(
            'cereal',
            options: cereal_opts,
            required: false,
        )
        assert(cereal_proj.found(), 'cereal is required')
        cereal = cereal_proj.dependency('cereal')
    endif

    boost_args = [
        '-DBOOST_ALL_NO_LIB',
        '-DBOOST_ASIO_DISABLE_THREADS',
        '-DBOOST_ERROR_CODE_HEADER_ONLY',
        '-DBOOST_NO_RTTI',
        '-DBOOST_NO_TYPEID',
        '-DBOOST_SYSTEM_NO_DEPRECATED',
        '-DBOOST_ASIO_HAS_IO_URING',
    ]

    liburing = dependency('liburing', required: true)

    test_deps = [
        gtest_dep,
        gmock_dep,
        dependency('boost'),
        dependency('phosphor-dbus-interfaces'),
        dependency('phosphor-logging'),
        dependency('sdbusplus'),
        dependency('libsystemd'),
        dependency('openssl'),
        dependency('nlohmann_json', include_type: 'system'),
        cereal,
        liburing,
    ]

    common_sources = [
        '../src/manager.cpp',
        '../src/manager_serialize.cpp',
        '../src/password.cpp',
        '../src/rfutility.cpp',
        '../src/boot_option.cpp',
        '../src/bootvalidflag.cpp',
        '../src/secureboot.cpp',
    ]

    tests = [
        'manager_test',
        'password_test',
        'boot_option_test',
        'manager_serialize_test',
        'rfutility_test',
        'secureboot_test',
        'bootvalidflag_test',
    ]

    foreach t : tests
        exe_sources = [t + '.cpp'] + common_sources
        test(
            t.underscorify(),
            executable(
                t.underscorify(),
                exe_sources,
                include_directories: [root_inc, test_inc],
                dependencies: test_deps,
                cpp_args: boost_args,
            ),
            protocol: 'gtest',
            # Tests create sdbusplus/asio fixtures backed by io_uring; running
            # binaries concurrently can exhaust CI io_uring resources.
            is_parallel: false,
        )
    endforeach

    if 'address' in get_option('b_sanitize')
        add_test_setup(
            'valgrind',
            exclude_suites: [meson.project_name()],
            timeout_multiplier: 10,
        )
    else
        add_test_setup(
            'valgrind',
            exe_wrapper: [
                'valgrind',
                '--error-exitcode=1',
                '--leak-check=full',
                '--show-leak-kinds=definite,indirect',
            ],
            timeout_multiplier: 10,
        )
    endif
endif
