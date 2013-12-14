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

CType = namedtuple('CType','name code float')
types = {}

for t in [
    ('int8_t','i8',INTEGER),
    ('uint8_t','u8',INTEGER),
    ('int16_t','i16',INTEGER),
    ('uint16_t','u16',INTEGER),
    ('int32_t','i32',INTEGER),
    ('uint32_t','u32',INTEGER),
    ('int64_t','i64',INTEGER),
    ('uint64_t','u64',INTEGER),
    ('float','s',FLOAT),
    ('double','d',FLOAT)]:
    types[t[0]] = CType(*t)

binary_op = """        FORCE_INLINE({wrap_type}) operator{extra}({wrap_type} b) const {{
            return {intr}(this->data.p,b.data.p);
        }}
        FORCE_INLINE({wrap_type}&) operator{extra}=({wrap_type} b) {{
            this->data.p = {intr}(this->data.p,b.data.p);
            return *this;
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
            return {intr}(source);
        }}
"""

store_meth = """        FORCE_INLINE(void) {extra}({item_type} *dest) {{
            {intr}(dest,this->data.p);
        }}
"""

set1_meth = """        static FORCE_INLINE({wrap_type}) {extra}({item_type} source) {{
            return {intr}(source);
        }}
"""

setzero_meth = """        static FORCE_INLINE({wrap_type}) {extra}() {{
            return {intr}();
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
        
    def intrinsic(self,type,width):
        return '_mm{0}_{1}_{2}p{3}'.format(
            width if width > 128 else '',
            self.base_name,
            '' if width == 64 or type.float == FLOAT else 'e',
            type.code)

class AltIFTransform(FTransform):
    def intrinsic(self,type,width):
        if type.float == INTEGER:
            return '_mm{0}_{1}_si{2}'.format(
                width if width > 128 else '',
                self.base_name,
                width)
        
        return super(AltIFTransform,self).intrinsic(type,width)

method_transforms = [
    FTransform('add',binary_op,'+'),
    FTransform('sub',binary_op,'-'),
    FTransform('mul',binary_op,'*',support=FLOAT),
    FTransform('div',binary_op,'/',support=FLOAT),
    AltIFTransform('and',binary_op,'&'),
    AltIFTransform('or',binary_op,'|'),
    AltIFTransform('xor',binary_op,'^'),
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
    AltIFTransform('store',load_meth),
    AltIFTransform('storeu',load_meth),
    FTransform('set1',set1_meth,'repeat'),
    AltIFTransform('setzero',setzero_meth,'zeros')]

function_transforms = [
    FTransform('max',binary_func),
    FTransform('min',binary_func),
    AltIFTransform('andnot',binary_func,'and_not')]


def macroize(x):
    return ''.join(c if c.isalnum() else '_' for c in x)

def wrap_type(type,width):
    return '{0}_v_{1}'.format(type.name[0:-2] if type.name.endswith('_t') else type.name,width)

def print_dependent(d_list,file,indent=0):
    for req,mt_list in d_list.items():
        print('{0}#ifdef SUPPORT_{1}'.format('    '*indent,macroize(req)),file=file)
            
        for intr,wrapped,type,template,extra in mt_list:
            print(template.format(
                intr=intr['name'],
                wrap_type=wrapped,
                item_type=type.name,
                extra=extra),file=file)
            
        print('    ' * indent + '#endif',file=file)


IMPLIED_REQ = {}
rs = set()
for r in ['SSE','SSE2','SSE3','SSSE3','SSE4.1','SSE4.2','AVX','AVX2','AVX-512']:
    rs.add(r)
    IMPLIED_REQ[r] = frozenset(rs)
del rs


meth_search = re.compile(r'^\s*//\s*\[\[\[(\w+)-(\d+)\]\]\]\s*$')
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
            wrapped = wrap_type(type,width)
        
            if width == 512:
                assert type.name in ('int32_t','uint32_t','int64_t','uint64_t','float','double')
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
                if not (mt.support & type.float): continue
            
                intr_name = mt.intrinsic(type,width)
                intr = intrinsics.get(intr_name)
                if intr:
                    req = intr['tech']
                    if req in IMPLIED_REQ[type_req]:
                        print(mt.template.format(
                            intr=intr_name,
                            wrap_type=wrapped,
                            item_type=type.name,
                            extra=mt.extra),file=output)
                    else:
                        if req not in dependent:
                            dependent[req] = []
                        dependent[req].append((intr,wrapped,type,mt.template,mt.extra))
                    
            print_dependent(dependent,output,1)
        else:
            m = func_search.match(line)
            if m:
                dependent = {}
                for type in types.values():
                    for width in widths:
                        wrapped = wrap_type(type,width)
                    
                        for ft in function_transforms:
                            intr = intrinsics.get(ft.intrinsic(type,width))
                            if intr:
                                req = intr['tech']
                                if req not in dependent:
                                    dependent[req] = []
                                dependent[req].append((intr,wrapped,type,ft.template,ft.extra))
                            
                print_dependent(dependent,output)
            else:
                print(line,end='',file=output)
                
    template.close()
    output.close()

