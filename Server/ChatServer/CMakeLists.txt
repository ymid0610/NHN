add_executable(ChatServer
    src/Channel.cpp
    src/ChatServer.cpp
    src/ChatSession.cpp
    src/Main.cpp
)

target_include_directories(ChatServer PRIVATE include)
target_link_libraries(ChatServer PRIVATE ServerCore Protocol Common nhn_compile_options)
