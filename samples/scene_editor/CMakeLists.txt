add_executable(pnkr_scene_editor
    main.cpp
    SceneEditorApp.cpp
    SceneEditorApp.hpp
    ui/SceneGraphEditor.cpp
    ui/SceneGraphEditor.hpp
    ui/MaterialEditor.cpp
    ui/MaterialEditor.hpp
)

target_compile_features(pnkr_scene_editor PRIVATE cxx_std_20)

target_link_libraries(pnkr_scene_editor PRIVATE 
    pnkr_engine
        imguizmo::imguizmo
)

set_target_properties(pnkr_scene_editor PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

add_custom_command(TARGET pnkr_scene_editor POST_BUILD
COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:pnkr_scene_editor>/shaders"
COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:pnkr_scene_editor>/assets"
)
