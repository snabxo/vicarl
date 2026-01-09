# SPDX-License-Identifier: Apache-2.0

add_executable(vicarl_ex_ledger_log ex_ledger_log.c)
target_link_libraries(vicarl_ex_ledger_log PRIVATE vicarl_shared)

if(VICARL_ENABLE_SQLITE)
  add_executable(vicarl_ex_ledger_sqlite ex_ledger_sqlite.c)
  target_link_libraries(vicarl_ex_ledger_sqlite PRIVATE vicarl_shared)
endif()

add_executable(vicarl_ex_read_verify ex_read_verify.c)
target_link_libraries(vicarl_ex_read_verify PRIVATE vicarl_shared)

add_executable(vicarl_ex_mother_like_integration ex_mother_like_integration.c)
target_link_libraries(vicarl_ex_mother_like_integration PRIVATE vicarl_shared)

if(VICARL_ENABLE_P2P)
  add_executable(vicarl_ex_p2p_loopback ex_p2p_loopback.c)
  target_link_libraries(vicarl_ex_p2p_loopback PRIVATE vicarl_shared vicarl::p2p)

  add_executable(vicarl_ex_p2p_tcp_server
    ex_p2p_tcp_server.c
    p2p_tcp_common.h
  )
  target_link_libraries(vicarl_ex_p2p_tcp_server PRIVATE vicarl_shared vicarl::p2p)

  add_executable(vicarl_ex_p2p_tcp_client
    ex_p2p_tcp_client.c
    p2p_tcp_common.h
  )
  target_link_libraries(vicarl_ex_p2p_tcp_client PRIVATE vicarl_shared vicarl::p2p)
endif()
