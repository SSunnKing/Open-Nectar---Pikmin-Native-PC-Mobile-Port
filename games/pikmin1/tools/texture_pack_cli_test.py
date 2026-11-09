import json, os, subprocess, tempfile, zipfile, sys
from pathlib import Path
exe = Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(prefix='nectar-pack-cli-') as tmp:
    root=Path(tmp).resolve(); (root/'Load/Textures').mkdir(parents=True)
    env=dict(os.environ,SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',XDG_CONFIG_HOME=str(root/'config'),XDG_CACHE_HOME=str(root/'cache'),LOCALAPPDATA=str(root/'localdata'),APPDATA=str(root/'appdata'),HOME=str(root))
    def run(*args):
        p=subprocess.run([str(exe),*args],cwd=root,env=env,text=True,capture_output=True,timeout=30)
        data=[line for line in p.stdout.splitlines() if line.startswith('{')]
        assert data,(p.returncode,p.stdout,p.stderr)
        return p,json.loads(data[-1])
    for variant in ('hd-1080p','hd-4k'):
        zip=root/(variant+'.zip')
        with zipfile.ZipFile(zip,'w') as z: z.writestr('Pack/Load/Textures/GPI/tex1_test.png','fixture')
        p,s=run('--texpack-install',str(zip),variant)
        assert (root/'Load/Textures'/variant/'GPI/tex1_test.png').exists(), 'variant must install separately'
        assert p.returncode == 0 and not s['messageError'],s
    # A failed or repeated install must preserve both successful variants.
    p,s=run('--texpack-install',str(zip),'hd-4k')
    assert p.returncode != 0 and s['messageError']
    invalid=root/'unsafe.zip'
    with zipfile.ZipFile(invalid,'w') as z: z.writestr('Load/Textures/GPI/../../../../save.dat','bad')
    # Remove only the fixture 4K pack, then test rollback of unsafe extraction.
    p,s=run('--texpack-delete','hd-4k')
    p,s=run('--texpack-install',str(invalid),'hd-4k')
    assert p.returncode != 0 and s['messageError']
    assert not (root/'Load/Textures/hd-4k').exists()
    assert not (root/'Load/Textures/.install-hd-4k').exists()
    assert not (root/'save.dat').exists()
    p,s=run('--texpack-install',str(zip),'hd-4k')
    assert p.returncode == 0
    # Legacy folder IDs are resolved case-insensitively without moving files.
    legacy=root/'Load/Textures/GPI'; legacy.mkdir(); (legacy/'tex1_old.png').write_text('legacy')
    (legacy/'.nectar-pack').write_text('1080p\n')
    p,s=run('--texpack-activate','gpi')
    assert s['texturePacks']['active']=='GPI'
    assert s['texturePacks']['details'][-1]['name']=='HD 1080p (legacy)'
    p,s=run('--texpack-delete','GPI')
    assert s['texturePacks']['active']==''
    p,s=run('--texpack-activate','hd-4k')
    assert s['texturePacks']['active']=='hd-4k'
    assert [p['name'] for p in s['texturePacks']['details']]==['HD 1080p','HD 4K']
    p,s=run('--texpack-delete','hd-4k')
    assert s['texturePacks']['active']=='' and 'Off' in s['message']
    assert 'freed' in s['message'] and str(root/'Load/Textures/hd-4k') in s['message']
    assert (root/'Load/Textures/hd-1080p/GPI/tex1_test.png').exists()
    p,s=run('--texpack-delete','../Textures')
    assert s['messageError']
    p,s=run('--settings-dump')
    assert s['texturePacks']['active']==''
    assert s['texturePacks']['installed']==['hd-1080p']
    details=s['texturePacks']['details']
    assert [p['id'] for p in details]==['hd-1080p','hd-4k']
    assert details[0]['installed'] and not details[1]['installed']
    # Reinstalling a deleted known variant restores the row, but never selects it.
    p,s=run('--texpack-install',str(zip),'hd-4k')
    assert p.returncode == 0 and s['texturePacks']['details'][1]['installed']
    assert s['texturePacks']['active']==''

    print('settings CLI installation, metadata, selection and safe deletion: PASS')
