# Installed-config smoke (Batch G P1.5): a foreign mini CMake project
# consuming the INSTALLED package. The staging prefix is passed by the
# driver: -DIMPRINT_STAGING_PREFIX=<cmake --install output>. CI
# (linux-desktop) runs the install + configure + build + run sequence
# directly; test/CMakeLists.txt registers the same sequence with
# ctest --build-and-test for the local path.
cmake_minimum_required(VERSION 3.16)
project(imprint_installed_smoke CXX)

if (NOT IMPRINT_STAGING_PREFIX)
    message(FATAL_ERROR "pass -DIMPRINT_STAGING_PREFIX=<install prefix>")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# the installed shared imcore lives under the staging prefix's bin/;
# the smoke executable joins it so the .so is found at runtime
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

enable_testing()

find_package(imprint 0.1 CONFIG REQUIRED
    PATHS ${IMPRINT_STAGING_PREFIX}
    NO_DEFAULT_PATH)

# the installed shared imcore stays in the staging prefix's lib/; the
# build rpath pins the smoke executable to it (test isolation, no env)
set(CMAKE_BUILD_RPATH ${IMPRINT_STAGING_PREFIX}/lib)

add_executable(installed_smoke main.cpp)
target_link_libraries(installed_smoke PRIVATE imprint::imapp_canvas imprint::snapshot)

add_test(NAME installed_smoke COMMAND installed_smoke)
