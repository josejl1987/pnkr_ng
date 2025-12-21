add_executable(pnkr_rhi_unlit main.cpp)

target_compile_features(pnkr_rhi_unlit PRIVATE cxx_std_20)
target_link_libraries(pnkr_rhi_unlit PRIVATE pnkr_engine)

set_target_properties(pnkr_rhi_unlit PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# Compile Shaders and generate C++ structs
add_shader_target("shaders/unlit.vert")
add_dependencies(pnkr_rhi_unlit ${LAST_GENERATED_TARGET})

add_shader_target("shaders/unlit.frag")
add_dependencies(pnkr_rhi_unlit ${LAST_GENERATED_TARGET})

target_include_directories(pnkr_rhi_unlit PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")

# Copy assets and shaders to binary dir
add_custom_command(TARGET pnkr_rhi_unlit POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:pnkr_rhi_unlit>/shaders"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_BINARY_DIR}/shaders" "$<TARGET_FILE_DIR:pnkr_rhi_unlit>/shaders"
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:pnkr_rhi_unlit>/assets"
    COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_SOURCE_DIR}/samples/rhiGltfBindless/assets/Duck.glb" "$<TARGET_FILE_DIR:pnkr_rhi_unlit>/assets/Duck.glb"
)
