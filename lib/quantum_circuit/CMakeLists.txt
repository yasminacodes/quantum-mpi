cmake_minimum_required(VERSION 3.16)

project(quantum_circuit LANGUAGES CXX)

include(CTest)

add_library(quantum_circuit STATIC
    src/quantum_circuit.cpp
)

target_include_directories(quantum_circuit
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(quantum_circuit PUBLIC cxx_std_17)

if(BUILD_TESTING)
    add_executable(quantum_circuit_json_tests
        tests/test_quantum_circuit_json.cpp
    )

    target_link_libraries(quantum_circuit_json_tests
        PRIVATE
            quantum_circuit
    )

    if(COMMAND quantum_mpi_append_compiler_runtime_rpath)
        quantum_mpi_append_compiler_runtime_rpath(quantum_circuit_json_tests)
    endif()

    add_test(
        NAME quantum_circuit_json_tests
        COMMAND quantum_circuit_json_tests
    )

    add_custom_target(check
        COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
        DEPENDS quantum_circuit_json_tests
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    )
endif()
