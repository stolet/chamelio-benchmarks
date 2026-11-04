#!/usr/bin/env python3
"""Check split and pipelined requests against a Linux TCP echo server.

Use an AddressSanitizer build to check receive-buffer bounds as well as data.
"""

import argparse
import os
from pathlib import Path
import socket
import subprocess
import time


MESSAGE_BYTES = 64 * 1024
PREFIX_BYTES = 8 * 1024
START_TIMEOUT_SEC = 5
SOCKET_TIMEOUT_SEC = 3


def receive(client, length):
  data = bytearray()
  while len(data) < length:
    part = client.recv(length - len(data))
    if not part:
      raise RuntimeError("Server closed before echoing all data")
    data.extend(part)
  return bytes(data)


def connect(port):
  deadline = time.monotonic() + START_TIMEOUT_SEC
  while True:
    try:
      return socket.create_connection(
          ("127.0.0.1", port), timeout=SOCKET_TIMEOUT_SEC)
    except ConnectionRefusedError:
      if time.monotonic() >= deadline:
        raise
      time.sleep(0.01)


def main():
  parser = argparse.ArgumentParser(description=__doc__)
  parser.add_argument("--server", required=True, type=Path)
  args = parser.parse_args()

  # Queue the burst before letting the server read it on the same CPU.
  os.sched_setaffinity(0, {min(os.sched_getaffinity(0))})
  with socket.socket() as reserve:
    reserve.bind(("127.0.0.1", 0))
    port = reserve.getsockname()[1]

  server = subprocess.Popen([
      str(args.server.resolve()), "127.0.0.1", str(port),
      "--msize", str(MESSAGE_BYTES), "--buf-size", str(MESSAGE_BYTES),
      "--ncores", "1", "--mconns", "1",
  ], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)

  error = None
  try:
    with connect(port) as client:
      prefix = b"a" * PREFIX_BYTES
      client.sendall(prefix)
      if receive(client, len(prefix)) != prefix:
        raise RuntimeError("Split request prefix was corrupted")

      time.sleep(0.01)
      remaining = b"b" * (MESSAGE_BYTES - PREFIX_BYTES)
      remaining += b"c" * MESSAGE_BYTES
      client.sendall(remaining)
      if receive(client, len(remaining)) != remaining:
        raise RuntimeError("Pipelined echo was corrupted")
  except Exception as caught:
    error = caught
  finally:
    server.terminate()
    output = server.communicate(timeout=START_TIMEOUT_SEC)[0]

  if error is not None or "AddressSanitizer" in output:
    print(output)
    raise RuntimeError("Split/pipelined echo failed") from error
  print("PASS: split request followed by a pipelined request")


if __name__ == "__main__":
  main()
