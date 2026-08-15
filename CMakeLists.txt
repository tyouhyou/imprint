cmake_minimum_required(VERSION 3.16)

project(myapp)

set(IMAPPLIB imapp)
set(IMCORELIB imcore)
set(IMUTILLIB imutil)
set(IMEVENTLIB imevent)
set(IMINPUTLIB iminput)
set(IMUILIB imui)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# host shared libs (zbapi) link against the static libs; -fPIC is required
# on linux/elf (msvc and the nds toolchain don't need it)
if (NOT MSVC AND NOT NDS_PLATFORM)
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
endif()

set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -DDEBUG")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

add_subdirectory(imutil)
add_subdirectory(imcore)
add_subdirectory(imevent)
add_subdirectory(iminput)
add_subdirectory(imui)
add_subdirectory(imapp)
add_subdirectory(apps)
add_subdirectory(binding)
add_subdirectory(imshell)

# build-time UI design file packer (validates with the library parser,
# embeds documents as byte arrays). Host tool only: embedded targets
# ship pre-packed documents from their own build.
if (NOT NDS_PLATFORM)
    add_executable(ui_embed tools/ui_embed.cpp imui/src/ui_file.cpp)
    target_include_directories(ui_embed PRIVATE
        ${CMAKE_SOURCE_DIR}/imui/include
        ${CMAKE_SOURCE_DIR}/imutil/include
    )
endif()

add_subdirectory(test)
