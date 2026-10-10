# XuanLing Architecture (MVP)

## Layers

1. CLI Layer
- Parse args
- Route subcommands
- Format output

2. Core Layer
- Gather runtime and device state
- Provide stable data model for commands

3. Adapter/Utility Layer
- Optional shell command execution
- File-based fallback for constrained environments

## Design Notes

- Keep startup fast and dependencies minimal.
- Prefer `/proc` and built-in APIs before external commands.
- Treat OpenWrt as first-class target but keep POSIX fallback.
