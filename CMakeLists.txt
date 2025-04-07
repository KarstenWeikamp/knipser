cmake_minimum_required(VERSION 3.10)

# Project name and version
project(knipser VERSION 1.0)

# Set C standard
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED True)

# Set build type flags
set(CMAKE_C_FLAGS_DEBUG "-g -O0")
set(CMAKE_C_FLAGS_RELEASE "-O3 -DNDEBUG")

# Find the wayland-scanner executable
find_program(WAYLAND_SCANNER wayland-scanner REQUIRED)

# Define where to find the protocol XML file
set(PROTOCOL_DIR "/usr/share/wayland-protocols" CACHE PATH "Wayland protocols directory")
set(WLR_PROTOCOL_DIR "/usr/share/wlr-protocols" CACHE PATH "wlr-protocols directory")

# Function to generate protocol files
function(generate_protocol PROTOCOL_NAME PROTOCOL_XML_PATH)
    set(PROTO_H_FILE "${CMAKE_CURRENT_SOURCE_DIR}/wayland-protocols/${PROTOCOL_NAME}-client-protocol.h")
    set(PROTO_C_FILE "${CMAKE_CURRENT_SOURCE_DIR}/wayland-protocols/${PROTOCOL_NAME}-client-protocol.c")

    # Generate client header
    add_custom_command(
        OUTPUT "${PROTO_H_FILE}"
        COMMAND ${WAYLAND_SCANNER} client-header
        "${PROTOCOL_XML_PATH}"
        "${PROTO_H_FILE}"
        DEPENDS "${PROTOCOL_XML_PATH}"
        VERBATIM
    )

    # Generate code
    add_custom_command(
        OUTPUT "${PROTO_C_FILE}"
        COMMAND ${WAYLAND_SCANNER} private-code
        "${PROTOCOL_XML_PATH}"
        "${PROTO_C_FILE}"
        DEPENDS "${PROTOCOL_XML_PATH}"
        VERBATIM
    )

    # Add to global lists
    set(PROTOCOL_HEADERS ${PROTOCOL_HEADERS} "${PROTO_H_FILE}" PARENT_SCOPE)
    set(PROTOCOL_SOURCES ${PROTOCOL_SOURCES} "${PROTO_C_FILE}" PARENT_SCOPE)
endfunction()

# Initialize protocol lists
set(PROTOCOL_HEADERS "")
set(PROTOCOL_SOURCES "")

# Generate protocol files
generate_protocol(
    "wlr-screencopy-unstable-v1"
    "${CMAKE_CURRENT_SOURCE_DIR}/wayland-protocols/wlr-screencopy-unstable-v1.xml"
)

generate_protocol(
    "wlr-output-management-unstable-v1"
    "${CMAKE_CURRENT_SOURCE_DIR}/wayland-protocols/wlr-output-management-unstable-v1.xml"
)

# Make sure the include directory is in the include path
include_directories(${CMAKE_BINARY_DIR})

# Add the generated files as dependencies for your target
add_custom_target(wayland-protocols-headers DEPENDS ${PROTOCOL_HEADERS})
add_custom_target(wayland-protocols-sources DEPENDS ${PROTOCOL_SOURCES})

# Add executable with all protocol sources
add_executable(knipser
    knipser.c
    main.c
    wayland.c
    tray.c
    ${PROTOCOL_SOURCES}
)

# Append the build type to the executable name in lowercase with underscores
string(TOLOWER "${CMAKE_BUILD_TYPE}" BUILD_TYPE_LOWERCASE)
set_target_properties(knipser PROPERTIES OUTPUT_NAME "knipser_${BUILD_TYPE_LOWERCASE}")

# Add dependencies
add_dependencies(knipser wayland-protocols-headers)

# Include Wayland libraries
find_package(PkgConfig REQUIRED)
pkg_check_modules(WAYLAND REQUIRED wayland-client)

# Link Wayland libraries
target_include_directories(knipser PRIVATE ${WAYLAND_INCLUDE_DIRS})
target_link_libraries(knipser PRIVATE systemd ${WAYLAND_LIBRARIES})
target_compile_options(knipser PRIVATE ${WAYLAND_CFLAGS_OTHER})

find_package(PNG REQUIRED)
target_link_libraries(knipser PRIVATE PNG::PNG)