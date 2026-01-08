# Ensure the cmake directory is in the module path so we can include ShaderTools
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

# Include the tools
include(ShaderTools)

# Include sample subdirectories
add_subdirectory(rhiTriangle)
add_subdirectory(rhiCube)
add_subdirectory(rhiMillionCubes)
add_subdirectory(rhiComputeTexture)
add_subdirectory(rhiComputedMesh)
add_subdirectory(rhiIndirectGLTF)
add_subdirectory(rhiSprites)
add_subdirectory(rhiOffscreenMipRendering)
add_subdirectory(scene_editor)
add_subdirectory(debug_canvas)
