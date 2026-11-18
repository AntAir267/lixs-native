"""Generate the Direct3D 8 bridge's method wrappers from DXVK's d3d8.h (from etm-native's
tools/d3d9gen.py).

    d3d8gen.py <d3d8.h> <output .c>

Each COM interface the program uses becomes a ComClass whose vtable entries are runtime
functions: they read the program's 32-bit stdcall arguments, convert what differs between the
program's 32-bit and the host's 64-bit layouts (interface pointers, window handles), call DXVK's
object through its vtable by index, and wrap returned interfaces. Methods needing more (locks,
present parameters, QueryInterface, Release, ...) are written by hand in runtime/d3d8.c and named
in SPECIAL; the generated table points at those. The header also gets every method's vtable
index (VI_<Iface>_<Method>) for the hand-written side.
"""
import re
import sys

IFACES = ['IDirect3D8', 'IDirect3DDevice8', 'IDirect3DSwapChain8', 'IDirect3DResource8',
          'IDirect3DBaseTexture8', 'IDirect3DTexture8', 'IDirect3DVolumeTexture8',
          'IDirect3DCubeTexture8', 'IDirect3DVertexBuffer8', 'IDirect3DIndexBuffer8',
          'IDirect3DSurface8', 'IDirect3DVolume8']
# interfaces the program can hold (the abstract bases only appear as parameter types)
CLASSES = [i for i in IFACES if i not in ('IDirect3DResource8', 'IDirect3DBaseTexture8')]

# hand-written in runtime/d3d8.c: "Iface::Method" or "*::Method" for every interface
SPECIAL = {
    '*::QueryInterface', '*::AddRef', '*::Release', '*::SetPrivateData', '*::GetPrivateData',
    'IDirect3D8::CreateDevice', 'IDirect3D8::GetAdapterMonitor', 'IDirect3D8::RegisterSoftwareDevice',
    'IDirect3D8::GetAdapterModeCount', 'IDirect3D8::EnumAdapterModes',
    'IDirect3DDevice8::Reset', 'IDirect3DDevice8::CreateAdditionalSwapChain',
    'IDirect3DDevice8::GetCreationParameters', 'IDirect3DDevice8::Present',
    'IDirect3DSwapChain8::Present',
    'IDirect3DVertexBuffer8::Lock', 'IDirect3DVertexBuffer8::Unlock',
    'IDirect3DIndexBuffer8::Lock', 'IDirect3DIndexBuffer8::Unlock',
    'IDirect3DTexture8::LockRect', 'IDirect3DTexture8::UnlockRect',
    'IDirect3DCubeTexture8::LockRect', 'IDirect3DCubeTexture8::UnlockRect',
    'IDirect3DVolumeTexture8::LockBox', 'IDirect3DVolumeTexture8::UnlockBox',
    'IDirect3DSurface8::LockRect', 'IDirect3DSurface8::UnlockRect',
    'IDirect3DVolume8::LockBox', 'IDirect3DVolume8::UnlockBox',
    'IDirect3DSurface8::GetContainer', 'IDirect3DVolume8::GetContainer',
}

INTERFACE_TYPES = set(IFACES) | {'IUnknown'}


def parse(header):
    s = open(header).read()
    out = {}
    for name in IFACES:
        m = re.search(r'DECLARE_INTERFACE_\(%s,\s*(\w+)\)\s*\{(.*?)\n\};' % name, s, re.S)
        if not m:
            sys.exit('%s not found' % name)
        base, body = m.group(1), m.group(2)
        body = re.sub(r'/\*.*?\*/', '', body, flags=re.S)
        methods = []
        pat = r'STDMETHOD(?:_\(\s*([^,]+?)\s*,\s*(\w+)\s*\)|\((\w+)\))\s*\((.*?)\)\s*PURE;'
        for r, n1, n2, args in re.findall(pat, body.replace('\n', ' '), re.S):
            ret, mname = r or 'HRESULT', n1 or n2
            params = []
            for a in args.split(','):
                a = a.replace('THIS_', '').replace('THIS', '').strip()
                if not a or a == 'void':
                    continue
                mm = re.match(r'^(.*?)(\w+)\s*$', a)
                if not mm:                      # no name, ends in '*'
                    typ, pname = a, 'a%d' % len(params)
                else:
                    typ, pname = mm.group(1).strip(), mm.group(2)
                if not typ:                     # an unnamed parameter
                    typ, pname = pname, 'a%d' % len(params)
                typ = typ.replace('struct ', '')
                const = 'const' in typ.split()
                stars = typ.count('*')
                bare = typ.replace('const', '').replace('*', '').strip()
                params.append(dict(type=bare, stars=stars, const=const, name=pname))
            methods.append(dict(ret=ret.strip(), name=mname, params=params))
        out[name] = (base, methods)
    return out


def all_methods(defs, name):
    base, methods = defs[name]
    if methods and methods[0]['name'] == 'QueryInterface':
        return methods                  # the header lists the inherited methods again: complete
    if base in defs:
        return all_methods(defs, base) + methods
    if base == 'IUnknown':
        return [dict(ret='HRESULT', name='QueryInterface', params=[]),
                dict(ret='ULONG', name='AddRef', params=[]),
                dict(ret='ULONG', name='Release', params=[])] + \
               [m for m in methods if m['name'] not in ('QueryInterface', 'AddRef', 'Release')]
    return methods


def ctype(p):
    t = ('const ' if p['const'] else '') + p['type'] + ' ' + '*' * p['stars']
    return t.strip()


def gen_method(iface, idx, m):
    """C body for one method, or None if it cannot be generated (then it must be SPECIAL)"""
    pre, args, post, decl = [], [], [], []
    for k, p in enumerate(m['params']):
        a = 'ARG(%d)' % (k + 1)
        t, st = p['type'], p['stars']
        if t in INTERFACE_TYPES and st == 1:
            args.append('(void *)d3d_host_or_null(%s)' % a)
        elif t in INTERFACE_TYPES and st == 2:
            decl.append('void *o%d = NULL;' % k)
            args.append('%s ? &o%d : NULL' % (a, k))
            wrapper = 'd3d_wrap_new' if m['name'].startswith('Create') else 'd3d_wrap'
            post.append('if (%s) W32(%s, o%d ? %s(DK_%s, o%d) : 0);' % (a, a, k, wrapper, t, k))
        elif t == 'HWND' and st == 0:
            args.append('d3d_hwnd(%s)' % a)
        elif t == 'HANDLE' and st == 1:
            pre.append('if (%s) W32(%s, 0);' % (a, a))
            args.append('NULL')
        elif t in ('HDC', 'HMONITOR') or (t in ('void', 'BYTE') and st == 2):
            return None
        elif t in ('D3DPRESENT_PARAMETERS', 'D3DLOCKED_RECT', 'D3DLOCKED_BOX',
                   'D3DDEVICE_CREATION_PARAMETERS', 'RGNDATA') and st:
            return None
        elif st >= 1 or t in ('REFGUID', 'REFIID'):
            args.append('(%s)(uintptr_t)%s' % (ctype(p), a))
        elif t == 'float':
            args.append('d3d_argf(c, %d)' % (k + 1))
        else:
            args.append('(%s)%s' % (ctype(p), a))
    def sigtype(p):
        if p['type'] in INTERFACE_TYPES:
            return 'void ' + '*' * p['stars']
        return ctype(p)
    sig = ', '.join(['void *'] + [sigtype(p) for p in m['params']])
    sig = sig.replace('HWND', 'void *')
    ret = m['ret']
    if ret == 'float' or ret == 'FLOAT':
        return None
    rtype = 'void' if ret == 'void' else ret
    call = '((%s (*)(%s))VT(self)[%d])(%s)' % (rtype, sig, idx, ', '.join(['self'] + args))
    body = ['    void *self = d3d_host(ARG(0));'] + ['    ' + d for d in decl] + ['    ' + p for p in pre]
    if ret == 'void':
        body.append('    %s;' % call)
        body += ['    ' + p for p in post]
        body.append('    return 0;')
    else:
        body.append('    uint32_t r = (uint32_t)%s;' % call)
        body += ['    ' + p for p in post]
        if ret == 'HRESULT':
            body.append('    if ((int32_t)r < 0 && r != 0x8876086au) LOG(LOG_D3D, "%s::%s -> %%08x", r);' % (iface, m['name']))
        body.append('    return r;')
    return body


def main():
    header, out = sys.argv[1:3]
    defs = parse(header)
    lines = ['/* Generated by tools/d3d8gen.py from DXVK\'s d3d8.h. Do not edit. */',
             '#include "d3d8_bridge.h"', '']
    tables = []
    for iface in CLASSES:
        methods = all_methods(defs, iface)
        names = []
        for idx, m in enumerate(methods):
            key, star = '%s::%s' % (iface, m['name']), '*::%s' % m['name']
            nargs = 1 + len(m['params'])
            if key in SPECIAL or star in SPECIAL:
                fn = 'sp_%s_%s' % (iface, m['name']) if key in SPECIAL else 'sp_any_%s' % m['name']
                lines.append('void %s(CPU *c);' % fn)
                names.append((fn, m['name']))
                continue
            body = gen_method(iface, idx, m)
            fn = 'm_%s_%s' % (iface, m['name'])
            if body is None:
                lines.append('static void %s(CPU *c) { d3d_unsupported(c, "%s::%s", %d); }' % (fn, iface, m['name'], nargs))
            else:
                lines.append('METHOD(%s, %d)' % (fn, nargs))
                lines.append('{')
                lines += body
                lines.append('}')
            names.append((fn, m['name']))
        lines.append('static void (*const fns_%s[])(CPU *) = {%s};' % (iface, ', '.join(f for f, n in names)))
        lines.append('static const char *const names_%s[] = {%s};' % (iface, ', '.join('"%s"' % n for f, n in names)))
        lines.append('')
        tables.append((iface, len(names)))
    lines.append('ComClass d3d_classes[DK_COUNT];')
    lines.append('void d3d_classes_init(void)')
    lines.append('{')
    for iface, n in tables:
        lines.append('    com_class_init(&d3d_classes[DK_%s], "%s", %d, fns_%s, names_%s);' % (iface, iface, n, iface, iface))
    lines.append('}')
    with open(out, 'w', newline='\n') as fh:
        fh.write('\n'.join(lines) + '\n')
    # the kinds and vtable indices, for the hand-written side
    with open(out.replace('.c', '.h'), 'w', newline='\n') as fh:
        fh.write('/* Generated by tools/d3d8gen.py. */\n#pragma once\nenum {\n')
        for iface in IFACES:
            fh.write('    DK_%s,\n' % iface)
        fh.write('    DK_IUnknown,\n    DK_COUNT\n};\n')
        for iface in IFACES:
            for idx, m in enumerate(all_methods(defs, iface)):
                fh.write('#define VI_%s_%s %d\n' % (iface, m['name'], idx))


if __name__ == '__main__':
    main()
