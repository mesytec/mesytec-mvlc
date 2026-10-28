# MVLC parse_error_counter register (0x8000)

Findings from 2026-10-28, firmware 0x0048, hwId 0x5008. No documentation exists in mesytec-docs,
the wiki or mvme; only `mvlc_constants.h` and the warning in `CmdApi::stackTransaction`.

- Firmware command parser error counter. Not reset on connect, survives across sessions, only a
  power cycle resets it. Read with `mvlc-cli --mvlc eth://<host> register_read 0x8000`.
- Garbage sent to the ETH command port 0x8000 increments it: +1 per invalid word, +1 extra when
  wrapped in F100/F200.
- ETH data packet loss (saturated link) does not increment it.
- Super transactions with many reference words over USB increment it, although the responses are
  correct (`mvlc-super-size-sweep`, 0 failures). Same on Linux and Windows, D3XX 1.3 and 1.4:
  - USB2: starts around n=180..200 words, not deterministic (n=200: +366 on Windows, +1209 on
    Linux; n=190 once +0), n=255..257 about +1540, n=500 +4360.
  - USB3: n<=500 +0, n=1000 +1043 on both platforms.
- ETH with n<=350 (UDP payload limit) gives +0.
