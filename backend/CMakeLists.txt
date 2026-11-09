if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    cmake_minimum_required(VERSION 3.20)
    project(PulseNet VERSION 0.1.0 LANGUAGES C CXX)
endif()

if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT CMAKE_RUNTIME_OUTPUT_DIRECTORY)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
endif()
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Threads REQUIRED)
find_package(Boost 1.74 REQUIRED)
find_package(OpenSSL REQUIRED)

# Vendored SQLite amalgamation (public domain). The backend does not need a
# system SQLite package, and Windows and Linux build the same source.
add_library(sqlite3 STATIC third_party/sqlite/sqlite3.c)
target_include_directories(sqlite3 SYSTEM PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/third_party/sqlite")
target_compile_definitions(sqlite3 PUBLIC SQLITE_OMIT_LOAD_EXTENSION)
target_link_libraries(sqlite3 PUBLIC Threads::Threads ${CMAKE_DL_LIBS})
if(UNIX)
    target_link_libraries(sqlite3 PUBLIC m)
endif()
if(MSVC)
    target_compile_options(sqlite3 PRIVATE /w)
    target_compile_definitions(sqlite3 PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(sqlite3 PRIVATE -w)
endif()

function(pulsenet_set_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /Zc:__cplusplus)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

add_library(pulsenet_core
    src/utilities/logger.cpp
    src/utilities/time.cpp
    src/error.cpp
    src/config.cpp
    src/models/enums.cpp
    src/models/validation.cpp
    src/database/database.cpp
    src/database/statement.cpp
    src/database/migrator.cpp
    src/database/target_repository.cpp
    src/database/result_repository.cpp
    src/database/incident_repository.cpp
    src/monitoring/health.cpp
    src/monitoring/incident_detector.cpp
    src/monitoring/diagnostics.cpp
    src/monitoring/probe_engine.cpp
    src/monitoring/check_service.cpp
    src/monitoring/scheduler.cpp
    src/monitoring/teams_notifier.cpp
    src/networking/sessions.cpp
    src/api/http_server.cpp
)

target_include_directories(pulsenet_core
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
target_include_directories(pulsenet_core SYSTEM PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/third_party")
target_link_libraries(pulsenet_core PUBLIC sqlite3 OpenSSL::SSL OpenSSL::Crypto)
if(TARGET Boost::headers)
    target_link_libraries(pulsenet_core PUBLIC Boost::headers)
elseif(TARGET Boost::boost)
    target_link_libraries(pulsenet_core PUBLIC Boost::boost)
else()
    target_include_directories(pulsenet_core SYSTEM PUBLIC ${Boost_INCLUDE_DIRS})
endif()
target_compile_definitions(pulsenet_core PUBLIC "PULSENET_VERSION=\"${PROJECT_VERSION}\"")
pulsenet_set_warnings(pulsenet_core)

add_executable(pulsenet src/main.cpp)
target_link_libraries(pulsenet PRIVATE pulsenet_core)
pulsenet_set_warnings(pulsenet)

include(CTest)
if(BUILD_TESTING)
    set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

    include(FetchContent)
    FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.zip
        SYSTEM
    )
    FetchContent_MakeAvailable(googletest)

    add_subdirectory(tests)
endif()
