# Finds GLSLangValidator and sets the executable path
find_program(Vulkan_GLSLANG_VALIDATOR_EXECUTABLE glslangValidator
        PATHS
        "$ENV{VULKAN_SDK}/Bin"
        "$ENV{VULKAN_SDK}/Bin32"
        "$ENV{VULKAN_SDK}/bin"
        ENV PATH
        NO_CMAKE_FIND_ROOT_PATH
        DOC "Path to glslangValidator executable"
)

if(NOT Vulkan_GLSLANG_VALIDATOR_EXECUTABLE)
    message(FATAL_ERROR "glslangValidator not found. Please install the Vulkan SDK or ensure it's in your PATH.")
endif()

if(CMAKE_BUILD_TYPE STREQUAL "Debug" OR CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
    # -g: Generate debug information
    set(GLSLANG_DEBUG_FLAGS "-g")
    message("Debug shaders activated")
endif()


# Function to compile shader to SPIR-V
function(add_shader_target SHADER_FILE)
    get_filename_component(SHADER_NAME ${SHADER_FILE} NAME)

    # Generate a unique hash based on the current binary directory
    # This ensures that "cube.vert" in rhiCube and "cube.vert" in rhiMultidraw
    # generate different target names.
    string(MD5 DIR_HASH "${CMAKE_CURRENT_BINARY_DIR}")

    # Build outputs go in the local binary dir to avoid cross-target collisions.
    set(SPV_OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/shaders/${SHADER_NAME}.spv")

    add_custom_command(
            OUTPUT ${SPV_OUTPUT}
            COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/shaders"
            COMMAND ${Vulkan_GLSLANG_VALIDATOR_EXECUTABLE} -V ${GLSLANG_DEBUG_FLAGS} "${CMAKE_CURRENT_SOURCE_DIR}/${SHADER_FILE}" -o "${SPV_OUTPUT}"
            DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${SHADER_FILE}"
            COMMENT "Compiling ${SHADER_NAME}"
    )

    # Unique Target Name: Compile_<filename>_<dir_hash>
    set(TARGET_NAME "Compile_${SHADER_NAME}_${DIR_HASH}")

    add_custom_target(${TARGET_NAME} DEPENDS ${SPV_OUTPUT})

    # Expose the target name so the caller knows what to depend on
    set(LAST_GENERATED_TARGET ${TARGET_NAME} PARENT_SCOPE)
endfunction()

# Function to compile shader with custom preprocessor defines and output name.
function(add_shader_target_with_defines SHADER_FILE OUTPUT_NAME)
    if(NOT OUTPUT_NAME)
        message(FATAL_ERROR "add_shader_target_with_defines requires OUTPUT_NAME")
    endif()

    get_filename_component(OUTPUT_NAME_ONLY ${OUTPUT_NAME} NAME)

    # Generate a unique hash based on the current binary directory
    string(MD5 DIR_HASH "${CMAKE_CURRENT_BINARY_DIR}")

    # Build outputs go in the local binary dir to avoid cross-target collisions.
    set(SPV_OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/shaders/${OUTPUT_NAME_ONLY}.spv")

    set(DEFINE_FLAGS "")
    foreach(def IN LISTS ARGN)
        list(APPEND DEFINE_FLAGS "-D${def}")
    endforeach()

    add_custom_command(
            OUTPUT ${SPV_OUTPUT}
            COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/shaders"
            COMMAND ${Vulkan_GLSLANG_VALIDATOR_EXECUTABLE} -V ${GLSLANG_DEBUG_FLAGS} ${DEFINE_FLAGS} "${CMAKE_CURRENT_SOURCE_DIR}/${SHADER_FILE}" -o "${SPV_OUTPUT}"
            DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${SHADER_FILE}"
            COMMENT "Compiling ${OUTPUT_NAME_ONLY}"
    )

    # Unique Target Name: Compile_<output>_<dir_hash>
    set(TARGET_NAME "Compile_${OUTPUT_NAME_ONLY}_${DIR_HASH}")

    add_custom_target(${TARGET_NAME} DEPENDS ${SPV_OUTPUT})

    # Expose the target name so the caller knows what to depend on
    set(LAST_GENERATED_TARGET ${TARGET_NAME} PARENT_SCOPE)
endfunction()
