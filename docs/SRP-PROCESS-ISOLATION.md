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
the cleanup isolation, not two actual Skyrim sessions or a complete DLL build.
The full local build failed while accessing the previous D: checkout; the
candidate-only Windows workflow builds the platform binaries without installing
a client, publishing a release, touching CDN, or replacing a server native.

Human acceptance still needs two games, independent characters, and closing
one game while confirming the other remains connected. Any independent reason
for the second game's exit must be diagnosed separately rather than hidden.
