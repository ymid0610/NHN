add_library(ServerCore STATIC
    src/Buffer.cpp
    src/Config.cpp
    src/CoreGlobal.cpp
    src/FatalHandler.cpp
    src/IocpCore.cpp
    src/JobQueue.cpp
    src/Listener.cpp
    src/Log.cpp
    src/Net.cpp
    src/Random.cpp
    src/Service.cpp
    src/Session.cpp
    src/ThreadManager.cpp
    src/Types.cpp
    src/UdpSocket.cpp
    src/WebSocket.cpp
)

target_include_directories(ServerCore PUBLIC include)
target_link_libraries(ServerCore PUBLIC nhn_compile_options)
