"""Create an isolated patched XMake recipe using the single pinned vcpkg graph.

Never mutate the pinned source. This prepares the later SDK build; M0 does not
compile audio. No XMake remote registry is needed by the generated recipe.
"""
import hashlib, json, pathlib, shutil
from acquire import ROOT

def main():
    source=ROOT/'third_party/amplitude'
    pins=json.loads((ROOT/'cmake/sources.lock.json').read_text(encoding='utf-8'))
    expected={'source':next(p['commit'] for p in pins['sources'] if p['name']=='amplitude'),'recipe_sha256':hashlib.sha256(pathlib.Path(__file__).read_bytes()).hexdigest()}
    dest=ROOT/'.cache'/('amplitude-build-source-'+expected['recipe_sha256'][:12])
    marker=dest/'.darkangel-recipe.json'
    if dest.exists():
        if not marker.exists() or json.loads(marker.read_text(encoding='utf-8'))!=expected:raise RuntimeError('Existing Amplitude recipe differs; preserved. Use a new build-source destination for a changed recipe.')
        print('Verified existing Amplitude build-source recipe');return 0
    shutil.copytree(source,dest,ignore=shutil.ignore_patterns('.git','.xmake','build'))
    recipe=(source/'xmake.lua').read_text(encoding='utf-8')
    recipe=recipe.replace('add_repositories("repo xmake/repo", { rootdir = os.scriptdir() })','')
    # Local packages import installed headers/libraries; they never build a second copy.
    packages={'flatbuffers':('flatbuffers','flatbuffers'),'xsimd':('xsimd',None),'miniaudio':('miniaudio',None),'eigen':('eigen3',None),'lz4':('lz4','lz4'),'dylib':('dylib',None)}
    prefix=(ROOT/'vcpkg_installed/x64-windows-darkangel').as_posix()
    definitions=['set_runtimes(is_mode("debug") and "MDd" or "MD")']
    for alias,(port,link) in packages.items():
        lines=[f'package("darkangel-{alias}")', '  set_kind("library", {headeronly = '+('false' if link else 'true')+'})', '  on_load(function(package)',f'    package:add("includedirs", "{prefix}/include")']
        if alias=='eigen':lines.append(f'    package:add("includedirs", "{prefix}/include/eigen3")')
        if link:
            lines += [f'    package:add("linkdirs", is_mode("debug") and "{prefix}/debug/lib" or "{prefix}/lib")',f'    package:add("links", "{link}")']
        lines+=['  end)','package_end()']
        definitions.extend(lines)
        import re
        pattern=r'add_requires\("'+alias+r' [^"]+"\)'
        recipe,count=re.subn(pattern,f'add_requires("darkangel-{alias}", {{alias = "{alias}", system = false}})',recipe)
        if count!=1:raise RuntimeError('Upstream dependency declaration changed: '+alias)
    recipe=recipe.replace('-- Dependencies','\n'.join(definitions)+'\n\n-- Dependencies',1)
    # Optional CLI/sample/test branches remain off. Prevent accidental acquisition through them.
    recipe=recipe.replace('add_requires("libsdl2", { configs = { sdlmain = true } })','raise("M0 recipe disables samples")')
    recipe=recipe.replace('add_requires("cli11")','raise("M0 recipe disables CLI tools")').replace('add_requires("libmysofa 1.3.2")','')
    (dest/'xmake.lua').write_text(recipe,encoding='utf-8')
    marker.write_text(json.dumps(expected,indent=2)+'\n',encoding='utf-8')
    (ROOT/'.cache/amplitude-recipe-location.json').write_text(json.dumps({'source_directory':dest.relative_to(ROOT).as_posix()},indent=2)+'\n')
    (ROOT/'.cache/amplitude-recipe.patch').write_text(''.join(__import__('difflib').unified_diff((source/'xmake.lua').read_text(encoding='utf-8').splitlines(True),recipe.splitlines(True),fromfile='a/xmake.lua',tofile='b/xmake.lua')),encoding='utf-8')
    print('Prepared static /MD Amplitude recipe using pinned vcpkg providers; build/import acceptance pending')
    return 0

if __name__=='__main__':raise SystemExit(main())
