add_executable(MatchServer
    src/ClientSession.cpp
    src/Main.cpp
    src/MatchServer.cpp
    src/PeerRegistry.cpp
    src/Room.cpp
    src/RoomManager.cpp
    src/SessionManager.cpp
)

target_include_directories(MatchServer PRIVATE include)
target_link_libraries(MatchServer PRIVATE ServerCore Protocol Common nhn_compile_options)
