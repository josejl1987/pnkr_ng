
CPMAddPackage(
    NAME doctest
    GITHUB_REPOSITORY doctest/doctest
    GIT_TAG v2.4.11
)

add_executable(pnkr_tests
    doctest_main.cpp
    assets/texture_loader_test.cpp
)

target_include_directories(pnkr_tests PRIVATE ${CMAKE_SOURCE_DIR}/engine/include)

# Link against the engine library. 
# Ensure 'pnkr_engine' is a library target from engine/CMakeLists.txt
target_link_libraries(pnkr_tests PRIVATE pnkr_engine doctest::doctest)

add_test(NAME pnkr_tests COMMAND pnkr_tests)
