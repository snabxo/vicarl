# Vicarl

Vicarl (Vicar Ledger) is an embeddable, modular C library for building tamper-evident, append-only ledgers. It is designed for consumer and embedded applications, with particular attention to mobile platforms with limited I/O, battery constraints, and intermittent connectivity.

## Overview

Vicarl provides:

- **Tamper-evident storage**: Append-only ledger with cryptographic hash chaining
- **Pluggable backends**: File-based log storage or SQLite with indexed queries
- **P2P replication**: Lightweight peer-to-peer synchronization protocol
- **Verifiable data**: SHA-256 hashing, Merkle proofs, optional signatures
- **FFI support**: Stable C API suitable for bindings to other languages
- **Zero external dependencies**: Built-in SHA-256 implementation (optional libsodium/mbedtls)

## Use Cases

- Audit trails for microservices and distributed systems
- Offline-first mobile applications requiring verifiable sync
- Embedded systems needing lightweight cryptographic ledgers
- Event sourcing with tamper-evident guarantees

## Building

Vicarl requires a C23-compatible compiler.

```bash
mkdir build && cd build
cmake ..
make
```

### Build Options

| Option | Default | Description |
|--------|---------|-------------|
| `VICARL_ENABLE_P2P` | OFF | Enable P2P replication module |
| `VICARL_ENABLE_SQLITE` | OFF | Enable SQLite storage backend |
| `VICARL_CRYPTO_BACKEND` | builtin | Crypto backend: `builtin`, `sodium`, or `mbedtls` |

Example with all features enabled:

```bash
cmake -DVICARL_ENABLE_P2P=ON -DVICARL_ENABLE_SQLITE=ON ..
make
```

## Quick Start

### Creating a Ledger

```c
#include <vicarl/vicarl.h>

vicarl_ledger_options_t opts = {
    .store_kind = VICARL_STORE_LOG,
    .enable_merkle = 1,
    .segment_target_records = 100,
};

vicarl_ledger_t* ledger;
vicarl_status_t st = vicarl_ledger_open(&ledger, "./my_ledger", &opts);
if (st != VICARL_OK) {
    fprintf(stderr, "error: %s\n", vicarl_last_error_message());
    return 1;
}
```

### Appending Records

```c
vicarl_record_meta_t meta = {
    .namespace_utf8 = { .ptr = (uint8_t*)"app.events", .len = 10 },
    .schema_utf8 = { .ptr = (uint8_t*)"event.v1", .len = 8 },
    .timestamp_ms = current_time_ms(),
};

vicarl_slice_t payload = { .ptr = (uint8_t*)data, .len = data_len };
vicarl_bytes_t encoded;
vicarl_record_encode(&meta, payload, NULL, &encoded);

vicarl_hash32_t record_id;
vicarl_ledger_append_record(ledger, 
    (vicarl_slice_t){ encoded.ptr, encoded.len }, &record_id);

vicarl_free(encoded.ptr);
```

### Verifying the Ledger

```c
vicarl_status_t st = vicarl_ledger_verify(ledger);
if (st == VICARL_OK) {
    printf("ledger integrity verified\n");
}
```

### Closing

```c
vicarl_ledger_flush(ledger);
vicarl_ledger_close(ledger);
```

## Architecture

Vicarl follows a layered architecture:

| Layer | Components | Purpose |
|-------|------------|---------|
| 0 | alloc, error, platform | OS abstractions and memory management |
| 1 | codec, hash, merkle | Cryptographic primitives and encoding |
| 2 | record, segment | Data model with canonical binary format |
| 3 | store_log, store_sqlite | Pluggable storage backends |
| 4 | ledger | Ledger lifecycle and verification |
| 5 | query | Record filtering and indexing |
| 6 | wire, sync | P2P replication protocol |
| 7 | ffi | Foreign function interface |

## Storage Backends

### Log Backend (default)

Minimal file-based storage. Segments are stored as individual files in a directory with an index file for O(1) tip access. Suitable for embedded systems and minimal deployments.

### SQLite Backend

Full-featured backend with atomic transactions, WAL mode, and indexed queries. Supports filtering by namespace, author, and timestamp ranges.

```c
vicarl_ledger_options_t opts = {
    .store_kind = VICARL_STORE_SQLITE,
    .store_options.sqlite_wal = 1,
    .store_options.sqlite_synchronous = 1,
};
```

## P2P Replication

Vicarl includes a lightweight sync protocol for peer-to-peer ledger replication.

```c
vicarl_p2p_sync_t* sync;
vicarl_p2p_sync_options_t opts = { .max_segments_per_request = 64 };

vicarl_p2p_sync_init(&sync, store, send_callback, user_data, &opts);
vicarl_p2p_sync_send_tip(sync);

// On receiving a message:
vicarl_p2p_sync_on_message(sync, &msg);
```

The protocol uses a simple message format (VCP1) with these message types:

- `HELLO`: Protocol version and node identity
- `TIP`: Current ledger tip (segment number and hash)
- `GET_SEGMENTS`: Request segments by range
- `SEGMENT`: Segment data delivery
- `ERROR`: Error reporting

## Examples

The `examples/` directory contains working examples:

- `ex_ledger_log.c` - Basic ledger with log storage
- `ex_ledger_sqlite.c` - Ledger with SQLite backend
- `ex_read_verify.c` - Reading and verifying records
- `ex_mother_like_integration.c` - Integration pattern example
- `ex_p2p_loopback.c` - P2P sync in a single process
- `ex_p2p_tcp_server.c` / `ex_p2p_tcp_client.c` - TCP-based P2P sync

Build and run examples:

```bash
cmake -DVICARL_ENABLE_P2P=ON -DVICARL_ENABLE_SQLITE=ON ..
make
./examples/vicarl_ex_ledger_log
```

## Testing

```bash
./tests/vicarl_tests
```

## API Reference

### Core Types

```c
typedef struct { const uint8_t* ptr; size_t len; } vicarl_slice_t;  // Read-only view
typedef struct { uint8_t* ptr; size_t len; } vicarl_bytes_t;        // Owned bytes
typedef struct { uint8_t bytes[32]; } vicarl_hash32_t;              // SHA-256 hash
```

### Status Codes

| Code | Meaning |
|------|---------|
| `VICARL_OK` | Success |
| `VICARL_ERR_INVALID_ARGUMENT` | Invalid parameter |
| `VICARL_ERR_FORMAT` | Malformed data |
| `VICARL_ERR_IO` | I/O error |
| `VICARL_ERR_NOT_FOUND` | Resource not found |
| `VICARL_ERR_OOM` | Out of memory |
| `VICARL_ERR_INTERNAL` | Internal error |

### Error Handling

```c
vicarl_status_t st = vicarl_ledger_open(...);
if (st != VICARL_OK) {
    const char* msg = vicarl_last_error_message();
    fprintf(stderr, "error: %s\n", msg);
}
```

## Binary Formats

Vicarl uses canonical binary formats for deterministic hashing:

- **VCR1**: Record format (magic + flags + metadata + payload + optional signature)
- **VCS1**: Segment format (magic + header + records + optional signature)
- **VCP1**: P2P wire protocol (magic + type + flags + payload)

All multi-byte integers use little-endian encoding. Variable-length integers use LEB128.

## Thread Safety

- Error messages are thread-local
- Store and ledger instances are not thread-safe; use external synchronization
- The library does not spawn threads

## License

Apache License 2.0. See [LICENSE.md](LICENSE.md) for details.

## Contributing

Contributions are welcome. Please ensure code compiles with strict warnings enabled (`-Wall -Wextra -Werror`).
