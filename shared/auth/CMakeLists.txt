# DriveVault shared Google account (see CLAUDE.md in this folder). Used by every app:
#   add_subdirectory("${CMAKE_SOURCE_DIR}/../../shared/auth" "${CMAKE_BINARY_DIR}/dv_account")
#   target_link_libraries(<target> PRIVATE dv_account)
#   dv_account_qml(DV_ACCOUNT_QML)   # then add ${DV_ACCOUNT_QML} to the QML_FILES of the app's qt_add_qml_module (the component DvAccountChip)
# Needs Qt Core / Gui / Network / Quick in the parent project.

set(DV_ACCOUNT_DIR_GENERATED "${CMAKE_BINARY_DIR}/generated/dv_account")
execute_process(
    COMMAND powershell -NoProfile -ExecutionPolicy Bypass -File "${CMAKE_CURRENT_LIST_DIR}/../scripts/make-oauth-header.ps1"
            -Output "${DV_ACCOUNT_DIR_GENERATED}/GoogleDesktopOAuth.h"
    RESULT_VARIABLE dv_oauth_result
    OUTPUT_VARIABLE dv_oauth_output
    ERROR_VARIABLE dv_oauth_error)
if(dv_oauth_result)
    message(FATAL_ERROR "make-oauth-header.ps1 failed: ${dv_oauth_error}")
endif()
message(STATUS "Google account: ${dv_oauth_output}")

add_library(dv_account STATIC src/GoogleAccount.cpp src/GoogleAccount.h src/OAuthConfig.cpp src/OAuthConfig.h)
target_include_directories(dv_account PUBLIC src PRIVATE "${DV_ACCOUNT_DIR_GENERATED}")
target_link_libraries(dv_account PUBLIC Qt6::Core Qt6::Gui Qt6::Network PRIVATE bcrypt crypt32)
set_target_properties(dv_account PROPERTIES AUTOMOC ON)

# The shared QML component is compiled into each app's own QML module: the files keep their names (QT_RESOURCE_ALIAS), no import is needed.
set(DV_ACCOUNT_QML_DIR "${CMAKE_CURRENT_LIST_DIR}/qml")
set(DV_ACCOUNT_QML_DIR "${DV_ACCOUNT_QML_DIR}" PARENT_SCOPE)    # the macro below runs in the scope of the program
macro(dv_account_qml out_var)
    set(${out_var} "${DV_ACCOUNT_QML_DIR}/DvAccountChip.qml")
    set_source_files_properties(${${out_var}} PROPERTIES QT_RESOURCE_ALIAS DvAccountChip.qml)
endmacro()

# The unit test of the shared account (call it where the program registers its own tests, after enable_testing()).
set(DV_ACCOUNT_TEST_SOURCE "${CMAKE_CURRENT_LIST_DIR}/tests/tst_account.cpp")
function(dv_account_add_tests)
    add_executable(tst_account "${DV_ACCOUNT_TEST_SOURCE}")
    target_link_libraries(tst_account PRIVATE dv_account Qt6::Test bcrypt)
    add_test(NAME tst_account COMMAND tst_account)
    set_tests_properties(tst_account PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;DV_NO_AUDIO=1" TIMEOUT 120)
endfunction()
set(DV_ACCOUNT_TEST_SOURCE "${DV_ACCOUNT_TEST_SOURCE}" PARENT_SCOPE)
