set(IXRAY_FOLDER_TESTS FOLDER "Test")

add_subdirectory("benchmark")
set_target_properties(TestBenchmark PROPERTIES ${IXRAY_FOLDER_TESTS})

if(TARGET xrNetServerMultiplayer)
    add_subdirectory("multiplayer/portable")
    set_target_properties(TestMultiplayerUnit PROPERTIES ${IXRAY_FOLDER_TESTS})
endif()
