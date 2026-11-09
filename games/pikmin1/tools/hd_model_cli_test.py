"""HD model zips from the settings CLI (fork issue #189).

The launcher's Get HD models window sends each new zip in Downloads to
--hdmodel-identify, then --hdmodel-install -1; Delete uses --hdmodel-delete.
Checks every marker plus negative controls, that a zip that is not a model is
never changed, and that Delete removes only that model's files.
"""
import hashlib, json, os, subprocess, sys, tempfile, zipfile
from pathlib import Path

exe = Path(sys.argv[1]).resolve()

with tempfile.TemporaryDirectory(prefix='nectar-hdmodel-cli-') as tmp:
    root = Path(tmp).resolve()
    (root / 'Load').mkdir()
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', XDG_CONFIG_HOME=str(root / 'config'),
               XDG_CACHE_HOME=str(root / 'cache'), LOCALAPPDATA=str(root / 'localdata'), APPDATA=str(root / 'appdata'),
               HOME=str(root))

    def run(*args):
        p = subprocess.run([str(exe), *args], cwd=root, env=env, text=True, capture_output=True, timeout=30)
        data = [line for line in p.stdout.splitlines() if line.startswith('{')]
        assert data, (p.returncode, p.stdout, p.stderr)
        return p, json.loads(data[-1])

    def make_zip(name, files):
        path = root / 'Downloads' / name
        path.parent.mkdir(exist_ok=True)
        with zipfile.ZipFile(path, 'w') as z:
            for f in files:
                z.writestr(f, '<COLLADA/>' if f.lower().endswith('.dae') else 'png')
        return path

    def fingerprint(path):
        st = path.stat()
        return hashlib.sha256(path.read_bytes()).hexdigest(), st.st_size, st.st_mtime_ns

    # Each marker, as laid out in the Models Resource zips (some in a folder).
    markers = {
        0: ['playerE.dae', 'playerE_body.png', 'playerE_head.png'],
        1: ['Captain Louie/orima3.dae', 'Captain Louie/luzy_565.png'],
        2: ['playerD.dae', 'playerD_body.png'],
        3: ['Pikmin/piki_p3_red.dae', 'Pikmin/piki_p3_blue.dae', 'Pikmin/Textures/piki_red_all.png'],
        4: ['Red Bulborb/model.dae', 'Red Bulborb/face.0.png'],
        5: ['Dwarf Bulborb/kochappy.dae', 'Dwarf Bulborb/kochappy_tex.png'],
    }
    for row, files in markers.items():
        zip = make_zip(f'row{row}.zip', files)
        p, s = run('--hdmodel-identify', str(zip))
        assert s['hdModelDetected'] == row, (row, s.get('hdModelDetected'))
        assert s['hdModelInstalled'] == -1 and p.returncode == 0
    # Upper case from another rip still matches.
    p, s = run('--hdmodel-identify', str(make_zip('upper.zip', ['Dwarf Bulborb/Kochappy.dae'])))
    assert s['hdModelDetected'] == 5
    # An unzipped folder is identified too (Safari unzips downloads).
    folder = root / 'Downloads' / 'Louie'
    folder.mkdir()
    (folder / 'playerD.dae').write_text('<COLLADA/>')
    p, s = run('--hdmodel-identify', str(folder))
    assert s['hdModelDetected'] == 2

    # Negative controls.
    negatives = {
        'olimar-p2.zip': ['Captain Olimar/orima3.dae', 'Captain Olimar/orima_565.png'],  # Pikmin 2 Olimar
        'other.zip': ['Captain Charlie/playerC.dae'],
        'bare-model.zip': ['model.dae'],                                                    # not "Red Bulborb/"
        'renamed.zip': ['playerE.dae.bak', 'notplayerE.daex'],
        'textures.zip': ['Pack/Load/Textures/GPI/tex1_test.png'],
        'empty.zip': [],
    }
    for name, files in negatives.items():
        p, s = run('--hdmodel-identify', str(make_zip(name, files)))
        assert s['hdModelDetected'] == -1, (name, s['hdModelDetected'])
    notzip = root / 'Downloads' / 'notes.zip'
    notzip.write_text('not a zip at all')
    p, s = run('--hdmodel-identify', str(notzip))
    assert s['hdModelDetected'] == -1
    p, s = run('--hdmodel-identify', str(root / 'Downloads' / 'missing.zip'))
    assert s['hdModelDetected'] == -1
    # Requests other than install/identify do not report a detection.
    p, s = run('--settings-dump')
    assert 'hdModelDetected' not in s and 'hdModelInstalled' not in s

    # Installing a zip that is not a model changes nothing anywhere.
    other = root / 'Downloads' / 'other.zip'
    before = fingerprint(other)
    p, s = run('--hdmodel-install', '-1', str(other))
    assert p.returncode != 0 and s['messageError'] and 'Not a known model zip' in s['message']
    assert s['hdModelDetected'] == -1 and s['hdModelInstalled'] == -1
    assert fingerprint(other) == before and other.exists()
    assert not (root / 'Load' / 'Models').exists()
    # A recognised but broken rip fails without claiming an install.
    broken = root / 'Downloads' / 'row0.zip'
    before = fingerprint(broken)
    p, s = run('--hdmodel-install', '-1', str(broken))
    assert s['hdModelDetected'] == 0 and s['hdModelInstalled'] == -1 and s['messageError']
    assert fingerprint(broken) == before
    # A row's own Install button still refuses another character's zip.
    p, s = run('--hdmodel-install', '3', str(root / 'Downloads' / 'row4.zip'))
    assert s['messageError'] and 'Bulborb model, not Pikmin' in s['message'], s['message']

    # Delete removes only that row's files. Bulborb and Dwarf Bulborb share a folder.
    models = root / 'Load' / 'Models'
    (models / 'BulborbHD').mkdir(parents=True)
    (models / 'BulborbHD' / 'bulborb.nhm').write_bytes(b'b' * 3000)
    (models / 'BulborbHD' / 'bulborb_dwarf.nhm').write_bytes(b'd' * 2000)
    (models / 'BulborbHD' / 'notes.txt').write_text('mine')
    (models / 'OlimarHD').mkdir()
    (models / 'OlimarHD' / 'olimar_hd.nhm').write_bytes(b'o' * 1500)
    p, s = run('--hdmodel-enable', '5', '0')
    p, s = run('--settings-dump')
    rows = {m['row']: m for m in s['hdModels']}
    assert rows[5]['installed'] and not rows[5]['enabled'] and rows[5]['bytes'] == 2000 and rows[5]['size'] == '2.0 KB'
    assert rows[5]['path'] == str(models / 'BulborbHD'), rows[5]['path']
    assert rows[0]['bytes'] == 1500 and not rows[3]['installed'] and rows[3]['bytes'] == 0
    p, s = run('--hdmodel-delete', '5')
    assert p.returncode == 0 and not s['messageError'], s['message']
    assert s['message'] == f"Uninstalled Dwarf Bulborb HD (2.0 KB freed) from {models / 'BulborbHD'}.", s['message']
    assert not (models / 'BulborbHD' / 'bulborb_dwarf.nhm').exists()
    assert (models / 'BulborbHD' / 'bulborb.nhm').exists() and (models / 'BulborbHD' / 'notes.txt').exists()
    rows = {m['row']: m for m in s['hdModels']}
    assert not rows[5]['installed'] and rows[5]['enabled'], 'a reinstall starts On again'
    assert rows[4]['installed']
    # The folder goes once it is empty; files the model did not write keep it.
    p, s = run('--hdmodel-delete', '4')
    assert not (models / 'BulborbHD' / 'bulborb.nhm').exists() and (models / 'BulborbHD').exists()
    p, s = run('--hdmodel-delete', '0')
    assert '1.5 KB freed' in s['message'] and not (models / 'OlimarHD').exists()
    # Nothing to delete, or an unknown row, is an error.
    p, s = run('--hdmodel-delete', '0')
    assert p.returncode != 0 and s['messageError'] and 'not installed' in s['message']
    p, s = run('--hdmodel-delete', '9')
    assert p.returncode != 0 and s['messageError']
    # Links are never followed or removed.
    if os.name != 'nt':
        outside = root / 'outside.nhm'
        outside.write_bytes(b'keep')
        (models / 'LouieHD').mkdir()
        (models / 'LouieHD' / 'louie_hd.nhm').symlink_to(outside)
        p, s = run('--hdmodel-delete', '2')
        assert s['messageError'] and outside.read_bytes() == b'keep'
        assert (models / 'LouieHD' / 'louie_hd.nhm').is_symlink()

    # The zips in Downloads are all still there, untouched by any of this.
    assert len(list((root / 'Downloads').glob('*.zip'))) == len(markers) + 1 + len(negatives) + 1

    print('HD model zip identify, install refusal and safe delete: PASS')
