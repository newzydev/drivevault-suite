add_library(dv_mcp STATIC
    McpServer.cpp McpServer.h)
target_include_directories(dv_mcp PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(dv_mcp PUBLIC dv_core dv_settings dv_project dv_timeline dv_ai Qt6::Core Qt6::Gui Qt6::Network)
