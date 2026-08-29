add_library(
    ${IMEVENTLIB} INTERFACE
    include/event.hpp
    include/event_def.hpp
)

target_include_directories(
    ${IMEVENTLIB}
    INTERFACE
    "${PROJECT_SOURCE_DIR}/imevent/include"
)
