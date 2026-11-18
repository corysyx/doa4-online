# The profile-guided build (PGO)

Profile-guided optimisation lets the compiler lay out and inline the game's code by how often each part really runs.
An instrumented build plays a few scripted fights and counts what runs. The release build then reads those counts
(clang's `-fprofile-use`) and spends its optimisation on the hot code.

## What is in the repository

- `pgo/doa4.profdata`: the merged profile (binary, `.gitattributes` marks it so).
- `pgo/source.txt`: what the profile was made from, four lines: `game=` (SHA-256 of the `git ls-tree -r HEAD` lines
  of the game's inputs: `src`, `generated`, `third_party`, `CMakeLists.txt`, `CMakePresets.json`, `hints.toml`,
  `dead_or_alive_4_manifest.toml`), `sdk=` (the SDK commit), `llvm=23.1.4` and `names=basename` (see the release
  workflow below). Locally `sdk=` is the SDK checkout's HEAD commit, so uncommitted SDK edits are not in the id.

`py -3 -I tools/pgo_source_id.py check --sdk-from pin` (or `worktree:<sdk folder>`) decides:

- both files present and `source.txt` names this tree, this SDK, this LLVM and this naming: `pgo: use <profile>`;
- neither file: `pgo: none`, a plain build;
- anything else (one file alone, the game tree, SDK, LLVM or naming differs, or the game files are edited and not
  committed): `pgo: refused: <reason>`, and the build stops. The message ends with the way out: `DOA4_PGO=off`
  locally; in CI, delete both `pgo/` files.

## Making the profile

From a clean commit (no uncommitted change under the paths above), with no game running:

```bat
py -3 -I tools/pgo_recipe.py --worktree C:/doa4/doa4-online --sdk C:/doa4/rexglue-sdk --out C:/doa4/pgo_run ^
   --tag-prefix pg --toml C:/doa4/dead_or_alive_4.toml --userdata C:/doa4/userdata_pristine --cache C:/doa4/cache
```

It builds the instrumented snapshot `<prefix>I` (`DOA4_PGO=gen:<out>/prof`), records the four training fights `tag`,
`single`, `single_c` and `single_savanna` (muted: the toml must say `audio_mute = true`), refuses any run that did not
end with `pgo dump: exe 0, rexruntime.dll 0` and exactly two `.profraw` files, merges them with `llvm-profdata`, and
writes `pgo/doa4.profdata` and `pgo/source.txt` (uncommitted). `<out>/recipe.log` has one line per step and ends with
`recipe done` or `recipe stopped: <reason>`. Commit the two files together.

**Remake the profile for every release whose game or SDK code changed.** A profile made from other code is refused,
never used stale.

## Building locally

`tools\np_build.bat configure` runs the check above against `DOA4_SDK`'s checkout and prints a `pgo:` line.
`DOA4_PGO=off` forces a plain build (the A of a timing comparison); `DOA4_PGO=gen:<folder>` makes the instrumented
build, which is never timed or shipped; the recipe ends with a `DOA4_PGO=off` configure, so the tree is plain again.
`check` runs at configure only, and ninja does not track the profile file: after a new profile at the same path,
delete `out/build/win-amd64-release` or build clean.

## The release workflow

`.github/workflows/netplay-release.yml` runs the check with `--sdk-from pin` (the commit in `netplay_sdk.txt`) before
CMake. `pgo: use` adds `-fprofile-use` to the C and C++ flags; `pgo: none` builds plainly; a refusal stops the job,
so nothing is published with a stale profile. A push that changes `pgo/` publishes a release like any game change.
Both profile modes pass `-mllvm -static-func-full-module-prefix=false` (the use mode here and in `np_build.bat`, the
instrumented mode in `np_build.bat`): a file-local function's profile name is `<file name>;<function>`, not its path,
which a build elsewhere (CI) cannot match (6.6 % of round 1's block counts, lost silently). `names=basename` records it.

Online, a PGO build and a plain build are different code: the handshake refuses to pair them, as it does any two
different builds.
