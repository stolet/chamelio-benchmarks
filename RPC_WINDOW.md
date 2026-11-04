TCP and Homa echo clients support two load modes:

```
--rate-limit rate --rate 10
--rate-limit window --window 16
```

Rate mode is the default and retains the existing rate units and behavior.
Window mode sends whenever a slot is free, without rate pacing. It limits
requests awaiting a complete response to N per TCP connection or Homa endpoint.
TCP partial writes occupy a slot from their first successful write. Existing
Chamelio TCP burst schedules still gate transmission in window mode.

Window mode requires a positive `--window` and rejects an explicit `--rate`.
Rate mode rejects `--window`. Homa windows are limited to 1..128; eTran Homa
also accepts a comma-separated window per worker, like its rate and size lists.
Homa's legacy `--mpending` remains available in rate mode, but conflicts with
window mode. TCP's `--mpending` still controls concurrent connection setup.
Servers require no new arguments. UDP remains rate-only.

The artifact runner accepts `--rate-limit window --window N` for TCP and Homa
echo clients. Config constructors also accept `rate_limit="window", window=N`.
Other workloads retain their existing load controls.
