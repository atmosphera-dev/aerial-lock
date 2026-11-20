cmake_minimum_required(VERSION 3.16)
project(aerial_client LANGUAGES CXX)
find_package(Qt6 REQUIRED COMPONENTS Core DBus)
get_filename_component(AERIAL_CLIENT_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
foreach(kind IN ITEMS companion job)
    if(kind STREQUAL "companion")
        set(api Aerial1)
        set(proxy CompanionClient)
    else()
        set(api Aerial1.Job1)
        set(proxy JobClient)
    endif()
    add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/${kind}_client.cpp" "${CMAKE_CURRENT_BINARY_DIR}/${kind}_client.h"
        COMMAND Qt6::qdbusxml2cpp -N -c "${proxy}" -p "${CMAKE_CURRENT_BINARY_DIR}/${kind}_client"
            -i common/dbus-types.h "${AERIAL_CLIENT_SOURCE}/dbus/org.aeriallock.${api}.xml"
        DEPENDS "${AERIAL_CLIENT_SOURCE}/dbus/org.aeriallock.${api}.xml" "${AERIAL_CLIENT_SOURCE}/common/dbus-types.h")
    list(APPEND proxies "${CMAKE_CURRENT_BINARY_DIR}/${kind}_client.cpp" "${CMAKE_CURRENT_BINARY_DIR}/${kind}_client.h")
endforeach()
add_library(aerial-client STATIC ${proxies} client-support.cpp blocking-client.cpp service-connection.cpp
    client-support.h blocking-client.h service-connection.h "${AERIAL_CLIENT_SOURCE}/common/config-document.cpp")
set_target_properties(aerial-client PROPERTIES AUTOMOC ON POSITION_INDEPENDENT_CODE ON CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
target_include_directories(aerial-client PUBLIC "${AERIAL_CLIENT_SOURCE}" "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(aerial-client PUBLIC Qt6::Core Qt6::DBus)
