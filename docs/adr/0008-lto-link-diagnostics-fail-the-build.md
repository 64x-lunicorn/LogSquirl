# Link time optimization's diagnostics fail the build, on the compilers that produce them

LogSquirl builds with link time optimization on every platform and every build type (`LOGSQUIRL_USE_LTO`, default on, `CMakeLists.txt`; `lto_covers_project_targets` keeps it covering every target under `src/` and `tests/` alike, #280). That means the compiler generates code twice: once per source file, and once more at the link step, over the whole program at once. The second round produces its own diagnostics, and until #454 nothing was done with them.

`project_warnings` carries `-Wall -Wextra -Wshadow -Wconversion …` and, with `WARNINGS_AS_ERRORS` (default on), `-Werror`; it is applied with `target_compile_options(project_warnings INTERFACE …)`. CMake expands a target's compile options into its *compile* rules only. The default C++ link rule is `<CMAKE_CXX_COMPILER> <FLAGS> <CMAKE_CXX_LINK_FLAGS> <LINK_FLAGS> <OBJECTS> -o <TARGET> <LINK_LIBRARIES>`, where `<FLAGS>` at that step is the language-level `CMAKE_CXX_FLAGS`, not the target's compile options. So `-Werror` never reached the link line, and a whole class of GCC diagnostics could appear in a CI log with everything staying green.

That is not hypothetical. The appimage (GCC 12) and noble (GCC 13) jobs printed, on every run:

```
src/textviewscrolling/src/textviewscrolling.cpp:319:25: warning: writing 13 bytes into a region of size 0 [-Wstringop-overflow=]
src/ui/src/logtableview.cpp:292:22: warning: writing 14 bytes into a region of size 0 [-Wstringop-overflow=]
```

## The two warnings, examined on their own merits

Both lines are a plain assignment of one struct to a member of the same type: `presentationPolicy_ = policy;` in `TextViewScrolling::setPresentationPolicy`, and `quickFindPolicy_ = policy;` in `LogTableView::setQuickFindPolicy`. `PresentationPolicy` is seven `bool` and one `int` — the 13 bytes of members the first warning names; `QuickFindPolicy` is two enums and six `bool` — the 14 of the second. There is nothing to write past: the destination is a member of exactly that type, and the source is a `const&` to one.

The warning appears only when GCC inlines across LogSquirl's two Presentations, which both inherit more than one base: `class LogMainView : public AbstractLogView, public LogPresentation` and `class LogTableView : public QTableView, public LogPresentation`. That shape is not incidental — ADR 0003 records why it exists: Qt forbids a class from inheriting two `QObject`s, so the Text View is a `QAbstractScrollArea` and the Table View a `QTableView`, and what they share is a plain interface, `LogPresentation`, with no data members at all.

`LogPresentation` being an interface is what produces the report. It is the *second* base, so its subobject is the bare vptr the warning calls "size 8", and a pointer to it is not a pointer to the complete object. GCC's own text says as much: it reports the write "at offset -856 into destination object of size 8" — a negative offset, measured from the interface base subobject instead of from the complete object. The pointer adjustment was lost somewhere in the inlining. Every one of the setters involved is defined in a `.cpp`, which is why no ordinary compile ever reports this: without LTO, GCC never sees the bodies of `LogMainView::setPresentationPolicy`, `AbstractLogView::setPresentationPolicy` and `TextViewScrolling::setPresentationPolicy` together and never performs the cross-translation-unit inlining that loses it.

GCC 16 (the fedora job) does not report it. GCC 14 and 15 are not built here, so they are not measured either way; the guard is written against the versions that do report it.

**Conclusion: a GCC 12/13 bug in the late interprocedural passes, not a finding about LogSquirl's code.** There is no write to correct and no member to move.

## Decision

- **LTO link diagnostics fail the build.** `set_project_warnings()` now also calls `target_link_options(project_warnings INTERFACE …)`, and `WARNINGS_AS_ERRORS` puts `-Werror` there for GCC and for Clang. Both carry the warning switches themselves into the intermediate code, so the link line needs nothing but the flag that says what to do with what they report — passing the whole `-W…` set again would be noise.
- **The coverage check already in place covers this too, and on purpose.** `project_warnings_cover_project_targets` (#451) walks the link graph and fails when a target under `src/` or `tests/` reaches neither `project_warnings` nor a library that passes it on. Putting the link flags on the same INTERFACE library, rather than into `CMAKE_CXX_FLAGS` or a second one, is what makes that walk answer the link-step question as well as the compile-step one.
- **`tests/project_warning_flags.cmake` holds the wiring to that.** `logsquirl_project_warnings()` takes the compiler, its version, and whether warnings are errors as arguments and returns the compile and the link flags, so every one of these decisions can be read back in a `cmake -P` check without having that compiler to hand.
- **MSVC is left out.** Its linker's `/WX` turns the *linker's* own `LNK` warnings into errors — a different class from the code generation this is about (a missing PDB for a third-party import library, say, would fail the build and tell us nothing). LTCG's own diagnostics on MSVC were not measured here, so nothing is claimed about them; this is a decision not to guess, not a finding that there is nothing to catch.
- **The GCC 12/13 `-Wstringop-overflow` is recorded as an accepted warning, with an expiry.** `-Wno-error=stringop-overflow` goes on the link line for `GNU` below version 14, and nowhere else. It is deliberately *not* `-Wno-stringop-overflow`: the warning stays in the log, where the next person can see it is still there. And deliberately not a `#pragma GCC diagnostic` around the two setters: GCC raises this one from the LTRANS partition at link time, where the source file's pragma state is not reliably honoured, and a pragma would also put a workaround for a compiler bug into production code that has nothing wrong with it.
- **The expiry is a guard, not a reminder.** `ARG_COMPILER_VERSION VERSION_LESS 14` takes the flag back by itself as soon as the oldest GCC LogSquirl is built with reaches 14; `project_warning_flags` asserts both halves, that GCC 12 and 13 get it and that GCC 14 and 16 do not. Note this is not close: the appimage job pins GCC 12 on purpose (`docker/ubuntu22.04/Dockerfile`), because it fixes the AppImage's glibc/libstdc++ floor at 2.35, so that pin will outlive several Ubuntu releases.

## Consequences

- A GCC warning raised at link time now fails the build, on four Linux jobs at once, and the sanitizer job is no exception (it builds with `-DLOGSQUIRL_USE_LTO=OFF`, so it has no link-time code generation to report on; its own accepted warning, `-Wno-error=maybe-uninitialized`, stays where it was, at compile time).
- Only the compiler's diagnostics are escalated, not the linker's. `-Werror` is a flag of the compiler driver; a warning from `ld` itself would need `-Wl,--fatal-warnings`, which is not set here. So the duplicate-library warnings #450 dealt with, and their kind, still report without failing.
- The accepted warning is the narrowest thing that could be accepted: one diagnostic, at the link step only, on one compiler, below one version. A `-Wstringop-overflow` from a *compile* is a finding about a write the compiler can see all of, and still fails the build.
- Anything else GCC 12 or 13 reports from the link step now fails the build rather than scrolling past. That is the point, and it is also the risk: a second latent diagnostic of this kind would show up as a red job rather than as a line in a log. It should be judged the way this one was — read the code, decide whether the write is real — and not appended to the same guard because it happens to come from the same compilers.
- macOS and Windows are unchanged in practice. AppleClang gets `-Werror` on the link line and reported nothing new when this was measured; MSVC gets nothing, by the decision above.
- `WARNINGS_AS_ERRORS=OFF` still builds: it takes `-Werror` off the link line as well as off the compile lines, so a build that asked for warnings does not get errors out of the link anyway.
