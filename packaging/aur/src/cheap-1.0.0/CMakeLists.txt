cmake_minimum_required(VERSION 3.16)
project(cheap
    VERSION 1.0.0
    DESCRIPTION "Cheapest Hierarchical Enchant-Anvil Planner -- a native Minecraft enchantment ordering tool"
    LANGUAGES C
)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK4 REQUIRED gtk4)

pkg_get_variable(GLIB_COMPILE_RESOURCES gio-2.0 glib_compile_resources)
if(NOT GLIB_COMPILE_RESOURCES)
    find_program(GLIB_COMPILE_RESOURCES glib-compile-resources REQUIRED)
endif()

file(GLOB ICON_FILES ${CMAKE_SOURCE_DIR}/resources/icons/*.gif)
set(RESOURCE_XML ${CMAKE_SOURCE_DIR}/resources/cheap.gresource.xml)
set(GENERATED_RESOURCES_C ${CMAKE_BINARY_DIR}/resources.c)

add_custom_command(
    OUTPUT ${GENERATED_RESOURCES_C}
    COMMAND ${GLIB_COMPILE_RESOURCES}
            --sourcedir=${CMAKE_SOURCE_DIR}/resources
            --target=${GENERATED_RESOURCES_C}
            --generate-source
            ${RESOURCE_XML}
    DEPENDS ${RESOURCE_XML} ${ICON_FILES}
    COMMENT "Compiling icon resources"
)

add_library(optimizer STATIC
    src/enchant_data.c
    src/optimizer.c
)
target_include_directories(optimizer PUBLIC src)
target_compile_options(optimizer PRIVATE -Wall -Wextra -Wno-missing-field-initializers)

add_executable(cheap
    src/main.c
    src/ui.c
    ${GENERATED_RESOURCES_C}
)
target_include_directories(cheap PRIVATE ${GTK4_INCLUDE_DIRS})
target_link_directories(cheap PRIVATE ${GTK4_LIBRARY_DIRS})
target_compile_options(cheap PRIVATE ${GTK4_CFLAGS_OTHER} -Wall -Wextra)
target_link_libraries(cheap PRIVATE optimizer ${GTK4_LIBRARIES} m)

# Small CLI used only to validate the optimizer against the reference
# implementation; not installed, not part of the shipped app.
add_executable(optimizer-cli EXCLUDE_FROM_ALL src/optimizer_cli.c)
target_link_libraries(optimizer-cli PRIVATE optimizer m)

install(TARGETS cheap RUNTIME DESTINATION bin)
install(FILES resources/cheap.desktop DESTINATION share/applications)
install(FILES resources/cheap.png DESTINATION share/icons/hicolor/128x128/apps)

# ---- CPack: `cpack` in the build dir produces a .deb installable via
# `sudo apt install ./cheap_1.0.0_<arch>.deb` (apt resolves the GTK4 runtime
# dependency, auto-detected via dpkg-shlibdeps). ----
set(CPACK_PACKAGE_NAME "cheap")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_VENDOR "lokifisch")
set(CPACK_PACKAGE_CONTACT "lokifisch@lokifisch.dev")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PROJECT_DESCRIPTION}")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/lokifisch/cheap")
set(CPACK_GENERATOR "DEB")
set(CPACK_DEBIAN_PACKAGE_SECTION "games")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "lokifisch <lokifisch@lokifisch.dev>")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
include(CPack)
