add_library(
    ${IMUTILLIB} INTERFACE
    include/logging.hpp
)

target_include_directories(
    ${IMUTILLIB}
    INTERFACE
    "${PROJECT_SOURCE_DIR}/imutil/include"
)