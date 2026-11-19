add_library(xrNetServerMultiplayer OBJECT SerialSequence.cpp SerialSequence.h)
target_include_directories(xrNetServerMultiplayer PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
set_target_properties(xrNetServerMultiplayer PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
    POSITION_INDEPENDENT_CODE ON
)
