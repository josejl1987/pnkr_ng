
# CPMAddPackage(
#     NAME doctest
#     GITHUB_REPOSITORY doctest/doctest
#     GIT_TAG v2.4.11
# )

find_package(doctest CONFIG REQUIRED)
include(doctest)

add_executable(pnkr_tests
    doctest_main.cpp
    assets/texture_loader_test.cpp
)

target_include_directories(pnkr_tests PRIVATE ${CMAKE_SOURCE_DIR}/engine/include)

# Link against the engine library. 
target_link_libraries(pnkr_tests PRIVATE pnkr_engine doctest::doctest quill::quill)

doctest_discover_tests(pnkr_tests)
