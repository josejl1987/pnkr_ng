add_executable(pnkr_rhi_sprites main.cpp)

target_compile_features(pnkr_rhi_sprites PRIVATE cxx_std_20)
target_link_libraries(pnkr_rhi_sprites PRIVATE pnkr_engine)

set_target_properties(pnkr_rhi_sprites PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

add_custom_command(TARGET pnkr_rhi_sprites POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:pnkr_rhi_sprites>/assets"
    COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_SOURCE_DIR}/samples/rhiUnifiedGLTF/assets/TransmissionRoughnessTest/glTF/GridWithDetails.png" "$<TARGET_FILE_DIR:pnkr_rhi_sprites>/assets/frame0.png"
    COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_SOURCE_DIR}/samples/rhiUnifiedGLTF/assets/TransmissionRoughnessTest/glTF/RoughnessGrid.png" "$<TARGET_FILE_DIR:pnkr_rhi_sprites>/assets/frame1.png"
    COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_SOURCE_DIR}/samples/rhiUnifiedGLTF/assets/TransmissionRoughnessTest/glTF/RoughnessGrid-1.png" "$<TARGET_FILE_DIR:pnkr_rhi_sprites>/assets/frame2.png"
    COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_SOURCE_DIR}/samples/rhiUnifiedGLTF/assets/TransmissionRoughnessTest/glTF/IOR_Labels.png" "$<TARGET_FILE_DIR:pnkr_rhi_sprites>/assets/frame3.png"
)
