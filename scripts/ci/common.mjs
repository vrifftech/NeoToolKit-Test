import fs from 'node:fs';
import path from 'node:path';
import {spawnSync} from 'node:child_process';
import {createHash} from 'node:crypto';
import {fileURLToPath} from 'node:url';

export const hostRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
export const sha256 = data => createHash('sha256').update(data).digest('hex');
export function writeJson(file, value) {
  fs.mkdirSync(path.dirname(file), {recursive: true});
  fs.writeFileSync(file, JSON.stringify(value, null, 2) + '\n');
}
export function readJson(file) { return JSON.parse(fs.readFileSync(file, 'utf8')); }
export function args(argv, allowed) {
  const result = {};
  for (let i = 0; i < argv.length; i++) {
    const key = argv[i].replace(/^--/, '');
    if (!argv[i].startsWith('--') || !allowed.includes(key) || ++i >= argv.length)
      throw new Error(`Expected --${allowed.join(', --')} followed by a value`);
    if (Object.hasOwn(result, key)) throw new Error(`Repeated option: --${key}`);
    result[key] = argv[i];
  }
  return result;
}
// Each argument is passed directly to the program, never through a shell.
// Keep failed commands' output in CI diagnostics as well as the Actions log.
export function run(command, argv, options = {}) {
  const {log, quiet = false, timeout = 30 * 60 * 1000, ...spawnOptions} = options;
  if (!quiet) console.log(`+ ${command} ${argv.map(x => JSON.stringify(x)).join(' ')}`);
  const result = spawnSync(command, argv, {encoding:'utf8', maxBuffer:128*1024*1024,
    timeout, windowsHide:true, ...spawnOptions});
  const output = (result.stdout || '') + (result.stderr || '');
  if (log) {
    fs.mkdirSync(path.dirname(log), {recursive:true});
    fs.appendFileSync(log, `$ ${command} ${argv.map(x => JSON.stringify(x)).join(' ')}\n${output}\n`);
  }
  if (!quiet || result.status !== 0) process.stdout.write(output);
  if (result.error || result.status !== 0)
    throw new Error(`${command} failed (${result.status ?? result.signal ?? 'spawn'}): ${result.error?.message || output.slice(-6000)}`);
  return result.stdout || '';
}
export function ownedDirectory(parent, name, clear = false) {
  if (!/^[A-Za-z0-9][A-Za-z0-9._ -]*$/.test(name)) throw new Error('Invalid owned directory name');
  const dir = path.join(parent, name);
  if (fs.existsSync(dir) && fs.lstatSync(dir).isSymbolicLink()) throw new Error(`Refusing directory link: ${dir}`);
  if (clear) fs.rmSync(dir, {recursive:true, force:true});
  fs.mkdirSync(dir, {recursive:true});
  return dir;
}
export function walk(dir) {
  const result = [];
  for (const entry of fs.readdirSync(dir, {withFileTypes:true})) {
    const p = path.join(dir, entry.name);
    if (entry.isDirectory()) result.push(...walk(p));
    else result.push(p);
  }
  return result.sort();
}
export function main(fn) { Promise.resolve().then(fn).catch(e => { console.error(`ERROR: ${e.message}`); process.exitCode=1; }); }
export function isMain(url) { return process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(url); }
