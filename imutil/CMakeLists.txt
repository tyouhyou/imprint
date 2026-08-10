if (MSVC OR WIN32)
    set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)
endif()

add_library(
    ${IMUTILLIB} INTERFACE
    include/logging.hpp
)

target_include_directories(
    ${IMUTILLIB}
    INTERFACE
    "${PROJECT_SOURCE_DIR}/imutil/include"
)