"""generates wrapper methods for several different vector types, which would be
tedious to do by hand"""

# must work in CPython 2.7 and 3.0+

from __future__ import print_function

import os
import errno
import os.path
import tempfile
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


binary_op = """        FORCE_INLINE {wrap_type} operator{extra}(const {wrap_type} &b) const {{
            return {intr}(this->data.p,b.data.p);
        }}
        FORCE_INLINE {wrap_type} &operator{extra}=(const {wrap_type} &b) {{
            this->data.p = {intr}(this->data.p,b.data.p);
            return *this;
        }}
"""
cmp_op = """        FORCE_INLINE mask operator{extra[0]}(const {wrap_type} &b) const {{
            return mask({intr}(this->data.p,b.data.p{extra[1]}));
        }}
"""
unary_meth = """        FORCE_INLINE {wrap_type} {extra}() const {{
            return {intr}(this->data.p);
        }}
"""
mask_meth = """        FORCE_INLINE {wrap_type} {extra}(const mask &m) const {{
            return {intr}(m.data,this->data.p);
        }}
"""
pseudo_maskz_move = """        FORCE_INLINE {wrap_type} {extra}(const mask &m) const {{
            return {intr}(impl::cast<{base_type}>(m.data),this->data.p);
        }}
"""

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

broadcast_meth = """        static FORCE_INLINE {wrap_type} repeat(const single<{item_type}> &x) {{
            return {intr}(x.data);
        }}
"""
shuffle_meth = """        static FORCE_INLINE {wrap_type} repeat(const single<{item_type}> &x) {{
            return {intr}(x.data{extra},0);
        }}
"""

setzero_meth = """        static FORCE_INLINE {wrap_type} {extra}() {{
            return {intr}();
        }}
"""

cast_meth = """        explicit FORCE_INLINE {wrap_type}(const {wrap_type_src} &b) {{
            this->data.p = {intr}(b.data.p);
        }}
"""
cast_meth_dec = """        explicit {wrap_type}(const {wrap_type_src} &b);
"""
cast_meth_def = """    FORCE_INLINE {wrap_type}::{wrap_type}(const {wrap_type_src} &b) {{
        this->data.p = {intr}(b.data.p);
    }}
"""

binary_func = """    FORCE_INLINE {wrap_type} {extra}(const {wrap_type} &a,const {wrap_type} &b) {{
        return {intr}(a.data.p,b.data.p);
    }}
"""
cmp_func = """    FORCE_INLINE {wrap_type}::mask {extra[0]}(const {wrap_type} &a,const {wrap_type} &b) {{
        return {wrap_type}::mask({intr}(a.data.p,b.data.p{extra[1]}));
    }}
"""

testz_func = """    FORCE_INLINE int {extra}(const {wrap_type} &a,const {wrap_type} &b) {{
        return {intr}(a.data.p,b.data.p);
    }}
    FORCE_INLINE int {extra}(const {wrap_type} &a) {{
        return {intr}(a.data.p,a.data.p);
    }}
"""

def print_mask_function(output,swidth,pwidth,func,args,body,fallback,explicit_cast=True):
    print('    FORCE_INLINE mask{0}_v_{1} {2}({3}) {{\n        return '.format(
        swidth,
        pwidth,
        func,
        ','.join('const mask{0}_v_{1} &{2}'.format(swidth,pwidth,chr(ord('a')+i)) for i in range(args))),end='',file=output)
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

def alt_int_intrinsic_name(type,width,base):
    if type.float == FLOAT: return generic_intrinsic_name(type,width,base)
    
    return '{0}_{1}_si{2}'.format(mm_prefix(width),base,width)

def letter_suffix(type):
    if type is types['double']: return 'pd'
    elif type is types['float']: return 'ps'
    return 'si'

def lane_code(type,lwidth):
    return '{0}{1}x{2}'.format(
        'f' if type.float == FLOAT else 'i',
        type.size,
        lwidth//type.size)

def int_log2(x):
    r = 0
    x >>= 1
    while x:
        r += 1
        x >>= 1
    return r

class FTransform(object):
    def __init__(self,base_name,template=None,extra=None,support=FLOAT|INTEGER,prefer=None,scalar=False):
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
                if r: return r['cpuid']
            return self.prefer.antirequisite(type,width,intrinsics)
        return frozenset()
    
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
        # GCC doesn't seem to have _mm_broadcastsd_pd, but _mm_broadcastsd_pd
        # is an alias for _mm_movedup_pd which GCC does have
        if type is types['double'] and width == 128: return '_mm_movedup_pd'
        
        return generic_intrinsic_name(type,width,'broadcast' + {'int8_t':'b','int16_t':'w','int32_t':'d','int64_t':'q','float':'ss','double':'sd'}[type.name]) 

class ReduceTransform(FTransform):
    def supported(self,type,width):
        return type.size >= 32
    
    @staticmethod
    def shuffle1(type):
        return '_MM_SHUFFLE2(0,1)' if type.size == 64 else '_MM_SHUFFLE(0,0,0,1)'
    
    def reduce_to_128(self,type,width,base):
        if width == 512:
            r = '''            auto tmp = {0}(data.p,_mm512_shuffle_{2}(data.p,data.p,_MM_SHUFFLE(0,0,3,2)));
            auto r = _mm512_cast{1}512_{1}128({0}(tmp,_mm512_shuffle_{2}(tmp,tmp,_MM_SHUFFLE(0,0,0,1))));
'''.format(
                generic_intrinsic_name(type,512,base),
                letter_suffix(type),
                lane_code(type,128))
            return r,'r',''
        
        if width == 256:
            r = '            auto r = {0}(_mm256_cast{1}256_{1}128(data.p),{2}(data.p,{3}));\n'.format(
                generic_intrinsic_name(type,128,base),
                letter_suffix(type),
                alt_int_intrinsic_name(type,256,'extractf128'),
                self.shuffle1(type))
            return r,'r',''
        
        assert width == 128
        return '','data.p','auto '
    
    def output(self,type,width,intr):
        inter,invar,r_decl = self.reduce_to_128(type,width,self.base_name)
        
        action = lambda scalar: generic_intrinsic_name(type,128,self.base_name,type.float == FLOAT and scalar)
        
        assert type.size == 32 or type.size == 64
        if type.size == 32:
            # the floating-point shuffles operate on two vectors while the
            # integer shuffles operate on one
            extra = lambda: invar + ',' if type.float == FLOAT else ''
        
            shuf = generic_intrinsic_name(type,128,'shuffle')
            inter += '            {0}r = {1}({2},{3}({2},{4}_MM_SHUFFLE(0,0,3,2)));\n'.format(
                r_decl,action(False),invar,shuf,extra())
            invar = 'r'
            shuf = '{0}(r,{1}_MM_SHUFFLE(0,0,0,1))'.format(shuf,extra())
        elif type.float == FLOAT:
            shuf = '{0}({1},{1},_MM_SHUFFLE2(0,1))'.format(generic_intrinsic_name(type,128,'shuffle'),invar)
        else:
            shuf = '_mm_srli_si128({0},8)'.format(invar)
        
        return '''        FORCE_INLINE single<{0}> {1}() const {{
{2}            return {3}({4},{5});
        }}
'''.format(type.name,self.extra,inter,action(True),invar,shuf)

class ReduceAddTransform(ReduceTransform):
    def __init__(self,prefer=None):
        super(ReduceAddTransform,self).__init__('hadd',prefer=prefer)
    
    def intrinsic(self,type,width):
        return super(ReduceAddTransform,self).intrinsic(type,128)
    
    def output(self,type,width,intr):
        inter,invar,r_decl = self.reduce_to_128(type,width,'add')
        
        assert type.size == 32 or type.size == 64
        if type.size == 32:
            inter += '            {0}r = {1}({2},{2});\n'.format(r_decl,intr['name'],invar)
            invar = 'r'
        
        return '''        FORCE_INLINE single<{0}> reduce_add() const {{
{1}            return {2}({3},{3});
        }}
'''.format(type.name,inter,intr['name'],invar)

class AltIFTransform(FTransform):
    def intrinsic(self,type,width):
        return alt_int_intrinsic_name(type,width,self.base_name)

class CmpFTransform(FTransform):
    def __init__(self,op,template,code,ord):
        self.op = op
        self.code = code
        self.ord = bool(ord)
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
            arg3 = ',_CMP_{0}_{1}Q'.format(self.code.upper(),'UO'[self.ord])

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
    
    def output(self,type,width,intr):
        r = self.template.format(
            intr=intr['name'],
            iargs=intr['parameters'],
            wrap_type=wrap_type(type,width),
            base_type=base_type(type,width),
            item_type=type.name,
            extra=self.extra)
        
        if intr['name'][-1] == 'x':
            r = ifdef_support(['X64'],indent=1) + '\n' + r + '    #endif\n'
        
        return r

class BlendTransformBase(FTransform):
    def __init__(self):
        super(BlendTransformBase,self).__init__(None,self.template)
    
    def intrinsic(self,type,width):
        base = 'mask_blend'
        if width < 512:
            base = 'blendv'
            if type.float == INTEGER:
                # The only integer versions of the blendv instructions operate
                # on 8bit types, but as long as each value in the mask is all
                # ones or all zeros, the result is the same for any integer
                # size.
                type = types['int8_t']
        return generic_intrinsic_name(type,width,base)
    
    def output(self,type,width,intr):
        params = self.base_params
        mparam = 'm.data'
        if type.float == INTEGER and type.size >= 32 and width < 512:
            mparam = 'impl::cast<{0}>({1})'.format(base_type(type,width),mparam)
        
        if width < 512:
            params += ',' + mparam
        else:
            params = mparam + ',' + params
        
        return self.template.format(
            type.size,
            width,
            wrap_type(type,width),
            intr['name'],
            params)

class MaskSetTransform(BlendTransformBase):
    base_params = 'this->data.p,b.data.p'
    template = '''        FORCE_INLINE {2} &mask_set(mask{0}_v_{1} m,{2} b) {{
            this->data.p = {3}({4});
            return *this;
        }}
'''

class BlendTransform(BlendTransformBase):
    # parameters are reversed to match "A ? B : C" syntax
    base_params = 'b.data.p,a.data.p'
    template = '''    FORCE_INLINE {2} mask_blend(const mask{0}_v_{1} &m,const {2} &a,const {2} &b) {{
        return {3}({4});
    }}
'''

class CastTransform(object):
    def supported(self,type,width,src_width,inline):
        return not (width == src_width or
            (width < src_width and not inline) or
            ((width == 512 or src_width == 512) and type.size < 32))
    
    def intrinsic(self,type,width,src_width,inline):
        return '{0}_cast{1}{2}_{1}{3}'.format(
            mm_prefix(max(width,src_width)),
            letter_suffix(type),
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
        super(MixedFTransformAdapter,self).__init__('')
    
    def supported(self,type,width):
        return self.base.supported(type,width,self.src_width,True)
        
    def intrinsic(self,type,width):
        return self.base.intrinsic(type,width,self.src_width,True)
    
    def output(self,type,width,intr):
        return self.base.output(type,width,self.src_width,intr,True)


mixed_method_transforms = [
    CastTransform()]

reduce_add = ReduceAddTransform()
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
    CmpFTransform('==',cmp_op,'eq',True),
    CmpFTransform('!=',cmp_op,'neq',True),
    CmpFTransform('>',cmp_op,'gt',True),
    CmpFTransform('>=',cmp_op,'ge',True),
    CmpFTransform('<',cmp_op,'lt',True),
    CmpFTransform('<=',cmp_op,'le',True),
    FTransform('sqrt',unary_meth,support=FLOAT),
    FTransform('rsqrt',unary_meth,support=FLOAT),
    FTransform('abs',unary_meth,support=INTEGER), # GCC doesn't seem to have _mm512_abs_ps or _mm512_abs_pd
    FTransform('ceil',unary_meth,support=FLOAT),
    FTransform('floor',unary_meth,support=FLOAT),
    reduce_add,
    ReduceTransform('add',extra='reduce_add',prefer=reduce_add),
    ReduceTransform('max',extra='reduce_max'),
    ReduceTransform('min',extra='reduce_min'),
    AltIFTransform('load',load_meth),
    AltIFTransform('loadu',load_meth),
    AltIFTransform('store',store_meth),
    AltIFTransform('storeu',store_meth),
    repeat_a,
    ShuffleTransform(prefer=repeat_a),
    RepeatTransform('set1',set1_meth,'repeat'),
    maskz_mov,
    FTransform('and',pseudo_maskz_move,'zfilter',prefer=maskz_mov),
    
    # if maskz_mov is available, use the fallback nzfilter (see simd.hpp.in)
    # method instead this
    FTransform('andnot',pseudo_maskz_move,'nzfilter',prefer=maskz_mov),
    
    AltIFTransform('setzero',setzero_meth,'zeros'),
    MaskSetTransform()]

function_transforms = [
    FTransform('max',binary_func),
    FTransform('min',binary_func),
    AltIFTransform('andnot',binary_func,'and_not'),
    AltIFTransform('testz',testz_func,'test_z'),
    CmpFTransform('cmp_nlt',cmp_func,'nlt',False),
    CmpFTransform('cmp_nle',cmp_func,'nle',False),
    CmpFTransform('cmp_ngt',cmp_func,'ngt',False),
    CmpFTransform('cmp_nge',cmp_func,'nge',False),
    BlendTransform()]


def data_args(args):
    return ['{0}.data'.format(chr(ord('a')+i)) for i in range(args)]

def ideal_args(swidth,pwidth,args):
    if swidth >= 32: args = ['impl::cast<__m{0}i>({1})'.format(pwidth,a) for a in args]
    return ','.join(args)

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

        body = '{0}_{1}_si{2}({3})'.format(mm_prefix(pwidth),intr_base,pwidth,ideal_args(swidth,pwidth,data_args(2)))
        
        req = intr['cpuid'] - type_req
        if req:
            assert swidth >= 32
            fallback = req,'{0}_{1}_{2}(a.data,b.data)'.format(mm_prefix(pwidth),intr_base,suffix)
        
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
            ideal_args(swidth,pwidth,data_args(1)),
            pwidth,
            [',_CMP_EQ_UQ',''][in_name])
        
        req = intr['cpuid'] - type_req
        if req:
            assert swidth >= 32
            fallback = [req,'{0}_cmp{1}_{2}(a.data,a.data{3})'.format(
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
            ideal_args(swidth,pwidth,data_args(2)),
            [',_CMP_EQ_UQ',''][in_name])
        
        req = intr['cpuid'] - type_req
        if req:
            assert swidth >= 32
            fallback = req,'!l_xor(a,b)'
        
    print_mask_function(output,swidth,pwidth,'l_xnor',2,body,fallback,pwidth == 512)

def mask_method_body(intr,intr_base,swidth,pwidth,type_req,args,suffix):
    body = '{0}({1})'.format(intr['name'],ideal_args(swidth,pwidth,args))
    
    req = intr['cpuid'] - type_req
    if req:
        assert swidth >= 32
        body = '''
    {0}
                {1}
    #else
                {2}_{3}_{4}({5})
    #endif
                '''.format(ifdef_support(req),body,mm_prefix(pwidth),intr_base,suffix,','.join(args))

    return body

def setzero_mask_method(output,intrinsics,swidth,pwidth,type_req,suffix):
    print('        static FORCE_INLINE mask{0}_v_{1} zeros() {{'.format(swidth,pwidth),sep='',end='',file=output)
    
    if pwidth == 512:
        print(' return mask{0}_v_{1}(0); '.format(swidth,pwidth),sep='',end='',file=output)
    else:
        intr = intrinsics['{0}_setzero_si{1}'.format(mm_prefix(pwidth),pwidth)]
    
        print(
            '\n            return mask{0}_v_{1}('.format(swidth,pwidth),
            mask_method_body(intr,'setzero',swidth,pwidth,type_req,[],suffix),
            ');\n        ',
            sep='',end='',file=output)

    print('}\n',sep='',file=output)

def any_mask_method(output,intrinsics,swidth,pwidth,type_req,suffix):
    # the default method is fine for AVX512
    if pwidth != 512:
        intr = intrinsics['{0}_testz_si{1}'.format(mm_prefix(pwidth),pwidth)]
        req = intr['cpuid'] - type_req
        
        if req:
            print(ifdef_support(req,indent=1),sep='',file=output)
            type_req = req | type_req
        
        print(
            '        FORCE_INLINE bool any() const {{\n            return {0} == 0;\n        }}'.format(
                mask_method_body(intr,'testz',swidth,pwidth,type_req,['data','data'],suffix)),
            sep='',file=output)
        
        if req: print('    #endif\n',sep='',file=output)


def macroize(x):
    return ''.join(c if c.isalnum() else '_' for c in x)

def wrap_type(type,width):
    return '{0}_v_{1}'.format(type.name[0:-2] if type.name.endswith('_t') else type.name,width)

def base_type(type,width):
    suffix = ''
    if type is types['double']: suffix = 'd'
    elif type.float == INTEGER: suffix = 'i'
    return '__m{0}{1}'.format(width,suffix)

def ifdef_support(req,antireq=frozenset(),indent=0):
    assert req or antireq
    
    indent = '    '*indent
    if len(req) + len(antireq) > 1:
        parts = ['defined(SUPPORT_{0})'.format(macroize(r)) for r in req]
        parts.extend('!defined(SUPPORT_{0})'.format(macroize(r)) for r in antireq)
        return '{0}#if {1}'.format(indent,' && '.join(parts))

    extra = ''
    if not req:
        assert antireq
        extra = 'n'
        req = antireq
    return '{0}#if{1}def SUPPORT_{2}'.format(indent,extra,macroize(list(req)[0]))

def print_dependent(d_list,file,indent=0):
    for req,mt_list in d_list.items():
        print(ifdef_support(req[0],req[1],indent=indent),file=file)
            
        for trans,intr,type,width in mt_list:
            print(trans.output(type,width,intr),file=file)
            
        print('    ' * indent + '#endif',file=file)


IMPLIED_REQ = {}
rs = set()
for r in ['SSE','SSE2','SSE3','SSSE3','SSE4.1','SSE4.2','AVX','AVX2','AVX512F']:
    rs.add(r)
    IMPLIED_REQ[r] = frozenset(rs)
for r in ['AVX512BW','AVX512CD','AVX512DQ','AVX512ER','AVX512IFMA52','AVX512PF','AVX512VBMI','AVX512VL']:
    IMPLIED_REQ[r] = frozenset(rs.union([r]))
del rs


meth_search = re.compile(r'^\s*//\s*\[\[\[(\w+)-(\d+)(?:-(\d+)|)\]\]\]\s*$')
func_search = re.compile(r'^\s*//\s*\[\[\[functions\]\]\]\s*$')
mask_search = re.compile(r'^\s*//\s*\[\[\[mask-(\d+)-(\d+)(-outer|)\]\]\]\s*$')


def generate(data_file,template_file,output_file):
    with open(data_file,'r') as input:
        intrinsics = json.load(input)
    
    for name,intr in intrinsics.items():
        intr['cpuid'] = frozenset(intr['cpuid'])
        intr['name'] = name

    template = open(template_file,'r')
    output = tempfile.NamedTemporaryFile(mode='w',dir=os.path.dirname(output_file),delete=False)
    
    try:
        print('// THIS FILE WAS GENERATED BY generate_simd.py. DO NOT EDIT IT DIRECTLY.\n\n',file=output)

        for line in template:
            outer = True
            m = mask_search.match(line)
            if m:
                swidth = int(m.group(1),10)
                pwidth = int(m.group(2),10)
                outer = bool(m.group(3))
                
                if pwidth == 512:
                    type_req = 'AVX512F'
                elif pwidth == 256:
                    type_req = 'AVX2' if swidth < 32 else 'AVX'
                else:
                    assert pwidth == 128
                    type_req = 'SSE' if swidth == 32 else 'SSE2'
                
                type_req = IMPLIED_REQ[type_req]
                
                if swidth == 64:
                    suffix = 'pd'
                elif swidth == 32:
                    suffix = 'ps'
                else:
                    suffix = 'epi{0}'.format(swidth)
                
                if outer:
                    common_mask_function(output.file,intrinsics,swidth,pwidth,type_req,suffix,'operator&&','and')
                    common_mask_function(output.file,intrinsics,swidth,pwidth,type_req,suffix,'operator||','or')
                    not_mask_function(output.file,intrinsics,swidth,pwidth,type_req,suffix)
                    common_mask_function(output.file,intrinsics,swidth,pwidth,type_req,suffix,'l_andn','andn' if pwidth == 512 else 'andnot')
                    common_mask_function(output.file,intrinsics,swidth,pwidth,type_req,suffix,'l_xor','xor')
                    xnor_mask_function(output.file,intrinsics,swidth,pwidth,type_req,suffix)
                else:
                    setzero_mask_method(output.file,intrinsics,swidth,pwidth,type_req,suffix)
                    any_mask_method(output.file,intrinsics,swidth,pwidth,type_req,suffix)
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
                            print(mt.output(type,width,width2,intr,False),file=output.file)
                else:
                    if width == 512:
                        assert type.size >= 32
                        type_req = 'AVX512F'
                    elif type is types['float']:
                        type_req = {128:'SSE',256:'AVX'}[width]
                    elif type is types['double']:
                        type_req = {128:'SSE2',256:'AVX'}[width]
                    elif width == 256:
                        type_req = 'AVX2'
                    else:
                        assert width == 128
                        type_req = 'SSE2'
                    
                    type_req = IMPLIED_REQ[type_req]
                    
                    dependent = {}
                    for mt in method_transforms:
                        if mt.supported(type,width):
                            intr = intrinsics.get(mt.intrinsic(type,width))
                            if intr:
                                req = intr['cpuid'] - type_req
                                antireq = mt.antirequisite(type,width,intrinsics)

                                if not (req or antireq):
                                    print(mt.output(type,width,intr),file=output.file)
                                elif antireq.isdisjoint(type_req):
                                    rs = (req,antireq)
                                    if rs not in dependent:
                                        dependent[rs] = []
                                    dependent[rs].append((mt,intr,type,width))
                            
                    print_dependent(dependent,output.file,1)
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
                                rs = (intr['cpuid'],ft.antirequisite(type,width,intrinsics))
                                if rs not in dependent:
                                    dependent[rs] = []
                                dependent[rs].append((ft,intr,type,width))
                            
                print_dependent(dependent,output.file)
                continue

            print(line,end='',file=output.file)
    except:
        output.file.close()
        os.remove(output.name)
        raise
    
    template.close()
    output.file.close()
    try:
        os.remove(output_file)
    except OSError as e:
        if e.errno != errno.ENOENT: raise
    os.rename(output.name,output_file)

