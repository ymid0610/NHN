add_executable(InstanceServer
    src/Instance.cpp
    src/InstanceServer.cpp
    src/Main.cpp
)

target_include_directories(InstanceServer PRIVATE include)
target_link_libraries(InstanceServer PRIVATE ServerCore Protocol Common nhn_compile_options)
