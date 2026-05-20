add_subdirectory(utils)
add_subdirectory(quantum_circuit)

if(QUANTUM_MPI_ENABLE_CUNQA_INTEGRATION)
    add_subdirectory(cunqa_api)
else()
    message(FATAL_ERROR
        "This project currently requires CUNQA integration. "
        "Set QUANTUM_MPI_ENABLE_CUNQA_INTEGRATION=ON."
    )
endif()

add_subdirectory(quantum_mpi)
