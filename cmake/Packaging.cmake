include_guard(GLOBAL)

# Install only the workspace and its runtime resources. Optional standalone
# wrappers are not part of the workspace download.
function(neotoolkit_configure_packaging)
    set(_host_source "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/..")
    set(_component toolkit-runtime)
    set(NEOTOOLKIT_VERSION "${PROJECT_VERSION}")
    if(WIN32)
        set(NEOTOOLKIT_ICON "${NEOTOOLKIT_NEOBIF_SOURCE_DIR}/resources/neobif.ico")
        configure_file("${_host_source}/resources/neotoolkit.rc.in" "${CMAKE_CURRENT_BINARY_DIR}/neotoolkit.rc" @ONLY)
        target_sources(NeoToolKit-Test PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/neotoolkit.rc")
        set(_platform windows-x64)
    elseif(APPLE)
        include("${NEOSHARED_ROOT}/cmake/NeoMacOSBundle.cmake")
        neo_configure_macos_bundle(NeoToolKit-Test
            NAME "NeoToolKit Test" IDENTIFIER "com.vrifftech.neotoolkit-test"
            VERSION "${PROJECT_VERSION}" ICON "${NEOTOOLKIT_NEOBIF_SOURCE_DIR}/resources/neobif.icns")
        set(_platform "macos-${CMAKE_OSX_ARCHITECTURES}")
    else()
        set(_platform linux-x64)
    endif()
    install(TARGETS NeoToolKit-Test
        BUNDLE DESTINATION . COMPONENT ${_component}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR} COMPONENT ${_component})
    if(APPLE)
        set(_licenses "NeoToolKit-Test.app/Contents/Resources/licenses")
    else()
        set(_licenses "${CMAKE_INSTALL_DATAROOTDIR}/licenses/neotoolkit-test")
    endif()
    if(UNIX AND NOT APPLE)
        configure_file("${_host_source}/resources/neotoolkit-test.desktop.in"
            "${CMAKE_CURRENT_BINARY_DIR}/neotoolkit-test.desktop" @ONLY)
        install(FILES "${CMAKE_CURRENT_BINARY_DIR}/neotoolkit-test.desktop"
            DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/applications" COMPONENT ${_component})
        foreach(size 16 24 32 48 64 128 256)
            install(FILES "${NEOTOOLKIT_NEOBIF_SOURCE_DIR}/resources/icons/${size}x${size}/neobif.png"
                DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/${size}x${size}/apps"
                RENAME neotoolkit-test.png COMPONENT ${_component})
        endforeach()
        install(FILES "${NEOTOOLKIT_NEOBIF_SOURCE_DIR}/resources/neobif.svg"
            DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/scalable/apps"
            RENAME neotoolkit-test.svg COMPONENT ${_component})
    endif()
    # Include copyright notices supplied by vcpkg for Windows static libraries.
    if(WIN32 AND DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
        file(GLOB _copyrights "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/share/*/copyright")
        foreach(notice IN LISTS _copyrights)
            get_filename_component(_port_dir "${notice}" DIRECTORY)
            get_filename_component(_port "${_port_dir}" NAME)
            install(FILES "${notice}" DESTINATION "${_licenses}/${_port}" COMPONENT ${_component})
        endforeach()
    endif()
    set(CPACK_PACKAGE_NAME "NeoToolKit-Test")
    set(CPACK_PACKAGE_VENDOR "NeoTools")
    set(CPACK_PACKAGE_CONTACT "NeoTools maintainers")
    set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "NeoTools workspace with integrated resource editors")
    set(CPACK_PACKAGE_FILE_NAME "NeoToolKit-Test-${PROJECT_VERSION}-${_platform}")
    set(CPACK_INSTALL_CMAKE_PROJECTS "${CMAKE_BINARY_DIR};${PROJECT_NAME};${_component};/")
    set(CPACK_COMPONENTS_ALL ${_component})
    set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY ON)
    set(CPACK_PACKAGING_INSTALL_PREFIX "/")
    set(CPACK_PACKAGE_CHECKSUM SHA256)
    if(WIN32)
        set(CPACK_GENERATOR ZIP)
    elseif(NOT APPLE)
        set(CPACK_GENERATOR TGZ)
        set(CPACK_DEBIAN_PACKAGE_NAME neotoolkit-test)
        set(CPACK_DEBIAN_PACKAGE_MAINTAINER "NeoTools maintainers")
        set(CPACK_DEBIAN_PACKAGE_SECTION devel)
        set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
        set(CPACK_DEBIAN_PACKAGE_GENERATE_SHLIBS OFF)
        set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
        # TGZ remains relative (bin/, share/). DEB belongs under /usr, not /bin.
        set(CPACK_PROJECT_CONFIG_FILE "${_host_source}/cmake/CPackProjectConfig.cmake")
    endif()
    include(CPack)
    file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/ci-build-$<CONFIG>.json" CONTENT
"{\n  \"version\": \"${PROJECT_VERSION}\",\n  \"executable\": \"$<TARGET_FILE:NeoToolKit-Test>\",\n  \"sharedRoot\": \"${NEOSHARED_ROOT}\",\n  \"platform\": \"${_platform}\"\n}\n")
endfunction()
