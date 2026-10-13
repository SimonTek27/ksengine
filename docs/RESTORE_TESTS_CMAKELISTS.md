# Restore tests/ksengine/CMakeLists.txt

If `CMakeLists_body.cmake` is missing, recover the pre-truncate file:

```bash
git show 82172929:tests/ksengine/CMakeLists.txt > tests/ksengine/CMakeLists_body.cmake
```

Then append the E1/E9 block after `scene_bridge_test`:

```cmake
if(TARGET ksengine_c AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/c_api_test.cpp")
    add_executable(c_api_test c_api_test.cpp)
    target_link_libraries(c_api_test PRIVATE ksengine_c)
    add_test(NAME c_api_test COMMAND c_api_test)
endif()
```

Or copy from the artifact `tests_ksengine_CMakeLists.txt` in the project folder.
