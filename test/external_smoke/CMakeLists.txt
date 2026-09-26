# External-consumption smoke (Batch G P1): a foreign mini CMake project.
# It add_subdirectory's the imprint repository — the gates default to
# libraries only in subproject mode — and links the public imprint::
# aliases, exactly the consumption story of ARCHITECTURE §3.2.
# CI (linux-desktop) configures/builds/runs this project directly; the
# parent test/CMakeLists.txt also registers it with ctest --build-and-test
# for the local `ctest -R external_smoke` path.
cmake_minimum_required(VERSION 3.16)
project(imprint_external_smoke CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# the imprint targets route their outputs to bin/; the smoke executable
# joins them so a shared imcore(.dll/.so) is found at runtime
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

enable_testing()

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../.. imprint_build)

add_executable(external_smoke main.cpp)
target_link_libraries(external_smoke PRIVATE imprint::imapp_canvas)

add_test(NAME external_smoke COMMAND external_smoke)
