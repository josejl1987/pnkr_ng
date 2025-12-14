add_executable(pnkr_multidraw main.cpp)

target_compile_features(pnkr_multidraw PRIVATE cxx_std_20)

target_link_libraries(pnkr_multidraw PRIVATE pnkr_engine)

if(MSVC)
  target_compile_options(pnkr_multidraw PRIVATE /W4)
else()
  target_compile_options(pnkr_multidraw PRIVATE -Wall -Wextra -Wpedantic)
endif()

set_target_properties(pnkr_multidraw PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)


find_program(GLSLC_EXECUTABLE glslc HINTS
        "$ENV{VULKAN_SDK}/Bin"
        "$ENV{VULKAN_SDK}/Bin32"
)
if (NOT GLSLC_EXECUTABLE)
  message(FATAL_ERROR "glslc not found. Install Vulkan SDK and ensure VULKAN_SDK is set.")
endif()


set(MULTIDRAW_SHADER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/shaders")
set(MULTIDRAW_SHADER_OUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/shaders")

file(MAKE_DIRECTORY "${MULTIDRAW_SHADER_OUT_DIR}")

set(MULTIDRAW_SHADERS
        "${MULTIDRAW_SHADER_DIR}/plane_tint.frag"
        "${MULTIDRAW_SHADER_DIR}/cube.vert"
        "${MULTIDRAW_SHADER_DIR}/cube.frag"
)

set(MULTIDRAW_SPV_OUTPUTS "")
foreach(SHADER ${MULTIDRAW_SHADERS})
  get_filename_component(FILE ${SHADER} NAME)
  set(OUT_SPV "${MULTIDRAW_SHADER_OUT_DIR}/${FILE}.spv")

  add_custom_command(
          OUTPUT "${OUT_SPV}"
          COMMAND "${GLSLC_EXECUTABLE}" -o "${OUT_SPV}" "${SHADER}"
          DEPENDS "${SHADER}"
          COMMENT "Compiling shader ${FILE} -> ${FILE}.spv"
          VERBATIM
  )

  list(APPEND MULTIDRAW_SPV_OUTPUTS "${OUT_SPV}")
endforeach()

add_custom_target(multidraw_shaders ALL DEPENDS ${MULTIDRAW_SPV_OUTPUTS})
add_dependencies(pnkr_multidraw multidraw_shaders)

add_custom_command(TARGET pnkr_multidraw POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:pnkr_multidraw>/shaders"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${MULTIDRAW_SHADER_OUT_DIR}"
        "$<TARGET_FILE_DIR:pnkr_multidraw>/shaders"
        COMMENT "Copying SPIR-V shaders to runtime output directory"
)