# copyright defined in LICENSE.txt

include(ExternalProject)

cmake_minimum_required (VERSION 3.10)
set(VERSION_MAJOR 0)
set(VERSION_MINOR 5)
set(VERSION_PATCH 0)
# set(VERSION_SUFFIX alpha)

if(VERSION_SUFFIX)
    set(VERSION_FULL "${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH}-${VERSION_SUFFIX}")
else()
    set(VERSION_FULL "${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH}")
endif()

project(history-tools LANGUAGES CXX)


cmake_policy(SET CMP0077 NEW)
set(ENABLE_INSTALL OFF cache bool "")
set(ENABLE_TOOLS OFF cache bool "")
set(ENABLE_TESTS OFF cache bool "")

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake/Modules")

set(EOSIO_WASM_OLD_BEHAVIOR "Off")
set(Boost_USE_STATIC_LIBS ON)

set( PUBLIC_KEY_LEGACY_PREFIX FO )

add_definitions(-DBOOST_ASIO_DISABLE_STD_EXPERIMENTAL_STRING_VIEW)

include( GNUInstallDirs )

message(STATUS "Building history-tools v${VERSION_FULL} ...")

# find_package(PostgreSQL REQUIRED COMPONENTS Libraries)
find_package(Boost 1.70 REQUIRED COMPONENTS date_time filesystem chrono system iostreams program_options unit_test_framework)

set(SKIP_PQXX_SHARED ON)
set(SKIP_BUILD_TEST ON)
add_subdirectory(external/libpqxx EXCLUDE_FROM_ALL)
set(PQXX_LIBRARIES pqxx_static)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
   if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS 8.0)
      message(FATAL_ERROR "GCC version must be at least 8.0.")
   endif()
   if("${CMAKE_GENERATOR}" STREQUAL "Ninja")
      add_compile_options(-fdiagnostics-color=always)
   endif()
endif()

# add defaults for openssl
if(APPLE AND UNIX AND "${OPENSSL_ROOT_DIR}" STREQUAL "")
   set(OPENSSL_ROOT_DIR "/usr/local/opt/openssl@1.1")
endif()

set(SKIP_FC_TESTS ON)

if(NOT DEFINED SKIP_SUBMODULE_CHECK)
    execute_process(COMMAND git submodule status --recursive
                    COMMAND grep -c "^[+\-]"
                    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
                    OUTPUT_VARIABLE submodule_status
                    OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(submodule_status GREATER 0)
    message(FATAL_ERROR "git submodules are not up to date.
Please run the command 'git submodule update --init --recursive'.")
    endif()
endif()

# Build Types
set(CMAKE_BUILD_TYPE ${CMAKE_BUILD_TYPE}
    CACHE STRING "Choose the type of build, options are: None Debug Release RelWithDebInfo MinSizeRel asan ubsan"
    FORCE)

# AddressSanitize
set(CMAKE_C_FLAGS_ASAN
    "-fsanitize=address -fno-optimize-sibling-calls -fsanitize-address-use-after-scope -fno-omit-frame-pointer -g -O1"
    CACHE STRING "Flags used by the C compiler during AddressSanitizer builds."
    FORCE)
set(CMAKE_CXX_FLAGS_ASAN
    "-fsanitize=address -fno-optimize-sibling-calls -fsanitize-address-use-after-scope -fno-omit-frame-pointer -g -O1"
    CACHE STRING "Flags used by the C++ compiler during AddressSanitizer builds."
    FORCE)

# UndefinedBehaviour
set(CMAKE_C_FLAGS_UBSAN
    "-fsanitize=undefined"
    CACHE STRING "Flags used by the C compiler during UndefinedBehaviourSanitizer builds."
    FORCE)
set(CMAKE_CXX_FLAGS_UBSAN
    "-fsanitize=undefined"
    CACHE STRING "Flags used by the C++ compiler during UndefinedBehaviourSanitizer builds."
    FORCE)

set(WITH_TESTS OFF)

add_compile_options(-Wno-error=shadow)

set(APPBASE_DIR external/appbase)
set(LIBFC_DIR external/fc)

add_subdirectory(libraries)
add_subdirectory(external/flon_abi EXCLUDE_FROM_ALL)
add_subdirectory(${APPBASE_DIR} EXCLUDE_FROM_ALL)
add_subdirectory(${LIBFC_DIR} EXCLUDE_FROM_ALL)

add_executable(fill-pg src/main.cpp src/fill_plugin.cpp src/pg_plugin.cpp src/fill_pg_plugin.cpp src/abieos_sql_converter.cpp)

target_include_directories(fill-pg
    PRIVATE
        external/flon_abi/src
        external/flon_abi/include
        external/flon_abi/external/rapidjson/include
        ${APPBASE_DIR}/include
        ${LIBFC_DIR}/include
        # ${Boost_INCLUDE_DIR}
)
target_link_libraries(fill-pg version appbase fc flon_abi Boost::date_time Boost::filesystem Boost::chrono
                      Boost::system Boost::iostreams Boost::program_options Boost::unit_test_framework
                      "${PQXX_LIBRARIES}" -lpthread)

if(APPLE)
else()
    target_link_libraries(fill-pg -latomic)
endif()
target_compile_options(fill-pg PUBLIC -DAPP_NAME="fill-pg" "-DDEFAULT_PLUGINS=fill_pg_plugin;-DINCLUDE_FILL_PG_PLUGIN")

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_options(fill-pg PRIVATE -D DEBUG)
endif (CMAKE_BUILD_TYPE STREQUAL "Debug")

if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(fill-pg PRIVATE -Wall -Wextra -Wno-unused-parameter -fcolor-diagnostics -Wno-sign-compare -Wno-unused-variable -Wno-macro-redefined)
endif()
if (CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    target_compile_options(fill-pg PRIVATE -fdiagnostics-color=auto)
endif()
enable_testing()
add_subdirectory( unittests )

