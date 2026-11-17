if(ESP_PLATFORM)
    idf_component_register(
        SRCS "src/loggable.cpp" "src/loggable_os.cpp"
        INCLUDE_DIRS "include"
    )
    include(FetchContent)

    FetchContent_Declare(
      fmt
      GIT_REPOSITORY https://github.com/fmtlib/fmt
      GIT_TAG        12.1.0)
    FetchContent_MakeAvailable(fmt)
    # See main/CMakeLists.txt: keeps stdc++ locale/stdexcept machinery out of
    # the link. Consumers must see the same overrides as fmt's own TUs.
    target_compile_options(fmt PUBLIC "-DFMT_THROW(x)=::fmt::assert_fail(__FILE__,__LINE__,\"fmt error\")")
    target_compile_definitions(fmt PUBLIC "-DFMT_USE_LOCALE=0")
    target_link_libraries(${COMPONENT_LIB} PUBLIC fmt::fmt)
else()
    project(loggable)
    set(CMAKE_CXX_STANDARD 20)
    find_package(fmt REQUIRED)
    add_library(loggable STATIC src/loggable.cpp src/loggable_os.cpp)
    target_include_directories(loggable PUBLIC include)
    target_compile_features(loggable PUBLIC cxx_std_20)
    target_link_libraries(loggable PUBLIC fmt::fmt)

    # Host-side unit tests: cmake -S components/loggable -B build-host && cmake --build build-host
    option(LOGGABLE_BUILD_HOST_TESTS "Build host-side unit tests" OFF)
    if(LOGGABLE_BUILD_HOST_TESTS)
        enable_testing()
        add_executable(test_loggable_host test/test_loggable_host.cpp)
        # src/loggable_os.cpp is an empty shell on host; tests bring their own backend.
        target_sources(test_loggable_host PRIVATE src/loggable_os.cpp)
        target_include_directories(test_loggable_host PRIVATE include test)
        target_compile_features(test_loggable_host PRIVATE cxx_std_20)
        target_link_libraries(test_loggable_host PRIVATE fmt::fmt)
        add_test(NAME loggable_host COMMAND test_loggable_host)
    endif()
endif()
