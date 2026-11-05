import assert from 'node:assert/strict';
import { appendFileSync, chmodSync, copyFileSync, existsSync, mkdirSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { resolve, join } from 'node:path';
import { spawnSync } from 'node:child_process';
import { test } from 'node:test';

const root = resolve(import.meta.dirname, '../..');
const outputScript = join(root, '.github/scripts/release-output.mjs');
const config = JSON.parse(readFileSync(join(root, '.releaserc.json'), 'utf8'));
const releaseBin = process.env.RELEASE_TEST_BIN;

function command(cmd, args, cwd, env = {}, success = true) {
  const clean = { ...process.env };
  for (const key of Object.keys(clean)) {
    if (/TOKEN|SECRET|PASSWORD/.test(key) || key.startsWith('GITHUB_') || key.startsWith('NODE_TEST_') || key === 'CI') delete clean[key];
  }
  const result = spawnSync(cmd, args, {
    cwd, encoding: 'utf8', timeout: 90000,
    env: { ...clean, GIT_CONFIG_GLOBAL: '/dev/null', GIT_CONFIG_NOSYSTEM: '1',
      GIT_AUTHOR_NAME: 'Release Test', GIT_AUTHOR_EMAIL: 'release@example.invalid',
      GIT_COMMITTER_NAME: 'Release Test', GIT_COMMITTER_EMAIL: 'release@example.invalid', ...env },
  });
  const diagnostics = `${cmd}: ${result.error ?? ''}\n${result.stdout}\n${result.stderr}`;
  assert.ifError(result.error);
  assert.equal(result.signal, null, diagnostics);
  if (success) assert.equal(result.status, 0, diagnostics);
  return result;
}

test('tag handoff rejects invalid and injected values without writing outputs', () => {
  const dir = mkdtempSync(join(tmpdir(), 'release-output-'));
  try {
    const output = join(dir, 'output');
    for (const tag of ['', 'main', 'v01.2.3', 'v1.2.3\ntag=main', 'v1.2.3;touch BAD', 'v1.2.3-rc.1']) {
      const result = command(process.execPath, [outputScript, tag], dir, { GITHUB_OUTPUT: output }, false);
      assert.notEqual(result.status, 0, tag);
      assert.equal(existsSync(output), false);
    }
    command(process.execPath, [outputScript, 'v2.3.4'], dir, { GITHUB_OUTPUT: output });
    assert.equal(readFileSync(output, 'utf8'), 'tag=v2.3.4\n');
    assert.notEqual(command(process.execPath, [outputScript, 'v2.3.4'], dir, {}, false).status, 0);
  } finally { rmSync(dir, { recursive: true, force: true }); }
});

function fixture(t, publishFailure = false) {
  assert.ok(releaseBin && existsSync(releaseBin), 'Set RELEASE_TEST_BIN to the installed semantic-release CLI');
  const dir = mkdtempSync(join(tmpdir(), 'release-local-'));
  t.after(() => rmSync(dir, { recursive: true, force: true }));
  const repo = join(dir, 'repo');
  const remote = join(dir, 'remote.git');
  mkdirSync(repo);
  const git = (...args) => command('git', args, repo).stdout.trim();
  command('git', ['init', '--bare', '--initial-branch=main', remote], dir);
  git('init', '--initial-branch=main');
  git('remote', 'add', 'origin', `file://${remote}`);
  writeFileSync(join(repo, 'source'), 'initial\n');
  git('add', '.'); git('commit', '-m', 'feat: initial release');
  git('tag', 'v1.0.0');
  appendFileSync(join(repo, 'source'), 'fix\n');
  git('commit', '-am', 'fix: verify a tag-only release');
  git('push', 'origin', 'main', '--tags');
  // A real server-side hook rejects ANY main update. Tags and notes are allowed.
  const hook = join(remote, 'hooks/pre-receive');
  writeFileSync(hook, '#!/bin/sh\nwhile read old new ref; do\n [ "$ref" != refs/heads/main ] || exit 1\ndone\n');
  chmodSync(hook, 0o755);
  mkdirSync(join(repo, '.github/scripts'), { recursive: true });
  copyFileSync(outputScript, join(repo, '.github/scripts/release-output.mjs'));
  const stub = join(repo, 'publish.cjs');
  writeFileSync(stub,
    'exports.publish = (_, ctx) => {\n' +
    '  const fs = require("node:fs");\n' +
    '  fs.writeFileSync("publish-attempt.json", JSON.stringify(ctx.nextRelease));\n' +
    (publishFailure
      ? '  throw new Error("deliberate publish failure");\n'
      : '  fs.writeFileSync("published", ctx.nextRelease.gitTag); return {name:"Local test"};\n') +
    '};\n');
  // Preserve the real analyzer, notes, tag/push, and handoff plugins. Only the
  // external GitHub API is replaced; every remote URL stays in this tempdir.
  const local = { ...config, repositoryUrl: `file://${remote}`, plugins: config.plugins.map(p => p === '@semantic-release/github' ? stub : p) };
  writeFileSync(join(repo, '.releaserc.json'), JSON.stringify(local));
  const head = git('rev-parse', 'HEAD');
  const output = join(dir, 'github-output');
  const run = (args = [], success = true) => command(releaseBin, ['--no-ci', ...args], repo, { GITHUB_OUTPUT: output }, success);
  const remoteHead = () => command('git', ['--git-dir', remote, 'rev-parse', 'refs/heads/main'], dir).stdout.trim();
  const remoteTag = () => command('git', ['--git-dir', remote, 'rev-parse', 'refs/tags/v1.0.1'], dir).stdout.trim();
  // Prove the hook really enforces branch protection, without changing the
  // checked-out branch or adding the rejected commit to the release history.
  const rejected = git('commit-tree', 'HEAD^{tree}', '-p', head, '-m', 'fix: rejected branch update');
  const push = command('git', ['push', 'origin', `${rejected}:refs/heads/main`], repo, {}, false);
  assert.notEqual(push.status, 0);
  assert.match(push.stderr, /pre-receive hook declined/);
  assert.equal(remoteHead(), head);
  return { repo, remote, dir, git, head, output, run, remoteHead, remoteTag };
}

test('publishes a tag without updating protected main, then no-ops without an asset handoff', t => {
  const f = fixture(t);
  const first = f.run();
  assert.ok(existsSync(join(f.repo, 'published')), first.stdout + first.stderr);
  assert.equal(f.remoteHead(), f.head);
  assert.equal(f.git('rev-parse', 'HEAD'), f.head);
  assert.equal(readFileSync(join(f.repo, 'published'), 'utf8'), 'v1.0.1');
  const release = JSON.parse(readFileSync(join(f.repo, 'publish-attempt.json'), 'utf8'));
  assert.equal(release.version, '1.0.1');
  assert.equal(release.type, 'patch');
  assert.match(release.notes, /verify a tag-only release/);
  assert.equal(readFileSync(f.output, 'utf8'), 'tag=v1.0.1\n');
  assert.equal(f.remoteTag(), f.head);
  rmSync(f.output);
  rmSync(join(f.repo, 'published'));
  rmSync(join(f.repo, 'publish-attempt.json'));
  const second = f.run();
  assert.match(second.stdout + second.stderr, /no new version is released/);
  assert.equal(existsSync(f.output), false);
  assert.equal(existsSync(join(f.repo, 'published')), false);
  assert.equal(existsSync(join(f.repo, 'publish-attempt.json')), false);
  assert.equal(f.remoteHead(), f.head);
  assert.equal(f.remoteTag(), f.head);
});

test('authenticated local dry run creates no tag, publication or asset handoff', t => {
  const f = fixture(t);
  const run = f.run(['--dry-run']);
  assert.match(run.stdout + run.stderr, /The next release version is 1\.0\.1/);
  assert.match(run.stdout + run.stderr, /verify a tag-only release/);
  assert.equal(f.remoteHead(), f.head);
  assert.equal(f.git('rev-parse', 'HEAD'), f.head);
  assert.equal(existsSync(f.output), false);
  assert.equal(existsSync(join(f.repo, 'published')), false);
  assert.equal(existsSync(join(f.repo, 'publish-attempt.json')), false);
  assert.notEqual(command('git', ['rev-parse', '--verify', 'refs/tags/v1.0.1'], f.repo, {}, false).status, 0);
  assert.notEqual(command('git', ['--git-dir', f.remote, 'rev-parse', '--verify', 'refs/tags/v1.0.1'], f.dir, {}, false).status, 0);
});

test('publication failure does not hand off assets even if semantic-release already pushed its tag', t => {
  const f = fixture(t, true);
  const failed = f.run([], false);
  assert.notEqual(failed.status, 0);
  assert.match(failed.stdout + failed.stderr, /deliberate publish failure/);
  const attempt = JSON.parse(readFileSync(join(f.repo, 'publish-attempt.json'), 'utf8'));
  assert.equal(attempt.gitTag, 'v1.0.1');
  assert.equal(attempt.type, 'patch');
  assert.match(attempt.notes, /verify a tag-only release/);
  assert.equal(f.remoteHead(), f.head);
  assert.equal(f.git('rev-parse', 'HEAD'), f.head);
  assert.equal(f.remoteTag(), f.head);
  assert.equal(existsSync(join(f.repo, 'published')), false);
  assert.equal(existsSync(f.output), false);
});
