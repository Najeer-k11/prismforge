# --- pf_core: diagnostics, shader validation, project model (no GPU, no widgets)
add_library(pf_core STATIC
    diagnostics/Diagnostic.h
    diagnostics/Diagnostic.cpp
    diagnostics/Logging.h
    diagnostics/Logging.cpp
    diagnostics/LogCapture.h
    diagnostics/LogCapture.cpp
    shader/ShaderTypes.h
    shader/ShaderTypes.cpp
    shader/GlslangValidator.h
    shader/GlslangValidator.cpp
    project/Project.h
    project/Project.cpp
    material/MaterialParameter.h
    material/MaterialParameter.cpp
)
target_include_directories(pf_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(pf_core PUBLIC Qt6::Core)
if(PRISMFORGE_HAVE_GLSLANG)
    target_compile_definitions(pf_core PRIVATE PRISMFORGE_HAVE_GLSLANG=1)
    target_link_libraries(pf_core PRIVATE glslang::glslang glslang::glslang-default-resource-limits)
endif()

# --- pf_render: renderer interface, meshes, camera, OpenGL backend
add_library(pf_render STATIC
    render/Renderer.h
    render/Mesh.h
    render/Mesh.cpp
    render/OrbitCamera.h
    render/OrbitCamera.cpp
    render/gl/GLRenderer.h
    render/gl/GLRenderer.cpp
)
target_include_directories(pf_render PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(pf_render PUBLIC pf_core Qt6::Gui Qt6::OpenGL)

# --- pf_editor: source editing widgets
add_library(pf_editor STATIC
    editor/CodeEditor.h
    editor/CodeEditor.cpp
    editor/GlslHighlighter.h
    editor/GlslHighlighter.cpp
)
target_include_directories(pf_editor PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(pf_editor PUBLIC pf_core Qt6::Widgets)

# --- application
qt_add_executable(prismforge
    app/main.cpp
    app/MainWindow.h
    app/MainWindow.cpp
    app/ViewportWidget.h
    app/ViewportWidget.cpp
    app/DiagnosticsPanel.h
    app/DiagnosticsPanel.cpp
    app/MaterialPanel.h
    app/MaterialPanel.cpp
    app/LogPanel.h
    app/LogPanel.cpp
    app/Theme.h
    app/Theme.cpp
)
qt_add_resources(prismforge "builtin_shaders"
    PREFIX "/"
    BASE "${PROJECT_SOURCE_DIR}/resources"
    FILES
        "${PROJECT_SOURCE_DIR}/resources/shaders/default.vert"
        "${PROJECT_SOURCE_DIR}/resources/shaders/default.frag"
)
target_link_libraries(prismforge PRIVATE
    pf_core pf_render pf_editor
    Qt6::Widgets Qt6::OpenGLWidgets Qt6::Concurrent)
target_compile_definitions(prismforge PRIVATE PRISMFORGE_VERSION="${PROJECT_VERSION}")
