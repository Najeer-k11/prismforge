# Installation layout and RPM/DEB packaging (CPack).
#
#   cmake --preset release && cmake --build --preset release
#   cd build/release && cpack -G RPM     # or: cpack -G DEB
#
# Build each package on the distribution it targets so it links against that
# distribution's Qt and C++ runtime (see README: "Packages").

include(GNUInstallDirs)
# Lower-case, matching the package name (Fedora/Debian convention).
set(PRISMFORGE_DOCDIR ${CMAKE_INSTALL_DATADIR}/doc/prismforge)

configure_file(${CMAKE_CURRENT_LIST_DIR}/prismforge.desktop.in
               ${CMAKE_BINARY_DIR}/packaging/${PRISMFORGE_APP_ID}.desktop @ONLY)
configure_file(${CMAKE_CURRENT_LIST_DIR}/prismforge.metainfo.xml.in
               ${CMAKE_BINARY_DIR}/packaging/${PRISMFORGE_APP_ID}.metainfo.xml @ONLY)

install(TARGETS prismforge RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES ${CMAKE_BINARY_DIR}/packaging/${PRISMFORGE_APP_ID}.desktop
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES ${CMAKE_BINARY_DIR}/packaging/${PRISMFORGE_APP_ID}.metainfo.xml
        DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo)
install(FILES ${CMAKE_CURRENT_LIST_DIR}/prismforge-mime.xml
        DESTINATION ${CMAKE_INSTALL_DATADIR}/mime/packages
        RENAME ${PRISMFORGE_APP_ID}.xml)
foreach(size 16 24 32 48 64 128 256 512)
    install(FILES ${PROJECT_SOURCE_DIR}/resources/icons/prismforge-${size}.png
            DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/${size}x${size}/apps
            RENAME ${PRISMFORGE_APP_ID}.png)
endforeach()
install(FILES ${PROJECT_SOURCE_DIR}/README.md ${PROJECT_SOURCE_DIR}/docs/ARCHITECTURE.md
        DESTINATION ${PRISMFORGE_DOCDIR})
install(DIRECTORY ${PROJECT_SOURCE_DIR}/examples/
        DESTINATION ${PRISMFORGE_DOCDIR}/examples)

# ---------------------------------------------------------------- CPack
set(CPACK_PACKAGE_NAME "prismforge")
set(CPACK_PACKAGE_VENDOR "Najeer-k11")
set(CPACK_PACKAGE_CONTACT "Najeer-k11 <kvenkatanajeer@gmail.com>")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Native GPU shader development environment")
set(CPACK_PACKAGE_DESCRIPTION
"PrismForge is a native desktop environment for writing GLSL shaders,
previewing them on the GPU in real time, building them with a visual node
graph, inspecting diagnostics and exporting reusable shader assets.")
set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")
set(CPACK_STRIP_FILES ON)

# RPM (Fedora): shared-library requirements are generated automatically.
set(CPACK_RPM_PACKAGE_LICENSE "Proprietary")
set(CPACK_RPM_PACKAGE_GROUP "Development/Tools")
set(CPACK_RPM_PACKAGE_RELEASE 1)
set(CPACK_RPM_PACKAGE_RELEASE_DIST ON)
set(CPACK_RPM_FILE_NAME RPM-DEFAULT)
set(CPACK_RPM_PACKAGE_REQUIRES "hicolor-icon-theme, shared-mime-info")
# Don't claim ownership of directories that belong to other packages.
set(CPACK_RPM_EXCLUDE_FROM_AUTO_FILELIST_ADDITION
    /usr/share/applications
    /usr/share/metainfo
    /usr/share/mime
    /usr/share/mime/packages
    /usr/share/icons
    /usr/share/icons/hicolor)
foreach(size 16 24 32 48 64 128 256 512)
    list(APPEND CPACK_RPM_EXCLUDE_FROM_AUTO_FILELIST_ADDITION
         /usr/share/icons/hicolor/${size}x${size}
         /usr/share/icons/hicolor/${size}x${size}/apps)
endforeach()

# DEB (Debian/Ubuntu): dpkg-shlibdeps computes library dependencies.
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
set(CPACK_DEBIAN_PACKAGE_SECTION "devel")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
# Qt platform plugins are separate packages on Debian/Ubuntu.
set(CPACK_DEBIAN_PACKAGE_DEPENDS "qt6-qpa-plugins, hicolor-icon-theme, shared-mime-info")
set(CPACK_DEBIAN_PACKAGE_RECOMMENDS "qt6-wayland")

include(CPack)
