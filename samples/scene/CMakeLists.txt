add_executable(pnkr_scene main.cpp)

target_compile_features(pnkr_scene PRIVATE cxx_std_20)

target_link_libraries(pnkr_scene PRIVATE pnkr_engine)

if(MSVC)
  target_compile_options(pnkr_scene PRIVATE /W4)
else()
  target_compile_options(pnkr_scene PRIVATE -Wall -Wextra -Wpedantic)
endif()

set_target_properties(pnkr_scene PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)


find_program(GLSLC_EXECUTABLE glslc HINTS
        "$ENV{VULKAN_SDK}/Bin"
        "$ENV{VULKAN_SDK}/Bin32"
)
if (NOT GLSLC_EXECUTABLE)
  message(FATAL_ERROR "glslc not found. Install Vulkan SDK and ensure VULKAN_SDK is set.")
endif()


set(SCENE_SHADER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/shaders")
set(SCENE_SHADER_OUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/shaders")

file(MAKE_DIRECTORY "${SCENE_SHADER_OUT_DIR}")

set(SCENE_SHADERS
        "${SCENE_SHADER_DIR}/plane_tint.frag"
        "${SCENE_SHADER_DIR}/cube.vert"
        "${SCENE_SHADER_DIR}/cube.frag"
)

set(SCENE_SPV_OUTPUTS "")
foreach(SHADER ${SCENE_SHADERS})
  get_filename_component(FILE ${SHADER} NAME)
  set(OUT_SPV "${SCENE_SHADER_OUT_DIR}/${FILE}.spv")

  add_custom_command(
          OUTPUT "${OUT_SPV}"
          COMMAND "${GLSLC_EXECUTABLE}" -o "${OUT_SPV}" "${SHADER}"
          DEPENDS "${SHADER}"
          COMMENT "Compiling shader ${FILE} -> ${FILE}.spv"
          VERBATIM
  )

  list(APPEND SCENE_SPV_OUTPUTS "${OUT_SPV}")
endforeach()

add_custom_target(scene_shaders ALL DEPENDS ${SCENE_SPV_OUTPUTS})
add_dependencies(pnkr_scene scene_shaders)

add_custom_command(TARGET pnkr_scene POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:pnkr_scene>/shaders"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${SCENE_SHADER_OUT_DIR}"
        "$<TARGET_FILE_DIR:pnkr_scene>/shaders"
        COMMENT "Copying SPIR-V shaders to runtime output directory"
)