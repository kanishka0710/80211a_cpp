cmake_minimum_required(VERSION 3.16)

# macOS and Linux/WSL only. Point at a vcpkg install if VCPKG_ROOT is set,
# otherwise dependencies are resolved via find_package()/pkg-config below
# (e.g. Homebrew on macOS, apt/conda on Linux/WSL).
if(DEFINED ENV{VCPKG_ROOT})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" CACHE STRING "")
endif()

project(wifi80211a LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Dependencies ────────────────────────────────────────────────────────────

find_package(FFTW3 CONFIG QUIET)
if(TARGET FFTW3::fftw3)
    # Config already provides a modern imported target (e.g. vcpkg).
elseif(FFTW3_FOUND AND DEFINED FFTW3_LIBRARIES)
    # Some FFTW3Config.cmake files (e.g. Homebrew's) only set legacy
    # FFTW3_LIBRARIES / FFTW3_INCLUDE_DIRS / FFTW3_LIBRARY_DIRS variables
    # instead of creating an imported target. Wrap them in one ourselves.
    find_library(FFTW3_LIBRARY
        NAMES ${FFTW3_LIBRARIES}
        PATHS ${FFTW3_LIBRARY_DIRS}
        NO_DEFAULT_PATH)
    if(NOT FFTW3_LIBRARY)
        message(FATAL_ERROR "FFTW3Config.cmake was found but the library "
            "'${FFTW3_LIBRARIES}' could not be located in '${FFTW3_LIBRARY_DIRS}'.")
    endif()
    add_library(FFTW3::fftw3 UNKNOWN IMPORTED)
    set_target_properties(FFTW3::fftw3 PROPERTIES
        IMPORTED_LOCATION "${FFTW3_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${FFTW3_INCLUDE_DIRS}")
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(FFTW3 REQUIRED IMPORTED_TARGET fftw3)
    add_library(FFTW3::fftw3 ALIAS PkgConfig::FFTW3)
endif()

find_package(Eigen3 CONFIG QUIET)
if(NOT Eigen3_FOUND)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(EIGEN3 REQUIRED IMPORTED_TARGET eigen3)
    add_library(Eigen3::Eigen ALIAS PkgConfig::EIGEN3)
endif()

find_package(Matplot++ CONFIG REQUIRED)

# Some Matplot++ distributions (e.g. Homebrew) only export Matplot++::matplot
# and bundle CImg into it, rather than exposing Matplot++::cimg (as vcpkg does).
# Link cimg only when it exists.
if(TARGET Matplot++::cimg)
    set(WIFI_MATPLOTPP_CIMG_TARGET Matplot++::cimg)

    # Guard against a broken Matplot++ install that baked in Windows/Conda
    # paths (e.g. .../miniconda3/Library/lib/tiff.lib) — this can happen on
    # WSL when a Conda env is on PATH during the vcpkg build.
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
else()
    set(WIFI_MATPLOTPP_CIMG_TARGET "")
endif()

# ── Shared PHY library ──────────────────────────────────────────────────────

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
    src/signal_module.cpp
    src/tx_chain.cpp
    src/rx_chain.cpp
    src/transceiver.cpp
    src/channel_simulator.cpp
)

target_include_directories(wifi_phy
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(wifi_phy
    PUBLIC
        FFTW3::fftw3
        Eigen3::Eigen
)

target_compile_options(wifi_phy PRIVATE -Wall -Wextra -Wpedantic)

# ── App (main.cpp) ──────────────────────────────────────────────────────────

add_executable(wifi80211a_app main.cpp)

target_link_libraries(wifi80211a_app
    PRIVATE
        wifi_phy
        Matplot++::matplot
        ${WIFI_MATPLOTPP_CIMG_TARGET}
)

# Imported Matplot++ usage requirements are not always applied to the
# executable compile line (e.g. CMake "unscanned" CXX rules / stale IDE caches).
target_include_directories(wifi80211a_app SYSTEM PRIVATE
    $<TARGET_PROPERTY:Matplot++::matplot,INTERFACE_INCLUDE_DIRECTORIES>)
target_compile_definitions(wifi80211a_app PRIVATE
    $<TARGET_PROPERTY:Matplot++::matplot,INTERFACE_COMPILE_DEFINITIONS>)

# ── Tests ───────────────────────────────────────────────────────────────────

include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.17.0
)
FetchContent_MakeAvailable(googletest)

add_executable(wifi80211a_tests
    tests/test_forward_error_correction.cpp
    tests/test_preamble.cpp
    tests/test_interleaver.cpp
    tests/test_pilot_utils.cpp
    tests/test_modulation.cpp
    tests/test_signal.cpp
)

target_link_libraries(wifi80211a_tests
    PRIVATE
        wifi_phy
        GTest::gtest_main
)

target_compile_options(wifi80211a_tests PRIVATE -Wall -Wextra -Wpedantic)

include(CTest)
include(GoogleTest)
gtest_discover_tests(wifi80211a_tests)
