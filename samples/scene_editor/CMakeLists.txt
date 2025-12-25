add_executable(pnkr_scene_editor 
    main.cpp 
    SceneEditorApp.cpp 
    IndirectRenderer.cpp
    ui/SceneGraphEditor.cpp
    ui/MaterialEditor.cpp
)

target_compile_features(pnkr_scene_editor PRIVATE cxx_std_20)

target_link_libraries(pnkr_scene_editor PRIVATE pnkr_engine  imguizmo::imguizmo)

# Include the common sample utilities
target_include_directories(pnkr_scene_editor PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../common")

if(MSVC)
  target_compile_options(pnkr_scene_editor PRIVATE /W4)
else()
  target_compile_options(pnkr_scene_editor PRIVATE -Wall -Wextra -Wpedantic)
endif()

set_target_properties(pnkr_scene_editor PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# --- Shaders ---
add_shader_target("shaders/scene.vert")
add_dependencies(pnkr_scene_editor ${LAST_GENERATED_TARGET})

add_shader_target("shaders/scene.frag")
add_dependencies(pnkr_scene_editor ${LAST_GENERATED_TARGET})

# Allow the code to find the generated headers
target_include_directories(pnkr_scene_editor PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")

# --- Assets and Binary setup ---
add_custom_command(TARGET pnkr_scene_editor POST_BUILD
        # 1. Create directories
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:pnkr_scene_editor>/shaders"

        # 2. Copy compiled shaders
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_BINARY_DIR}/shaders" "$<TARGET_FILE_DIR:pnkr_scene_editor>/shaders"
)