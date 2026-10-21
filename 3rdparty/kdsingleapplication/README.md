# KDSingleApplication, vendored

Source: https://github.com/variar/KDSingleApplication (klogg's fork of
https://github.com/KDAB/KDSingleApplication), commit
5b30db30266f92bc01f1439777803ce8dbf16c79, `src/` only. MIT licensed, see
LICENSE.txt and LICENSES/MIT.txt; the NOTICE file at the repository root
carries the notice.

The files keep upstream's formatting; the Format check leaves `3rdparty/`
out.

## Changes for LogSquirl (#827)

The single-instance lock and local socket of upstream live in
`QDir::tempPath()` under a predictable name: on a multi-user Linux host, that
is the shared `/tmp`, where another user can hold the lock or listen on the
socket. Each change is marked `LogSquirl (#827)` in the code.

- `KDSingleApplication(name, directory)`: the lock file and, on Unix, the
  socket are created in `directory`, which the caller keeps reachable by the
  user alone (LogSquirl: `singleInstanceLocation()`). The socket is `<name>`
  inside it, with neither the user id nor the `kdsingleapp-` prefix of
  upstream, which only kept the path longer, and without `-<XDG_SESSION_ID>`
  (#841): the login session id is the caller's to add, because it is what
  the caller drops when the socket's path would not fit `sun_path` (104
  bytes on macOS, 108 on Linux) otherwise. On Windows the named pipe keeps
  upstream's `kdsingleapp-<session>-<name>`: pipes are not scoped by a
  directory. An empty directory makes an instance that takes no lock and
  listens nowhere; it is primary, on its own.
- The server sets `QLocalServer::UserAccessOption`: the socket (on Windows
  the pipe's DACL) is reachable by the user alone.
- Holding the lock makes an instance primary, even when it fails to listen.
  Upstream released the lock and reported a secondary instance then, and a
  caller handed its message to whoever was listening. `isListening()` tells
  the two apart.
- `sendMessageWithResult()` tells a send that could not connect
  (`NotConnected`: nothing listens) from one that connected and failed. The
  caller starts on its own instead of exiting when nothing answers.
- `socketName()` exposes the socket's name (its path on Unix) for the tests.
