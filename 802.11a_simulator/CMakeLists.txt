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

find_package(FFTW3 CONFIG REQUIRED)
find_package(Matplot++ CONFIG REQUIRED)

add_library(wifi_phy
    src/link_settings.cpp
        src/ofdm_module.cpp
        src/fec_module.cpp
        src/interleaver_module.cpp
        src/modulation_module.cpp
        src/front_end_module.cpp
        include/phy/front_end_module.h
)

add_executable(wifi80211a_app
    main.cpp
        include/phy/front_end_module.h
)

add_executable(test_forward_error_correction
    tests/test_forward_error_correction.cpp
        include/phy/interleaver_module.h
        include/phy/modulation_module.h
)
target_link_libraries(test_forward_error_correction PRIVATE wifi_phy)

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
