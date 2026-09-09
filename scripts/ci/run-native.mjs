import fs from 'node:fs';
import path from 'node:path';
import os from 'node:os';
import {args, hostRoot, isMain, main, ownedDirectory, readJson, run, sha256, writeJson} from './common.mjs';
import {inspectStage, verifyInstall} from './verify-install.mjs';

export function platformInfo(platform=process.platform, arch=process.arch) {
  if(platform==='win32' && arch==='x64')return {slug:'windows-x64',generator:'Visual Studio 18 2026'};
  if(platform==='linux' && arch==='x64')return {slug:'linux-x64',generator:'Ninja'};
  if(platform==='darwin' && (arch==='arm64'||arch==='x64'))return {slug:`macos-${arch==='arm64'?'arm64':'x86_64'}`,generator:'Ninja',arch:arch==='arm64'?'arm64':'x86_64'};
  throw new Error(`Unconfigured release target: ${platform}/${arch}`);
}
export function configurationArgs(source,build,{platform=process.platform,arch=process.arch,env=process.env,brewPrefix}={}) {
  const info=platformInfo(platform,arch);
  const result=['-S',source,'-B',build,'-G',info.generator,'-DCMAKE_BUILD_TYPE=Release',
    '-DNEOTOOLKIT_BUILD_GUI=ON','-DNEOTOOLKIT_BUILD_STANDALONES=OFF','-DNEOTOOLKIT_USE_SIBLINGS=ON',
    '-DBUILD_SHARED_LIBS=OFF'];
  if(platform==='win32') {
    if(!env.VCPKG_INSTALLATION_ROOT)throw new Error('VCPKG_INSTALLATION_ROOT is required on Windows');
    result.push('-A','x64',`-DCMAKE_TOOLCHAIN_FILE=${path.join(env.VCPKG_INSTALLATION_ROOT,'scripts/buildsystems/vcpkg.cmake')}`,
      '-DVCPKG_TARGET_TRIPLET=x64-windows-static','-DVCPKG_MANIFEST_MODE=OFF',
      '-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded$<$<CONFIG:Debug>:Debug>');
  } else if(platform==='darwin') {
    if(!brewPrefix)throw new Error('Homebrew wxWidgets prefix is required');
    result.push(`-DCMAKE_OSX_ARCHITECTURES=${info.arch}`,'-DCMAKE_OSX_DEPLOYMENT_TARGET=15.0',
      `-DwxWidgets_CONFIG_EXECUTABLE=${brewPrefix}/bin/wx-config`);
  }
  return result;
}
function logRun(logs,name,cmd,argv,extra={}) {return run(cmd,argv,{log:path.join(logs,name+'.log'),...extra});}
export function packageFiles(dir) {
  return fs.readdirSync(dir).filter(x=>/\.(?:zip|tar\.gz|deb)$/.test(x)).sort();
}
function addSdkRecord(build,logs) {
  const record={os:os.platform(),architecture:os.arch(),release:os.release(),node:process.version,
    runnerImage:process.env.ImageOS||null,runnerImageVersion:process.env.ImageVersion||null,
    cmake:run('cmake',['--version'],{quiet:true}).trim(),
    vcpkgCommit:null};
  if(process.env.VCPKG_INSTALLATION_ROOT) {
    try{record.vcpkgCommit=run('git',['-C',process.env.VCPKG_INSTALLATION_ROOT,'rev-parse','HEAD'],{quiet:true}).trim();}catch{}
  }
  writeJson(path.join(logs,'toolchain.json'),record);
  // CMakeCache gives the exact compiler, SDK paths, runtime flags and packages.
  if(fs.existsSync(path.join(build,'CMakeCache.txt')))
    fs.copyFileSync(path.join(build,'CMakeCache.txt'),path.join(logs,'CMakeCache.txt'));
}
export function packageProduct(source,build,logs) {
  const info=readJson(path.join(build,'ci-build-Release.json'));
  if(!fs.existsSync(path.join(logs,'toolchain.json')))addSdkRecord(build,logs);
  const platform=platformInfo();
  if(!fs.existsSync(info.executable))throw new Error(`Build did not produce the workspace: ${info.executable}`);
  const stage=ownedDirectory(build,'stage-workspace',true);
  const dist=ownedDirectory(build,'dist',true);
  logRun(logs,'install','cmake',['--install',build,'--config','Release','--prefix',stage,'--component','toolkit-runtime']);
  inspectStage(stage);
  if(process.platform==='darwin') {
    // The shared packager embeds non-system dylibs, checks ONLY Mach-O code,
    // fixes install names, verifies bundle links, and ad-hoc signs inside-out.
    const zip=path.join(dist,`NeoToolKit-Test-${info.version}-${platform.slug}.zip`);
    logRun(logs,'macos-bundle','bash',[path.join(info.sharedRoot,'scripts/package-macos-app.sh'),
      '--app',path.join(stage,'NeoToolKit-Test.app'),'--output',zip,'--arch',platform.arch,
      '--app-name','NeoToolKit Test','--version',info.version,'--deployment-target','15.0']);
  } else {
    for(const generator of process.platform==='win32'?['ZIP']:['TGZ','DEB'])
      logRun(logs,`cpack-${generator}`,'cpack',['--config',path.join(build,'CPackConfig.cmake'),
        '-C','Release','-G',generator,'-B',dist],{cwd:build});
  }
  // Check the distributable layout and linked runtime dependencies.
  const files=packageFiles(dist);
  const expected=process.platform==='linux'?2:1;
  if(files.length!==expected)throw new Error(`Expected ${expected} distribution files; found ${files.length}`);
  const results=[];
  for(let i=0;i<files.length;++i) {
    const file=files[i], full=path.join(dist,file);
    const unpack=ownedDirectory(build,`installed package ${i}`,true);
    let installed;
    if(file.endsWith('.deb')) {
      const meta=logRun(logs,'deb-metadata','dpkg-deb',['--field',full]);
      if(!/^Depends:\s*\S/m.test(meta))throw new Error('DEB has no computed runtime dependency list');
      logRun(logs,'deb-extract','dpkg-deb',['--extract',full,unpack]);
      installed=path.join(unpack,'usr');
    } else {
      if(process.platform==='darwin')logRun(logs,'archive-extract','ditto',['-x','-k',full,unpack]);
      else logRun(logs,'archive-extract','cmake',['-E','tar','xf',full],{cwd:unpack});
      installed=process.platform==='darwin'?unpack:path.join(unpack,`NeoToolKit-Test-${info.version}-${platform.slug}`);
    }
    verifyInstall(installed,{logDirectory:path.join(logs,`installed-${i}`)});
    const digest=sha256(fs.readFileSync(full));
    fs.writeFileSync(full+'.sha256',`${digest}  ${file}\n`);
    results.push({file,sha256:digest,size:fs.statSync(full).size,packageChecks:'passed'});
  }
  const lock=path.join(source,'ci-resolved-components.json');
  if(fs.existsSync(lock))fs.copyFileSync(lock,path.join(dist,'component-lock.json'));
  fs.copyFileSync(path.join(logs,'toolchain.json'),path.join(dist,'toolchain.json'));
  writeJson(path.join(dist,'packages.json'),{version:info.version,platform:platform.slug,packages:results,
    signing:process.platform==='darwin'?'ad-hoc; not notarized':process.platform==='win32'?'unsigned':'not applicable'});
  if(process.env.GITHUB_STEP_SUMMARY)fs.appendFileSync(process.env.GITHUB_STEP_SUMMARY,
    `## ${platform.slug} packages\n\nBuild and package checks passed.\n\n`+
    results.map(r=>`- ${r.file} (${r.size} bytes), SHA-256 \`${r.sha256}\``).join('\n')+'\n');
  return results;
}
if(isMain(import.meta.url))main(()=>{
  const a=args(process.argv.slice(2),['phase','build-dir']);
  const expected=process.env.NEOTOOLKIT_EXPECTED_PLATFORM;
  if(expected && expected!==platformInfo().slug)
    throw new Error(`Runner architecture mismatch: expected ${expected}, Node reports ${platformInfo().slug}`);
  const build=path.resolve(a['build-dir']||path.join(hostRoot,'build/ci'));
  const logs=ownedDirectory(build,'ci-logs');
  const phase=a.phase;
  if(phase==='configure') {
    let prefix;
    if(process.platform==='darwin')prefix=run('brew',['--prefix','wxwidgets'],{quiet:true}).trim();
    logRun(logs,'configure','cmake',configurationArgs(hostRoot,build,{brewPrefix:prefix}));
    addSdkRecord(build,logs);
  } else if(phase==='build') {
    logRun(logs,'build','cmake',['--build',build,'--config','Release','--parallel',process.env.NEOTOOLKIT_BUILD_JOBS||'2']);
    const info=readJson(path.join(build,'ci-build-Release.json'));
    if(!fs.existsSync(info.executable))throw new Error(`Workspace executable not produced: ${info.executable}`);
  } else if(phase==='package')packageProduct(hostRoot,build,logs);
  else throw new Error('Use --phase configure|build|package');
});
