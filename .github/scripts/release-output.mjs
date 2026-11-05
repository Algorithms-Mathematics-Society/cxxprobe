import { appendFileSync } from 'node:fs';

// Only a newly published stable release reaches this hook. No shell evaluation
// or github.event text is used to construct the output file's contents.
const tag = process.argv[2];
if (process.argv.length !== 3 || !/^v(?:0|[1-9]\d*)\.(?:0|[1-9]\d*)\.(?:0|[1-9]\d*)$/.test(tag ?? '')) {
  throw new Error('Expected a stable release tag: vMAJOR.MINOR.PATCH');
}
if (!process.env.GITHUB_OUTPUT) {
  throw new Error('GITHUB_OUTPUT is required to hand off the published release');
}
appendFileSync(process.env.GITHUB_OUTPUT, `tag=${tag}\n`);
