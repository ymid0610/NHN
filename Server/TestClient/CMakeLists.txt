add_executable(TestClient
    src/ClientApp.cpp
    src/Commands.cpp
    src/Main.cpp
)

target_include_directories(TestClient PRIVATE include)
target_link_libraries(TestClient PRIVATE ServerCore Protocol Common nhn_compile_options)
