cmake_minimum_required(VERSION 3.16)
project(SevenToolsBandit CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT WIN32)
  message(FATAL_ERROR "Este proyecto es exclusivamente para Windows (usa Winsock, ICMP y WLAN API). Compila en MSYS2/MinGW64 o con el workflow de GitHub Actions incluido.")
endif()

find_package(SDL2 REQUIRED)
find_package(SDL2_ttf REQUIRED)

add_executable(SevenToolsBandit WIN32 src/main.cpp)

target_include_directories(SevenToolsBandit PRIVATE ${SDL2_INCLUDE_DIRS} ${SDL2_TTF_INCLUDE_DIRS})
target_link_libraries(SevenToolsBandit
  SDL2::SDL2main SDL2::SDL2 SDL2_ttf::SDL2_ttf
  ws2_32 iphlpapi wlanapi ole32
  -static -static-libgcc -static-libstdc++
)

# Copia la carpeta de assets (fuentes) junto al .exe generado
add_custom_command(TARGET SevenToolsBandit POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          ${CMAKE_SOURCE_DIR}/assets $<TARGET_FILE_DIR:SevenToolsBandit>/assets)
