add_library(quantum_mpi STATIC
    src/quantum_mpi.cpp
    src/quantum_mpi_comm.cpp
)

target_include_directories(quantum_mpi
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(quantum_mpi PUBLIC cxx_std_17)

if(BUILD_TESTING)
    add_executable(quantum_mpi_tests
        tests/test_quantum_mpi.cpp
    )

    target_link_libraries(quantum_mpi_tests
        PRIVATE
            quantum_mpi
    )

    add_test(NAME quantum_mpi_tests COMMAND quantum_mpi_tests)
endif()
