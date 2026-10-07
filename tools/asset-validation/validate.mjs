import { readFile } from 'node:fs/promises';
import { validateBytes } from 'gltf-validator';
const path = process.argv[2];
if (!path) throw new Error('Usage: node validate.mjs asset.gltf-or-glb');
const report = await validateBytes(new Uint8Array(await readFile(path)), { uri: path, maxIssues: 100 });
console.log(JSON.stringify(report, null, 2));
process.exitCode = report.issues.numErrors ? 1 : 0;
