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

    add_test(NAME quantum_circuit_json_tests COMMAND quantum_circuit_json_tests)
endif()
