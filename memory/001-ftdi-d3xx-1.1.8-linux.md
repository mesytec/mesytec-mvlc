# libftd3xx 1.1.8 on Linux (FT601 / MVLC)

Findings from porting mvlc_impl_usb_linux_x86_64.cc from d3xx 0.5.21 to 1.1.8.

- Thin libusb wrapper, no internal URB ring. Each FT_ReadPipe() is one
  libusb_bulk_transfer() of exactly the requested size.
- Reads smaller than the endpoint max packet size (1024 @ USB3) overflow
  (-EOVERFLOW -> FT_IO_ERROR). Afterwards the IN endpoint returns -EPROTO
  (FT_OPERATION_ABORTED, ~3.5ms) on every read until FT_ResetDevicePort() or
  an MVLC power cycle. => Impl::read() buffers, always reads
  USBStreamPipeReadSize.
- Without FT_SetStreamPipe() FT_ReadPipe() sends a read request and loops
  until the full size arrived or the timeout expired (short packets don't end
  the read). With stream mode a short packet completes the read.
- Timeout 0 means wait forever (libusb semantics), not "return immediately".
  The per call timeout is used, FT_SetPipeTimeout() is irrelevant for sync reads.
- Stream mode FT_ReadPipe() installs signal(SIGALRM, SendRequestForZLP) and
  alarm(5); the alarm is not cancelled when the read fails. FT_Close() does not
  cancel it either -> Impl::closeHandle() calls alarm(0).
- FT_GetReadQueueStatus() returns FT_NOT_SUPPORTED. FT_AbortPipe(0x82) returns
  FT_OPERATION_ABORTED.
- FTD3XX_LOG_LEVEL env var enables library debug output.
- Debugging aid: LD_PRELOAD shim on ioctl() logging USBDEVFS_SUBMITURB /
  REAPURB (endpoint, length, status) shows the real USB-level errors.
