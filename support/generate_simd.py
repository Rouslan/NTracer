"""generates wrapper methods for several different vector types, which would be
tedious to do by hand"""

# must work in CPython 2.7 and 3.0+

from __future__ import print_function

import json
import re

from collections import namedtuple


# 64 is intentionally omitted (not supported)
widths = [128,256,512]

FLOAT = 1
INTEGER = 2

CType = namedtuple('CType','name size code float')
types = {}

for t in [
    ('int8_t',8,'i8',INTEGER),
#    ('uint8_t',8,'u8',INTEGER),
    ('int16_t',16,'i16',INTEGER),
#    ('uint16_t',16,'u16',INTEGER),
    ('int32_t',32,'i32',INTEGER),
#    ('uint32_t',32,'u32',INTEGER),
    ('int64_t',64,'i64',INTEGER),
#    ('uint64_t',64,'u64',INTEGER),
    ('float',32,'s',FLOAT),
    ('double',64,'d',FLOAT)]:
    types[t[0]] = CType(*t)

binary_op = """        FORCE_INLINE({wrap_type}) operator{extra}({wrap_type} b) const {{
            return {intr}(this->data.p,b.data.p);
        }}
        FORCE_INLINE({wrap_type}&) operator{extra}=({wrap_type} b) {{
            this->data.p = {intr}(this->data.p,b.data.p);
            return *this;
        }}
"""
cmp_op = """        FORCE_INLINE(mask) operator{extra}({wrap_type} b) const {{
            return mask({intr}(this->data.p,b.data.p));
        }}
"""
unary_meth = """        FORCE_INLINE({wrap_type}) {extra}() const {{
            return {intr}(this->data.p);
        }}
"""
reduce_meth = """        FORCE_INLINE({item_type}) {extra}() const {{
            return {intr}(this->data.p);
        }}
"""
load_meth = """        static FORCE_INLINE({wrap_type}) {extra}(const {item_type} *source) {{
            return {intr}(reinterpret_cast<{iargs[0][type]}>(source));
        }}
"""

store_meth = """        FORCE_INLINE(void) {extra}({item_type} *dest) const {{
            {intr}(reinterpret_cast<{iargs[0][type]}>(dest),this->data.p);
        }}
"""

set1_meth = """        static FORCE_INLINE({wrap_type}) {extra}({item_type} x) {{
            return {intr}(x);
        }}
"""

setzero_meth = """        static FORCE_INLINE({wrap_type}) {extra}() {{
            return {intr}();
        }}
"""

cast_meth = """        explicit FORCE_INLINE() {wrap_type}({wrap_type_src} b) {{
            this->data.p = {intr}(b.data.p);
        }}
"""
cast_meth_dec = """        explicit {wrap_type}({wrap_type_src} b);
"""
cast_meth_def = """    FORCE_INLINE() {wrap_type}::{wrap_type}({wrap_type_src} b) {{
        this->data.p = {intr}(b.data.p);
    }}
"""

binary_func = """    FORCE_INLINE({wrap_type}) {extra}({wrap_type} a,{wrap_type} b) {{
        return {intr}(a.data.p,b.data.p);
    }}
"""


class FTransform(object):
    def __init__(self,base_name,template,extra=None,support=FLOAT|INTEGER):
        self.base_name = base_name
        self.template = template
        self.extra = extra or base_name
        self.support = support
        
    def supported(self,type,width):
        return self.support & type.float and (width != 512 or type.size >= 32)
        
    def intrinsic(self,type,width):
        return '_mm{0}_{1}_{2}p{3}'.format(
            width if width > 128 else '',
            self.base_name,
            '' if width == 64 or type.float == FLOAT else 'e',
            type.code)
    
    def output(self,type,width,intr):
        return self.template.format(
            intr=intr['name'],
            iargs=intr['parameters'],
            wrap_type=wrap_type(type,width),
            base_type=base_type(type,width),
            item_type=type.name,
            extra=self.extra)


class AltIFTransform(FTransform):
    def intrinsic(self,type,width):
        if type.float == INTEGER:
            return '_mm{0}_{1}_si{2}'.format(
                width if width > 128 else '',
                self.base_name,
                width)
        
        return super(AltIFTransform,self).intrinsic(type,width)

class CmpFTransform(FTransform):
    def intrinsic(self,type,width):
        r = super(CmpFTransform,self).intrinsic(type,width)
        if width == 512:
            r += '_mask'
        return r

class RepeatTransform(FTransform):
    def intrinsic(self,type,width):
        r = super(RepeatTransform,self).intrinsic(type,width)
        if type is types['int64_t'] and width < 512:
            r += 'x'
        return r

class CastTransform(object):
    def supported(self,type,width,src_width,inline):
        return not (width == src_width or
            (width < src_width and not inline) or
            ((width == 512 or src_width == 512) and type.size < 32))
    
    def intrinsic(self,type,width,src_width,inline):
        code = 'si'
        if type is types['double']: code = 'pd'
        elif type is types['float']: code = 'ps'
        return '_mm{0}_cast{1}{2}_{1}{3}'.format(
            max(width,src_width),
            code,
            src_width,
            width)
    
    def output(self,type,width,src_width,intr,inline):
        tmpl = cast_meth
        if width > src_width:
            tmpl = cast_meth_dec if inline else cast_meth_def
        else:
            assert inline
        
        return tmpl.format(
            intr=intr['name'],
            wrap_type=wrap_type(type,width),
            wrap_type_src=wrap_type(type,src_width))

class MixedFTransformAdapter(FTransform):
    def __init__(self,src_width,base):
        self.src_width = src_width
        self.base = base
        super(MixedFTransformAdapter,self).__init__('',None)
        
    def intrinsic(self,type,width):
        return CastTransform().intrinsic(type,width,self.src_width,True)
    
    def output(self,type,width,intr):
        return CastTransform().output(type,width,self.src_width,intr,True)


mixed_method_transforms = [
    CastTransform()]

method_transforms = [MixedFTransformAdapter(w,mmt) for w in widths for mmt in mixed_method_transforms] + [
    FTransform('add',binary_op,'+'),
    FTransform('sub',binary_op,'-'),
    FTransform('mul',binary_op,'*',support=FLOAT),
    FTransform('div',binary_op,'/',support=FLOAT),
    AltIFTransform('and',binary_op,'&'),
    AltIFTransform('or',binary_op,'|'),
    AltIFTransform('xor',binary_op,'^'),
    CmpFTransform('cmpeq',cmp_op,'=='),
    CmpFTransform('cmpneq',cmp_op,'!='),
    CmpFTransform('cmpgt',cmp_op,'>'),
    CmpFTransform('cmpge',cmp_op,'>='),
    CmpFTransform('cmplt',cmp_op,'<'),
    CmpFTransform('cmple',cmp_op,'<='),
    FTransform('sqrt',unary_meth,support=FLOAT),
    FTransform('rsqrt',unary_meth,support=FLOAT),
    FTransform('abs',unary_meth),
    FTransform('ceil',unary_meth,support=FLOAT),
    FTransform('floor',unary_meth,support=FLOAT),
    FTransform('reduce_add',reduce_meth),
    FTransform('reduce_max',reduce_meth),
    FTransform('reduce_min',reduce_meth),
    AltIFTransform('load',load_meth),
    AltIFTransform('loadu',load_meth),
    AltIFTransform('store',store_meth),
    AltIFTransform('storeu',store_meth),
    RepeatTransform('set1',set1_meth,'repeat'),
    AltIFTransform('setzero',setzero_meth,'zeros')]

function_transforms = [
    FTransform('max',binary_func),
    FTransform('min',binary_func),
    AltIFTransform('andnot',binary_func,'and_not')]


def macroize(x):
    return ''.join(c if c.isalnum() else '_' for c in x)

def wrap_type(type,width):
    return '{0}_v_{1}'.format(type.name[0:-2] if type.name.endswith('_t') else type.name,width)

def base_type(type,width):
    suffix = ''
    if type is types['double']: suffix = 'd'
    elif type.float == INTEGER: suffix = 'i'
    return '__m{0}{1}'.format(width,suffix)

def print_dependent(d_list,file,indent=0):
    for req,mt_list in d_list.items():
        print('{0}#ifdef SUPPORT_{1}'.format('    '*indent,macroize(req)),file=file)
            
        for trans,intr,type,width in mt_list:
            print(trans.output(type,width,intr),file=file)
            
        print('    ' * indent + '#endif',file=file)


IMPLIED_REQ = {}
rs = set()
for r in ['SSE','SSE2','SSE3','SSSE3','SSE4.1','SSE4.2','AVX','AVX2','AVX-512']:
    rs.add(r)
    IMPLIED_REQ[r] = frozenset(rs)
del rs


meth_search = re.compile(r'^\s*//\s*\[\[\[(\w+)-(\d+)(?:-(\d+)|)\]\]\]\s*$')
func_search = re.compile(r'^\s*//\s*\[\[\[functions\]\]\]\s*$')


def generate(data_file,template_file,output_file):
    with open(data_file,'rb') as input:
        data = json.load(input)

    intrinsics = {}
    for item in data:
        intrinsics[item['name']] = item

    del data


    template = open(template_file,'rb')
    output = open(output_file,'wb')

    print('// THIS FILE WAS GENERATED BY generate_simd.py. DO NOT EDIT IT DIRECTLY.\n\n',file=output)

    for line in template:
        m = meth_search.match(line)
        if m is not None:
            type = types[m.group(1)]
            width = int(m.group(2),10)
            assert width in widths
            
            if m.group(3):
                width2 = int(m.group(3),10)
                assert width2 in widths
                
                for mt in mixed_method_transforms:
                    if not mt.supported(type,width,width2,False): continue
                
                    intr_name = mt.intrinsic(type,width,width2,False)
                    intr = intr_name and intrinsics.get(intr_name)
                    if intr:
                        print(mt.output(type,width,width2,intr,False),file=output)
            else:
                if width == 512:
                    assert type.size >= 32
                    type_req = 'AVX-512'
                elif type is types['float']:
                    type_req = {128:'SSE',256:'AVX'}[width]
                elif type is types['double']:
                    type_req = {128:'SSE2',256:'AVX'}[width]
                elif width == 256:
                    type_req = 'AVX2'
                else:
                    assert width == 128
                    type_req = 'SSE2'
                
                dependent = {}
                for mt in method_transforms:
                    if not mt.supported(type,width): continue
                
                    intr = intrinsics.get(mt.intrinsic(type,width))
                    if intr:
                        req = intr['tech']
                        if req in IMPLIED_REQ[type_req]:
                            print(mt.output(type,width,intr),file=output)
                        else:
                            if req not in dependent:
                                dependent[req] = []
                            dependent[req].append((mt,intr,type,width))
                        
                print_dependent(dependent,output,1)
        else:
            m = func_search.match(line)
            if m:
                dependent = {}
                for type in types.values():
                    for width in widths:
                        for ft in function_transforms:
                            if not ft.supported(type,width): continue
                            
                            intr = intrinsics.get(ft.intrinsic(type,width))
                            if intr:
                                req = intr['tech']
                                if req not in dependent:
                                    dependent[req] = []
                                dependent[req].append((ft,intr,type,width))
                            
                print_dependent(dependent,output)
            else:
                print(line,end='',file=output)
                
    template.close()
    output.close()

