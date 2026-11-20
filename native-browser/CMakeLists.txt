cmake_minimum_required(VERSION 3.20)
project(netsurf_native_dependencies C CXX)
set(CMAKE_NINJA_FORCE_RESPONSE_FILE ON)
add_compile_options(-Oz -ffunction-sections -fdata-sections -mlongcalls)
add_compile_definitions(NDEBUG)
option(NETSURF_NATIVE_LTO "Optimize the native browser libraries together at firmware link" OFF)
if(NETSURF_NATIVE_LTO)
  include(CheckIPOSupported)
  check_ipo_supported(RESULT _native_ipo_supported OUTPUT _native_ipo_error LANGUAGES C)
  if(NOT _native_ipo_supported)
    message(FATAL_ERROR "Native browser LTO is unavailable: ${_native_ipo_error}")
  endif()
endif()

# Exact NetSurf 3.11 publisher archive unpacked for the existing host probe.
set(NETSURF_SOURCE_ROOT "D:/crossloop-netsurf-probe-20261114/source/netsurf-all-3.11" CACHE PATH "NetSurf all-in-one source root")
if(NOT EXISTS "${NETSURF_SOURCE_ROOT}/libcss/Makefile" OR
   NOT EXISTS "${NETSURF_SOURCE_ROOT}/inst-monkey/include")
  message(FATAL_ERROR "Expected verified host source and installed/generated headers under ${NETSURF_SOURCE_ROOT}")
endif()
include("${CMAKE_CURRENT_LIST_DIR}/sources.cmake")

set(_public_includes "${NETSURF_SOURCE_ROOT}/inst-monkey/include")
foreach(_lib libwapcaplet libparserutils libhubbub libdom libcss libnsutils)
  list(APPEND _public_includes "${NETSURF_SOURCE_ROOT}/${_lib}/include")
endforeach()

function(add_netsurf_portable_library name)
  string(TOUPPER "${name}" _upper)
  set(_sources "${${_upper}_SOURCES}")
  if(NOT _sources)
    message(FATAL_ERROR "No captured host sources for ${name}")
  endif()
  # NetSurf's optional XML backend requires Expat. HTML uses the Hubbub
  # binding; Expat is deliberately outside this first portable-library gate.
  if(name STREQUAL "libdom")
    list(FILTER _sources EXCLUDE REGEX "^bindings/xml/")
  endif()
  set(_absolute_sources)
  foreach(_source IN LISTS _sources)
    set(_absolute "${NETSURF_SOURCE_ROOT}/${name}/${_source}")
    if(NOT EXISTS "${_absolute}")
      message(FATAL_ERROR "Missing captured source ${_absolute}")
    endif()
    list(APPEND _absolute_sources "${_absolute}")
  endforeach()
  add_library(${name} STATIC ${_absolute_sources})
  target_compile_features(${name} PRIVATE c_std_99)
  target_compile_definitions(${name} PRIVATE _BSD_SOURCE _DEFAULT_SOURCE STMTEXPR=1
    "_ALIGNED=__attribute__((aligned))")
  target_include_directories(${name} PUBLIC ${_public_includes}
    PRIVATE "${NETSURF_SOURCE_ROOT}/${name}/src"
            "${NETSURF_SOURCE_ROOT}/${name}/binding")
endfunction()

add_netsurf_portable_library(libwapcaplet)
add_netsurf_portable_library(libparserutils)
add_netsurf_portable_library(libhubbub)
add_netsurf_portable_library(libdom)
add_netsurf_portable_library(libcss)
add_netsurf_portable_library(libnsutils)

# Header/API compile probe only. Static archives and object compilation do not
# prove that the complete NetSurf renderer can link into PlatformIO firmware.
add_library(native_api_probe OBJECT probe.c)
target_compile_features(native_api_probe PRIVATE c_std_99)
target_include_directories(native_api_probe PRIVATE ${_public_includes})

message(STATUS "ESP32-S3 libraries and frontend compile gate; no linked firmware or device execution")

# Real offline browser-core compile gate. No networking, image codecs or JS.
set(ZLIB_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(ZLIB_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(ZLIB_BUILD_STATIC ON CACHE BOOL "" FORCE)
set(ZLIB_INSTALL OFF CACHE BOOL "" FORCE)
set(ZLIB_PREFIX "z_" CACHE STRING "Keep browser zlib separate from firmware PNGdec" FORCE)
add_subdirectory(third_party/zlib-1.4)
# zlib's public Z_PREFIX mapping omits this internal global; PNGdec also
# embeds it. Keep the browser's definition and references private by name.
target_compile_definitions(zlibstatic PRIVATE z_errmsg=crossloop_ns_z_errmsg)
include("${CMAKE_CURRENT_LIST_DIR}/core-sources.cmake")
set(_core_sources)
foreach(_source IN LISTS NETSURF_CORE_CONTENT_SOURCES NETSURF_CORE_DESKTOP_SOURCES NETSURF_CORE_UTILS_SOURCES)
  set(_absolute "${NETSURF_SOURCE_ROOT}/netsurf/${_source}")
  if(NOT EXISTS "${_absolute}")
    message(FATAL_ERROR "Missing captured browser core source ${_absolute}")
  endif()
  list(APPEND _core_sources "${_absolute}")
endforeach()
add_library(netsurf_core STATIC ${_core_sources})
target_compile_features(netsurf_core PRIVATE c_std_99)
target_compile_definitions(netsurf_core PRIVATE _BSD_SOURCE _DEFAULT_SOURCE STMTEXPR=1
  NETSURF_NO_FILE_FETCHER=1 NETSURF_NO_KNOCKOUT=1 NETSURF_NO_DIAGNOSTIC_PAGES=1
  NETSURF_LOG_LEVEL=WARNING NETSURF_HOMEPAGE="about:blank"
  NETSURF_BUILTIN_LOG_FILTER="level:WARNING"
  NETSURF_BUILTIN_VERBOSE_FILTER="level:DEBUG")
target_include_directories(netsurf_core PRIVATE
  ${_public_includes}
  "${NETSURF_SOURCE_ROOT}/netsurf"
  "${NETSURF_SOURCE_ROOT}/netsurf/include"
  "${NETSURF_SOURCE_ROOT}/netsurf/content/handlers"
  "${NETSURF_SOURCE_ROOT}/netsurf/build/Linux-monkey")
target_link_libraries(netsurf_core PUBLIC zlibstatic libcss libdom libhubbub libparserutils libwapcaplet libnsutils)
include(sdk-includes.cmake)

# Device renderer adapter compile gate. The enclosing firmware owns display,
# fonts and refresh timing; this library has no startup or task side effects.
set(CROSSLOOP_SOURCE "" CACHE PATH "Explicit CrossLoop reader checkout")
set(CROSSLOOP_PIO_ENV "x4pro-native-probe" CACHE STRING "Reader dependency environment")
if(NOT EXISTS "${CROSSLOOP_SOURCE}/lib/GfxRenderer/GfxRenderer.h" OR
   NOT EXISTS "${CROSSLOOP_SOURCE}/.pio/libdeps/${CROSSLOOP_PIO_ENV}/SdFat/src/SdFat.h")
  message(FATAL_ERROR "Set CROSSLOOP_SOURCE and CROSSLOOP_PIO_ENV to the reader checkout being linked")
endif()
add_library(netsurf_frontend STATIC
  native-probe/frontend/FontBridge.cpp
  native-probe/frontend/PlotBridge.cpp
  native-probe/frontend/EngineMemory.cpp
  native-probe/frontend/ProbeScheduler.cpp
  native-probe/frontend/BitmapBridge.cpp
  native-probe/frontend/ProbeResources.c
  native-probe/frontend/NativeFetch.c
  native-probe/frontend/ProbeBrowser.c)
target_compile_features(netsurf_frontend PRIVATE cxx_std_20)
target_compile_options(netsurf_frontend PRIVATE
  "$<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions;-fno-rtti>")
target_compile_definitions(netsurf_frontend PRIVATE
  ESP_PLATFORM ESP32 _GNU_SOURCE IDF_VER="v5.5.5" ARDUINO_ARCH_ESP32 ARDUINO
  FREEINK_DEVICE_X4PRO=1 BOARD_HAS_PSRAM EINK_DISPLAY_SINGLE_BUFFER_MODE=1
  ARDUINO_USB_MODE=1 ARDUINO_USB_CDC_ON_BOOT=1)
target_include_directories(netsurf_frontend PRIVATE
  ${_public_includes} "${NETSURF_SOURCE_ROOT}/netsurf" "${NETSURF_SOURCE_ROOT}/netsurf/include"
  "${CROSSLOOP_SOURCE}/lib/GfxRenderer" "${CROSSLOOP_SOURCE}/lib/EpdFont"
  "${CROSSLOOP_SOURCE}/lib/hal" "${CROSSLOOP_SOURCE}/lib/InflateReader"
  "${CROSSLOOP_SOURCE}/lib/uzlib/src"
  "${CROSSLOOP_SOURCE}/.pio/libdeps/${CROSSLOOP_PIO_ENV}/SdFat/src"
  "${CROSSLOOP_SOURCE}/freeink-sdk/libs/display/FreeInkDisplay/include"
  "${CROSSLOOP_SOURCE}/freeink-sdk/libs/hardware/BoardConfig/include"
  "${X4_PIO_CORE}/packages/framework-arduinoespressif32/cores/esp32"
  "${X4_PIO_CORE}/packages/framework-arduinoespressif32/variants/esp32s3"
  "${X4_PIO_CORE}/packages/framework-arduinoespressif32/libraries/SPI/src"
  "${_x4_sdk}/dio_opi/include")
target_compile_options(netsurf_frontend PRIVATE "-iprefix${_x4_sdk}/include/")
foreach(_token IN LISTS _sdk_include_tokens)
  if(NOT _token STREQUAL "-iwithprefixbefore" AND IS_DIRECTORY "${_x4_sdk}/include/${_token}")
    target_compile_options(netsurf_frontend PRIVATE "SHELL:-iwithprefixbefore ${_token}")
  endif()
endforeach()

# Keep allocation ownership inside the browser libraries. SDK/Wi-Fi/Arduino
# allocations are untouched; no process-wide linker wrapping is used.
foreach(_target libwapcaplet libparserutils libhubbub libdom libcss libnsutils netsurf_core zlibstatic)
  if(NETSURF_NATIVE_LTO)
    set_property(TARGET ${_target} PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
  endif()
  target_compile_definitions(${_target} PRIVATE
    malloc=engine_memory_malloc calloc=engine_memory_calloc realloc=engine_memory_realloc
    free=engine_memory_free strdup=engine_memory_strdup strndup=engine_memory_strndup)
endforeach()
