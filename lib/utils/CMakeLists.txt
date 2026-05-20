add_library(utils STATIC
    src/utils.cpp
)

target_include_directories(utils
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(utils PUBLIC cxx_std_17)
