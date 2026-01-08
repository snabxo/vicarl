cmake_minimum_required(VERSION 4.1)
project(vicarl VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

include(GNUInstallDirs)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(vicarl_options)
include(vicarl_warnings)

include(GenerateExportHeader)
include(CMakePackageConfigHelpers)

# Core sources compiled once
add_library(vicarl_core_obj OBJECT
  src/core/error.c
  src/core/alloc.c
  src/core/codec.c
  src/core/hash.c
  src/model/record.c
  src/model/segment.c
  src/model/merkle.c
  src/store/store_log.c
  src/store/store_sqlite.c  # compile as stub unless SQLITE enabled
  src/ledger/ledger.c
)

set_property(TARGET vicarl_core_obj PROPERTY POSITION_INDEPENDENT_CODE ON)

target_include_directories(vicarl_core_obj
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/generated/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_compile_definitions(vicarl_core_obj
  PUBLIC
    VICARL_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    VICARL_VERSION_MINOR=${PROJECT_VERSION_MINOR}
    VICARL_VERSION_PATCH=${PROJECT_VERSION_PATCH}
)

vicarl_apply_warnings(vicarl_core_obj)

# Static + shared
add_library(vicarl_static STATIC $<TARGET_OBJECTS:vicarl_core_obj>)
set_target_properties(vicarl_static PROPERTIES OUTPUT_NAME vicarl)

add_library(vicarl_shared SHARED $<TARGET_OBJECTS:vicarl_core_obj>)
set_target_properties(vicarl_shared PROPERTIES OUTPUT_NAME vicarl)

target_include_directories(vicarl_shared
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/generated/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_include_directories(vicarl_static
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/generated/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

# On Windows, static .lib name must differ from import .lib produced by shared
if(WIN32)
    set_target_properties(vicarl_static PROPERTIES OUTPUT_NAME vicarl_static)
endif()

# Hide symbols by default on ELF/Mach-O, export only what you mark in headers
set_target_properties(vicarl_shared PROPERTIES
  C_VISIBILITY_PRESET hidden
  VISIBILITY_INLINES_HIDDEN YES
)

# Export header for core shared lib
generate_export_header(vicarl_shared
  BASE_NAME vicarl
  EXPORT_FILE_NAME "${CMAKE_CURRENT_BINARY_DIR}/generated/include/vicarl/vicarl_export.h"
)

# Convenience imported target names for consumers
add_library(vicarl::shared ALIAS vicarl_shared)
add_library(vicarl::static ALIAS vicarl_static)

# SQLite backend (optional)
# store_sqlite.c should compile as a stub when this is OFF.
if(VICARL_ENABLE_SQLITE)
    find_package(SQLite3 REQUIRED)

    target_compile_definitions(vicarl_core_obj PUBLIC VICARL_ENABLE_SQLITE=1)

    # Link SQLite into both core libs. Make it PUBLIC so vicarl_ffi gets it via vicarl_static.
    target_link_libraries(vicarl_shared PUBLIC SQLite::SQLite3)
    target_link_libraries(vicarl_static PUBLIC SQLite::SQLite3)
endif()

# Optional P2P module
if(VICARL_ENABLE_P2P)
    add_library(vicarl_p2p_obj OBJECT
    src/p2p/wire.c
    src/p2p/sync.c
  )
    set_property(TARGET vicarl_p2p_obj PROPERTY POSITION_INDEPENDENT_CODE ON)

    target_include_directories(vicarl_p2p_obj
    PUBLIC
      $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
      $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/generated/include>
      $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
  )

    target_compile_definitions(vicarl_p2p_obj PUBLIC VICARL_ENABLE_P2P=1)
    vicarl_apply_warnings(vicarl_p2p_obj)

    add_library(vicarl_p2p_shared SHARED $<TARGET_OBJECTS:vicarl_p2p_obj>)
    set_target_properties(vicarl_p2p_shared PROPERTIES OUTPUT_NAME vicarl_p2p)
    target_link_libraries(vicarl_p2p_shared PUBLIC vicarl_shared)
    add_library(vicarl::p2p ALIAS vicarl_p2p_shared)
endif()

# FFI facade (shared) for Python/Java
add_library(vicarl_ffi SHARED
  src/ffi/ffi.c
)

# Make FFI a single shared library that *contains* core (easy to ship)
target_link_libraries(vicarl_ffi PRIVATE vicarl_static)

set_target_properties(vicarl_ffi PROPERTIES
  OUTPUT_NAME vicarl_ffi
  C_VISIBILITY_PRESET hidden
  VISIBILITY_INLINES_HIDDEN YES
)

generate_export_header(vicarl_ffi
  BASE_NAME vicarl_ffi
  EXPORT_FILE_NAME "${CMAKE_CURRENT_BINARY_DIR}/generated/include/vicarl/vicarl_ffi_export.h"
)

target_include_directories(vicarl_ffi
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/generated/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

vicarl_apply_warnings(vicarl_ffi)

add_library(vicarl::ffi ALIAS vicarl_ffi)

# Install headers
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated/include/"
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

# Install libraries + export targets
install(TARGETS vicarl_shared vicarl_static vicarl_ffi
  EXPORT vicarlTargets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if(VICARL_ENABLE_P2P)
    install(TARGETS vicarl_p2p_shared
    EXPORT vicarlTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  )
endif()

install(EXPORT vicarlTargets
  FILE vicarlTargets.cmake
  NAMESPACE vicarl::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/vicarl
)

# Package config (find_package(vicarl CONFIG))
configure_package_config_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/cmake/vicarlConfig.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/vicarlConfig.cmake"
  INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/vicarl"
)

write_basic_package_version_file(
  "${CMAKE_CURRENT_BINARY_DIR}/vicarlConfigVersion.cmake"
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion
)

install(FILES
  "${CMAKE_CURRENT_BINARY_DIR}/vicarlConfig.cmake"
  "${CMAKE_CURRENT_BINARY_DIR}/vicarlConfigVersion.cmake"
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/vicarl"
)

include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

add_subdirectory(examples)
