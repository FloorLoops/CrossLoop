# Compile-only Xtensa ESP32-S3 toolchain. Final firmware linking belongs to
# PlatformIO/Arduino's linker script and runtime, not this standalone project.
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR xtensa-esp32s3)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(X4_PIO_CORE "D:/The Primordial/ChatGPT/Non-Project Chats/2026-11-05/xteink-crosspoint-plugins-settings/work/x4-pio-core" CACHE PATH "Existing pinned PlatformIO core")
set(_xtensa_bin "${X4_PIO_CORE}/packages/toolchain-xtensa-esp-elf/bin")
set(CMAKE_C_COMPILER "${_xtensa_bin}/xtensa-esp32s3-elf-gcc.exe")
set(CMAKE_CXX_COMPILER "${_xtensa_bin}/xtensa-esp32s3-elf-g++.exe")
set(CMAKE_AR "${_xtensa_bin}/xtensa-esp32s3-elf-ar.exe")
set(CMAKE_RANLIB "${_xtensa_bin}/xtensa-esp32s3-elf-ranlib.exe")

if(NOT EXISTS "${CMAKE_C_COMPILER}" OR NOT EXISTS "${CMAKE_AR}")
  message(FATAL_ERROR "Pinned ESP32-S3 toolchain missing at ${_xtensa_bin}")
endif()
