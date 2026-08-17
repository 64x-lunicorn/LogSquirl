# Context

LogSquirl is a fast, cross-platform log file explorer built with C++23 and Qt6, for programmers and system administrators who need to browse, search, and monitor large or fast-moving log files.

## Glossary

### Index Cache

A saved copy of a file's line index. Re-opening a file that's already been indexed loads instantly from this cache instead of re-scanning the whole file from disk.

### Log Format

A definition (compatible with lnav) that tells LogSquirl how to split a line into columns — timestamp, level, body, and custom fields — so matching log files display as a structured table instead of plain text.

### Plugin

An extension that adds a data source, a data converter, or a UI feature to LogSquirl. Plugins can be compiled code or Lua scripts, and can be browsed and installed from the built-in Plugin Repository.

### Highlighter

A saved set of colorization rules that highlights matching lines in the log view. Users can keep several highlighter sets and switch between them.
