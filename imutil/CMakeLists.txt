add_library(
    ${IMUTILLIB} INTERFACE
    include/logging.hpp
)

target_include_directories(
    ${IMUTILLIB}
    INTERFACE
    # P1.5 install/export: the source-tree path serves in-tree consumers;
    # the installed tree reproduces the module's -I root under
    # include/imprint/<module>/include
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/imutil/include>
    $<INSTALL_INTERFACE:include/imprint/imutil/include>
)