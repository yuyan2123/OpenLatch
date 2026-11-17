cmake_minimum_required(VERSION 3.16)
set(SDKCONFIG_DEFAULTS "${CMAKE_CURRENT_LIST_DIR}/firmware/sdkconfig.defaults")
get_filename_component(VENDOR "${CMAKE_CURRENT_LIST_DIR}/../../third_party" ABSOLUTE)
foreach(component pn532_cxx pn532_hal loggable)
    if(NOT EXISTS "${VENDOR}/${component}/CMakeLists.txt")
        message(FATAL_ERROR "Missing vendored component: third_party/${component}")
    endif()
endforeach()
set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/firmware/main"
    "${VENDOR}/pn532_cxx"
    "${VENDOR}/pn532_hal"
    "${VENDOR}/loggable")
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
