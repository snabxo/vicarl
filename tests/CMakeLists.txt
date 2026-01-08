# SPDX-License-Identifier: Apache-2.0

add_executable(vicarl_tests
        test_main.c
        test_codec.c
        test_record.c
        test_record_segment.c
        test_merkle.c
        test_store_log.c
        test_ledger.c
        test_p2p_wire.c
        test_p2p_sync.c
        test.c
)

target_link_libraries(vicarl_tests PRIVATE vicarl_shared)

if(VICARL_ENABLE_P2P)
    target_link_libraries(vicarl_tests PRIVATE vicarl_p2p_shared)
endif()

target_include_directories(vicarl_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_BINARY_DIR}/generated/include
)

add_test(NAME vicarl_tests COMMAND vicarl_tests)

# Optional: sqlite tests only if enabled + compiled in
if(VICARL_ENABLE_SQLITE)
    target_sources(vicarl_tests PRIVATE
            test_store_sqlite.c
    )
    add_test(NAME vicarl_tests_sqlite COMMAND vicarl_tests)
endif()
