set(IXRAY_FOLDER_TESTS FOLDER "Test")

add_subdirectory("benchmark")
set_target_properties(TestBenchmark PROPERTIES ${IXRAY_FOLDER_TESTS})

if(TARGET xrNetServerMultiplayer)
    add_subdirectory("multiplayer/portable")
    set_target_properties(TestMultiplayerUnit PROPERTIES ${IXRAY_FOLDER_TESTS})

    if(TARGET Steam::Sockets)
        add_subdirectory("multiplayer/gns")
        set_target_properties(TestMultiplayerGns PROPERTIES ${IXRAY_FOLDER_TESTS})
    endif()
endif()
