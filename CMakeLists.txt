cmake_minimum_required(VERSION 3.16)
project(hybrid_lora_packet_tests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(hybrid_lora_packet lib/packet/packet.cpp)
target_include_directories(hybrid_lora_packet PUBLIC lib/packet)

add_executable(packet_tests tests/packet/test_packet.cpp)
target_link_libraries(packet_tests PRIVATE hybrid_lora_packet)

enable_testing()
add_test(NAME packet_tests COMMAND packet_tests)
