cmake_minimum_required(VERSION 3.10)
project(ai_hardware_agent_examples VERSION 0.1.0 LANGUAGES C CXX)

# ====================================================================
#  Global settings
# ====================================================================

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# Enable compile warnings
if(MSVC)
    add_compile_options(/W3)
else()
    add_compile_options(-Wall -Wextra)
endif()

# ====================================================================
#  Examples  (ws63 firmware / win simulator)
# ====================================================================

if(CONVAI_PLATFORM STREQUAL "ws63")
    # ---- C Compile options (RISC-V specific) ----
    add_compile_options(
        -w
        -march=rv32imfc
        -mabi=ilp32f
        --short-enums
        -std=gnu99
        -fasm
        -fcommon
        -Wall -Wextra
        -Winit-self -Wpointer-arith -Wstrict-prototypes
        -fno-strict-aliasing
        -Os
        -fno-unwind-tables
        -ffreestanding
        -fdata-sections -ffunction-sections
        -nostdlib -pipe
        -fno-tree-scev-cprop
        -mpush-pop -msmall-data-limit=0 -fno-ipa-ra
        -g
    )
    add_subdirectory(examples/goldieos)
elseif(CONVAI_PLATFORM STREQUAL "win")
    # ---- Windows simulator (native MinGW gcc/g++) ----
    # Configure with:
    #   cmake -G "MinGW Makefiles" -DCONVAI_PLATFORM=win \
    #         -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ \
    #         -DCMAKE_C_COMPILER_WORKS=1 -DCMAKE_CXX_COMPILER_WORKS=1 \
    #         -DCMAKE_MAKE_PROGRAM=mingw32-make ..
    message(STATUS "CONVAI_PLATFORM=win: building Windows simulator (goldieos.exe)")
    add_subdirectory(examples/goldieos)
else()
    message(STATUS "CONVAI_PLATFORM not set or unsupported: '${CONVAI_PLATFORM}'. Supported platforms: ws63, win")
endif()

