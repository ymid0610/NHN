add_executable(VoiceServer
    src/Main.cpp
    src/VoiceRouter.cpp
)

target_include_directories(VoiceServer PRIVATE include)
target_link_libraries(VoiceServer PRIVATE ServerCore Protocol Common nhn_compile_options)
