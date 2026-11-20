if(WIN32)
    set(STEAM_SOCKETS_PATH ${CMAKE_BINARY_DIR}/packages/ImeSense.Packages.GameNetworkingSockets.Vcpkg.Runtimes.win-${NUGET_PACKAGE_PLATFORM}.1.4.1)

    set(STEAM_SOCKETS_INCLUDE "${STEAM_SOCKETS_PATH}/native/include/GameNetworkingSockets")
    set(STEAM_SOCKETS_LIB "${STEAM_SOCKETS_PATH}/runtimes/win-x64/native/Release/GameNetworkingSockets.lib")
    set(STEAM_SOCKETS_DLLS
        "${STEAM_SOCKETS_PATH}/runtimes/win-x64/native/Release/GameNetworkingSockets.dll"
        "${STEAM_SOCKETS_PATH}/runtimes/win-x64/native/Release/legacy.dll"
        "${STEAM_SOCKETS_PATH}/runtimes/win-x64/native/Release/libcrypto-3-x64.dll"
        "${STEAM_SOCKETS_PATH}/runtimes/win-x64/native/Release/libssl-3-x64.dll"
        "${STEAM_SOCKETS_PATH}/runtimes/win-x64/native/Release/libprotobuf-lite.dll"
        "${STEAM_SOCKETS_PATH}/runtimes/win-x64/native/Release/libprotobuf.dll"
        "${STEAM_SOCKETS_PATH}/runtimes/win-x64/native/Release/libprotoc.dll"
    )

    add_imported_lib(Steam::Sockets "${STEAM_SOCKETS_INCLUDE}" "${STEAM_SOCKETS_LIB}" "${STEAM_SOCKETS_DLLS}")
else()
    include(FetchContent)
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
    # Match the 1.4.1 headers and runtime used by the Windows package above.
    FetchContent_Declare(GameNetworkingSockets
        URL https://codeload.github.com/ValveSoftware/GameNetworkingSockets/tar.gz/505c697d0abef5da2ff3be35aa4ea3687597c3e9
        URL_HASH SHA256=5c138d89185113d4deb67ad359f6245a77af654326e6eb50923b1d732ca8ad25
        DOWNLOAD_EXTRACT_TIMESTAMP FALSE
    )
    add_compile_definitions(STEAMNETWORKINGSOCKETS_SNP_PARANOIA=0)
    FetchContent_MakeAvailable(GameNetworkingSockets)
    add_library(Steam::Sockets ALIAS GameNetworkingSockets)
endif()
