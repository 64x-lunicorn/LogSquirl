# A profile-guided build accepts two diagnostics of its profile as warnings

#682 added profile-guided optimization (`LOGSQUIRL_PGO=GENERATE|USE`, `cmake/ProfileGuidedOptimization.cmake`). ADR 0009 holds every build to `-Werror` on its compile and link lines, and lets a diagnostic through as a warning only when it is examined on its own merits, accepted as narrowly as the compiler allows, and given a way to go away. A build with `LOGSQUIRL_PGO=USE` reports two diagnostics that are not about LogSquirl's code. They are about how well the profile fits the code. This ADR records how they are accepted.

## The two diagnostics

**GCC's `-Wmissing-profile`**: "profile count data file not found". GCC writes one `.gcda` per object that ran during training. An object whose code the training never reached has none, and GCC says so for every function in it. The training is the benchmark mode's scenarios (`tests/e2e`, `-m performance`). They cover the paths LogSquirl has to be fast on, not everything a user can do. So any USE build of LogSquirl has objects of this kind, such as the dialogs, the plugin catalog and the crash reporter. `-fprofile-partial-training` is the matching decision: such code is optimized as it would be without a profile, and not for size as if it were cold. GCC's own diagnostic for a profile that no longer fits its object, `-Wcoverage-mismatch`, is not accepted and still fails the build.

**Clang's hash mismatch**: "function control flow change detected (hash mismatch) … count discarded". Clang looks up a function's profile record by name, and checks the record's hash of the control flow against the function it is compiling. An inline function whose copies differ between translation units (other macros, another include order) meets the record of another copy. Clang then optimizes that function without a profile, which is what the warning says. The first local USE build reported it for `MessageReceiver::receiveMessage`. The commit trained is the commit built, in the same run, so a profile from another commit cannot cause this here. Chromium and Firefox build their PGO with this diagnostic off.

## Narrower than this is not available

Clang has no diagnostic group for the hash mismatch. Measured with Apple clang 21.0.0 (clang-2100.3.34.2), with a profile of one `f` and a changed `f` compiled against it, with and without `-flto`: the warning is reported as `[-Wbackend-plugin]`. `-Wno-error=profile-instr-out-of-date`, `-Wno-error=profile-instr-unprofiled` and `-Wno-error=profile-instr-missing` do not catch it, because those groups belong to Clang's front-end instrumentation (`-fprofile-instr-generate`). LLVM reports the IR instrumentation's mismatch as a generic backend diagnostic, and Clang maps every backend diagnostic without a group of its own to `backend-plugin`. So accepting the hash mismatch means accepting the whole group.

## Decision

- **Each is accepted with `-Wno-error=`, never `-Wno-`.** The diagnostic stays in the log, where the next reader can see it is still there.
- **Each on its own compiler and only where the profile is.** `-Wno-error=missing-profile` goes to GCC, `-Wno-error=backend-plugin` to Clang and AppleClang, and only in USE mode. Both go only to the targets that get the profile: the libraries, `logsquirl` and `logsquirl_grep`. GCC's also goes on the link line, where LTO generates the code again. Clang's goes on the compile line only, where the profile is read. A GENERATE build and a build without PGO accept neither. `tests/pgo_flags.cmake` asserts all of this, and that no other diagnostic is relaxed. GCC's `-Wcoverage-mismatch` is named in that check explicitly.
- **The Clang group is held to the hash mismatch outside the compiler.** No flag can do it, so `.github/scripts/pgo.py build --mode USE`, which every USE build of the PGO workflow runs through, fails when the build prints a `-Wbackend-plugin` diagnostic that is not the hash mismatch. Any other diagnostic in that group would fail a build without PGO, and with this check it fails the PGO workflow too. The same step logs how many of each accepted diagnostic the build printed.
- **How each goes away.** This is not ADR 0009's compiler bug with a version bound: no compiler version will make either diagnostic wrong.
  - The missing profile lasts as long as the training covers part of LogSquirl, which is by design. It goes when the training covers every object. It also goes, together with `-fprofile-partial-training`, if partial training is ever given up.
  - The hash mismatch goes when the PGO workflow's USE builds on Clang report it zero times. `pgo.py` logs the count. At that point, or when Clang gives the mismatch a group of its own, `-Wno-error=backend-plugin` is replaced or removed.
  - Before a release job's `pgo` switch in `ci-build.yml` is turned on, the counts of that platform's last PGO run are read again, and this decision is confirmed or narrowed.

## Consequences

- A USE build fails on every diagnostic a build without PGO fails on, except these two. On Clang, the release path's USE build does not run through `pgo.py`: its compile accepts the whole `backend-plugin` group, and only the PGO workflow holds it to the hash mismatch. While every release switch is off, the release path builds nothing with PGO, so this gap has no effect.
- If the training stops reaching code it used to reach, there is no build failure. It shows as more missing profiles in the count, and as slower benchmarks in the PGO workflow's A/B tables.
