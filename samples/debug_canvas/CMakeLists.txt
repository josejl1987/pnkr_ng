add_executable(pnkr_debug_canvas main.cpp)

target_compile_features(pnkr_debug_canvas PRIVATE cxx_std_20)

target_link_libraries(pnkr_debug_canvas PRIVATE pnkr_engine)

if(MSVC)
  target_compile_options(pnkr_debug_canvas PRIVATE /W4)
else()
  target_compile_options(pnkr_debug_canvas PRIVATE -Wall -Wextra -Wpedantic)
endif()

set_target_properties(pnkr_debug_canvas PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

find_program(GLSLC_EXECUTABLE glslc HINTS
        "$ENV{VULKAN_SDK}/Bin"
        "$ENV{VULKAN_SDK}/Bin32"
)
if (NOT GLSLC_EXECUTABLE)
  message(FATAL_ERROR "glslc not found. Install Vulkan SDK and ensure VULKAN_SDK is set.")
endif()

set(DEBUG_CANVAS_SHADER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/shaders")
set(DEBUG_CANVAS_SHADER_OUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/shaders")

file(MAKE_DIRECTORY "${DEBUG_CANVAS_SHADER_OUT_DIR}")

set(DEBUG_CANVAS_SHADERS
        "${DEBUG_CANVAS_SHADER_DIR}/line_canvas.vert"
        "${DEBUG_CANVAS_SHADER_DIR}/line_canvas.frag"
)

set(DEBUG_CANVAS_SPV_OUTPUTS "")
foreach(SHADER ${DEBUG_CANVAS_SHADERS})
  get_filename_component(FILE ${SHADER} NAME)
  set(OUT_SPV "${DEBUG_CANVAS_SHADER_OUT_DIR}/${FILE}.spv")

  add_custom_command(
          OUTPUT "${OUT_SPV}"
          COMMAND "${GLSLC_EXECUTABLE}" -o "${OUT_SPV}" "${SHADER}"
          DEPENDS "${SHADER}"
          COMMENT "Compiling shader ${FILE} -> ${FILE}.spv"
          VERBATIM
  )

  list(APPEND DEBUG_CANVAS_SPV_OUTPUTS "${OUT_SPV}")
endforeach()

add_custom_target(debug_canvas_shaders ALL DEPENDS ${DEBUG_CANVAS_SPV_OUTPUTS})
add_dependencies(pnkr_debug_canvas debug_canvas_shaders)

add_custom_command(TARGET pnkr_debug_canvas POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:pnkr_debug_canvas>/shaders"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${DEBUG_CANVAS_SHADER_OUT_DIR}"
        "$<TARGET_FILE_DIR:pnkr_debug_canvas>/shaders"
        COMMENT "Copying SPIR-V shaders to runtime output directory"
)