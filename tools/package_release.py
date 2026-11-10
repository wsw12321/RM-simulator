#!/usr/bin/env python3
"""Build a download-and-run release archive with prebuilt native binaries.

The archive is the committed source tree at HEAD plus the runtime binaries in
build/. Sources stay in the package on purpose: the Python bridges re-hash
their sources at runtime and refuse a binary whose compiled build ID differs,
so binaries alone would not start.

  1. `git archive HEAD` into a fresh staging directory (uncommitted changes are
     never packaged);
  2. tools/native_build.py there; every native test binary must pass;
  3. drop the test binaries and the object cache, add start.sh, QUICKSTART.md,
     BUILD_INFO.json and SHA256SUMS (covering the binaries too);
  4. write a reproducible tar.gz (fixed order, owners and commit mtime) plus a
     .sha256 file;
  5. extract that archive again and smoke-test it: launcher, server, every
     mode's native engine and the command-line tools.

  python3 tools/package_release.py                  # dist/rmuc-tactics-lab-<version>-<platform>.tar.gz
  python3 tools/package_release.py --version v0.2.0
  CXX="clang++ -arch arm64 -arch x86_64" python3 tools/package_release.py --platform macos-universal

On Linux the binaries are linked fully static (LDFLAGS="-static -s" unless
LDFLAGS is set), so the package does not depend on the glibc or libstdc++
version of the machine that runs it. The official rules manual PDF (22 MB,
never read at runtime) is left out unless --include-manual is given.
"""
import argparse
import gzip
import hashlib
import io
import json
import os
import platform
import shlex
import shutil
import signal
import socket
import subprocess
import sys
import tarfile
import tempfile
import time
import urllib.error
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
NAME = 'rmuc-tactics-lab'
EXCLUDE = ['.github', 'MANIFEST.sha256']  # the manifest is replaced by SHA256SUMS, which also covers binaries
MANUAL = 'docs/rules/*.pdf'
EXTRAS = {'tools/release/start.sh': ('start.sh', 0o755), 'tools/release/QUICKSTART.md': ('QUICKSTART.md', 0o644)}
TAR_FILTER = {'filter': 'data'} if hasattr(tarfile, 'data_filter') else {}


def log(message):
    print(f'[package] {message}', flush=True)


def git(*args):
    return subprocess.run(['git', *args], cwd=ROOT, check=True, capture_output=True, text=True).stdout.strip()


def default_platform():
    system = {'Darwin': 'macos'}.get(platform.system(), platform.system().lower())
    machine = {'amd64': 'x86_64', 'aarch64': 'arm64'}.get(platform.machine().lower(), platform.machine().lower())
    return f'{system}-{machine}'


def stage_sources(stage, include_manual):
    archive = subprocess.run(['git', 'archive', '--format=tar', 'HEAD'], cwd=ROOT, check=True, capture_output=True).stdout
    with tarfile.open(fileobj=io.BytesIO(archive)) as tar:
        tar.extractall(stage, **TAR_FILTER)
    for rel in EXCLUDE:
        path = stage / rel
        if path.is_dir():
            shutil.rmtree(path)
        elif path.exists():
            path.unlink()
    if not include_manual:
        for pdf in stage.glob(MANUAL):
            pdf.unlink()


def build(stage, env):
    log(f'native build in staging (CXX={env.get("CXX", "g++")!r}, LDFLAGS={env.get("LDFLAGS", "")!r})')
    env = {**env, 'PYTHONDONTWRITEBYTECODE': '1'}
    result = subprocess.run([sys.executable, 'tools/native_build.py'], cwd=stage, env=env)
    if result.returncode:
        raise SystemExit('native build or native tests failed; nothing was packaged')
    targets = subprocess.run([sys.executable, '-c', 'import json, sys; sys.path.insert(0, "tools"); import native_build; '
                              'print(json.dumps([out for out, test, _, _ in native_build.plan() if not test]))'],
                             cwd=stage, env=env, check=True, capture_output=True, text=True).stdout
    runtime = sorted(json.loads(targets))
    for path in (stage / 'build').iterdir():
        if path.name not in runtime:
            shutil.rmtree(path) if path.is_dir() else path.unlink()
    for cache in list(stage.rglob('__pycache__')):
        shutil.rmtree(cache)
    missing = [name for name in runtime if not (stage / 'build' / name).is_file()]
    if missing:
        raise SystemExit(f'missing runtime binaries: {missing}')
    return runtime


def compiler_version(env):
    try:
        out = subprocess.run([*shlex.split(env.get('CXX', 'g++')), '--version'], capture_output=True, text=True).stdout
        return out.splitlines()[0] if out else 'unknown'
    except OSError:
        return 'unknown'


def add_extras(stage, info):
    for src, (dest, mode) in EXTRAS.items():
        shutil.copyfile(stage / src, stage / dest)  # from the committed tree, like everything else
        (stage / dest).chmod(mode)
    (stage / 'BUILD_INFO.json').write_text(json.dumps(info, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')


def files_under(base):
    return sorted(p for p in base.rglob('*') if p.is_file() and not p.is_symlink())


def sha256(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for chunk in iter(lambda: f.read(1 << 20), b''):
            h.update(chunk)
    return h.hexdigest()


def write_sums(stage):
    lines = [f'{sha256(p)}  {p.relative_to(stage).as_posix()}' for p in files_under(stage) if p.name != 'SHA256SUMS']
    (stage / 'SHA256SUMS').write_text('\n'.join(lines) + '\n', encoding='utf-8')


def write_tarball(stage, target, mtime):
    """Fixed order, numeric root owner and commit mtime: same inputs, same bytes."""
    entries = sorted([stage, *stage.rglob('*')], key=lambda p: p.relative_to(stage).as_posix())
    with open(target, 'wb') as raw, gzip.GzipFile(filename='', mode='wb', fileobj=raw, mtime=mtime, compresslevel=9) as gz, \
            tarfile.open(fileobj=gz, mode='w', format=tarfile.PAX_FORMAT) as tar:
        for path in entries:
            info = tar.gettarinfo(str(path), arcname=(Path(stage.name) / path.relative_to(stage)).as_posix())
            info.uid = info.gid = 0
            info.uname = info.gname = ''
            info.mtime = mtime
            if info.isdir() or info.mode & 0o100:
                info.mode = 0o755
            else:
                info.mode = 0o644
            if info.isfile():
                with open(path, 'rb') as f:
                    tar.addfile(info, f)
            else:
                tar.addfile(info)


def free_port():
    with socket.socket() as s:
        s.bind(('127.0.0.1', 0))
        return s.getsockname()[1]


def smoke_test(tarball, top):
    """Run the extracted archive the way a user would."""
    opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))  # loopback must not go through a proxy

    def call(port, path, body=None):
        headers = {'X-Sandbox-Client': '1', 'Content-Type': 'application/json'} if body is not None else {}
        request = urllib.request.Request(f'http://127.0.0.1:{port}{path}', headers=headers,
                                         data=None if body is None else json.dumps(body).encode())
        try:
            with opener.open(request, timeout=60) as response:
                return response.read()
        except urllib.error.HTTPError as exc:  # an answer, not a missing server: report the server's message
            raise SystemExit(f'smoke: {path} returned HTTP {exc.code}: {exc.read()[:500]!r}') from exc

    with tempfile.TemporaryDirectory(prefix='rmuc-smoke-') as tmp:
        with tarfile.open(tarball) as tar:
            tar.extractall(tmp, **TAR_FILTER)
        pkg = Path(tmp) / top
        bad = [line for line in (pkg / 'SHA256SUMS').read_text(encoding='utf-8').splitlines()
               if sha256(pkg / line.split('  ', 1)[1]) != line.split('  ', 1)[0]]
        if bad:
            raise SystemExit(f'smoke: SHA256SUMS mismatch: {bad[:3]}')

        port = free_port()
        env = {**os.environ, 'PYTHON': sys.executable}
        server = subprocess.Popen(['bash', 'start.sh', '--no-browser', '--port', str(port)], cwd=pkg, env=env,
                                  stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        try:
            deadline = time.monotonic() + 20
            while True:
                if server.poll() is not None:
                    raise SystemExit(f'smoke: server exited early:\n{server.stdout.read()}')
                try:
                    meta = json.loads(call(port, '/api/meta'))
                    break
                except OSError:
                    if time.monotonic() > deadline:
                        raise SystemExit('smoke: server did not answer within 20 s')
                    time.sleep(0.2)
            for page in ['/', '/lab.html', '/roster.html', '/tactical.html', '/combat.html', '/layered.html', '/arena.html']:
                call(port, page)
            # Each first call starts that mode's native engine and checks binary build ID against the sources.
            for path in ['/api/state', '/api/objective/state', '/api/layered/state', '/api/combat/state',
                         '/api/tactical/state', '/api/roster/state', '/api/roster/profiles/catalog']:
                data = json.loads(call(port, path))
                if 'error' in data:
                    raise SystemExit(f'smoke: {path}: {data["error"]}')
            example = json.loads(call(port, '/api/lab/example'))
            lab = json.loads(call(port, '/api/lab/run', {'config': example, 'frame_every': 0}))
            if 'error' in lab:
                raise SystemExit(f'smoke: /api/lab/run: {lab["error"]}')
        except (OSError, ValueError) as exc:
            raise SystemExit(f'smoke: {exc}') from exc
        finally:
            server.send_signal(signal.SIGINT)
            try:
                server.wait(10)
            except subprocess.TimeoutExpired:
                server.kill()
        for cmd in (['match_lab/cli.py', 'run'],
                    ['scenario_harness/cli.py', 'run', 'scenario_harness/scenarios/timeout_control_priority.json'],
                    ['integrated_scenario/cli.py', 'run', 'integrated_scenario/scenarios/capture_credit_timeout.json']):
            result = subprocess.run([sys.executable, *cmd], cwd=pkg, capture_output=True, text=True)
            if result.returncode:
                raise SystemExit(f'smoke: {" ".join(cmd)} failed:\n{result.stdout}{result.stderr}')
    log(f'smoke test passed: server {meta.get("build", "")[:12] or "ok"}, 7 pages, 7 native engines, rules lab run, 3 CLIs')


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    parser.add_argument('--version', help='version label (default: git describe --tags --always)')
    parser.add_argument('--platform', default=default_platform(), help='platform label (default: %(default)s)')
    parser.add_argument('--out', type=Path, default=ROOT / 'dist', help='output directory (default: dist/)')
    parser.add_argument('--include-manual', action='store_true', help='also ship the 22 MB rules manual PDF')
    args = parser.parse_args()

    if git('status', '--porcelain', '--untracked-files=no'):
        log('WARNING: uncommitted changes are NOT packaged; the package is built from HEAD only')
    commit = git('rev-parse', 'HEAD')
    commit_time = int(git('log', '-1', '--format=%ct', 'HEAD'))
    version = args.version or git('describe', '--tags', '--always', 'HEAD')
    top = f'{NAME}-{version}-{args.platform}'
    env = os.environ.copy()
    if platform.system() == 'Linux':
        env.setdefault('LDFLAGS', '-static -s')

    with tempfile.TemporaryDirectory(prefix='rmuc-release-') as tmp:
        stage = Path(tmp) / top
        stage.mkdir()
        stage_sources(stage, args.include_manual)
        runtime = build(stage, env)
        add_extras(stage, {
            'name': NAME, 'version': version, 'platform': args.platform, 'commit': commit,
            'commit_time': time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime(commit_time)),
            'compiler': compiler_version(env), 'cxx': env.get('CXX', 'g++'), 'ldflags': env.get('LDFLAGS', ''),
            'macosx_deployment_target': env.get('MACOSX_DEPLOYMENT_TARGET') if platform.system() == 'Darwin' else None,
            'requires': 'Python 3.10+ (standard library only)', 'rules_manual_included': args.include_manual,
            'binaries': runtime,
        })
        write_sums(stage)
        args.out.mkdir(parents=True, exist_ok=True)
        tarball = args.out / f'{top}.tar.gz'
        write_tarball(stage, tarball, commit_time)
    digest = sha256(tarball)
    (args.out / f'{tarball.name}.sha256').write_text(f'{digest}  {tarball.name}\n', encoding='utf-8')
    log(f'wrote {tarball} ({tarball.stat().st_size / 1e6:.1f} MB, sha256 {digest[:16]}...)')
    smoke_test(tarball, top)
    return 0


if __name__ == '__main__':
    sys.exit(main())
