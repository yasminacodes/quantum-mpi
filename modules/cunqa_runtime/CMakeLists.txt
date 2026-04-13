if(NOT QUANTUM_MPI_ENABLE_CUNQA_INTEGRATION)
    return()
endif()

if(NOT EXISTS "${QUANTUM_MPI_CUNQA_SOURCE_DIR}/src/comm/client.hpp")
    message(FATAL_ERROR
        "CUNQA integration is ON but no valid CUNQA source tree was found.\n"
        "Set -DQUANTUM_MPI_CUNQA_SOURCE_DIR=/path/to/cunqa."
    )
endif()

if(NOT QUANTUM_MPI_ZMQ_INCLUDE_DIR)
    find_path(QUANTUM_MPI_ZMQ_INCLUDE_DIR zmq.hpp)
endif()
if(NOT QUANTUM_MPI_ZMQ_LIBRARY)
    find_library(QUANTUM_MPI_ZMQ_LIBRARY NAMES zmq)
endif()
if(NOT QUANTUM_MPI_SPDLOG_INCLUDE_DIR)
    find_path(QUANTUM_MPI_SPDLOG_INCLUDE_DIR spdlog/spdlog.h)
endif()

if(NOT QUANTUM_MPI_ZMQ_INCLUDE_DIR OR NOT QUANTUM_MPI_ZMQ_LIBRARY)
    message(FATAL_ERROR
        "CUNQA integration requires ZeroMQ C++ headers (zmq.hpp) and libzmq."
    )
endif()

if(NOT QUANTUM_MPI_SPDLOG_INCLUDE_DIR)
    message(FATAL_ERROR
        "CUNQA integration requires spdlog headers (spdlog/spdlog.h)."
    )
endif()

add_library(cunqa_runtime STATIC
    src/cunqa_runtime.cpp
    src/dummy_logger.cpp
    ${QUANTUM_MPI_CUNQA_SOURCE_DIR}/src/comm/comm_impl/zmq/zmq_client.cpp
)

target_include_directories(cunqa_runtime
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${QUANTUM_MPI_CUNQA_SOURCE_DIR}/src
        ${QUANTUM_MPI_CUNQA_SOURCE_DIR}/src/utils/logger
        ${QUANTUM_MPI_ZMQ_INCLUDE_DIR}
        ${QUANTUM_MPI_SPDLOG_INCLUDE_DIR}
)

target_compile_features(cunqa_runtime PUBLIC cxx_std_17)

target_link_libraries(cunqa_runtime
    PUBLIC
        quantum_mpi
    PRIVATE
        ${QUANTUM_MPI_ZMQ_LIBRARY}
)

target_compile_options(cunqa_runtime PRIVATE -fno-lto)
target_link_options(cunqa_runtime PRIVATE -fno-lto)
