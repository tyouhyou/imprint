add_library(
    iminput INTERFACE
    include/input.hpp
)

target_include_directories(
    iminput
    INTERFACE
    "${PROJECT_SOURCE_DIR}/iminput/include"
)
