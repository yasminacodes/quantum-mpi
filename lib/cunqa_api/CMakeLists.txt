cmake_minimum_required(VERSION 3.16)

project(cunqa_api LANGUAGES CXX)

add_library(cunqa_api STATIC
    src/cunqa_api.cpp
    src/cunqa_client_logger.cpp
)

target_include_directories(cunqa_api
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${QUANTUM_MPI_CUNQA_INCLUDE_DIR}
)

if(NOT QUANTUM_MPI_ENABLE_CUNQA_INTEGRATION)
    message(FATAL_ERROR "cunqa_api requires QUANTUM_MPI_ENABLE_CUNQA_INTEGRATION=ON.")
endif()

if(NOT EXISTS "${QUANTUM_MPI_CUNQA_INCLUDE_DIR}/comm/client.hpp")
    message(FATAL_ERROR
        "Could not find CUNQA header 'comm/client.hpp' in:\n"
        "  ${QUANTUM_MPI_CUNQA_INCLUDE_DIR}\n"
        "Set QUANTUM_MPI_CUNQA_INCLUDE_DIR to CUNQA's header root (usually <cunqa>/src)."
    )
endif()

if(NOT EXISTS "${QUANTUM_MPI_CUNQA_BUILD_DIR}")
    message(FATAL_ERROR
        "Could not find compiled CUNQA build directory:\n"
        "  ${QUANTUM_MPI_CUNQA_BUILD_DIR}\n"
        "Compile CUNQA first or set QUANTUM_MPI_CUNQA_BUILD_DIR."
    )
endif()

find_library(
    CUNQA_CLIENT_LIBRARY
    NAMES client libclient
    HINTS
        "${QUANTUM_MPI_CUNQA_BUILD_DIR}/src/comm/comm_impl/zmq"
        "${QUANTUM_MPI_CUNQA_BUILD_DIR}/src/comm"
        "${QUANTUM_MPI_CUNQA_BUILD_DIR}/lib"
    NO_DEFAULT_PATH
)

find_library(
    CUNQA_SPDLOG_LIBRARY
    NAMES spdlog libspdlog
    HINTS
        "${QUANTUM_MPI_CUNQA_BUILD_DIR}/lib"
        "${QUANTUM_MPI_CUNQA_BUILD_DIR}/_deps/spdlog-build"
    NO_DEFAULT_PATH
)

find_library(
    CUNQA_ZMQ_LIBRARY
    NAMES zmq libzmq
    HINTS
        "${QUANTUM_MPI_CUNQA_BUILD_DIR}/_deps/libzmq-build/lib"
        "${QUANTUM_MPI_CUNQA_BUILD_DIR}/_deps/libzmq-build"
        "${QUANTUM_MPI_CUNQA_BUILD_DIR}/lib"
    NO_DEFAULT_PATH
)

find_path(
    CUNQA_SPDLOG_INCLUDE_DIR
    NAMES spdlog/spdlog.h
    HINTS
        "${QUANTUM_MPI_CUNQA_BUILD_DIR}/_deps/spdlog-src/include"
)

if(NOT CUNQA_CLIENT_LIBRARY)
    message(FATAL_ERROR
        "Could not find compiled CUNQA client library.\n"
        "Expected libclient.a or libclient.so under:\n"
        "  ${QUANTUM_MPI_CUNQA_BUILD_DIR}/src/comm/comm_impl/zmq\n"
        "  ${QUANTUM_MPI_CUNQA_BUILD_DIR}/src/comm\n"
        "  ${QUANTUM_MPI_CUNQA_BUILD_DIR}/lib\n"
    )
endif()

if(NOT CUNQA_SPDLOG_LIBRARY)
    message(FATAL_ERROR
        "Could not find spdlog library from compiled CUNQA build.\n"
        "Expected libspdlog.so/.a under:\n"
        "  ${QUANTUM_MPI_CUNQA_BUILD_DIR}/lib\n"
        "  ${QUANTUM_MPI_CUNQA_BUILD_DIR}/_deps/spdlog-build\n"
    )
endif()

if(NOT CUNQA_SPDLOG_INCLUDE_DIR)
    message(FATAL_ERROR
        "Could not find spdlog headers for CUNQA client logger shim.\n"
        "Expected spdlog/spdlog.h under:\n"
        "  ${QUANTUM_MPI_CUNQA_BUILD_DIR}/_deps/spdlog-src/include\n"
    )
endif()

if(NOT CUNQA_ZMQ_LIBRARY)
    message(FATAL_ERROR
        "Could not find compiled ZMQ library used by CUNQA.\n"
        "Expected libzmq.a or libzmq.so under:\n"
        "  ${QUANTUM_MPI_CUNQA_BUILD_DIR}/_deps/libzmq-build/lib\n"
        "  ${QUANTUM_MPI_CUNQA_BUILD_DIR}/_deps/libzmq-build\n"
        "  ${QUANTUM_MPI_CUNQA_BUILD_DIR}/lib\n"
    )
endif()

execute_process(
    COMMAND "${CMAKE_CXX_COMPILER}" -print-file-name=libstdc++.so
    OUTPUT_VARIABLE CUNQA_COMPILER_LIBSTDCPP
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

set(CUNQA_COMPILER_RUNTIME_DIR "")
if(CUNQA_COMPILER_LIBSTDCPP
   AND NOT CUNQA_COMPILER_LIBSTDCPP STREQUAL "libstdc++.so"
   AND EXISTS "${CUNQA_COMPILER_LIBSTDCPP}")
    get_filename_component(CUNQA_COMPILER_RUNTIME_DIR "${CUNQA_COMPILER_LIBSTDCPP}" DIRECTORY)
endif()

message(STATUS "Using CUNQA client library: ${CUNQA_CLIENT_LIBRARY}")
message(STATUS "Using CUNQA spdlog library: ${CUNQA_SPDLOG_LIBRARY}")
message(STATUS "Using CUNQA spdlog headers: ${CUNQA_SPDLOG_INCLUDE_DIR}")
message(STATUS "Using CUNQA ZMQ library: ${CUNQA_ZMQ_LIBRARY}")
message(STATUS "Using CUNQA headers from: ${QUANTUM_MPI_CUNQA_INCLUDE_DIR}")
if(CUNQA_COMPILER_RUNTIME_DIR)
    message(STATUS "Using compiler runtime dir for subprocesses: ${CUNQA_COMPILER_RUNTIME_DIR}")
endif()

add_library(cunqa_runtime INTERFACE)

target_include_directories(cunqa_runtime
    INTERFACE
        ${QUANTUM_MPI_CUNQA_INCLUDE_DIR}
)

target_include_directories(cunqa_api
    PRIVATE
        ${CUNQA_SPDLOG_INCLUDE_DIR}
)

target_link_libraries(cunqa_runtime
    INTERFACE
        ${CUNQA_CLIENT_LIBRARY}
        ${CUNQA_SPDLOG_LIBRARY}
        ${CUNQA_ZMQ_LIBRARY}
)

target_link_libraries(cunqa_api
    PUBLIC
        quantum_circuit
        utils
    PRIVATE
        cunqa_runtime
        Threads::Threads
        nlohmann_json::nlohmann_json
)

target_compile_features(cunqa_api PUBLIC cxx_std_20)

target_compile_definitions(cunqa_api
    PRIVATE
        QUANTUM_MPI_CUNQA_BUILD_DIR_COMPILED="${QUANTUM_MPI_CUNQA_BUILD_DIR}"
        QUANTUM_MPI_CXX_RUNTIME_DIR_COMPILED="${CUNQA_COMPILER_RUNTIME_DIR}"
)
