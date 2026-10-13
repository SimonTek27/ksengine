# Parity 1.4 — determinism
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_determinism.cpp")
    add_executable(test_determinism test_determinism.cpp)
    target_link_libraries(test_determinism PRIVATE ksengine)
    add_test(NAME test_determinism COMMAND test_determinism)
endif()

# Roadmap 2.6/2.7/2.8/2.9 gate tests and other special-link tests.
# (Recovered body part 2 of tests/ksengine/CMakeLists.txt)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/menu_session_gate_test.cpp")
    add_executable(menu_session_gate_test menu_session_gate_test.cpp)
    target_link_libraries(menu_session_gate_test PRIVATE ksengine)
    add_test(NAME menu_session_gate_test COMMAND menu_session_gate_test)
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/pit_strategy_menu_test.cpp")
    add_executable(pit_strategy_menu_test pit_strategy_menu_test.cpp)
    target_link_libraries(pit_strategy_menu_test PRIVATE ksengine)
    add_test(NAME pit_strategy_menu_test COMMAND pit_strategy_menu_test)
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/pit_queue_test.cpp")
    add_executable(pit_queue_test pit_queue_test.cpp)
    target_link_libraries(pit_queue_test PRIVATE ksengine)
    add_test(NAME pit_queue_test COMMAND pit_queue_test)
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/material_cache_test.cpp")
    add_executable(material_cache_test material_cache_test.cpp)
    target_link_libraries(material_cache_test PRIVATE ksengine)
    add_test(NAME material_cache_test COMMAND material_cache_test)
endif()

if(TARGET ksengine AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_renderer.cpp")
    add_executable(test_renderer test_renderer.cpp)
    target_link_libraries(test_renderer PRIVATE ksengine)
    if(TARGET ks_shaders)
        add_dependencies(test_renderer ks_shaders)
    endif()
    add_test(NAME test_renderer COMMAND test_renderer)
    set_tests_properties(test_renderer PROPERTIES TIMEOUT 120)
endif()
