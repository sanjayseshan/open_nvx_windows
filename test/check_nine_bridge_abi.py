#!/usr/bin/env python3
"""Compile-only comparison of Nine's backend ABI mirrors to Mesa's headers.

Run from the repository root after building Mesa XP32. Uses that build's
compiler flags and PE object data, so no Wine or guest is needed.
"""
import json, shlex, subprocess, re, pathlib, struct
root=pathlib.Path(__file__).resolve().parent.parent
import os, tempfile
os.chdir(root)
work=tempfile.TemporaryDirectory(prefix='nvx-nine-abi-')
tmp=pathlib.Path(work.name)
s=(root/'mesa_patches/nine/mesa99.c').read_text()
build=pathlib.Path(os.environ.get('NVX_MESA_XP32_BUILD', '/tmp/nvx-mesa-xp32-build'))
cmds=json.load(open(build/'compile_commands.json'))
e=next(e for e in cmds if e['file'].endswith('/nvc0_context.c'))
args=shlex.split(e['command']); cut=args.index('-MD'); args=args[:cut]
types={'pipe_resource':['width0','height0','depth0','array_size','bind','flags','clientptr_heap','next','screen'], 'pipe_surface':['level','texture'], 'pipe_framebuffer_state':['width','height','layers','samples','nr_cbufs','viewmask','cbufs','zsbuf','resolve'], 'pipe_sampler_view':['texture','context','u'], 'pipe_draw_info':['start_instance','instance_count','restart_index','index','min_index','max_index'], 'pipe_vertex_buffer':['buffer_offset','buffer'], 'pipe_vertex_element':['src_stride','instance_divisor']}
types.update({'box':['x','width','y','height','z','depth'],
              'transfer':['resource','box','stride','layer_stride','offset'],
              'shader_state':['type','tokens','ir','stream_output','report_compile_error','error_message'],
              'blit_info':['dst','src','mask','filter','scissor','swizzle','window_rectangles','alpha_blend']})
mirrornames={t:'m27_'+t for t in types}
mirrornames.update({'box':'m27_box', 'transfer':'m27_transfer', 'shader_state':'m27_shader_state', 'blit_info':'m27_blit_info'})
types.update({'pipe_rasterizer_state':['sprite_coord_enable','line_width','point_size','offset_units','offset_clamp'], 'pipe_blend_state':['rt']})
mirrornames.update({t:'m27_'+t for t in ['pipe_rasterizer_state','pipe_blend_state']})
fields=[]
for t,fs in types.items():
 fields.append((t,'size'))
 fields.extend((t,f) for f in fs)
# Extract bridge's hand-maintained native declarations, not a second reconstruction.
mirrors=[]
# Declarations with dependencies must precede their users.
for t in ['m27_box', 'm27_scissor']+[mirrornames[t] for t in types if t!='box']:
 m=re.search(r'struct '+t+r'\s*\{.*?\};',s,re.S)
 if not m: raise Exception(t)
 mirrors.append(m.group())
common='\n'.join(mirrors)
for version in ['native','mirror']:
 source='#include <stddef.h>\n#include "pipe/p_context.h"\n#include "pipe/p_state.h"\n'
 if version=='mirror': source+=common
 vals=[]
 for t,f in fields:
  actual=('pipe_'+t if not t.startswith('pipe_') else t) if version=='native' else mirrornames[t]
  vals.append('sizeof(struct '+actual+')' if f=='size' else 'offsetof(struct '+actual+', '+f+')')
 source+='\n__attribute__((section(".nvxabi"),used)) const unsigned abi[]={'+','.join(vals)+'};\n'
 path=str(tmp/(version+'.c'));open(path,'w').write(source)
 subprocess.run(args+['-c',path,'-o',str(tmp/(version+'.o'))],cwd=e['directory'],check=True,stdout=subprocess.DEVNULL)
 subprocess.run(['i686-w64-mingw32-objcopy','-O','binary','--only-section=.nvxabi',str(tmp/(version+'.o')),str(tmp/(version+'.bin'))],check=True)
a=open(tmp/'native.bin','rb').read();b=open(tmp/'mirror.bin','rb').read()
fail=0
for i,(t,f) in enumerate(fields):
 x=struct.unpack_from('<I',a,i*4)[0];y=struct.unpack_from('<I',b,i*4)[0]
 if x!=y: print(t,f,'native',x,'bridge',y);fail+=1
print('ABI comparisons:',len(fields),'mismatches:',fail)
raise SystemExit(bool(fail))
