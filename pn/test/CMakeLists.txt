add_executable(test_pn test_pn.c)
target_link_libraries(test_pn pn)
target_include_directories(test_pn PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
add_test(NAME pn COMMAND test_pn)
