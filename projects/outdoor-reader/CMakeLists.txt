cmake_minimum_required(VERSION 3.16)
set(SDKCONFIG_DEFAULTS "${CMAKE_CURRENT_LIST_DIR}/firmware/sdkconfig.defaults")
set(UPSTREAM "${CMAKE_CURRENT_LIST_DIR}/HomeKey-ESP32")
foreach(component pn532_cxx pn532_hal loggable)
    if(NOT EXISTS "${UPSTREAM}/components/${component}/CMakeLists.txt")
        message(FATAL_ERROR "Run git submodule update --init --recursive from the repository root")
    endif()
endforeach()
set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/firmware/main"
    "${UPSTREAM}/components/pn532_cxx"
    "${UPSTREAM}/components/pn532_hal"
    "${UPSTREAM}/components/loggable")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(OpenLatchOutdoor VERSION 0.1.0)

# Upstream's quoted -DFMT_THROW macro is not preserved by PlatformIO/SCons on
# Windows. Retain fail-stop behavior with a compiler trap, without quoted text.
foreach(property COMPILE_OPTIONS INTERFACE_COMPILE_OPTIONS)
    get_target_property(options fmt ${property})
    if(options)
        list(FILTER options EXCLUDE REGEX "^-DFMT_THROW")
        set_property(TARGET fmt PROPERTY ${property} "${options}")
    endif()
endforeach()
target_compile_options(fmt PUBLIC "-DFMT_THROW(x)=__builtin_trap()")
