find_package(Qt6 ${QT_MIN_VERSION} REQUIRED COMPONENTS Test)

foreach(test RuleEngineTest RuleStoreTest BrowserProfilesTest LauncherTest DefaultBrowserManagerTest)
    add_executable(${test} ${test}.cpp)
    target_link_libraries(${test} PRIVATE lobcore Qt6::Test)
    add_test(NAME ${test} COMMAND ${test})
    ecm_mark_as_test(${test})
endforeach()
