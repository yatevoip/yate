add_test(NAME yate-help COMMAND yate --help)
add_executable(yate-core-test core.cpp)
target_link_libraries(yate-core-test PRIVATE libyate Threads::Threads)
add_test(NAME yate-core COMMAND yate-core-test)
if(TARGET yate-ss7test)
    add_test(NAME yate-ss7 COMMAND yate-ss7test)
    set_tests_properties(yate-ss7 PROPERTIES TIMEOUT 15)
endif()
find_package(Python3 3.6 QUIET COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND AND YATE_BUILD_MODULES)
    # Проверяем загрузку, инициализацию и выгрузку через штатный жизненный цикл ядра.
    add_test(NAME yate-load-modules COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/load_modules.py" "$<TARGET_FILE:yate>"
        "${PROJECT_BINARY_DIR}/modules" "${PROJECT_SOURCE_DIR}/share")
    set_tests_properties(yate-load-modules PROPERTIES TIMEOUT 40)
    if(TARGET ysipchan AND TARGET yrtpchan AND TARGET tonegen AND TARGET regexroute)
        add_test(NAME yate-sip-rtp COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/sip_rtp.py" "$<TARGET_FILE:yate>" "${PROJECT_BINARY_DIR}/modules")
        set_tests_properties(yate-sip-rtp PROPERTIES TIMEOUT 40)
    endif()
endif()
