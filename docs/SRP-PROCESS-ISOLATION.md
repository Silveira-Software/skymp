# Session process isolation

The previous DLL detach handler enumerated every `SkyrimSE.exe` and every CEF
process by image name, terminating other installations too. Detaching now does
no process work. Normal EndMain closes only this process's own direct CEF
helpers, checking creation time and rechecking parent ownership after opening
each handle. All opened handles are closed.

Run `python tools/test-flow-manager-windows.py <isolated-output-directory>`.
The production cleanup is compiled by MSVC and exercised against real Windows
processes: own helper, other game and helper, unrelated child, repeat cleanup,
empty and unknown selectors. Three source checks keep cleanup out of DllMain.

Seven process checks and three loader-lock checks passed locally. This proves
the cleanup isolation, not two actual Skyrim sessions.
The full local build failed while accessing the previous D: checkout; the
candidate-only Windows workflow builds the platform binaries without installing
a client, publishing a release, touching CDN, or replacing a server native.

The candidate-only Windows build also passed in Actions run `42343526234`
for commit `29c4766c388582fbdb203539300d74e6f89ac18a`. Downloaded binaries
were independently rehashed against its manifest:

- SkyrimPlatform.dll: `cddedfe99d17c84e0b9f35e2c05bd7271cd6f05765b411fe5e5d792549d80dea`
- SkyrimPlatformImpl.dll: `354df75525882fff284bce9c953a04cba660283532e1eb4f088b9f25f8c43dea`
- SkyrimPlatformCEF.exe.hidden: `613022906fa61f1973d04e02d80b4d439dbb21692cb2f1a5cd4575a72616f9da`

The proof artifact contains the same seven real-process and three source
checks. These artifacts are private candidates, with no local installation.
Existing PR checks are not all green: formatting reports 45 older source
files (none of the changed process sources); Dependent Issues cannot access
the PR integration; Arch fails configuring mongo-c-driver. The three Ubuntu
build jobs passed. No check was disabled or marked successful manually.

Human acceptance still needs two games, independent characters, and closing
one game while confirming the other remains connected. Any independent reason
for the second game's exit must be diagnosed separately rather than hidden.
