cmake_minimum_required(VERSION 3.16)
project(bedrock_server LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
if(NOT SERVER_VERSION)
  set(SERVER_VERSION "dev")      # di-override oleh Maven: -DSERVER_VERSION=${project.version}
endif()

find_package(Threads REQUIRED)

# ---- Server ----
file(GLOB_RECURSE SERVER_SOURCES CONFIGURE_DEPENDS src/main/cpp/*.cpp)
add_executable(bedrock-server ${SERVER_SOURCES})
target_include_directories(bedrock-server PRIVATE src/main/include)
target_compile_definitions(bedrock-server PRIVATE SERVER_VERSION="${SERVER_VERSION}")
target_link_libraries(bedrock-server PRIVATE Threads::Threads)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(bedrock-server PRIVATE -Wall -Wextra -Wpedantic)
endif()

# ---- Unit test (dijalankan Maven pada fase `test` lewat ctest) ----
enable_testing()
add_executable(core-tests src/test/cpp/test_core.cpp)
target_include_directories(core-tests PRIVATE src/main/include)
add_test(NAME core-tests COMMAND core-tests)
