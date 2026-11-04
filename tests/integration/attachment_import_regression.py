"""Exercise real packs against the companion API importer (requires its PyYAML dependency).

Run with the API virtualenv's Python and --cli /path/to/cxxprobe-cli
--api-module /path/to/ams_api/services/packages.py. No API files are modified.
"""

import argparse
import importlib.util
import json
import subprocess
import tempfile
import zipfile
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--cli', required=True, type=Path)
parser.add_argument('--api-module', required=True, type=Path)
args = parser.parse_args()
spec = importlib.util.spec_from_file_location('attachment_import', args.api_module)
pkg = importlib.util.module_from_spec(spec)
spec.loader.exec_module(pkg)
cli = str(args.cli.resolve())
with tempfile.TemporaryDirectory(prefix='cxxprobe-8-api63-') as temp:
    root = Path(temp)
    def run(*args, cwd=root):
        result = subprocess.run([cli, *map(str, args)], cwd=cwd, capture_output=True, text=True)
        assert result.returncode == 0, result.stdout + result.stderr
    run('new', 'contest', 'Crossrepo')
    contest = root / 'crossrepo'
    run('package', 'init', 'Controls', cwd=contest)
    problem = contest / 'controls'
    for directory in ('attachments', 'public', 'candidate/start', 'solutions'):
        private_config = 'solutions: {dir: private}\n' if directory == 'solutions' else ''
        (problem / 'problem.yaml').write_text(
            f'version: 2\nname: Controls\n{private_config}attachments: {{dir: {directory}}}\n'
        )
        legacy = problem / 'public' / 'legacy.cpp'
        legacy.unlink(missing_ok=True)
        if directory != 'public':
            legacy.parent.mkdir(exist_ok=True)
            legacy.write_text('// legacy fallback must not be imported\n')
        candidate = problem / directory
        candidate.mkdir(parents=True, exist_ok=True)
        (candidate / 'main.cpp').write_text('// expected starter\n')
        for code in [*range(1, 32), 127]:
            (candidate / f'a{chr(code)}.cpp').write_text('// forbidden\n')
            nested = candidate / f'd{chr(code)}'
            nested.mkdir()
            (nested / 'secret.cpp').write_text('// forbidden\n')
        out = root / 'pack.zip'
        run('package', 'pack', '-o', out, cwd=contest)
        with zipfile.ZipFile(out) as archive:
            assert all(all(ord(c) >= 32 and ord(c) != 127 for c in n) for n in archive.namelist())
            metadata = json.loads(archive.read('manifest.json'))['problems'][0]['attachments']
            assert metadata == {'dir': directory, 'files': ['main.cpp']}, metadata
        imported = pkg.inspect(out.read_bytes())
        assert imported['starter'] == {'filename': 'main.cpp', 'source': '// expected starter\n'}, imported
        (candidate / 'main.cpp').unlink()
        run('package', 'pack', '-o', out, cwd=contest)
        imported = pkg.inspect(out.read_bytes())
        assert imported['starter'] == {}, imported
        assert 'attachments declaration contains no files' in imported['warnings'], imported
        print(f'PASS real CLI -> API63: {directory}, controls excluded, starter imported, explicit-empty authoritative')
