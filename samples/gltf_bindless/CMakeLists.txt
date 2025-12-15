add_executable(pnkr_gltf_bindless main.cpp)

target_compile_features(pnkr_gltf_bindless PRIVATE cxx_std_20)

target_link_libraries(pnkr_gltf_bindless PRIVATE pnkr_engine)

if(MSVC)
  target_compile_options(pnkr_gltf_bindless PRIVATE /W4)
else()
  target_compile_options(pnkr_gltf_bindless PRIVATE -Wall -Wextra -Wpedantic)
endif()

set_target_properties(pnkr_gltf_bindless PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)


find_program(GLSLC_EXECUTABLE glslc HINTS
        "$ENV{VULKAN_SDK}/Bin"
        "$ENV{VULKAN_SDK}/Bin32"
)
if (NOT GLSLC_EXECUTABLE)
  message(FATAL_ERROR "glslc not found. Install Vulkan SDK and ensure VULKAN_SDK is set.")
endif()


set(GLTF_SHADER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/shaders")
set(GLTF_SHADER_OUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/shaders")
set(GLTF_TEXTURE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/textures")
set(GLTF_TEXTURE_OUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/shaders")
set(GLTF_ASSETS_OUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/assets")
set(GLTF_ASSETS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/assets")

file(MAKE_DIRECTORY "${GLTF_SHADER_OUT_DIR}")
file(MAKE_DIRECTORY "${GLTF_TEXTURE_OUT_DIR}")
file(MAKE_DIRECTORY "${GLTF_ASSETS_OUT_DIR}")

set(gltf_bindless_shaders
        "${GLTF_SHADER_DIR}/gltf.vert"
        "${GLTF_SHADER_DIR}/gltf.frag"
        "${GLTF_SHADER_DIR}/gltf_bindless.vert"
        "${GLTF_SHADER_DIR}/gltf_bindless.frag"
        "${GLTF_SHADER_DIR}/tonemap.comp"
)

set(GLTF_SPV_OUTPUTS "")
foreach(SHADER ${gltf_bindless_shaders})
  get_filename_component(FILE ${SHADER} NAME)
  set(OUT_SPV "${GLTF_SHADER_OUT_DIR}/${FILE}.spv")

  add_custom_command(
          OUTPUT "${OUT_SPV}"
          COMMAND "${GLSLC_EXECUTABLE}" -o "${OUT_SPV}" "${SHADER}"
          DEPENDS "${SHADER}"
          COMMENT "Compiling shader ${FILE} -> ${FILE}.spv"
          VERBATIM
  )

  list(APPEND GLTF_SPV_OUTPUTS "${OUT_SPV}")
endforeach()

add_custom_target(gltf_bindless_shaders ALL DEPENDS ${GLTF_SPV_OUTPUTS})
add_dependencies(pnkr_gltf_bindless gltf_bindless_shaders)


add_custom_command(TARGET pnkr_gltf_bindless POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:pnkr_gltf_bindless>/textures"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${GLTF_TEXTURE_OUT_DIR}"
        "$<TARGET_FILE_DIR:pnkr_gltf_bindless>/textures"
        COMMENT "Copying textures to runtime output directory"
)

add_custom_command(TARGET pnkr_gltf_bindless POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:pnkr_gltf_bindless>/shaders"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${GLTF_SHADER_OUT_DIR}"
        "$<TARGET_FILE_DIR:pnkr_gltf_bindless>/shaders"
        COMMENT "Copying SPIR-V shaders to runtime output directory"
)

add_custom_command(TARGET pnkr_gltf_bindless POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:pnkr_gltf_bindless>/assets"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${GLTF_ASSETS_DIR}"
        "$<TARGET_FILE_DIR:pnkr_gltf_bindless>/assets"
        COMMENT "Copying SPIR-V shaders to runtime output directory"
)