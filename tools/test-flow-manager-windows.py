"""Compile the production Windows cleanup and test real process isolation."""
import hashlib,json,os,re,subprocess,sys
from pathlib import Path
root=Path(__file__).resolve().parents[1]
out=Path(sys.argv[1]).resolve();out.mkdir(parents=True,exist_ok=True)
assert out!=root and not (out/'Data').exists() and not(out/'skse64_loader.exe').exists()
where=Path(os.environ['ProgramFiles(x86)'])/'Microsoft Visual Studio/Installer/vswhere.exe'
vs=Path(subprocess.check_output([str(where),'-latest','-products','*','-requires','Microsoft.VisualStudio.Component.VC.Tools.x86.x64','-property','installationPath'],text=True).strip())
vcvars=vs/'VC/Auxiliary/Build/vcvars64.bat';assert vcvars.is_file()
main=root/'skyrim-platform/src/platform_se/skyrim_platform/main.cpp'
text=main.read_text();detach=re.search(r'bool Detach\(\) override\s*\{(.*?)\n  \}',text,re.S).group(1)
end=re.search(r'bool EndMain\(\) override\s*\{(.*?)\n  \}',text,re.S).group(1)
assert 'FlowManager::'not in detach and 'CloseProcess(L"SkyrimSE.exe")'not in text
assert end.index('overlayService.reset()')<end.index('CloseChildProcesses')
sources=[root/'skyrim-platform/tests/FlowManagerWindowsTests.cpp',root/'skyrim-platform/src/platform_se/skyrim_platform/FlowManager.cpp']
script=out/'build.cmd';script.write_text('@call "'+str(vcvars)+'" >nul\n@cl /nologo /EHsc /std:c++20 /W4 /WX /utf-8 /Od /Zi /Fe:flow-manager-tests.exe '+' '.join('"'+str(p)+'"'for p in sources)+' /link /DEBUG\n')
p=subprocess.run(['cmd.exe','/c',str(script)],cwd=out,capture_output=True,text=True,errors='replace',timeout=90)
(out/'build.log').write_text(p.stdout+p.stderr);assert p.returncode==0,p.stdout+p.stderr
p=subprocess.run([str(out/'flow-manager-tests.exe')],cwd=out,capture_output=True,text=True,timeout=25);assert p.returncode==0,p.stdout+p.stderr
report=json.loads(p.stdout);report['loaderLockChecks']=3;report['sourceSha256']={str(f.relative_to(root)):hashlib.sha256(f.read_bytes()).hexdigest()for f in [*sources,main]}
report['binarySha256']=hashlib.sha256((out/'flow-manager-tests.exe').read_bytes()).hexdigest();(out/'result.json').write_text(json.dumps(report,indent=2));print(json.dumps(report))
