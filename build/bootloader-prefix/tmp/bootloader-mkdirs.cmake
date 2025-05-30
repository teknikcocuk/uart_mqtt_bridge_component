# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.5)

file(MAKE_DIRECTORY
  "C:/Espressif/frameworks/esp-idf-v5.3.1/components/bootloader/subproject"
  "C:/esp32_proj/uart_mqtt_bridge_component/build/bootloader"
  "C:/esp32_proj/uart_mqtt_bridge_component/build/bootloader-prefix"
  "C:/esp32_proj/uart_mqtt_bridge_component/build/bootloader-prefix/tmp"
  "C:/esp32_proj/uart_mqtt_bridge_component/build/bootloader-prefix/src/bootloader-stamp"
  "C:/esp32_proj/uart_mqtt_bridge_component/build/bootloader-prefix/src"
  "C:/esp32_proj/uart_mqtt_bridge_component/build/bootloader-prefix/src/bootloader-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "C:/esp32_proj/uart_mqtt_bridge_component/build/bootloader-prefix/src/bootloader-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "C:/esp32_proj/uart_mqtt_bridge_component/build/bootloader-prefix/src/bootloader-stamp${cfgdir}") # cfgdir has leading slash
endif()
