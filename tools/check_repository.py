#!/usr/bin/env python3
"""Check publishable source paths and Markdown links without local game assets."""
import argparse
from pathlib import Path
import re
import subprocess
from urllib.parse import unquote

GENERATED={'build','runtime','evidence','dist','tmp','out','.git','.venv','.cache','__pycache__'}
BAD_SUFFIXES=('.dat','.bik','.mp3','.wav','.exe','.dll','.so','.dylib','.acsave','.draft','.zip','.tar.gz','.tar.xz','.mp4','.mkv','.pdb')
CREDENTIALS={
    'private key':r'-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----',
    'GitHub token':r'gh[pousr]_[A-Za-z0-9]{30,}|github_pat_[A-Za-z0-9_]{30,}',
    'cloud access key':r'AKIA[0-9A-Z]{16}',
    'API key':r'sk-(?:proj-)?[A-Za-z0-9_-]{35,}',
}
LINK=re.compile(r'!?\[[^\]]*\]\((?:<([^>]+)>|([^\s)]+))(?:\s+"[^"]*")?\)')


def source_files(root):
    top=subprocess.run(['git','-C',str(root),'rev-parse','--show-toplevel'],capture_output=True,text=True)
    if top.returncode==0 and Path(top.stdout.strip()).resolve()==root:
        data=subprocess.check_output(['git','-C',str(root),'ls-files','-z'])
        return {Path(p) for p in data.decode().split('\0') if p}
    # An exported source tree deliberately has no .git or local work directories.
    import os
    files=set()
    for directory,children,names in os.walk(root):
        children[:]=[name for name in children if name not in GENERATED]
        files.update((Path(directory)/name).relative_to(root) for name in names)
    return files


def check(root):
    files=source_files(root);errors=[];links=0
    for relative in sorted(files):
        path=root/relative
        if not path.is_file():errors.append(f'{relative}: tracked file is missing');continue
        if relative.parts[0] in GENERATED or relative.name.lower().endswith(BAD_SUFFIXES):
            errors.append(f'{relative}: generated, private or original-game artifact in source')
        if relative.name=='arcmp-player.id' or relative.name.startswith('arcmp-content.'):
            errors.append(f'{relative}: runtime identity/content cache in source')
        if path.stat().st_size>50*1024*1024:errors.append(f'{relative}: oversized source file; use release assets')
        if path.suffix.lower() in {'.png','.bmp','.jpg','.jpeg','.ico','.jar'}:continue
        text=path.read_text(errors='replace')
        for label,pattern in CREDENTIALS.items():
            for match in re.finditer(pattern,text):
                line=text.count('\n',0,match.start())+1
                errors.append(f'{relative}:{line}: possible {label} (value suppressed)')
        if relative.suffix.lower()!='.md':continue
        for match in LINK.finditer(text):
            target=match[1] or match[2]
            if re.match(r'^(?:[a-zA-Z][a-zA-Z0-9+.-]*:|#)',target):continue
            target=unquote(target.split('#',1)[0].split('?',1)[0])
            if not target:continue
            links+=1;resolved=(path.parent/target).resolve()
            try:dest=resolved.relative_to(root)
            except ValueError:errors.append(f'{relative}: link leaves source checkout: {target}');continue
            if resolved.is_dir():valid=any(dest==p or dest in p.parents for p in files)
            else:valid=dest in files and resolved.is_file()
            if not valid:errors.append(f'{relative}: missing/untracked Markdown target: {target}')
    for entry in ['README.md','CONTRIBUTING.md','docs/BUILDING.md','docs/INSTALL.md','docs/PLAYER_GUIDE.md']:
        path=root/entry
        if path.exists() and re.search(r'/(?:home|Users)/[^\s/]+/',path.read_text()):
            errors.append(f'{entry}: machine-specific path in a public entry point')
    if errors:
        print('\n'.join(errors));print(f'FAILED: {len(errors)} issue(s)');return 1
    print(f'OK: {len(files)} source files, {links} local Markdown links; no local artifacts or targeted credential patterns.')
    return 0


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root',type=Path,default=Path(__file__).resolve().parents[1])
    args=parser.parse_args()
    raise SystemExit(check(args.root.resolve()))
