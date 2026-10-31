set(CPACK_PACKAGE_VENDOR            AmneziaVPN)
set(CPACK_PACKAGE_VERSION           ${AMNEZIAVPN_VERSION})
if(WIN32)
    set(CPACK_PACKAGE_FILE_NAME "AmneziaVPN_${AMNEZIAVPN_VERSION}_windows_x64")
elseif(APPLE AND NOT IOS AND NOT MACOS_NE)
    set(CPACK_PACKAGE_FILE_NAME "AmneziaVPN_${AMNEZIAVPN_VERSION}_macos_x64")
elseif(LINUX AND NOT ANDROID)
    set(CPACK_PACKAGE_FILE_NAME "AmneziaVPN_${AMNEZIAVPN_VERSION}_linux_x64")
endif()
set(CPACK_PACKAGE_INSTALL_DIRECTORY AmneziaVPN)
set(CPACK_PACKAGE_EXECUTABLES       AmneziaVPN AmneziaVPN)
set(CPACK_PRE_BUILD_SCRIPTS         ${CMAKE_CURRENT_LIST_DIR}/sign_binaries.cmake)
set(CPACK_POST_BUILD_SCRIPTS        ${CMAKE_CURRENT_LIST_DIR}/sign_packages.cmake)
set(CPACK_PROJECT_CONFIG_FILE       ${CMAKE_CURRENT_LIST_DIR}/CPackOptions.cmake)
if(WIN32 AND D1MISVPN_WINDOWS_FORK)
    set(CPACK_PACKAGE_NAME "${CLIENT_APPLICATION_NAME}")
    set(CPACK_PACKAGE_VENDOR "D1m7asis")
    set(CPACK_PACKAGE_FILE_NAME "${CLIENT_APPLICATION_NAME}_${AMNEZIAVPN_VERSION}_windows_x64")
    set(CPACK_PACKAGE_INSTALL_DIRECTORY "${CLIENT_APPLICATION_NAME}")
    set(CPACK_PACKAGE_EXECUTABLES "${CLIENT_APPLICATION_NAME}" "${CLIENT_DISPLAY_NAME}")
endif()
set(CPACK_RESOURCE_FILE_LICENSE     ${CMAKE_SOURCE_DIR}/deploy/data/LICENSE.txt)

list(PREPEND CPACK_COMPONENTS_ALL AmneziaVPN)

if(APPLE)
    set(CPACK_GENERATOR productbuild)
else()
    set(CPACK_GENERATOR IFW)
endif()

# === CPack IFW generator settings ===
set(CPACK_IFW_PACKAGE_NAME                          AmneziaVPN)
set(CPACK_IFW_PACKAGE_TITLE                         AmneziaVPN)
if(WIN32 AND D1MISVPN_WINDOWS_FORK)
    set(CPACK_IFW_PACKAGE_NAME "${CLIENT_APPLICATION_NAME}")
    set(CPACK_IFW_PACKAGE_TITLE "${CLIENT_DISPLAY_NAME}")
endif()
set(CPACK_IFW_PACKAGE_WIZARD_DEFAULT_WIDTH          600)
set(CPACK_IFW_PACKAGE_WIZARD_DEFAULT_HEIGHT         380)
set(CPACK_IFW_PACKAGE_WIZARD_STYLE                  Modern)
set(CPACK_IFW_PACKAGE_REMOVE_TARGET_DIR             ON)
set(CPACK_IFW_PACKAGE_ALLOW_SPACE_IN_PATH           ON)
set(CPACK_IFW_PACKAGE_ALLOW_NON_ASCII_CHARACTERS    ON)
set(CPACK_IFW_PACKAGE_CONTROL_SCRIPT                ${CMAKE_SOURCE_DIR}/deploy/installer/qif/controlscript.js)

# === CPack WIX generator settings ===
set(CPACK_WIX_VERSION               4)
set(CPACK_WIX_UPGRADE_GUID          "{2D55AC62-96D6-4692-8C05-0D85BBF95485}")
if(WIN32 AND D1MISVPN_WINDOWS_FORK)
    set(CPACK_WIX_UPGRADE_GUID "{8462A549-6680-484F-A1A8-E6E2843A4B05}")
endif()
set(CPACK_WIX_PRODUCT_ICON          ${CMAKE_SOURCE_DIR}/client/images/app.ico)
set(CPACK_WIX_CUSTOM_XMLNS          "util=http://wixtoolset.org/schemas/v4/wxs/util")
set(_AMNEZIA_WIX_PATCH_SERVICE      ${CMAKE_SOURCE_DIR}/deploy/installer/wix/service_install_patch.xml)
set(_AMNEZIA_WIX_PATCH_CLOSE_APP    ${CMAKE_SOURCE_DIR}/deploy/installer/wix/close_client_patch.xml)
set(WIX_SERVICE_FILE_ID "${CLIENT_SERVICE_NAME}.exe")
string(REPLACE "-" "_" WIX_SERVICE_FILE_ID "${WIX_SERVICE_FILE_ID}")
configure_file("${_AMNEZIA_WIX_PATCH_SERVICE}" "${CMAKE_CURRENT_BINARY_DIR}/wix-service.xml" @ONLY)
configure_file("${_AMNEZIA_WIX_PATCH_CLOSE_APP}" "${CMAKE_CURRENT_BINARY_DIR}/wix-close.xml" @ONLY)
set(_AMNEZIA_WIX_PATCH_SERVICE "${CMAKE_CURRENT_BINARY_DIR}/wix-service.xml")
set(_AMNEZIA_WIX_PATCH_CLOSE_APP "${CMAKE_CURRENT_BINARY_DIR}/wix-close.xml")
file(TO_CMAKE_PATH                  "${_AMNEZIA_WIX_PATCH_SERVICE}" _AMNEZIA_WIX_PATCH_SERVICE_CMAKE)
file(TO_CMAKE_PATH                  "${_AMNEZIA_WIX_PATCH_CLOSE_APP}" _AMNEZIA_WIX_PATCH_CLOSE_APP_CMAKE)
list(APPEND CPACK_WIX_PATCH_FILE "${_AMNEZIA_WIX_PATCH_SERVICE_CMAKE}")
if(NOT D1MISVPN_WINDOWS_FORK)
    list(APPEND CPACK_WIX_PATCH_FILE "${_AMNEZIA_WIX_PATCH_CLOSE_APP_CMAKE}")
endif()
list(APPEND CPACK_WIX_EXTENSIONS    "WixToolset.Util.wixext")

# === CPack productbuild generator settings ===
set(CPACK_PRODUCTBUILD_IDENTIFIER       org.amneziavpn)
set(CPACK_PREFLIGHT_AMNEZIAVPN_SCRIPT   ${CMAKE_SOURCE_DIR}/deploy/data/macos/post_uninstall.sh)
set(CPACK_POSTFLIGHT_AMNEZIAVPN_SCRIPT  ${CMAKE_SOURCE_DIR}/deploy/data/macos/post_install.sh)
set(CPACK_POSTFLIGHT_UNINSTALL_SCRIPT   ${CMAKE_SOURCE_DIR}/deploy/data/macos/post_uninstall.sh)
# provide custom CPack.distribution.dist.in
list(APPEND CMAKE_MODULE_PATH           ${CMAKE_SOURCE_DIR}/deploy/data/macos)

if(LINUX AND NOT ANDROID)
    install(FILES
        ${CMAKE_SOURCE_DIR}/deploy/data/linux/AmneziaVPN.service
        ${CMAKE_SOURCE_DIR}/deploy/data/linux/AmneziaVPN.png
        ${CMAKE_SOURCE_DIR}/deploy/data/linux/AmneziaVPN.desktop
        ${CMAKE_SOURCE_DIR}/deploy/data/linux/post_install.sh
        ${CMAKE_SOURCE_DIR}/deploy/data/linux/post_uninstall.sh
        DESTINATION "."
        COMPONENT AmneziaVPN
    )
endif()

if(WIN32)
    set(_windows_install_data "${CMAKE_SOURCE_DIR}/deploy/data/windows")
    if(D1MISVPN_WINDOWS_FORK)
        set(_windows_install_data "${CMAKE_CURRENT_BINARY_DIR}/windows-fork")
        file(MAKE_DIRECTORY "${_windows_install_data}")
        foreach(_script post_install.cmd post_uninstall.cmd manage-install.ps1)
            configure_file("${CMAKE_SOURCE_DIR}/deploy/data/windows/fork/${_script}.in" "${_windows_install_data}/${_script}" @ONLY)
        endforeach()
        install(FILES "${_windows_install_data}/manage-install.ps1" DESTINATION "." COMPONENT AmneziaVPN)
    endif()
    install(FILES
        ${_windows_install_data}/post_install.cmd
        ${_windows_install_data}/post_uninstall.cmd
        DESTINATION "."
        COMPONENT AmneziaVPN
    )

    set(CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS_SKIP TRUE)
    include(InstallRequiredSystemLibraries)
    if(CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS)
        install(PROGRAMS ${CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS}
            DESTINATION "."
            COMPONENT AmneziaVPN
        )
    else()
        message(WARNING "MSVC runtime libraries were not found, packages will not ship them")
    endif()
endif()

if (APPLE AND NOT IOS AND NOT MACOS_NE)
    install(FILES ${CMAKE_SOURCE_DIR}/deploy/data/macos/AmneziaVPN.plist
        DESTINATION "AmneziaVPN.app/Contents/Resources"
        COMPONENT AmneziaVPN
    )
endif()

include(CPackIFW)
cpack_ifw_configure_component(AmneziaVPN
    VERSION ${AMNEZIAVPN_VERSION}
    RELEASE_DATE ${RELEASE_DATE}
    REQUIRES_ADMIN_RIGHTS
    FORCED_INSTALLATION
    SCRIPT ${CMAKE_SOURCE_DIR}/deploy/installer/qif/componentscript.js
)

include(CPack)
cpack_add_component(Uninstall
    DISPLAY_NAME "Uninstall AmneziaVPN"
    REQUIRES_ADMIN_RIGHTS
    DISABLED
)
