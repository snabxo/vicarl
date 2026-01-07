option(VICARL_ENABLE_P2P "Enable P2P replication module" OFF)
option(VICARL_ENABLE_SQLITE "Enable SQLite storage backend" OFF)

set(VICARL_CRYPTO_BACKEND "sodium" CACHE STRING "Crypto backend: sodium|mbedtls|builtin")
set_property(CACHE VICARL_CRYPTO_BACKEND PROPERTY STRINGS sodium mbedtls builtin)
