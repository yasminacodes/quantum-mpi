add_library(quantum_mpi STATIC
    src/quantum_mpi.cpp
)

target_include_directories(quantum_mpi
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(quantum_mpi PUBLIC cxx_std_20)

target_link_libraries(quantum_mpi
    PUBLIC
        cunqa_api
        quantum_circuit
        utils
)

if(BUILD_TESTING)
    set(QUANTUM_MPI_TEST_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_quantum_mpi.cpp")
    if(EXISTS "${QUANTUM_MPI_TEST_SOURCE}")
        add_executable(quantum_mpi_tests
            tests/test_quantum_mpi.cpp
        )

        target_link_libraries(quantum_mpi_tests
            PRIVATE
                quantum_mpi
        )

        if(COMMAND quantum_mpi_append_compiler_runtime_rpath)
            quantum_mpi_append_compiler_runtime_rpath(quantum_mpi_tests)
        endif()

        add_test(NAME quantum_mpi_tests COMMAND quantum_mpi_tests)
    else()
        message(WARNING
            "BUILD_TESTING=ON but '${QUANTUM_MPI_TEST_SOURCE}' was not found. "
            "Skipping target quantum_mpi_tests."
        )
    endif()
endif()
