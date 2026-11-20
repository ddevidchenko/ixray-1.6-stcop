cmake_minimum_required(VERSION 3.26)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(MultiplayerGnsTests LANGUAGES C CXX)
    enable_testing()
    add_subdirectory("../../../xrNetServer/Multiplayer" xrNetServerMultiplayer)
    if(WIN32)
        message(FATAL_ERROR "On Windows, build TestMultiplayerGns from the engine configuration to use its pinned NuGet runtime.")
    endif()
    # This standalone build only exercises direct IP, so it needs neither ICE nor a second library build.
    set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
    set(ENABLE_ICE OFF)
    set(BUILD_STATIC_LIB OFF)
    include("../../../../cmake/modules/GameNetworkingSockets.cmake")
endif()

add_executable(TestMultiplayerGns GnsTransportTests.cpp GnsCallbackPumpTests.cpp)
target_link_libraries(TestMultiplayerGns PRIVATE Steam::Sockets xrNetServerMultiplayer)
set_target_properties(TestMultiplayerGns PROPERTIES CXX_STANDARD 23 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)

add_test(NAME Multiplayer.GnsLoopback COMMAND TestMultiplayerGns)
set_tests_properties(Multiplayer.GnsLoopback PROPERTIES LABELS multiplayer TIMEOUT 30)

add_test(NAME Multiplayer.GnsReliableSendPressure COMMAND TestMultiplayerGns --reliable-send-pressure)
set_tests_properties(Multiplayer.GnsReliableSendPressure PROPERTIES LABELS multiplayer TIMEOUT 10)

add_test(NAME Multiplayer.GnsSnapshotSendPressure COMMAND TestMultiplayerGns --snapshot-send-pressure)
set_tests_properties(Multiplayer.GnsSnapshotSendPressure PROPERTIES LABELS multiplayer TIMEOUT 10)

add_test(NAME Multiplayer.GnsOutgoingTrafficPressure COMMAND TestMultiplayerGns --outgoing-traffic-pressure)
set_tests_properties(Multiplayer.GnsOutgoingTrafficPressure PROPERTIES LABELS multiplayer TIMEOUT 12)
