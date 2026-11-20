cmake_minimum_required(VERSION 3.26)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(MultiplayerLegacyCompressionTests LANGUAGES CXX)
    enable_testing()
    if(WIN32)
        message(FATAL_ERROR "On Windows, build TestMultiplayerLegacyCompression from the engine configuration to use its pinned LZO runtime.")
    endif()
    find_path(LZO_INCLUDE_DIR NAMES lzo/lzo1x.h REQUIRED)
    find_library(LZO_LIBRARY NAMES lzo2 REQUIRED)
    add_library(LZO::lzo2 UNKNOWN IMPORTED)
    set_target_properties(LZO::lzo2 PROPERTIES
        IMPORTED_LOCATION "${LZO_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${LZO_INCLUDE_DIR}")
endif()

add_executable(TestMultiplayerLegacyCompression LegacyCompressionTests.cpp)
target_link_libraries(TestMultiplayerLegacyCompression PRIVATE LZO::lzo2)
set_target_properties(TestMultiplayerLegacyCompression PROPERTIES CXX_STANDARD 23 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)

add_test(NAME Multiplayer.LegacyCompression COMMAND TestMultiplayerLegacyCompression)
set_tests_properties(Multiplayer.LegacyCompression PROPERTIES LABELS multiplayer TIMEOUT 15)
