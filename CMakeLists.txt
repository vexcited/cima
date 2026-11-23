cmake_minimum_required(VERSION 3.29)

# c2y and the defer TS take clang 22, newer than the one Xcode ships.
if(APPLE AND NOT CMAKE_C_COMPILER AND NOT DEFINED ENV{CC})
  find_program(CMAKE_C_COMPILER clang
    PATHS /opt/homebrew/opt/llvm/bin /usr/local/opt/llvm/bin NO_DEFAULT_PATH)
endif()

project(cima VERSION 0.1.0 LANGUAGES C)

# cmake knows no standard past C23, so the flags are given by hand.
set(CIMA_C2Y -std=c2y -fdefer-ts)
include(CheckCCompilerFlag)
check_c_compiler_flag(-fdefer-ts CIMA_HAS_DEFER)
if(NOT CIMA_HAS_DEFER)
  message(FATAL_ERROR
    "cima is C2y with the defer TS, set CC to clang 22 or later")
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ${PROJECT_IS_TOP_LEVEL})

option(CIMA_BUILD_TESTS "build the cima tests, examples and benchmark" ${PROJECT_IS_TOP_LEVEL})

# a parent project may already provide cilo, otherwise it is fetched.
include(FetchContent)
if(NOT TARGET cilo::cilo)
  FetchContent_Declare(cilo GIT_REPOSITORY https://code.vexcited.com/ciko/cilo.git GIT_TAG main)
  FetchContent_MakeAvailable(cilo)
endif()

add_library(cima STATIC src/cache.c src/encode.c src/json.c src/num.c src/parse.c src/text.c)
add_library(cima::cima ALIAS cima)
target_include_directories(cima PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:include>)
target_link_libraries(cima PUBLIC cilo::cilo)
target_compile_options(cima PRIVATE ${CIMA_C2Y} -Wall -Wextra -Wpedantic)
# strtod_l, uselocale and regcomp sit behind feature macros on glibc.
target_compile_definitions(cima PRIVATE $<IF:$<PLATFORM_ID:Darwin>,_DARWIN_C_SOURCE,_GNU_SOURCE>)
find_library(CIMA_LIBM m)
if(CIMA_LIBM)
  target_link_libraries(cima PRIVATE ${CIMA_LIBM})
endif()
set_target_properties(cima PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(CIMA_BUILD_TESTS)
  enable_testing()
  file(GLOB tests CONFIGURE_DEPENDS tests/*.c)
  file(GLOB examples CONFIGURE_DEPENDS examples/*.c)
  foreach(source ${tests} ${examples})
    get_filename_component(name ${source} NAME_WE)
    get_filename_component(dir ${source} DIRECTORY)
    get_filename_component(dir ${dir} NAME)
    set(target ${dir}_${name})
    add_executable(${target} ${source})
    target_link_libraries(${target} PRIVATE cima)
    target_compile_options(${target} PRIVATE ${CIMA_C2Y} -Wall -Wextra -Wpedantic)
    # glibc hides M_PI under a strict -std.
    target_compile_definitions(${target} PRIVATE $<$<NOT:$<PLATFORM_ID:Darwin>>:_GNU_SOURCE>)
    add_test(NAME ${target} COMMAND ${target})
  endforeach()
  add_executable(cima_bench bench/bench.c)
  target_link_libraries(cima_bench PRIVATE cima)
  target_compile_options(cima_bench PRIVATE ${CIMA_C2Y} -O2 -Wall -Wextra -Wpedantic)
  # an installed yyjson, from brew for instance, adds a row to compare with. nothing else uses it.
  find_path(CIMA_YYJSON_INCLUDE yyjson.h HINTS /opt/homebrew/include /usr/local/include)
  find_library(CIMA_YYJSON_LIBRARY yyjson HINTS /opt/homebrew/lib /usr/local/lib)
  if(CIMA_YYJSON_INCLUDE AND CIMA_YYJSON_LIBRARY)
    target_compile_definitions(cima_bench PRIVATE CIMA_BENCH_YYJSON)
    target_include_directories(cima_bench SYSTEM PRIVATE ${CIMA_YYJSON_INCLUDE})
    target_link_libraries(cima_bench PRIVATE ${CIMA_YYJSON_LIBRARY})
  endif()
endif()
