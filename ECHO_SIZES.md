All TCP, UDP, and Homa echo clients and servers accept `--rsize N` for the
response payload size in bytes. `--msize N` remains the request payload size.
Both sizes must be at least 32 bytes. Omitting `--rsize` uses `--msize`.
Pass the same sizes to the client and server, for example:

```
--msize 128 --rsize 32
```

The server echoes the first `min(msize, rsize)` request bytes. When the response
is larger, the remaining bytes are unspecified and are not initialized per
request. Homa payload validation checks only the echoed prefix. Startup Homa
control messages retain their fixed size.

Load in Mb/s counts request bytes; throughput in Mb/s counts response bytes.
TCP clients count an entire `rsize` response before freeing a window slot.
The TCP server's `--buf-size` must hold a complete request; UDP's must hold the
larger of request and response. Homa's size limit remains 1,000,000 bytes.
eTran Homa accepts scalar or per-worker comma-separated `--rsize` values.

The artifact echo client and server configs accept `rsize=None`. Set `rsize`
in the configs directly; `None` omits the flag and preserves equal-size echoes.
