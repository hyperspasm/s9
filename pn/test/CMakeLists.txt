add_executable(test_pn test_pn.c amp_tree.c)
target_link_libraries(test_pn pn)
target_include_directories(test_pn PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
add_test(NAME pn COMMAND test_pn)

# Schema tool for the amp schema, whose manifest MANIFEST.md shows
add_executable(pn_amp_schema amp_schema.c amp_tree.c)
target_link_libraries(pn_amp_schema pn)

# The JavaScript implementation, when Node is available
find_program(NODE node)
if(NODE)
    add_test(NAME pn_js COMMAND ${NODE} --test ${CMAKE_CURRENT_SOURCE_DIR}/../js/test/pn.test.js)
    set_tests_properties(pn_js PROPERTIES TIMEOUT 120)
endif()

# Committed manifests must match what the schema tools produce
pn_add_manifest_test(NAME pn_amp_manifest TOOL pn_amp_schema EXPECTED ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/amp.manifest)
pn_add_manifest_test(NAME pn_mixer_manifest TOOL pn_example_mixer_schema EXPECTED ${CMAKE_CURRENT_SOURCE_DIR}/fixtures/mixer.manifest)
