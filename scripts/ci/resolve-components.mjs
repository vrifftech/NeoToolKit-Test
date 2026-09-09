import fs from 'node:fs';
import path from 'node:path';
import {args, hostRoot, isMain, main, readJson, run, sha256, writeJson} from './common.mjs';

const fullSha = /^[a-fA-F0-9]{40}$/;
export function validateRef(ref) {
  if (typeof ref !== 'string' || !/^[A-Za-z0-9][A-Za-z0-9._/+-]*$/.test(ref) || ref.includes('..') || ref.endsWith('/') || ref.includes('//'))
    throw new Error(`Invalid Git reference: ${JSON.stringify(ref)}`);
  return ref;
}
export function chooseRef() {
  // Always resolve the published development branch. Old Actions variables,
  // dispatch payloads and cached manual pins cannot select an older module.
  return 'main';
}
export function validateManifest(manifest) {
  if (manifest.schema !== 1 || !Array.isArray(manifest.components) || !manifest.components.length) throw new Error('Invalid component manifest');
  const keys=new Set(), folders=new Set();
  for (const c of manifest.components) {
    if (!/^[A-Z][A-Z0-9_]*$/.test(c.key) || !/^[A-Za-z0-9][A-Za-z0-9_-]*$/.test(c.folder)
        || c.folder === 'NeoToolKit-Test' || keys.has(c.key) || folders.has(c.folder.toLowerCase()))
      throw new Error(`Invalid/duplicate component identity: ${c.key}`);
    keys.add(c.key); folders.add(c.folder.toLowerCase());
    if (!/^https:\/\/github\.com\/[A-Za-z0-9_.-]+\/[A-Za-z0-9_.-]+(?:\.git)?$/.test(c.repository)
        && !c.repository.startsWith('file://')) throw new Error(`Unsupported repository URL: ${c.repository}`);
    validateRef(c.ref);
    if (!Array.isArray(c.required) || !c.required.length) throw new Error(`Missing API checks: ${c.key}`);
    for (const r of c.required) if (!/^[A-Za-z0-9_./-]+$/.test(r.file) || r.file.startsWith('/') || r.file.split('/').includes('..'))
      throw new Error('Invalid API marker path');
  }
  return manifest;
}
export function resolveRef(repository, ref, execute=run) {
  validateRef(ref);
  if (fullSha.test(ref)) return ref.toLowerCase();
  const specs = ref === 'HEAD' ? ['HEAD'] : ref.startsWith('refs/') ? [ref, `${ref}^{}`]
    : [`refs/heads/${ref}`, `refs/tags/${ref}`, `refs/tags/${ref}^{}`];
  const text=execute('git', ['ls-remote', '--exit-code', repository, ...specs], {quiet:true, timeout:120000});
  const entries=new Map(text.trim().split(/\r?\n/).map(line => {const [sha,name]=line.split(/\s+/);return [name,sha];}));
  // A branch wins over a same-named tag; an annotated tag is dereferenced.
  const order=ref==='HEAD' ? ['HEAD'] : ref.startsWith('refs/') ? [`${ref}^{}`,ref]
    : [`refs/heads/${ref}`,`refs/tags/${ref}^{}`,`refs/tags/${ref}`];
  for (const name of order) if(fullSha.test(entries.get(name)||'')) return entries.get(name).toLowerCase();
  throw new Error(`Cannot resolve ${repository} at ${ref}`);
}
export function checkApi(directory, component) {
  for (const requirement of component.required) {
    const file=path.join(directory, requirement.file);
    if (!fs.existsSync(file) || !fs.statSync(file).isFile()
        || (requirement.contains && !fs.readFileSync(file,'utf8').includes(requirement.contains)))
      throw new Error(`${component.folder} lacks the matched workspace API (${requirement.file}${requirement.contains ? ': '+requirement.contains : ''}). Publish the complete current ${component.folder} source to main. This workflow always builds main; no ref variables are used.`);
  }
}
export function checkoutResolved(component, commit, workspace) {
  if (!fullSha.test(commit)) throw new Error('Checkout requires a full commit SHA');
  const dest=path.join(workspace,component.folder);
  // Refuse to replace a developer's existing checkout.
  if (fs.existsSync(dest)) throw new Error(`Dependency destination already exists: ${dest}. Use a fresh CI workspace.`);
  fs.mkdirSync(dest,{recursive:true});
  run('git',['init','--quiet',dest],{quiet:true});
  run('git',['-C',dest,'remote','add','origin',component.repository],{quiet:true});
  run('git',['-C',dest,'-c','core.autocrlf=false','fetch','--no-tags','--depth=1','origin',commit],{quiet:true,timeout:180000});
  run('git',['-C',dest,'-c','core.autocrlf=false','checkout','--quiet','--detach',commit],{quiet:true});
  const actual=run('git',['-C',dest,'rev-parse','HEAD'],{quiet:true}).trim();
  const kind=run('git',['-C',dest,'cat-file','-t','HEAD'],{quiet:true}).trim();
  if (actual !== commit || kind !== 'commit') throw new Error(`Revision verification failed for ${component.folder}`);
  checkApi(dest,component);
  return dest;
}
export function prepare({workspace, manifest, env=process.env}) {
  validateManifest(manifest);
  const host=path.join(workspace,'NeoToolKit-Test');
  const cmake=fs.readFileSync(path.join(host,'CMakeLists.txt'),'utf8');
  const version=/project\(NeoToolKitTest\s+VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)/.exec(cmake)?.[1];
  if(!version)throw new Error('Host version was not found');
  const hostSha=run('git',['-C',host,'rev-parse','HEAD'],{quiet:true}).trim();
  const entries=manifest.components.map(c => {
    const requested=chooseRef();
    const commit=resolveRef(c.repository,requested);
    console.log(`${c.folder}: ${requested} -> ${commit}`);
    return {...c,requested,commit};
  });
  for(const c of entries) checkoutResolved(c,c.commit,workspace);
  const lock={schema:1,version,host:{repository:env.GITHUB_REPOSITORY||'local/NeoToolKit-Test',commit:hostSha},
    components:entries.map(({key,folder,repository,requested,commit})=>({key,folder,repository,requested,commit}))};
  const lockPath=path.join(host,'ci-resolved-components.json');writeJson(lockPath,lock);
  const output=path.join(workspace,'source-bundle');fs.mkdirSync(output,{recursive:true});
  const archive=path.join(output,'workspace-sources.tar.gz');
  run('tar',['--exclude=.git','-czf',archive,'NeoToolKit-Test',...entries.map(e=>e.folder)],{cwd:workspace});
  const digest=sha256(fs.readFileSync(archive));
  fs.writeFileSync(archive+'.sha256',`${digest}  workspace-sources.tar.gz\n`);
  writeJson(path.join(output,'component-lock.json'),lock);
  if(env.GITHUB_OUTPUT)fs.appendFileSync(env.GITHUB_OUTPUT,`source_sha256=${digest}\nversion=${version}\nhost_sha=${hostSha}\n`);
  if(env.GITHUB_STEP_SUMMARY)fs.appendFileSync(env.GITHUB_STEP_SUMMARY,
    `## Resolved source snapshot\n\nAll four native jobs consume the same source archive.\n\n| Component | Requested | Commit |\n|---|---|---|\n`+
    entries.map(e=>`| ${e.folder} | ${e.requested} | \`${e.commit}\` |`).join('\n')+'\n');
  return lock;
}
if(isMain(import.meta.url))main(()=>{
  const options=args(process.argv.slice(2),['workspace','manifest']);
  prepare({workspace:path.resolve(options.workspace||path.dirname(hostRoot)),
    manifest:readJson(options.manifest||path.join(hostRoot,'ci/components.json'))});
});
