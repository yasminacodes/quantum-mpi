cmake_minimum_required(VERSION 3.16)

project(quantum_mpi VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION OFF)

include(CTest)

find_package(Threads REQUIRED)
find_package(nlohmann_json REQUIRED)

function(quantum_mpi_append_compiler_runtime_rpath target_name)
    set(_runtime_dirs)

    foreach(_runtime_lib IN ITEMS libstdc++.so libgcc_s.so.1)
        execute_process(
            COMMAND "${CMAKE_CXX_COMPILER}" -print-file-name=${_runtime_lib}
            OUTPUT_VARIABLE _runtime_path
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )

        if(_runtime_path
           AND NOT _runtime_path STREQUAL "${_runtime_lib}"
           AND EXISTS "${_runtime_path}")
            get_filename_component(_runtime_dir "${_runtime_path}" DIRECTORY)
            list(APPEND _runtime_dirs "${_runtime_dir}")
        endif()
    endforeach()

    list(REMOVE_DUPLICATES _runtime_dirs)

    if(_runtime_dirs)
        set_property(TARGET ${target_name} APPEND PROPERTY BUILD_RPATH "${_runtime_dirs}")
        message(STATUS "Added compiler runtime RPATH for ${target_name}: ${_runtime_dirs}")
    endif()
endfunction()

option(
    QUANTUM_MPI_ENABLE_CUNQA_INTEGRATION
    "Build CUNQA integration components."
    ON
)

set(
    QUANTUM_MPI_CUNQA_ROOT_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/cunqa"
    CACHE PATH
    "Path to CUNQA checkout (used only to locate headers/build artifacts)."
)

set(
    QUANTUM_MPI_CUNQA_BUILD_DIR
    "${QUANTUM_MPI_CUNQA_ROOT_DIR}/build"
    CACHE PATH
    "Path to an already compiled CUNQA build directory."
)

set(
    QUANTUM_MPI_CUNQA_INCLUDE_DIR
    "${QUANTUM_MPI_CUNQA_ROOT_DIR}/src"
    CACHE PATH
    "Path containing CUNQA headers (must include comm/client.hpp)."
)

if(QUANTUM_MPI_ENABLE_CUNQA_INTEGRATION)
    message(STATUS "CUNQA root dir: ${QUANTUM_MPI_CUNQA_ROOT_DIR}")
    message(STATUS "CUNQA build dir: ${QUANTUM_MPI_CUNQA_BUILD_DIR}")
    message(STATUS "CUNQA include dir: ${QUANTUM_MPI_CUNQA_INCLUDE_DIR}")
endif()

add_subdirectory(lib)

if(QUANTUM_MPI_ENABLE_CUNQA_INTEGRATION)
    function(quantum_mpi_add_example target_name source_file)
        add_executable(${target_name}
            ${source_file}
        )

        target_include_directories(${target_name}
            PRIVATE
                ${CMAKE_CURRENT_SOURCE_DIR}/src
        )

        target_link_libraries(${target_name}
            PRIVATE
                cunqa_api
                quantum_circuit
                quantum_mpi
                Threads::Threads
                nlohmann_json::nlohmann_json
        )

        quantum_mpi_append_compiler_runtime_rpath(${target_name})
    endfunction()

    quantum_mpi_add_example(simple_telegate src/simple_telegate.cpp)
    quantum_mpi_add_example(distributed_qft src/distributed_qft.cpp)
endif()
