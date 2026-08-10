# Cross-compilation toolchain for the Nintendo DS (devkitARM / libnds).
#
# Usage:
#   export DEVKITARM=<path to devkitARM>
#   export LIBNDS=<path to libnds>          # usually $DEVKITPRO/libnds
#   cmake -S . -B build_nds \
#       -DCMAKE_TOOLCHAIN_FILE=cmake/nds.toolchain.cmake
#
# Produces a .elf for the NDS ARM9 and, as a POST_BUILD step of the shell
# target, packages it with ndstool into a .nds ROM (build_nds/bin/).

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

if (NOT DEFINED ENV{DEVKITARM})
    message(FATAL_ERROR "DEVKITARM is not set. export DEVKITARM=<path to devkitARM>")
endif()

set(DEVKITARM $ENV{DEVKITARM})
set(DEVKITPRO $ENV{DEVKITPRO})

# LIBNDS defaults to the devkitPro standard layout
if (NOT DEFINED ENV{LIBNDS})
    if (EXISTS "$ENV{DEVKITPRO}/libnds")
        set(LIBNDS "$ENV{DEVKITPRO}/libnds")
    else()
        message(FATAL_ERROR "LIBNDS is not set. export LIBNDS=<path to libnds>")
    endif()
else()
    set(LIBNDS $ENV{LIBNDS})
endif()
set(NDS_ARM ${DEVKITARM}/bin/arm-none-eabi)

set(CMAKE_C_COMPILER ${NDS_ARM}-gcc)
set(CMAKE_CXX_COMPILER ${NDS_ARM}-g++)
set(CMAKE_ASM_COMPILER ${NDS_ARM}-as)

# NDS ARM9 (arm946e-s) target flags; RTTI is not needed (imui avoids
# dynamic_cast), exceptions stay enabled.
set(NDS_CPU_FLAGS "-march=armv5te -mtune=arm946e-s -mthumb -mthumb-interwork")
set(NDS_DEFINES "-DARM9 -D__NDS__")
set(CMAKE_C_FLAGS_INIT "${NDS_CPU_FLAGS} ${NDS_DEFINES}")
set(CMAKE_CXX_FLAGS_INIT "${NDS_CPU_FLAGS} ${NDS_DEFINES} -fno-rtti -fexceptions")
# use calico's ds9.specs (matching the libnds shipped with devkitPro),
# not devkitARM's ds_arm9.specs which expects a newer libnds
set(CMAKE_EXE_LINKER_FLAGS_INIT "-specs=${DEVKITPRO}/calico/share/ds9.specs")

set(CMAKE_EXECUTABLE_SUFFIX ".elf")
# no host runnable during configuration
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# identify the NDS platform to the rest of the build
set(NDS_PLATFORM ON CACHE BOOL "Building for the Nintendo DS" FORCE)

# NDS displays are 16bpp (abgr1555)
set(COLOR_DEPTH 16 CACHE STRING "Pixel color depth in bits (16 or 32). Set 16 for embedded framebuffers." FORCE)

# NDS ARM9 has no FPU: keep the geometry scanlines integer-only (no
# std::sqrt); see the USE_INTEGER_GEOMETRY comment in imcore/CMakeLists.txt
set(USE_INTEGER_GEOMETRY ON CACHE BOOL "Switch it to ON for FPU-less embedded targets (integer-only circle/ellipse bounds)." FORCE)

# devkitARM's NDS toolchain has no libatomic: zb::SharedPtr switches to
# plain-int (non-atomic) refcounts; the UI is single-threaded, so this is
# safe and avoids a library call per refcount bump. See the
# USE_NON_ATOMIC_PTR comment in imcore/CMakeLists.txt.
set(USE_NON_ATOMIC_PTR ON CACHE BOOL "Switch it to ON for targets without atomic instructions (non-atomic refcounts)." FORCE)
