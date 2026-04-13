cmake_minimum_required(VERSION 3.16)

project(quantum_mpi VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION OFF)

include(CTest)
find_package(Threads REQUIRED)

option(
    QUANTUM_MPI_ENABLE_CUNQA_INTEGRATION
    "Build quantum_mpi_main with CUNQA-specific launcher/endpoint/transport."
    OFF
)
set(
    QUANTUM_MPI_CUNQA_SOURCE_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/cunqa"
    CACHE PATH
    "Path to CUNQA source tree."
)
set(
    QUANTUM_MPI_ZMQ_INCLUDE_DIR
    ""
    CACHE PATH
    "Path containing zmq.hpp (required for CUNQA integration)."
)
set(
    QUANTUM_MPI_ZMQ_LIBRARY
    ""
    CACHE FILEPATH
    "Path to libzmq shared/static library (required for CUNQA integration)."
)
set(
    QUANTUM_MPI_SPDLOG_INCLUDE_DIR
    ""
    CACHE PATH
    "Path containing spdlog headers (required for CUNQA integration)."
)

add_subdirectory(lib/quantum_circuit)
add_subdirectory(lib/quantum_mpi)
add_subdirectory(modules/cunqa_runtime)

add_executable(quantum_mpi_main
    src/main.cpp
)

target_include_directories(quantum_mpi_main
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(quantum_mpi_main
    PRIVATE
        quantum_circuit
        quantum_mpi
        Threads::Threads
)

if(QUANTUM_MPI_ENABLE_CUNQA_INTEGRATION)
    target_link_libraries(quantum_mpi_main PRIVATE cunqa_runtime)
    target_compile_definitions(quantum_mpi_main PRIVATE QUANTUM_MPI_WITH_CUNQA=1)
else()
    target_compile_definitions(quantum_mpi_main PRIVATE QUANTUM_MPI_WITH_CUNQA=0)
endif()

target_compile_options(quantum_mpi_main PRIVATE -fno-lto)
target_link_options(quantum_mpi_main PRIVATE -fno-lto)
