"""Compile real Hook.cpp method bodies and reproduce the previous gate leak."""
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
out = Path(sys.argv[1]).resolve()
assert out != root and not (out / 'Data').exists() and not (out / 'skse64_loader.exe').exists()
out.mkdir(parents=True, exist_ok=True)
where = Path(os.environ['ProgramFiles(x86)']) / 'Microsoft Visual Studio/Installer/vswhere.exe'
vs = Path(subprocess.check_output([str(where), '-latest', '-products', '*', '-requires',
          'Microsoft.VisualStudio.Component.VC.Tools.x86.x64', '-property', 'installationPath'], text=True).strip())
vcvars = vs / 'VC/Auxiliary/Build/vcvars64.bat'
assert vcvars.is_file()
source = root / 'skyrim-platform/src/platform_se/skyrim_platform/Hook.cpp'
baseline = subprocess.check_output(['git', '-C', str(root), 'show',
    '1c69c5d9a008887b8fcf48ff30de7e3f43053f42:skyrim-platform/src/platform_se/skyrim_platform/Hook.cpp']).decode('utf8')
test = root / 'skyrim-platform/tests/HookScopeWindowsTests.cpp'
def methods(text):
    result = []
    for name in ['Hook', 'AddHandler', 'RemoveHandler', 'GetName', 'Enter', 'Leave']:
        match = re.search(r'(?m)^[^\n]*\bHook::' + name + r'\(', text)
        assert match, name
        start = text.index('{', match.start())
        level = 1
        end = start + 1
        while level:
            if text[end] == '{': level += 1
            elif text[end] == '}': level -= 1
            end += 1
        result.append(text[match.start():end])
    return '\n\n'.join(result) + '\n'

reports = {}
for label, text in [('baseline', baseline), ('current', source.read_text(encoding='utf8'))]:
    folder = out / label
    folder.mkdir(exist_ok=True)
    extracted = methods(text)
    (folder / 'HookMethods.inc').write_bytes(extracted.encode('utf8'))
    script = folder / 'build.cmd'
    script.write_text('@call "' + str(vcvars) + '" >nul\n@cl /nologo /EHsc /std:c++20 /W4 /WX /utf-8 /Od '
        '/I"' + str(folder) + '" /Fe:hook-scope-tests.exe "' + str(test) + '"\n')
    built = subprocess.run(['cmd.exe', '/c', str(script)], cwd=folder, capture_output=True,
                           text=True, errors='replace', timeout=90)
    (folder / 'build.log').write_text(built.stdout + built.stderr, encoding='utf8')
    assert built.returncode == 0, built.stdout + built.stderr
    ran = subprocess.run([str(folder / 'hook-scope-tests.exe')], cwd=folder,
                         capture_output=True, text=True, timeout=10)
    reports[label] = {'returncode': ran.returncode, 'output': ran.stdout.strip(),
                      'error': ran.stderr.strip(), 'methodsSha256': hashlib.sha256(extracted.encode()).hexdigest()}
assert reports['baseline']['returncode'] == 1 and 'inside hook context' in reports['baseline']['error'], reports
assert reports['current']['returncode'] == 0, reports
report = {'status': 'PASS', 'checks': 8, 'previousLeakReproduced': True,
          'productionMethodsCompiled': True, 'engineCallbacksSimulated': True, 'realSkyrim': False,
          'sourceSha256': hashlib.sha256(source.read_bytes()).hexdigest(), 'runs': reports}
(out / 'result.json').write_bytes((json.dumps(report, indent=2) + '\n').encode('utf8'))
print(json.dumps(report))
