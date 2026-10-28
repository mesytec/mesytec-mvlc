# Windows test VM (win11-x64-dev)

- VirtualBox VM `win11-x64-dev` on the Linux host, reachable via `ssh win11-x64-dev` (key auth, cmd.exe
  default shell). Checkout at `C:\src\mesytec-mvlc`, Ninja build dir `C:\src\mesytec-mvlc\build`.
- Source sync while iterating: scp the changed/untracked files (`git diff --name-only` +
  `git ls-files --others --exclude-standard`) into the Windows checkout. Commit only once both
  platforms work, then reset the Windows checkout (`git checkout -- .`, remove the copied untracked
  files) and `git pull`. Deleted files must be deleted on Windows by hand.
- Toolchain: VS Build Tools 2026 (v18) with clang-cl. Over ssh run
  `call "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat"`
  before `cmake --build .`. MSBuild ClangCL toolset is not installed, use Ninja.
- PowerShell over ssh: pass scripts via `-EncodedCommand` (UTF-16LE base64), cmd quoting breaks
  otherwise.
- MVLCs are attached with `VBoxManage controlvm win11-x64-dev usbattach|usbdetach <uuid>`
  (`VBoxManage list usbhost` for uuids). 02220066 = USB3, 07260157 = USB2.
- Use `mvlc-cli show_usb_devices` to get an up-to-date list of attached MVLCs.
- Drivers: ftd3xxwu.inf 1.4.0.1 (WinUSB) and ftdibus3.inf 1.3.0.10 are both in the driver store.
  Both match `USB\VID_0403&PID_601F&MI_00`; Windows binds the newer 1.4 by default, 1.3 must be
  picked manually in Device Manager.
