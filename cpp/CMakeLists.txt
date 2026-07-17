cmake_minimum_required(VERSION 3.16)
if(DEFINED ENV{VCPKG_ROOT})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" CACHE STRING "")
endif()

# MinGW (GCC) cannot link vcpkg libraries built for MSVC (triplet x64-windows).
# CLion often sets CMAKE_CXX_COMPILER before the first project(); use that hint.
if(WIN32 AND DEFINED CMAKE_CXX_COMPILER)
    string(TOLOWER "${CMAKE_CXX_COMPILER}" _wifi_vcpkg_cxx_lower)
    if(_wifi_vcpkg_cxx_lower MATCHES "mingw|winlibs|w64-mingw32|msys")
        if(NOT VCPKG_TARGET_TRIPLET MATCHES "-mingw")
            message(STATUS "MinGW toolchain detected -> setting VCPKG_TARGET_TRIPLET to x64-mingw-dynamic")
            set(VCPKG_TARGET_TRIPLET "x64-mingw-dynamic" CACHE STRING "" FORCE)
        endif()
    endif()
endif()

project(wifi80211a LANGUAGES CXX)

if(WIN32 AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    if(NOT VCPKG_TARGET_TRIPLET MATCHES "-mingw")
        message(FATAL_ERROR
            "MinGW (GCC) cannot use vcpkg triplet '${VCPKG_TARGET_TRIPLET}' (MSVC-built libs).\n"
            "Delete this build directory and reconfigure with:\n"
            "  -DVCPKG_TARGET_TRIPLET=x64-mingw-dynamic\n"
            "Or switch to an MSVC kit and keep the default x64-windows triplet.")
    endif()
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(FFTW3 CONFIG QUIET)
if(NOT FFTW3_FOUND)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(FFTW3 REQUIRED IMPORTED_TARGET fftw3)
    add_library(FFTW3::fftw3 ALIAS PkgConfig::FFTW3)
endif()
find_package(Matplot++ CONFIG REQUIRED)

# Guard against a broken Matplot++ install that baked in Windows Conda paths
# (e.g. .../miniconda3/Library/lib/tiff.lib) while compiling with Linux g++.
# Do not use find_package(TIFF) here: with Conda on PATH it can resolve to
# Windows tiff.dll / tiff.lib and break the Linux link.
if(UNIX AND TARGET Matplot++::cimg)
    get_target_property(_wifi_cimg_link_libs Matplot++::cimg INTERFACE_LINK_LIBRARIES)
    get_target_property(_wifi_cimg_includes Matplot++::cimg INTERFACE_INCLUDE_DIRECTORIES)
    if(_wifi_cimg_link_libs MATCHES "miniconda3|/mnt/c/|\\.lib(;|$)|\\.dll(;|$)")
        find_library(WIFI_LINUX_TIFF NAMES tiff
            PATHS /usr/lib/x86_64-linux-gnu /usr/lib /usr/local/lib
            NO_DEFAULT_PATH REQUIRED)
        set(_wifi_cimg_fixed_libs "")
        foreach(_lib IN LISTS _wifi_cimg_link_libs)
            if(_lib MATCHES "tiff\\.(lib|dll)$|miniconda3|/mnt/c/")
                if(NOT _wifi_cimg_fixed_libs MATCHES "libtiff")
                    list(APPEND _wifi_cimg_fixed_libs "${WIFI_LINUX_TIFF}")
                endif()
            else()
                list(APPEND _wifi_cimg_fixed_libs "${_lib}")
            endif()
        endforeach()
        set_property(TARGET Matplot++::cimg PROPERTY INTERFACE_LINK_LIBRARIES "${_wifi_cimg_fixed_libs}")
        message(STATUS "Matplot++::cimg: replaced Windows/Conda TIFF with ${WIFI_LINUX_TIFF}")
    endif()
    if(_wifi_cimg_includes MATCHES "miniconda3|/mnt/c/")
        set(_wifi_cimg_fixed_incs "")
        foreach(_inc IN LISTS _wifi_cimg_includes)
            if(NOT _inc MATCHES "miniconda3|/mnt/c/")
                list(APPEND _wifi_cimg_fixed_incs "${_inc}")
            endif()
        endforeach()
        set_property(TARGET Matplot++::cimg PROPERTY INTERFACE_INCLUDE_DIRECTORIES "${_wifi_cimg_fixed_incs}")
        message(STATUS "Matplot++::cimg: dropped Windows/Conda include paths")
    endif()
endif()

add_library(wifi_phy
    src/link_settings.cpp
        src/ofdm_module.cpp
        src/fec_module.cpp
        src/interleaver_module.cpp
        src/modulation_module.cpp
        src/pilot_utils.cpp
        src/equalizer_module.cpp
        src/helpers.cpp
        src/preamble_module.cpp
        src/sync_module.cpp
        src/tx_chain.cpp
        src/rx_chain.cpp
        include/phy/equalizer_module.h
        include/phy/pilot_utils.h
        include/phy/sync_module.h
        include/phy/tx_chain.h
        include/phy/rx_chain.h
)

add_executable(wifi80211a_app
    main.cpp
        include/phy/equalizer_module.h
        src/transceiver.cpp
        include/phy/transceiver.h
        src/channel_simulator.cpp
        include/phy/channel_simulator.h
)

add_executable(test_forward_error_correction
    tests/test_forward_error_correction.cpp
        include/phy/interleaver_module.h
        include/phy/modulation_module.h
        include/phy/equalizer_module.h
        include/phy/transceiver.h
)
target_link_libraries(test_forward_error_correction PRIVATE wifi_phy)

add_executable(loopback_test
    tests/loopback_test.cpp
        include/phy/tx_chain.h
        include/phy/rx_chain.h
        include/phy/channel_simulator.h
        src/channel_simulator.cpp
)
target_link_libraries(loopback_test
    PRIVATE
        wifi_phy
        Matplot++::matplot
        Matplot++::cimg
)
target_include_directories(loopback_test SYSTEM PRIVATE
    $<TARGET_PROPERTY:Matplot++::matplot,INTERFACE_INCLUDE_DIRECTORIES>)
target_compile_definitions(loopback_test PRIVATE
    $<TARGET_PROPERTY:Matplot++::matplot,INTERFACE_COMPILE_DEFINITIONS>)

include(CTest)
add_test(NAME forward_error_correction_scrambler COMMAND test_forward_error_correction)

target_link_libraries(wifi_phy PRIVATE FFTW3::fftw3)

target_include_directories(wifi_phy
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include/
)

target_compile_options(wifi_phy PRIVATE -Wall -Wextra -Wpedantic)

target_link_libraries(wifi80211a_app
    PRIVATE
        wifi_phy
        Matplot++::matplot
        Matplot++::cimg
)

target_include_directories(wifi80211a_app
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include/
)

# Imported Matplot++ target usage requirements are not always applied to the
# executable compile line (e.g. CMake "unscanned" CXX rules / stale IDE caches).
target_include_directories(wifi80211a_app SYSTEM PRIVATE
    $<TARGET_PROPERTY:Matplot++::matplot,INTERFACE_INCLUDE_DIRECTORIES>)
target_compile_definitions(wifi80211a_app PRIVATE
    $<TARGET_PROPERTY:Matplot++::matplot,INTERFACE_COMPILE_DEFINITIONS>)
