add_library(Common STATIC
    src/PeerLink.cpp
    src/ServerApp.cpp
    src/TicketTable.cpp
)

target_include_directories(Common PUBLIC include)
target_link_libraries(Common PUBLIC ServerCore Protocol nhn_compile_options)
