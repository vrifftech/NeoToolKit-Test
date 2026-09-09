import fs from 'node:fs';
import path from 'node:path';
import {args, isMain, main, run, walk} from './common.mjs';

export function inspectStage(root, platform=process.platform) {
  const relative=platform==='darwin' ? 'NeoToolKit-Test.app/Contents/MacOS/NeoToolKit-Test'
    : `bin/NeoToolKit-Test${platform==='win32'?'.exe':''}`;
  const exe=path.join(root,relative);
  if(!fs.existsSync(exe) || !fs.statSync(exe).isFile() || fs.statSync(exe).size===0)
    throw new Error(`Installed workspace executable is missing or empty: ${exe}`);
  if(platform!=='win32' && process.platform!=='win32' && !(fs.statSync(exe).mode & 0o111)) throw new Error(`Installed executable lost its execute permission: ${exe}`);
  const executableDir=path.dirname(exe);
  for(const file of walk(root)) {
    if(path.dirname(file)===executableDir && file!==exe && !(platform==='win32' && /\.dll$/i.test(file)))
      throw new Error(`Unexpected executable in workspace package: ${file}`);
    if(/(?:^|\/)(?:CMakeCache\.txt|\.git|CMakeFiles)(?:\/|$)/.test(path.relative(root,file).replaceAll('\\','/')))
      throw new Error(`Build or source state leaked into package: ${file}`);
  }
  if(platform==='darwin') {
    const plist=path.join(root,'NeoToolKit-Test.app/Contents/Info.plist');
    if(!fs.existsSync(plist))throw new Error('Bundle Info.plist is missing');
    const extras=fs.readdirSync(root).filter(x=>x.endsWith('.app') && x!=='NeoToolKit-Test.app');
    if(extras.length)throw new Error(`Standalone app bundles leaked into package: ${extras.join(', ')}`);
  }
  return exe;
}
export function windowsDependencies(exe, log) {
  const vswhere=path.join(process.env['ProgramFiles(x86)']||'C:/Program Files (x86)', 'Microsoft Visual Studio/Installer/vswhere.exe');
  const matches=run(vswhere,['-latest','-products','*','-requires','Microsoft.VisualStudio.Component.VC.Tools.x86.x64',
    '-find','VC/Tools/MSVC/*/bin/Hostx64/x64/dumpbin.exe'],{quiet:true}).trim().split(/\r?\n/).filter(Boolean).sort();
  if(!matches.length)throw new Error('Cannot locate dumpbin for the installed-package dependency check');
  const text=run(matches.at(-1),['/NOLOGO','/DEPENDENTS',exe],{log});
  const names=[...new Set(text.split(/\r?\n/).map(x=>x.trim()).filter(x=>/^[A-Za-z0-9_.+-]+\.dll$/i.test(x)))];
  if(!names.length)throw new Error('dumpbin returned no import inventory');
  for(const name of names) {
    if(/^(?:msvcp|vcruntime|concrt)\d/i.test(name))throw new Error(`Unexpected redistributable DLL with the static CRT build: ${name}`);
    if(/^(?:api-ms-|ext-ms-)/i.test(name))continue;
    if(!fs.existsSync(path.join(process.env.SystemRoot||'C:/Windows','System32',name)))
      throw new Error(`Unbundled non-system dependency: ${name}`);
  }
  return names;
}
export function verifyInstall(root, {logDirectory, platform=process.platform}={}) {
  const exe=inspectStage(root,platform);
  fs.mkdirSync(logDirectory,{recursive:true});
  if(platform==='linux') {
    const deps=run('ldd',[exe],{log:path.join(logDirectory,'installed-dependencies.log')});
    if(/not found/.test(deps))throw new Error('Installed Linux executable has unresolved dependencies');
  } else if(platform==='win32')windowsDependencies(exe,path.join(logDirectory,'installed-dependencies.log'));
  else if(platform==='darwin') {
    run('codesign',['--verify','--deep','--strict','--verbose=2',path.join(root,'NeoToolKit-Test.app')],{log:path.join(logDirectory,'installed-signatures.log')});
  }
}
if(isMain(import.meta.url))main(()=>{
  const a=args(process.argv.slice(2),['stage','logs']);
  if(!a.stage||!a.logs)throw new Error('--stage and --logs are required');
  verifyInstall(path.resolve(a.stage),{logDirectory:path.resolve(a.logs)});
});
