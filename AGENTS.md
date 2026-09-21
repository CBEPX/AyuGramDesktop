# Agent Guide for Telegram Desktop

This guide defines repository-wide instructions for coding agents working with the Telegram Desktop codebase.

Do not start a build unless the user requested one. Documentation-only changes need link/content checks, not a Telegram build.

If you're asked to create a Pull Request, then clearly state in PR description that it was AI generated.

## Working from Codex on Windows + WSL

This checkout may be opened in Codex Desktop through the Windows UNC path `\\wsl.localhost\{distro}\home\{user}\Telegram\tdesktop`, while the real Linux path is `/home/{user}/Telegram/tdesktop`. Treat it as a WSL/Linux checkout first, not as a native Windows checkout.

- Prefer running repository-aware commands through WSL:

```powershell
wsl.exe -d {distro} --cd /home/{user}/Telegram/tdesktop -- <command>
```

- PowerShell can read and write files through the UNC path, but native Windows tools may see different ownership, path, executable, or line-ending behavior than Linux tools.
- Git from PowerShell over `\\wsl.localhost\...` can fail with `detected dubious ownership`. Use WSL Git instead. Do not change global Git `safe.directory` settings unless the user explicitly asks for that.
- Keep path styles matched to the shell. Use `/home/{user}/Telegram/tdesktop/...` with WSL commands, and quoted `\\wsl.localhost\{distro}\home\{user}\Telegram\tdesktop\...` paths with native Windows commands. Avoid passing UNC paths to Linux tools or Linux paths to native Windows tools unless the tool explicitly supports them.
- If a command behaves strangely from the PowerShell UNC working directory, retry the same command through `wsl.exe -d {distro} --cd /home/{user}/Telegram/tdesktop -- ...` before concluding the repository or command is broken.
- Recursive searches and repo inspection are usually faster and more faithful through WSL, for example `wsl.exe -d {distro} --cd /home/{user}/Telegram/tdesktop -- rg ...`.
- Do not assume the WSL host has the build toolchain installed directly. In this setup, WSL may not have `cmake`, while Windows may have `cmake`, and the configured `out/` tree may still target the Linux Docker toolchain. Do not run native Windows `cmake --build out` against a Linux/Docker build tree.
- For WSL/Linux builds, use the Docker build entry point from the repository root: `Telegram/build/docker/centos_env/build_debug.sh`. The Docker daemon must be reachable from WSL; checking `docker info` is fine, but do not start a build unless the user asked for one.
- Existing build outputs may be Linux binaries, for example `out/Debug/Telegram` as an ELF executable, not `Telegram.exe`. Verify the build tree before assuming which platform produced it.
- Be careful with text file line endings. In a WSL/Linux checkout, files should remain LF-only unless the file already uses another convention. CRLF finishing applies only to native, non-WSL Windows runs/checkouts. Do not let PowerShell or Windows tools silently rewrite WSL files to CRLF. If a file becomes mixed, normalize it back to the convention appropriate for the current checkout, without adding a UTF-8 BOM.
- When using the local `perform-task` skill from this WSL checkout, keep external AI task artifacts and edited project text files LF-only. Treat its Windows text-normalization phase as not applicable to WSL, except to record that line endings were checked and kept LF/no-BOM. Run CRLF normalization only in a native, non-WSL Windows checkout.

## Build System Structure

The build system expects this directory layout:

```text
L:\Telegram\                    # BuildPath
L:\Telegram\tdesktop\           # Repository (you work here)
L:\Telegram\Libraries\          # 32-bit dependencies (Linux/macOS)
L:\Telegram\win64\Libraries\    # 64-bit dependencies (Windows)
L:\Telegram\ThirdParty\         # Build tools (NuGet, Python, etc.)
```

Dependencies are located relative to the repository: `../Libraries`, `../win64/Libraries`, or `../ThirdParty`.

## Build Configuration

### Build Commands

**From repository root, run:**

```bash
cmake --build out --config Debug --target Telegram
```

That's it. The `out/` directory is already configured. The executable will be at `out/Debug/Telegram.exe`.

**From WSL, run through the Linux Docker build environment:**

```bash
Telegram/build/docker/centos_env/build_debug.sh
```

**Important:** When running cmake from a shell that doesn't support `cd`, use quoted absolute paths:
```bash
cmake --build "l:\Telegram\tx64\out" --config Debug --target Telegram
```

**Never build Release** - it's extremely heavy and not needed for testing changes.

## Platform-Specific Requirements

### Windows
- Requires Visual Studio 2022
- Must run from appropriate Native Tools Command Prompt:
  - "x64 Native Tools Command Prompt" for `win64`
  - "x86 Native Tools Command Prompt" for `win`
  - "ARM64 Native Tools Command Prompt" for `winarm`
- Dependencies: `../win64/Libraries` (64-bit) or `../Libraries` (32-bit)

### macOS
- Requires Xcode
- Dependencies: `../Libraries/local/Qt-*`
- Set `QT` environment variable: `export QT=6.8`

### Linux
- Build dependencies in `../Libraries`
- Set `QT` environment variable if needed

## Key Files

- **`Telegram/build/version`** - Version information
- **`out/`** - Build output directory

## Troubleshooting

### "Libraries not found"
Ensure the repository is in `L:\Telegram\tdesktop`. The build system requires `../win64/Libraries` to exist.

### Build fails with "wrong command prompt"
On Windows, use the correct Visual Studio Native Tools Command Prompt matching your target (x64/x86/ARM64).

### macOS crashes while reading the cached language pack

After an incremental Xcode build that regenerated `lang.strings` outputs, the
app can link a new generated key lookup with stale objects that still use an
older `kKeysCount`. The characteristic failure is:

- the Debug log stops immediately after
  `Lang Info: Loaded cached, keys: ...`;
- stderr and `tdata/working` may be empty;
- a fresh `~/Library/Logs/DiagnosticReports/Telegram-*.ips` shows `SIGABRT`
  from `std::vector<unsigned char>::operator[]`, then
  `Lang::Instance::applyValue()`, `fillFromSerialized()`, and
  `Local::readLangPack()`.

If this exact startup failure repeats twice, do not change the implementation,
test overlay, or portable account. Stop only this checkout's exact Telegram
process. Because Xcode's `CONFIGURATION_BUILD_DIR` is `out/Debug`, make a
safety copy of every existing portable folder outside `out/` before cleaning:

```bash
portable_backup_root="$(mktemp -d "${TMPDIR:-/tmp}/tdesktop-portable-clean.XXXXXX")"
for portable_name in \
  TelegramForcePortable \
  test_TelegramForcePortable \
  real_TelegramForcePortable; do
  if [ -d "out/Debug/$portable_name" ]; then
    ditto "out/Debug/$portable_name" "$portable_backup_root/$portable_name"
  fi
done
```

Require every expected backup copy to exist before continuing. Then perform
one full Xcode Debug clean and rebuild:

```bash
cmake --build out --config Debug --target clean
cmake --build out --config Debug --target Telegram
```

Afterward, restore a portable folder from the backup only when its original
path is missing; never overwrite a folder that survived the clean. Verify all
three original folder names that existed before the clean are present, keep
the backup until the rebuilt app completes one successful launch, and record
its path if the run stops before verification. Then rerun the same test once.
If the signature persists after that clean rebuild, continue normal crash
diagnosis or report the blocker. Do not loop clean rebuilds.

### Build output locks

For builds owned by the autonomous `continue` / `perform-task` workflow, read
and follow `.agents/shared/build-lock-recovery.md`. PDB, EXE, OBJ, and other
build-output lock errors are recoverable: stop only the exact checkout
executable or verified build-tree holders, delete only exact named artifacts
inside that checkout's build tree, and retry within the bounded recovery
budget. Never stop an installed Telegram client, another checkout, an IDE, or
an unknown process.

Outside that autonomous workflow, an exact checkout executable may be running
because the user is testing it. Do not terminate it or delete locked build
outputs without explicit permission. Report the exact locked path and ask the
user to close that checkout's Telegram/debugger before rebuilding.

## Best Practices

1. **Always use Debug builds** - Release builds are extremely heavy
2. **Don't build Release configuration** - it's too heavy for testing

## Text File Format

- On Windows, keep project text files with CRLF line endings.
- Do not save source, header, build/config, style, or localization files as UTF-8 with BOM. Use UTF-8 without BOM.
- When rewriting project text files for normalization, preserve file content otherwise and do not introduce a BOM.

## Commits

- Subject: one concise, plain-language line summarizing the change, ~50-60 characters, matching the style of recent `git log` subjects. This is usually the entire message.
- For an `ai-tdesktop` task, start the subject with exactly `[ai] ` when the
  retained task implementation changes permanent test-helper code, the agent
  harness, or agent documentation in any way. This includes
  `Telegram/SourceFiles/test/`, `.agents/`, `.claude/`, `AGENTS.md`,
  `CLAUDE.md`, and files whose sole role is supporting those systems. Do not
  count the disposable test overlay or external AI task artifacts. For every
  other task, the subject must not contain `[ai]` anywhere.
- For ordinary work not associated with an AI task, add a short plain-language body only when the subject can't carry it (what was done, not the technical how) — a line or two at most.
- Never add a `Co-Authored-By:` line or any tool/assistant attribution trailer.
- Never add `Autotask:`/attempt or other internal run markers. A commit owned by
  an `ai-tdesktop` task has exactly three lines: the concise subject, a blank
  line, and `Task: <task-id>`. Do not add a body. Keep rationale and
  implementation notes out of the commit message; put a short durable note
  under `tasks/<task-id>.md` only when useful. Do not copy commit hashes into
  that note or any AI task artifact; the task id is the cross-repository link.

## Local Storage Serialization

Both app-level (`Core::Settings`) and session-level (`Main::SessionSettings`) use sequential binary serialization via `QDataStream`. Key rules:

- New fields must ALWAYS be appended at the **end** of the stream, never inserted in the middle
- Reading new fields must be guarded with `!stream.atEnd()` and provide a meaningful default/fallback
- Inserting in the middle breaks reading of data saved by older versions (the new read code consumes bytes that belong to subsequent fields)
- For simple flags and values, prefer using the generic KV prefs facility (`writePref<Type>` / `readPref<Type>`) instead of adding to the binary stream -- this avoids serialization ordering issues entirely

---

## Development references

Before C++ changes, read [Coding Style](docs/agent-development.md#coding-style).
For API/schema changes, read [API Usage](docs/agent-development.md#api-usage),
including [callback lifetime](docs/agent-development.md#api-request-callback-lifetime):
session-owned callbacks can outlive UI objects; preserve the documented guard,
cancellation and request-delivery semantics. The local-storage rules above
remain mandatory for persisted data changes.

Read [UI styling](docs/agent-development.md#ui-styling),
[localization](docs/agent-development.md#localization), or
[RPL](docs/agent-development.md#rpl-reactive-programming-library) when touching
those components. These references retain the original detailed guidelines.
