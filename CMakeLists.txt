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

add_executable(packet_semantics_tests tests/packet/test_semantics.cpp)
target_link_libraries(packet_semantics_tests PRIVATE hybrid_lora_packet)
add_test(NAME packet_semantics_tests COMMAND packet_semantics_tests)

add_executable(gateway_state_tests tests/packet/test_gateway_state.cpp lib/gateway_state/gateway_state.cpp)
target_include_directories(gateway_state_tests PRIVATE lib/gateway_state)
target_link_libraries(gateway_state_tests PRIVATE hybrid_lora_packet)
add_test(NAME gateway_state_tests COMMAND gateway_state_tests)
