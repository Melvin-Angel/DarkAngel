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
    # Import already installed vcpkg artifacts directly. No XMake package registry
    # is consulted, and no second library implementation is compiled.
    prefix=(ROOT/'vcpkg_installed/x64-windows-darkangel').as_posix()
    dependencies=['flatbuffers','xsimd','miniaudio','eigen','lz4','dylib']
    import re
    for name in dependencies:
        recipe,count=re.subn(r'add_requires\("'+name+r' [^"]+"\)', '', recipe)
        if count!=1:raise RuntimeError('Upstream dependency declaration changed: '+name)
    recipe=recipe.replace('add_requireconfs("*", { debug = is_mode("debug") })','')
    recipe=recipe.replace('add_packages("dylib")','')
    recipe=recipe.replace('add_packages("flatbuffers", "xsimd", "eigen", "miniaudio", "lz4")','')
    imports=[
        'set_runtimes(is_mode("debug") and "MDd" or "MD")',
        f'add_includedirs("{prefix}/include", "{prefix}/include/eigen3")',
        f'add_linkdirs(is_mode("debug") and "{prefix}/debug/lib" or "{prefix}/lib")',
        'add_links("flatbuffers", "lz4")'
    ]
    recipe=recipe.replace('-- Dependencies','\n'.join(imports)+'\n\n-- Dependencies',1)
    for file in ['include/flatbuffers/flatbuffers.h','include/xsimd/xsimd.hpp','include/miniaudio.h','include/eigen3/Eigen/Core','include/dylib.hpp','lib/lz4.lib','lib/flatbuffers.lib']:
        if not (ROOT/'vcpkg_installed/x64-windows-darkangel'/file).is_file():
            raise RuntimeError('Install the pinned amplitude-deps feature first: '+file)
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
