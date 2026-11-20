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

# Cosmetic filters, scriptlets, site switches, the page script and the stand-in files (and, opt in, the real lists).
add_executable(tst_cosmetic tst_cosmetic.cpp)
target_link_libraries(tst_cosmetic PRIVATE browser_core Qt6::Test)
add_test(NAME tst_cosmetic COMMAND tst_cosmetic)
set_tests_properties(tst_cosmetic PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;DV_NO_AUDIO=1" TIMEOUT 240)

# The lab: pages on this computer that imitate real ones (hide-able boxes, scripts to tame, ad libraries), opened in the REAL program with Chromium.
add_executable(tst_lab tst_lab.cpp)
target_link_libraries(tst_lab PRIVATE Qt6::Test Qt6::Network)
target_compile_definitions(tst_lab PRIVATE BROWSER_EXE_PATH="$<TARGET_FILE:DriveVaultBrowserPro>")
target_compile_definitions(tst_lab PRIVATE TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data" DV_TOOLS_FFMPEG="${CMAKE_SOURCE_DIR}/../../.tools/ffmpeg/bin/ffmpeg.exe")
add_dependencies(tst_lab DriveVaultBrowserPro)
add_test(NAME tst_lab COMMAND tst_lab)
set_tests_properties(tst_lab PROPERTIES ENVIRONMENT "DV_NO_AUDIO=1" TIMEOUT 900)

# Saving videos: what is offered (policy, DRM refusal), saving a file from a server on this computer, saving an HLS stream through FFmpeg.
add_executable(tst_media tst_media.cpp)
target_link_libraries(tst_media PRIVATE browser_core Qt6::Test Qt6::Network)
add_test(NAME tst_media COMMAND tst_media)
set_tests_properties(tst_media PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;DV_NO_AUDIO=1" TIMEOUT 300)

add_executable(tst_profile tst_profile.cpp ../src/app/ProfileSession.cpp ../src/app/ProfileSession.h)
target_include_directories(tst_profile PRIVATE ../src/app "${CMAKE_BINARY_DIR}/generated")
target_link_libraries(tst_profile PRIVATE Qt6::Test Qt6::Widgets Qt6::Network bcrypt crypt32)
add_test(NAME tst_profile COMMAND tst_profile)
set_tests_properties(tst_profile PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;DV_NO_AUDIO=1" TIMEOUT 45)
