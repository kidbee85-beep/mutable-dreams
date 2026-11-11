"""Exercise the actual SDK build fingerprint without invoking its compiler or SDK."""
from pathlib import Path
import ast,hashlib,tempfile
root=Path(__file__).resolve().parents[1];tree=ast.parse((root/'base/tools/build.py').read_text());fn=next(n for n in tree.body if isinstance(n,ast.FunctionDef) and n.name=='build_id');fn.decorator_list=[]
with tempfile.TemporaryDirectory() as td:
 r=Path(td);app=r/'app';src=r/'base';bsp=src/'bsp'
 for p in [app,src/'device',src/'include',src/'lib',src/'bt',src/'ble',bsp/'chip',bsp/'boards'/'fm1']:p.mkdir(parents=True,exist_ok=True)
 ns={'APP':app,'SRC':src,'BSP':bsp,'VERSION':'2.0.0-dev','hashlib':hashlib};exec(compile(ast.Module(body=[fn],type_ignores=[]),'build-fingerprint','exec'),ns);fingerprint=ns['build_id']
 for suffix in ['.c','.cc','.cpp','.cxx','.h','.hpp','.inc','.S','.toml','.ld']:
  p=app/('probe'+suffix);p.write_text('first');a=fingerprint();p.write_text('second');assert fingerprint()!=a,suffix;p.unlink()
 p=app/'nested/third_party';p.mkdir(parents=True);(p/'oscillator.cc').write_text('voice A');a=fingerprint();(p/'oscillator.cc').write_text('voice B');assert fingerprint()!=a
 a=fingerprint();(app/'build').mkdir();(app/'build/generated.cpp').write_text('intermediate');assert fingerprint()==a;(app/'notes.txt').write_text('notes');assert fingerprint()==a
 for directory in [src/'lib',src/'bt',src/'ble',bsp/'boards'/'fm1']:
  p=directory/'included.c';p.write_text('A');a=fingerprint();p.write_text('B');assert fingerprint()!=a,str(directory);p.unlink()
 a=fingerprint();ns['VERSION']='2.0.0-next';assert fingerprint()!=a
print('PASS SDK build ID responds to C/C++/included DSP source, nested voices, platform helpers, BLE stacks, board and version; ignores build intermediates and prose.')
