# Oversized ETH writes

Fixed 2026-10-29 in a4c86fb9. Covered by the controller tests `TestUploadStackPartBoundaries`
and `TestSuperTransactionExceedUdpMaxPayload` in `extras/mvlc-ctrl-tests/mvlc_ctrl_test_basic.cc`.

- `eth::write_to_socket()` returns `MVLCErrorCode::UdpMaxOutgoingPacketSizeExceeded` for payloads
  > 1472 bytes instead of asserting. Without the assert (release builds) `send()` did not fail but
  sent an IP-fragmented datagram; the MVLC ignores it -> 2 s SuperCommandTimeout, cmd pipe resync
  warnings, retry.
- `CmdApi::uploadStack` `EthPartMaxSize` 181 -> 180. 181 resulted in 1476 byte packets for single
  part uploads of exactly 181 stack words (comment forgot CmdBufferStart/End).
- `CmdApi::superTransaction` still retries 3 times on this deterministic error.
