find_package(Catch2 3 CONFIG REQUIRED)
include(Catch)

add_executable(pf_unit_tests
    test_diagnostics.cpp
    test_project.cpp
    test_mesh_camera.cpp
    test_glslang.cpp
    test_material.cpp
    test_log_capture.cpp
    test_graph.cpp
)
target_link_libraries(pf_unit_tests PRIVATE pf_core pf_render pf_graph Catch2::Catch2WithMain)
target_compile_definitions(pf_unit_tests PRIVATE
    PF_SHADER_DIR="${PROJECT_SOURCE_DIR}/resources/shaders"
    $<$<BOOL:${PRISMFORGE_HAVE_GLSLANG}>:PRISMFORGE_HAVE_GLSLANG=1>)
catch_discover_tests(pf_unit_tests)

# Needs a real OpenGL 4.5 implementation; skips itself when none is available.
add_executable(pf_gl_tests test_gl_renderer.cpp)
target_link_libraries(pf_gl_tests PRIVATE pf_render pf_graph Qt6::Gui Qt6::OpenGL Catch2::Catch2)
target_compile_definitions(pf_gl_tests PRIVATE PF_SHADER_DIR="${PROJECT_SOURCE_DIR}/resources/shaders")
catch_discover_tests(pf_gl_tests PROPERTIES SKIP_RETURN_CODE 4 LABELS gpu)
