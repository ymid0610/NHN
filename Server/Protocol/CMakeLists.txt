add_library(Protocol STATIC
    src/GameTypes.cpp
    src/Protocol.cpp
)

target_include_directories(Protocol PUBLIC include)
target_link_libraries(Protocol PUBLIC ServerCore nhn_compile_options)
