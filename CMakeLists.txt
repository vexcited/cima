cmake_minimum_required(VERSION 3.29)
project(cima VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ${PROJECT_IS_TOP_LEVEL})

option(CIMA_BUILD_EXAMPLES "compile the cima examples" ${PROJECT_IS_TOP_LEVEL})

# a parent project may already provide cilo, otherwise it is fetched.
include(FetchContent)
if(NOT TARGET cilo::cilo)
  FetchContent_Declare(cilo GIT_REPOSITORY ssh://git@vexcited.com/ciko/cilo.git GIT_TAG main)
  FetchContent_MakeAvailable(cilo)
endif()

# headers only until the runtime exists.
add_library(cima INTERFACE)
add_library(cima::cima ALIAS cima)
target_include_directories(cima INTERFACE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:include>)
target_link_libraries(cima INTERFACE cilo::cilo)

if(CIMA_BUILD_EXAMPLES)
  # object libraries: nothing links yet, building them checks that the api compiles.
  file(GLOB examples CONFIGURE_DEPENDS examples/*.c)
  add_library(cima_examples OBJECT ${examples})
  target_link_libraries(cima_examples PRIVATE cima)
  target_compile_options(cima_examples PRIVATE -Wall -Wextra -Wpedantic)
endif()
