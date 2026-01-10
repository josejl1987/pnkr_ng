
find_package(doctest CONFIG REQUIRED)

add_executable(pnkr_tests
    doctest_main.cpp
    assets/texture_loader_test.cpp
    FiberTests.cpp
        core/Test_JobSystem.cpp
)


target_include_directories(pnkr_tests PRIVATE ${CMAKE_SOURCE_DIR}/engine/include)
target_link_libraries(pnkr_tests PRIVATE pnkr_engine doctest::doctest)

add_test(NAME pnkr_tests COMMAND pnkr_tests)
