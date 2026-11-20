# Use the installed X4 SDK's own include ordering, limited to runtime/network
# components required by this offline core compile. No substitute headers.
set(_x4_sdk "${X4_PIO_CORE}/packages/framework-arduinoespressif32-libs/esp32s3")
file(READ "${_x4_sdk}/flags/includes" _sdk_include_flags)
separate_arguments(_sdk_include_tokens UNIX_COMMAND "${_sdk_include_flags}")
target_compile_options(netsurf_core PRIVATE "-iprefix${_x4_sdk}/include/")
foreach(_token IN LISTS _sdk_include_tokens)
  if(_token MATCHES "^(newlib|freertos|esp_hw_support|heap|log|soc|hal|esp_rom|esp_common|esp_system|xtensa|esp_timer|lwip|esp_netif|esp_event|vfs)/")
    if(IS_DIRECTORY "${_x4_sdk}/include/${_token}")
      target_compile_options(netsurf_core PRIVATE "SHELL:-iwithprefixbefore ${_token}")
    else()
      # GCC normally ignores absent -I paths in the vendor's aggregate list.
      # Missing required headers still fail compilation; create no substitutes.
      message(STATUS "SDK aggregate lists absent include directory: ${_token}")
    endif()
  endif()
endforeach()
target_include_directories(netsurf_core PRIVATE "${_x4_sdk}/dio_opi/include")
target_compile_definitions(netsurf_core PRIVATE ESP_PLATFORM _GNU_SOURCE IDF_VER="v5.5.5")
