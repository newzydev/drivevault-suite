# Bookmarks, history, settings, address-bar rules and download names (no WebEngine).
add_executable(tst_core tst_core.cpp)
target_link_libraries(tst_core PRIVATE browser_core Qt6::Test)
add_test(NAME tst_core COMMAND tst_core)
set_tests_properties(tst_core PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;DV_NO_AUDIO=1" TIMEOUT 120)

# The real program: starts the exe, renders the window to a picture and checks that it is not blank.
add_executable(tst_smoke tst_smoke.cpp)
target_link_libraries(tst_smoke PRIVATE Qt6::Test Qt6::Gui Qt6::Network)
target_compile_definitions(tst_smoke PRIVATE BROWSER_EXE_PATH="$<TARGET_FILE:DriveVaultBrowserPro>")
add_dependencies(tst_smoke DriveVaultBrowserPro)
add_test(NAME tst_smoke COMMAND tst_smoke)
set_tests_properties(tst_smoke PROPERTIES ENVIRONMENT "DV_NO_AUDIO=1" TIMEOUT 180)

# The ad blocker engine: filter syntax, matching, the lists on disk, the update from a server on this computer, speed on a synthetic list.
add_executable(tst_adblock tst_adblock.cpp)
target_link_libraries(tst_adblock PRIVATE browser_core Qt6::Test)
add_test(NAME tst_adblock COMMAND tst_adblock)
set_tests_properties(tst_adblock PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;DV_NO_AUDIO=1" TIMEOUT 240)
