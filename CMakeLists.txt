cmake_minimum_required(VERSION 3.14)
project(health_tracker CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

find_package(CURL REQUIRED)
find_package(Threads REQUIRED)

# Prefer a system/package-manager install of nlohmann/json; fall back to
# fetching the single-header release if it isn't found.
find_package(nlohmann_json 3.2.0 QUIET)
if(NOT nlohmann_json_FOUND)
  include(FetchContent)
  FetchContent_Declare(
    json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz)
  FetchContent_MakeAvailable(json)
endif()

add_executable(health_tracker
  src/main.cpp
  src/curl_http_client.cpp
  src/env_config.cpp
  src/oauth_client.cpp
  src/data_types.cpp
  src/url_utils.cpp
)

target_include_directories(health_tracker PRIVATE include)
target_link_libraries(health_tracker PRIVATE CURL::libcurl nlohmann_json::nlohmann_json Threads::Threads)
target_compile_options(health_tracker PRIVATE -Wall -Wextra)
