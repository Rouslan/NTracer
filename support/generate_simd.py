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


binary_op = """        FORCE_INLINE {wrap_type} operator{extra}({wrap_type} b) const {{
            return {intr}(this->data.p,b.data.p);
        }}
        FORCE_INLINE {wrap_type} &operator{extra}=({wrap_type} b) {{
            this->data.p = {intr}(this->data.p,b.data.p);
            return *this;
        }}
"""
cmp_op = """        FORCE_INLINE mask operator{extra[0]}({wrap_type} b) const {{
            return mask({intr}(this->data.p,b.data.p{extra[1]}));
        }}
"""
unary_meth = """        FORCE_INLINE {wrap_type} {extra}() const {{
            return {intr}(this->data.p);
        }}
"""
reduce_meth = """        FORCE_INLINE {item_type} {extra}() const {{
            return {intr}(this->data.p);
        }}
"""
mask_meth = """        FORCE_INLINE {wrap_type} {extra}(mask m) const {{
            return {intr}(m.data,this->data.p);
        }}
"""
pseudo_maskz_move = """        FORCE_INLINE {wrap_type} {extra}(mask m) const {{
            return {intr}(reinterpret_cast<{base_type}>(m.data),this->data.p);
        }}
"""

have_vec_reduce_add = '        static constexpr bool has_vec_reduce_add = true;\n'
reduce_add_meth = reduce_meth + have_vec_reduce_add
reduce_add_meth_b = """        FORCE_INLINE single<{item_type}> reduce_add() const {{
            auto r = {intr}(this->data.p,this->data.p);{more_steps}
            return {intr}(r,r);
        }}
""" + have_vec_reduce_add

load_meth = """        static FORCE_INLINE {wrap_type} {extra}(const {item_type} *source) {{
            return {intr}(reinterpret_cast<{iargs[0][type]}>(source));
        }}
"""

store_meth = """        FORCE_INLINE void {extra}({item_type} *dest) const {{
            {intr}(reinterpret_cast<{iargs[0][type]}>(dest),this->data.p);
        }}
"""

set1_meth = """        static FORCE_INLINE {wrap_type} {extra}({item_type} x) {{
            return {intr}(x);
        }}
"""

broadcast_meth = """        static FORCE_INLINE {wrap_type} repeat(single<{item_type}> x) {{
            return {intr}(x.data);
        }}
"""
shuffle_meth = """        static FORCE_INLINE {wrap_type} repeat(single<{item_type}> x) {{
            return {intr}(x.data{extra},_MM_SHUFFLE(0,0,0,0));
        }}
"""

setzero_meth = """        static FORCE_INLINE {wrap_type} {extra}() {{
            return {intr}();
        }}
"""

cast_meth = """        explicit FORCE_INLINE {wrap_type}({wrap_type_src} b) {{
            this->data.p = {intr}(b.data.p);
        }}
"""
cast_meth_dec = """        explicit {wrap_type}({wrap_type_src} b);
"""
cast_meth_def = """    FORCE_INLINE {wrap_type}::{wrap_type}({wrap_type_src} b) {{
        this->data.p = {intr}(b.data.p);
    }}
"""

binary_func = """    FORCE_INLINE {wrap_type} {extra}({wrap_type} a,{wrap_type} b) {{
        return {intr}(a.data.p,b.data.p);
    }}
"""
cmp_func = """    FORCE_INLINE {wrap_type}::mask {extra[0]}({wrap_type} a,{wrap_type} b) {{
        return {wrap_type}::mask({intr}(a.data.p,b.data.p{extra[1]}));
    }}
"""

mask_func = """    FORCE_INLINE mask{ssize}_v_{psize} {extra}({args_a}) {{
        return
"""

testz_func = """    FORCE_INLINE int {extra}({wrap_type} a,{wrap_type} b) {{
        return {intr}(a.data.p,b.data.p);
    }}
    FORCE_INLINE int {extra}({wrap_type} a) {{
        return {intr}(a.data.p,a.data.p);
    }}
"""

def print_mask_function(output,swidth,pwidth,func,args,body,fallback,explicit_cast=True):
    print('    FORCE_INLINE mask{0}_v_{1} {2}({3}) {{\n        return '.format(
        swidth,
        pwidth,
        func,
        ','.join('mask{0}_v_{1} {2}'.format(swidth,pwidth,chr(ord('a')+i)) for i in range(args))),end='',file=output)
    if explicit_cast:
        print('mask{0}_v_{1}('.format(swidth,pwidth),end='',file=output)
    
    if fallback:
        print(
            '\n    ',
            ifdef_support(fallback[0]),
            '\n            ',
            body,
            '\n    #else\n            '
            ,sep='',end='',file=output)
        
        body = fallback[1]
    
    print(body,end='',file=output)

    if fallback:
        print('\n    #endif\n        ',end='',file=output)
    
    if explicit_cast:
        print(')',end='',file=output)
    print(';\n    }',file=output)


def mm_prefix(width):
    return '_mm{0}'.format(width if width > 128 else '')

def generic_intrinsic_name(type,width,base,scalar=False):
    return '{0}_{1}_{2}{3}{4}'.format(
        mm_prefix(width),
        base,
        '' if width == 64 or type.float == FLOAT else 'e',
        's' if scalar else 'p',
        type.code)

def int_log2(x):
    r = 0
    x >>= 1
    while x:
        r += 1
        x >>= 1
    return r

class FTransform(object):
    def __init__(self,base_name,template,extra=None,support=FLOAT|INTEGER,prefer=None,scalar=False):
        self.base_name = base_name
        self.template = template
        self.extra = extra or base_name
        self.support = support
        self.prefer = prefer
        self.scalar = scalar
        
    def supported(self,type,width):
        return self.support & type.float and (width != 512 or type.size >= 32)
        
    def intrinsic(self,type,width):
        return generic_intrinsic_name(type,width,self.base_name,self.scalar)
    
    def output(self,type,width,intr):
        return self.template.format(
            intr=intr['name'],
            iargs=intr['parameters'],
            wrap_type=wrap_type(type,width),
            base_type=base_type(type,width),
            item_type=type.name,
            extra=self.extra)
    
    def antirequisite(self,type,width,intrinsics):
        if self.prefer:
            if self.prefer.supported(type,width):
                r = intrinsics.get(self.prefer.intrinsic(type,width))
                if r: return r['tech']
            return self.prefer.antirequisite(type,width,intrinsics)
        return None
    
class ShuffleTransform(FTransform):
    def __init__(self,prefer=None):
        super(ShuffleTransform,self).__init__('shuffle',shuffle_meth,prefer=prefer)
    
    def supported(self,type,width):
        return width == 128 and type.size >= 32

    def output(self,type,width,intr):
        return self.template.format(
            intr=intr['name'],
            wrap_type=wrap_type(type,width),
            item_type=type.name,
            extra=',x.data' if type.float == FLOAT else '')

class BroadcastTransform(FTransform):
    def __init__(self):
        super(BroadcastTransform,self).__init__(None,broadcast_meth)
    
    def intrinsic(self,type,width):
        return generic_intrinsic_name(type,width,'broadcast' + {'int8_t':'b','int16_t':'w','int32_t':'d','int64_t':'q','float':'ss','double':'sd'}[type.name]) 

class ReduceAddBTransform(FTransform):
    def __init__(self,prefer=None):
        super(ReduceAddBTransform,self).__init__('hadd',reduce_add_meth_b,prefer=prefer)
    
    def output(self,type,width,intr):
        ms = int_log2(width//type.size) - 2
        if ms:
            ms = '\n            r = {0}(r,r);'.format(intr['name']) * ms
        else:
            ms = ''
        
        return self.template.format(
            intr=intr['name'],
            wrap_type=wrap_type(type,width),
            item_type=type.name,
            more_steps=ms)

class AltIFTransform(FTransform):
    def intrinsic(self,type,width):
        if type.float == INTEGER:
            return '{0}_{1}_si{2}'.format(
                mm_prefix(width),
                self.base_name,
                width)
        
        return super(AltIFTransform,self).intrinsic(type,width)

class CmpFTransform(FTransform):
    def __init__(self,op,template,code=None):
        self.op = op
        self.code = code
        super(CmpFTransform,self).__init__(None,template)
        
    def intrinsic(self,type,width):
        base = 'cmp'
        if width < 256 or type.float != FLOAT: base += self.code
        
        r = generic_intrinsic_name(type,width,base)
        
        if width == 512:
            r += '_mask'
        
        return r
    
    def output(self,type,width,intr):
        arg3 = ''
        if width >= 256 and type.float == FLOAT:
            arg3 = ',_CMP_{0}_UQ'.format(self.code.upper())

        return self.template.format(
            intr=intr['name'],
            iargs=intr['parameters'],
            wrap_type=wrap_type(type,width),
            base_type=base_type(type,width),
            item_type=type.name,
            extra=(self.op,arg3))

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
        return '{0}_cast{1}{2}_{1}{3}'.format(
            mm_prefix(max(width,src_width)),
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

reduce_add = FTransform('reduce_add',reduce_add_meth)
repeat_a = BroadcastTransform()
maskz_mov = FTransform('maskz_mov',mask_meth,'zfilter')

method_transforms = [MixedFTransformAdapter(w,mmt) for w in widths for mmt in mixed_method_transforms] + [
    FTransform('add',binary_op,'+'),
    FTransform('sub',binary_op,'-'),
    FTransform('mul',binary_op,'*',support=FLOAT),
    FTransform('div',binary_op,'/',support=FLOAT),
    AltIFTransform('and',binary_op,'&'),
    AltIFTransform('or',binary_op,'|'),
    AltIFTransform('xor',binary_op,'^'),
    CmpFTransform('==',cmp_op,'eq'),
    CmpFTransform('!=',cmp_op,'neq'),
    CmpFTransform('>',cmp_op,'gt'),
    CmpFTransform('>=',cmp_op,'ge'),
    CmpFTransform('<',cmp_op,'lt'),
    CmpFTransform('<=',cmp_op,'le'),
    FTransform('sqrt',unary_meth,support=FLOAT),
    FTransform('rsqrt',unary_meth,support=FLOAT),
    FTransform('abs',unary_meth),
    FTransform('ceil',unary_meth,support=FLOAT),
    FTransform('floor',unary_meth,support=FLOAT),
    reduce_add,
    ReduceAddBTransform(prefer=reduce_add),
    FTransform('reduce_max',reduce_meth),
    FTransform('reduce_min',reduce_meth),
    AltIFTransform('load',load_meth),
    AltIFTransform('loadu',load_meth),
    AltIFTransform('store',store_meth),
    AltIFTransform('storeu',store_meth),
    repeat_a,
    ShuffleTransform(prefer=repeat_a),
    RepeatTransform('set1',set1_meth,'repeat'),
    maskz_mov,
    FTransform('and',pseudo_maskz_move,'zfilter',prefer=maskz_mov),
    AltIFTransform('setzero',setzero_meth,'zeros')]

function_transforms = [
    FTransform('max',binary_func),
    FTransform('min',binary_func),
    AltIFTransform('andnot',binary_func,'and_not'),
    AltIFTransform('testz',testz_func,'test_z'),
    CmpFTransform('cmp_nlt',cmp_func,'nlt'),
    CmpFTransform('cmp_nle',cmp_func,'nle'),
    CmpFTransform('cmp_ngt',cmp_func,'ngt'),
    CmpFTransform('cmp_nge',cmp_func,'nge')]


def data_args(args):
    return ['{0}.data'.format(chr(ord('a')+i)) for i in range(args)]

def ideal_args(swidth,pwidth,args):
    r = data_args(args)
    if swidth >= 32: r = ['reinterpret_cast<__m{0}i>({1})'.format(pwidth,a) for a in r]
    return ','.join(r)

def avx512_mask_body(swidth,base,args):
    r = '_mm512_k{0}({1})'.format(base,','.join(data_args(args)))
    if swidth != 32: r = 'static_cast<__mmask{0}>({1})'.format(512//swidth,r)
    return r

def common_mask_function(output,intrinsics,swidth,pwidth,type_req,suffix,name,intr_base):
    fallback = None
    
    if pwidth == 512:
        body = avx512_mask_body(swidth,intr_base,2)
    else:
        intr = intrinsics['{0}_{1}_si{2}'.format(mm_prefix(pwidth),intr_base,pwidth)]

        body = '{0}_{1}_si{2}({3})'.format(mm_prefix(pwidth),intr_base,pwidth,ideal_args(swidth,pwidth,2))
        
        if intr['tech'] not in IMPLIED_REQ[type_req]:
            assert swidth >= 32
            fallback = intr['tech'],'{0}_{1}_{2}(a.data,b.data)'.format(mm_prefix(pwidth),intr_base,suffix)
        
    print_mask_function(output,swidth,pwidth,name,2,body,fallback)

def get_eq_intr(intrinsics,pwidth,swidth):
    intr = intrinsics.get('{0}_cmpeq_epi{1}'.format(mm_prefix(pwidth),swidth))
    if intr: return intr,True
    
    return intrinsics['{0}_cmp_epi{1}'.format(mm_prefix(pwidth),swidth)],False

def not_mask_function(output,intrinsics,swidth,pwidth,type_req,suffix):
    fallback = None
    
    if pwidth == 512:
        body = avx512_mask_body(swidth,'not',1)
    else:
        intr,in_name = get_eq_intr(intrinsics,pwidth,swidth)

        body = '{0}_cmp{1}_epi{2}({3},{0}_setzero_si{4}(){5})'.format(
            mm_prefix(pwidth),
            ['','eq'][in_name],
            swidth,
            ideal_args(swidth,pwidth,1),
            pwidth,
            [',_CMP_EQ_UQ',''][in_name])
        
        if intr['tech'] not in IMPLIED_REQ[type_req]:
            assert swidth >= 32
            fallback = [intr['tech'],'{0}_cmp{1}_{2}(a.data,a.data{3})'.format(
                mm_prefix(pwidth),
                ['','unord'][in_name],
                suffix,
                [',_CMP_UNORD_Q',''][in_name])]
        
    print_mask_function(output,swidth,pwidth,'operator!',1,body,fallback)

def xnor_mask_function(output,intrinsics,swidth,pwidth,type_req,suffix):
    fallback = None
    
    if pwidth == 512:
        body = avx512_mask_body(swidth,'xnor',2)
    else:
        intr,in_name = get_eq_intr(intrinsics,pwidth,swidth)
        
        body = 'mask{0}_v_{1}({2}_cmp{3}_epi{0}({4}{5}))'.format(
            swidth,
            pwidth,
            mm_prefix(pwidth),
            ['','eq'][in_name],
            ideal_args(swidth,pwidth,2),
            [',_CMP_EQ_UQ',''][in_name])
        
        if intr['tech'] not in IMPLIED_REQ[type_req]:
            assert swidth >= 32
            fallback = intr['tech'],'!l_xor(a,b)'
        
    print_mask_function(output,swidth,pwidth,'l_xnor',2,body,fallback,pwidth == 512)


def macroize(x):
    return ''.join(c if c.isalnum() else '_' for c in x)

def wrap_type(type,width):
    return '{0}_v_{1}'.format(type.name[0:-2] if type.name.endswith('_t') else type.name,width)

def base_type(type,width):
    suffix = ''
    if type is types['double']: suffix = 'd'
    elif type.float == INTEGER: suffix = 'i'
    return '__m{0}{1}'.format(width,suffix)

def ifdef_support(req,antireq=None,indent=0):
    indent = '    '*indent
    if req and antireq:
        return '{0}#if defined(SUPPORT_{1}) && !defined(SUPPORT_{2})'.format(indent,macroize(req),macroize(antireq))

    extra = ''
    if not req:
        assert antireq
        extra = 'n'
        req = antireq
    return '{0}#if{1}def SUPPORT_{2}'.format(indent,extra,macroize(req))

def print_dependent(d_list,file,indent=0):
    for req,mt_list in d_list.items():
        print(ifdef_support(req[0],req[1],indent=indent),file=file)
            
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
mask_search = re.compile(r'^\s*//\s*\[\[\[mask-(\d+)-(\d+)\]\]\]\s*$')


def generate(data_file,template_file,output_file):
    with open(data_file,'r') as input:
        data = json.load(input)

    intrinsics = {}
    for item in data:
        intrinsics[item['name']] = item

    del data


    template = open(template_file,'r')
    output = open(output_file,'w')

    print('// THIS FILE WAS GENERATED BY generate_simd.py. DO NOT EDIT IT DIRECTLY.\n\n',file=output)

    for line in template:
        m = mask_search.match(line)
        if m:
            swidth = int(m.group(1),10)
            pwidth = int(m.group(2),10)
            
            if pwidth == 512:
                type_req = 'AVX-512'
            elif pwidth == 256:
                type_req = 'AVX2' if swidth < 32 else 'AVX'
            else:
                assert pwidth == 128
                type_req = 'SSE' if swidth == 32 else 'SSE2'
            
            if swidth == 64:
                suffix = 'pd'
            elif swidth == 32:
                suffix = 'ps'
            else:
                suffix = 'epi{0}'.format(swidth)
            
            common_mask_function(output,intrinsics,swidth,pwidth,type_req,suffix,'operator&&','and')
            common_mask_function(output,intrinsics,swidth,pwidth,type_req,suffix,'operator||','or')
            not_mask_function(output,intrinsics,swidth,pwidth,type_req,suffix)
            common_mask_function(output,intrinsics,swidth,pwidth,type_req,suffix,'l_andn','andn' if pwidth == 512 else 'andnot')
            common_mask_function(output,intrinsics,swidth,pwidth,type_req,suffix,'l_xor','xor')
            xnor_mask_function(output,intrinsics,swidth,pwidth,type_req,suffix)
            continue
        
        m = meth_search.match(line)
        if m:
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
                    if mt.supported(type,width):
                        intr = intrinsics.get(mt.intrinsic(type,width))
                        if intr:
                            req = intr['tech']
                            antireq = mt.antirequisite(type,width,intrinsics)

                            if antireq is None and req in IMPLIED_REQ[type_req]:
                                print(mt.output(type,width,intr),file=output)
                            elif antireq is None or antireq not in IMPLIED_REQ[type_req]:
                                rs = (None if req in IMPLIED_REQ[type_req] else req,antireq)
                                if rs not in dependent:
                                    dependent[rs] = []
                                dependent[rs].append((mt,intr,type,width))
                        
                print_dependent(dependent,output,1)
            continue

        m = func_search.match(line)
        if m:
            dependent = {}
            for type in types.values():
                for width in widths:
                    for ft in function_transforms:
                        if not ft.supported(type,width): continue
                        
                        intr = intrinsics.get(ft.intrinsic(type,width))
                        if intr:
                            rs = (intr['tech'],ft.antirequisite(type,width,intrinsics))
                            if rs not in dependent:
                                dependent[rs] = []
                            dependent[rs].append((ft,intr,type,width))
                        
            print_dependent(dependent,output)
            continue

        print(line,end='',file=output)
                
    template.close()
    output.close()

