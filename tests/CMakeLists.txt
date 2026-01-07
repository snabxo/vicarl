# SPDX-License-Identifier: Apache-2.0

add_executable(vicarl_tests
        test_main.c
        test_codec.c
        test_record_segment.c
        test_merkle.c
        test_store_log.c
        test_ledger.c
        test_p2p_wire.c
        test_p2p_sync.c
)

target_link_libraries(vicarl_tests PRIVATE vicarl_shared)

target_include_directories(vicarl_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

add_test(NAME vicarl_tests COMMAND vicarl_tests)

# Optional: sqlite tests only if enabled + compiled in
if(VICARL_ENABLE_SQLITE)
    add_executable(vicarl_tests_sqlite
            test_main.c
            test_store_sqlite.c
    )
    target_link_libraries(vicarl_tests_sqlite PRIVATE vicarl_shared)
    add_test(NAME vicarl_tests_sqlite COMMAND vicarl_tests_sqlite)
endif()
