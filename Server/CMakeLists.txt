cmake_minimum_required(VERSION 3.21)

# ---------------------------------------------------------------------------
# vcpkg toolchain resolution.
#
# Resolved before project() because the toolchain file has to be in place for
# the first compiler probe. Honours an explicit -DCMAKE_TOOLCHAIN_FILE, then
# VCPKG_ROOT, then a few conventional install locations.
# ---------------------------------------------------------------------------
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    set(_nhn_vcpkg_candidates "")
    if(DEFINED ENV{VCPKG_ROOT})
        list(APPEND _nhn_vcpkg_candidates "$ENV{VCPKG_ROOT}")
    endif()
    list(APPEND _nhn_vcpkg_candidates
        "C:/tools/vcpkg"
        "C:/vcpkg"
        "C:/dev/vcpkg"
        "$ENV{USERPROFILE}/vcpkg")

    foreach(_candidate IN LISTS _nhn_vcpkg_candidates)
        if(EXISTS "${_candidate}/scripts/buildsystems/vcpkg.cmake")
            set(CMAKE_TOOLCHAIN_FILE "${_candidate}/scripts/buildsystems/vcpkg.cmake"
                CACHE STRING "vcpkg toolchain file")
            message(STATUS "NHN: using vcpkg at ${_candidate}")
            break()
        endif()
    endforeach()

    if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
        message(FATAL_ERROR
            "vcpkg not found. Set VCPKG_ROOT or pass "
            "-DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake")
    endif()
endif()

project(NHNServer
    VERSION 0.1.0
    DESCRIPTION "Arcade multiplayer backend: matchmaking, instance, chat and voice"
    LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "This project targets Windows only (IOCP).")
endif()

if(NOT MSVC)
    message(FATAL_ERROR "This project requires the MSVC toolset.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Every executable lands in one directory so the stack can be launched together.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# ---------------------------------------------------------------------------
# Shared compile settings, consumed by every target via link libraries.
# ---------------------------------------------------------------------------
add_library(nhn_compile_options INTERFACE)
target_compile_options(nhn_compile_options INTERFACE
    /utf-8              # sources and literals are UTF-8, not the ANSI codepage
    /W4
    /permissive-
    /Zc:preprocessor
    /MP
    /wd4100             # unreferenced formal parameter: noisy on virtual overrides
)
target_compile_definitions(nhn_compile_options INTERFACE
    _WIN32_WINNT=0x0A00 # Windows 10
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _CRT_SECURE_NO_WARNINGS
    $<$<CONFIG:Debug>:NHN_DEBUG>
)
# Debug info in every configuration: a minidump without symbols is useless.
target_compile_options(nhn_compile_options INTERFACE $<$<NOT:$<CONFIG:Debug>>:/Zi>)
target_link_options(nhn_compile_options INTERFACE $<$<NOT:$<CONFIG:Debug>>:/DEBUG /OPT:REF /OPT:ICF>)

enable_testing()

add_subdirectory(ServerCore)
add_subdirectory(Protocol)
add_subdirectory(Common)
add_subdirectory(MatchServer)
