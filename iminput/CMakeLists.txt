add_library(
    iminput INTERFACE
    include/input.hpp
)

target_include_directories(
    iminput
    INTERFACE
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/iminput/include>
    $<INSTALL_INTERFACE:include/imprint/iminput/include>
)
