add_library(pn_example STATIC mixer.c)
target_link_libraries(pn_example pn)
target_include_directories(pn_example PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(pn_example_server server.c)
target_link_libraries(pn_example_server pn pn_example pn_uds)

add_executable(pn_example_client client.c)
target_link_libraries(pn_example_client pn pn_uds)
