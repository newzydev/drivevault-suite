# A stand-in "setup program": has a DriveVault version resource (product name + version 2.0.0), records how it was started and exits with a chosen code.
add_executable(fake_setup fake_setup.cpp fake_setup.rc)

add_executable(tst_hub tst_hub.cpp)
target_link_libraries(tst_hub PRIVATE master_core Qt6::Test)
target_compile_definitions(tst_hub PRIVATE FAKE_SETUP_PATH="$<TARGET_FILE:fake_setup>")
add_dependencies(tst_hub fake_setup)
add_test(NAME tst_hub COMMAND tst_hub)
set_tests_properties(tst_hub PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 120)

# The real window, offscreen: a fake registry with the Video Editor installed, rendered to a picture and checked for the product colours.
add_executable(tst_window tst_window.cpp)
target_link_libraries(tst_window PRIVATE master_core Qt6::Test)
target_compile_definitions(tst_window PRIVATE MASTER_EXE_PATH="$<TARGET_FILE:DriveVaultMasterPro>")
add_dependencies(tst_window DriveVaultMasterPro)
add_test(NAME tst_window COMMAND tst_window)
set_tests_properties(tst_window PROPERTIES TIMEOUT 120)
