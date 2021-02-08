"""generates wrapper methods for several different vector types, which would be
tedious to do by hand"""

import json
import re
import sys
import itertools
import os.path
import io
import ast

from collections import namedtuple,defaultdict
from functools import partial
from distutils import log
from distutils.dep_util import newer

FLOAT = 1
INTEGER = 2

t_widths = [128,256,512]
m_widths = [8,16,32,64]

CPP_KEYWORDS = {
'alignas',
'alignof',
'and',
'and_eq',
'asm',
'atomic_cancel',
'atomic_commit',
'atomic_noexcept',
'auto',
'bitand',
'bitor',
'bool',
'break',
'case',
'catch',
'char',
'char8_t',
'char16_t',
'char32_t',
'class',
'compl',
'concept',
'const',
'consteval',
'constexpr',
'constinit',
'const_cast',
'continue',
'co_await',
'co_return',
'co_yield',
'decltype',
'default',
'delete',
'do',
'double',
'dynamic_cast',
'else',
'enum',
'explicit',
'export',
'extern',
'false',
'float',
'for',
'friend',
'goto',
'if',
'inline',
'int',
'long',
'mutable',
'namespace',
'new',
'noexcept',
'not',
'not_eq',
'nullptr',
'operator',
'or',
'or_eq',
'private',
'protected',
'public',
'reflexpr',
'register',
'reinterpret_cast',
'requires',
'return',
'short',
'signed',
'sizeof',
'static',
'static_assert',
'static_cast',
'struct',
'switch',
'synchronized',
'template',
'this',
'thread_local',
'throw',
'true',
'try',
'typedef',
'typeid',
'typename',
'union',
'unsigned',
'using',
'virtual',
'void',
'volatile',
'wchar_t',
'while',
'xor',
'xor_eq'}

CType = namedtuple('CType','name size code float')
types = {}

for t in [
    ('int8_t',8,'i8',INTEGER),
    ('uint8_t',8,'u8',INTEGER),
    ('int16_t',16,'i16',INTEGER),
    ('uint16_t',16,'u16',INTEGER),
    ('int32_t',32,'i32',INTEGER),
    ('uint32_t',32,'u32',INTEGER),
    ('int64_t',64,'i64',INTEGER),
    ('uint64_t',64,'u64',INTEGER),
    ('float',32,'s',FLOAT),
    ('double',64,'d',FLOAT)]:
    types[t[0]] = CType(*t)

wrapped_types = [
    types['int8_t'],
    types['int16_t'],
    types['int32_t'],
    types['int64_t'],
    types['float'],
    types['double']]

v_types = {}
for t in [
    ('__m128i',128,'si128',INTEGER),
    ('__m256i',256,'si256',INTEGER),
    ('__m512i',512,'si512',INTEGER),
    ('__m128',128,'ps',FLOAT),
    ('__m256',256,'ps',FLOAT),
    ('__m512',512,'ps',FLOAT),
    ('__m128d',128,'pd',FLOAT),
    ('__m256d',256,'pd',FLOAT),
    ('__m512d',512,'pd',FLOAT)]:
    v_types[t[0]] = CType(*t)

m_types = {}
for t in [
    ('__mmask8',8,'mask8',INTEGER),
    ('__mmask16',16,'mask16',INTEGER),
    ('__mmask32',32,'mask32',INTEGER),
    ('__mmask64',64,'mask64',INTEGER)]:
    m_types[t[0]] = CType(*t)

all_types = dict(types)
all_types.update(v_types)
all_types.update(m_types)

def mm_prefix(width):
    return '_mm{}'.format(width if width > 128 else '')

def mm_typecode(type,width,scalar):
    return '{}{}{}'.format(
        '' if width == 64 or type.float == FLOAT or scalar else 'e',
        's' if scalar else 'p',
        type.code)

def generic_intrinsic_name(type,width,base,scalar=False):
    return '{}_{}_{}'.format(
        mm_prefix(width),
        base,
        mm_typecode(type,width,scalar))

def rename_if_keyword(x):
    return x + '_' if x in CPP_KEYWORDS else x

def support_test_start(cpuid,file):
    print('#if ' + ' && '.join('defined(SUPPORT_{})'.format(req.replace('.','_')) for req in cpuid),file=file)

def support_test_end(cpuid,file):
    print('#endif',file=file)


def name_normal(type,width,base,scalar):
    yield base,generic_intrinsic_name(type,width,base,scalar)

def name_masked(type,width,base,scalar):
    yield base,generic_intrinsic_name(type,width,base,scalar)
    yield 'mask_'+base,generic_intrinsic_name(type,width,'mask_'+base,scalar)

def name_mask_and_maskz(type,width,base,scalar):
    yield base,generic_intrinsic_name(type,width,base,scalar)
    yield 'mask_'+base,generic_intrinsic_name(type,width,'mask_'+base,scalar)
    yield 'maskz_'+base,generic_intrinsic_name(type,width,'maskz_'+base,scalar)

def name_mask_and_maskz_x(type,width,base,scalar):
    iname = generic_intrinsic_name(type,width,base,scalar)
    if width < 512 and type is types['int64_t']:
        iname += 'x'
    yield base,iname
    yield 'mask_'+base,generic_intrinsic_name(type,width,'mask_'+base,scalar)
    yield 'maskz_'+base,generic_intrinsic_name(type,width,'maskz_'+base,scalar)

BROADCAST_SUFFIXES = {'int8_t':'b','int16_t':'w','int32_t':'d','int64_t':'q','float':'ss','double':'sd'}
def name_broadcast(type,width,base,scalar):
    if scalar or type.name not in BROADCAST_SUFFIXES: return
    suffixed = base + BROADCAST_SUFFIXES[type.name]
    yield base,generic_intrinsic_name(type,width,suffixed,False)
    yield 'mask_'+base,generic_intrinsic_name(type,width,'mask_'+suffixed)
    yield 'maskz_'+base,generic_intrinsic_name(type,width,'maskz_'+suffixed)

def name_cmp(type,width,base,scalar):
    for bname,iname in name_masked(type,width,base,scalar):
        yield bname,iname
        yield bname+'_mask',iname+'_mask'

def cvt_intr_name(type1,width1,type2,width2,scalar):
    return generic_intrinsic_name(type2,max(width1,width2),'cvt'+mm_typecode(type1,width1,scalar),scalar)

INTR_OPS = [
    # 2intersect
    # 4dpwssd, mask_4dpwssd, maskz_4dpwssd
    # 4dpwssds, mask_4dpwssds, maskz_4dpwssds
    # 4fmadd, mask_4fmadd, maskz_4fmadd
    # 4fnmadd, mask_4fnmadd, maskz_4fnmadd
    ('abs',name_mask_and_maskz),
    ('add',name_mask_and_maskz),
    ('add_round',name_mask_and_maskz),
    ('adds',name_mask_and_maskz),
    ('alignr',name_mask_and_maskz),
    ('and',name_mask_and_maskz),
    ('andnot',name_mask_and_maskz),
    ('avg',name_mask_and_maskz),
    # bitshuffle, mask_bitshuffle
    ('blend',name_masked),
    ('blendv',name_normal),
    ('broadcast',name_broadcast),
    # broadcastmb, mask_broadcastmb, maskz_braodcastmb
    # broadcastmw, mask_broadcastmw, maskz_braodcastmw
    # broadcast_Xx2, mask_broadcast_Xx2, maskz_braodcast_Xx2
    # broadcast_Xx4, mask_broadcast_Xx4, maskz_braodcast_Xx4
    # broadcast_Xx8, mask_broadcast_Xx8, maskz_braodcast_Xx8
    # bslli
    # bsrli
    # cast
    ('cmp',name_cmp),
    ('cmp_round',name_cmp),
    ('cmpeq',name_cmp),
    ('cmpneq',name_cmp),
    ('cmpgt',name_cmp),
    ('cmpge',name_cmp),
    ('cmplt',name_cmp),
    ('cmple',name_cmp),
    ('cmpngt',name_cmp),
    ('cmpnge',name_cmp),
    ('cmpnlt',name_cmp),
    ('cmpnle',name_cmp),
    # cmpord, mask_cmpord
    # cmpunord, mask_cmpunord
    # comieq
    # comige
    # comigt
    # comile
    # comilt
    # comineq
    # comi_round
    # mask_compress, maskz_compress
    # mask_compressstoreu
    # conflict, mask_conflict, maskz_conflict
    # cvtX, mask_cvtX, maskz_cvtX
    # cvt_roundX, mask_cvt_roundX, maskz_cvt_roundX
    # mask_cvtX_storeu
    # cvttX, mask_cvttX, maskz_cvttX
    # cvtt_roundX, mask_cvtt_roundX, maskz_cvtt_roundX
    # dbsad, mask_dbsad, maskz_dbsad
    ('div',name_mask_and_maskz),
    ('div_round',name_mask_and_maskz),
    # dpX, mask_dpX, maskz_dpX
    # exp2a23, mask_exp2a23, maskz_exp2a23
    # mask_expand, maskz_expand
    # mask_expandloadu, maskz_expandloadu
    # extractX, mask_extractX, maskz_extractX
    # fixupimm, mask_fixupimm, maskz_fixupimm
    # fixupimm_round, mask_fixupimm_round, maskz_fixupimm_round
    # fmadd, mask_fmadd, mask3_fmadd, maskz_fmadd
    # fmadd_round, mask_fmadd_round, mask3_fmadd_round, maskz_fmadd_round
    # fmaddsub, mask_fmaddsub, mask3_fmaddsub, maskz_fmaddsub
    # fmaddsub_round, mask_fmaddsub_round, mask3_fmaddsub_round, maskz_fmaddsub_round
    # fmsub, mask_fmsub, mask3_fmsub, maskz_fmsub
    # fmsub_round, mask_fmsub_round, mask3_fmsub_round, maskz_fmsub_round
    # fmsubadd, mask_fmsubadd, mask3_fmsubadd, maskz_fmsubadd
    # fmsubadd_round, mask_fmsubadd_round, mask3_fmsubadd_round, maskz_fmsubadd_round
    # fnmadd, mask_fnmadd, mask3_fnmadd, maskz_fnmadd
    # fnmadd_round, mask_fnmadd_round, mask3_fnmadd_round, maskz_fnmadd_round
    # fnmsub, mask_fnmsub, mask3_fnmsub, maskz_fnmsub
    # fnmsub_round, mask_fnmsub_round, mask3_fnmsub_round, maskz_fnmsub_round
    # fpclass, mask_flclass
    # getexp, mask_getexp, maskz_getexp
    # getexp_round, mask_getexp_round, maskz_getexp_round
    # getmant, mask_getmant, maskz_getmant
    # getmant_round, mask_getmant_round, maskz_getmant_round
    ('hadd',name_normal),
    ('hadds',name_normal),
    ('hsub',name_normal),
    ('hsubs',name_normal),
    # i32extgather, mask_i32extgather
    # i32gather, mask_i32gather
    # i32loextgather, mask_i32loextgather
    # i32loextscatter, mask_i32loextscatter
    # i32logather, mask_i32logather
    # i32loscatter, mask_i32loscatter
    # i32scatter, mask_i32scatter
    # i64gather, mask_i64gather
    # i64scatter, mask_i64scatter
    # insertX, mask_insertX, maskz_insertX
    # int2mask

    # the non-mask *_epi32 and *_epi64 variants are missing in GCC as of version
    # 10.2.1
    #('load',name_mask_and_maskz),
    ('mask_load',name_normal),
    ('maskz_load',name_normal),

    # load(mask), mask_load(mask), maskz_load(mask)
    # load1
    # loadh
    # loadl
    # loadr

    # the non-mask *_epi32 and *_epi64 variants are missing in GCC as of version
    # 10.2.1
    # ('loadu',name_mask_and_maskz),
    ('mask_loadu',name_normal),
    ('maskz_loadu',name_normal),

    # lzcnt, mask_lzcnt, maskz_lzcnt
    # madd, mask_madd, maskz_madd
    # madd52hi, mask_madd52hi, maskz_madd52hi
    # madd52lo, mask_madd52lo, maskz_madd52lo
    # maddubs, mask_maddubs, maskz_maddubs
    # mask2int
    ('max',name_mask_and_maskz),
    # max_round, mask_max_round, maskz_max_round
    ('min',name_mask_and_maskz),
    # min_round, mask_min_round, maskz_min_round
    ('mov',name_mask_and_maskz),
    # move, mask_move, maskz_move
    # movedup, mask_movedup, maskz_movedup
    # movehdup, mask_movehdup, maskz_movehdup
    # movehl
    # moveldup, mask_moveldup, maskz_moveldup
    # movelh
    ('movemask',name_normal),
    # movepi16
    # movepi32
    # movepi64
    # movepi8
    # movm
    # movpi64
    ('mul',name_mask_and_maskz),
    ('mulhi',name_mask_and_maskz),
    ('mulhrs',name_mask_and_maskz),
    ('mullo',name_mask_and_maskz),
    ('mullox',name_mask_and_maskz),
    # multishift_epi64, mask_multishift_epi64, maskz_multishift_epi64
    ('or',name_mask_and_maskz),
    # packs, mask_packs, maskz_packs
    # packus, mask_packus, maskz_packus
    ('permute',name_mask_and_maskz),
    # permutevar, mask_permutevar, maskz_permutevar
    ('permutex',name_mask_and_maskz),
    # permutex2var, mask_permutex2var, maskz_permutex2var
    ('permutexvar',name_mask_and_maskz),
    # popcnt, mask_popcnt, maskz_popcnt
    # prefetch_i32extgather, mask_prefetch_i32extgather
    # prefetch_i32extscatter, mask_prefetch_i32extscatter
    # prefetch_i32gather, mask_prefetch_i32gather
    # prefetch_i32scatter, mask_prefetch_i32scatter
    # prefetch_i64gather, mask_prefetch_i64gather
    # prefetch_i64scatter, mask_prefetch_i64scatter
    # range, mask_range, maskz_range
    # rcp
    # rcp14, mask_rcp14, maskz_rcp14
    # rcp28, mask_rcp28, maskz_rcp28
    # rcp28_round, mask_rcp28_round, maskz_rcp28_round
    ('reduce',name_mask_and_maskz),
    ('reduce_add',name_masked),
    ('reduce_and',name_masked),
    ('reduce_max',name_masked),
    ('reduce_min',name_masked),
    ('reduce_mul',name_masked),
    ('reduce_or',name_masked),

    # not available int GCC as of version 10.2.1
    # ('reduce_round',name_mask_and_maskz),

    ('rol',name_mask_and_maskz),
    ('rolv',name_mask_and_maskz),
    ('ror',name_mask_and_maskz),
    ('rorv',name_mask_and_maskz),
    # roundscale, mask_roundscale, maskz_roundscale
    # roundscale_round, mask_roundscale_round, maskz_roundscale_round
    ('rsqrt',name_normal),
    ('rsqrt14',name_mask_and_maskz),
    ('rsqrt28',name_mask_and_maskz),
    # sad
    # scalef, mask_scalef, maskz_scalef
    # scalef_round, mask_scalef_round, maskz_scalef_round
    # set
    ('set1',name_mask_and_maskz_x),
    # set4,
    # setcsr
    # setr
    # shldi, mask_shldi, maskz_shldi
    # shldv, mask_shldv, maskz_shldv
    # shrdi, mask_shrdi, maskz_shrdi
    # shrdv, mask_shrdv, maskz_shrdv
    # shuffle, mask_shuffle, maskz_shuffle
    # shufflehi, mask_shufflehi, maskz_shufflehi
    # shufflelo, mask_shufflelo, maskz_shufflelo
    # sign
    # sll, mask_sll, maskz_sll
    # slli, mask_slli, maskz_slli
    # sllv, mask_sllv, maskz_sllv
    ('sqrt',name_mask_and_maskz),
    # sqrt_round, mask_sqrt_round, maskz_sqrt_round
    # sra, mask_sra, maskz_sra
    # srai, mask_srai, maskz_srai
    # srav, mask_srav, maskz_srav
    # srl, mask_srl, maskz_srl
    # srli, mask_srli, maskz_srli
    # srlv, mask_srlv, maskz_srlv

    # the non-mask *_epi32 variant is missing in GCC as of version 10.2.1
    # ('store',name_masked),
    ('mask_store',name_normal),

    # store(mask)
    # store1
    # storeh
    # storel
    # storer

    # the non-mask *_epi8 and *_epi16 variants are missing in GCC as of version
    # 10.2.1
    #('storeu',name_masked),
    ('mask_storeu',name_normal),

    # stream
    ('sub',name_mask_and_maskz),
    ('subs',name_mask_and_maskz),
    # ternarylogic, mask_ternarylogic, maskz_ternarylogic
    # test, mask_test
    # testn, mask_testn
    # ucomieq
    # ucomige
    # ucomigt
    # ucomile
    # ucomilt
    # ucomineq
    # undefined
    # unpackhi, mask_unpackhi, maskz_unpackhi
    # unpacklo, mask_unpacklo, maskz_unpacklo
    ('xor',name_mask_and_maskz),
    # zextsX

    #('set1',generic_intrinsic_name),
    #('maskz_mov',generic_intrinsic_name),
]

# These are intrinsics that only care about the type of the register, not the
# size of the elements. Some operations appear both here and INTR_OPS, due to
# having intrinsics with both forms and needing both (this happens with masked
# variants of these intrinsics).
REG_INTR_OPS = [
    'and',
    'andnot',
    # bslli
    # bsrli
    # castX
    # cvtsi32
    # cvtsi64
    # cvtsi64x
    # lddqu
    'load',
    'loadu',
    # maskmoveu
    'or',
    'setzero',
    # slli
    # srli
    'store',
    'storeu',
    # stream
    # stream_load
    'testc',
    'testnzc',
    'testz',
    # undefined
    'xor'
]

# these are intrinsics that operate on __mmaskN types
MASK_INTR_OPS = [
    ('cvt_from','_cvtmask{}_u32'),
    ('cvt_from','_cvtmask{}_u64'),
    ('cvt_to','_cvtu32_mask{}'),
    ('cvt_to','_cvtu64_mask{}'),
    ('kadd',None),
    ('kand',None),
    ('kandn',None),
    ('knot',None),
    ('kor',None),
    ('kortest','_kortest_mask{}_u8'),
    ('kortestc','_kortestc_mask{}_u8'),
    ('kortestz','_kortestz_mask{}_u8'),
    ('kshiftli',None),
    ('kshiftri',None),
    ('ktest','_ktest_mask{}_u8'),
    ('ktestc','_ktestc_mask{}_u8'),
    ('ktestz','_ktestz_mask{}_u8'),
    ('kxnor',None),
    ('kxor',None)
]

def _float_type(type):
    return type.float == FLOAT

def _suffix(type,size):
    return '_{}_{}'.format(type.code,type.size*size)

def _raw_type(type,size):
    if type is types['float']:
        code = ''
    elif type is types['double']:
        code = 'd'
    else:
        code = 'i'

    return v_types['__m{}{}'.format(type.size*size,code)]

def _mask_type(type,size):
    return m_types['__mmask{}'.format(max(8,size))]

def _intr_param(intr,index,type,size=1,scalar=None):
    if scalar is None: scalar = size == 1
    return intr.intrinsics[(intr.name,type,size,scalar)]['parameters'][index]['type']

INT_CMP_CONSTS = {
    'CMP_EQ' : '_MM_CMPINT_EQ',
    'CMP_NEQ' : '_MM_CMPINT_NE',
    'CMP_LT' : '_MM_CMPINT_LT',
    'CMP_LE' : '_MM_CMPINT_LE',
    'CMP_GT' : '_MM_CMPINT_NLE',
    'CMP_GE' : '_MM_CMPINT_NLT'}
FLOAT_CMP_CONSTS = {
    'CMP_EQ' : '_CMP_EQ_OQ',
    'CMP_NEQ' : '_CMP_NEQ_OQ',
    'CMP_LT' : '_CMP_LT_OQ',
    'CMP_LE' : '_CMP_LE_OQ',
    'CMP_GT' : '_CMP_GT_OQ',
    'CMP_GE' : '_CMP_GE_OQ',
    'CMP_NLT' : '_CMP_NLT_UQ',
    'CMP_NLE' : '_CMP_NLE_UQ'}
def _cmp_const(type,op):
    return [INT_CMP_CONSTS,FLOAT_CMP_CONSTS][type.float == FLOAT][op]

MIN_SUPPORT = {
    (types['float'],128) : frozenset(['SSE']),
    (types['double'],128) : frozenset(['SSE2']),
    (types['int64_t'],128) : frozenset(['SSE2']),
    (types['int32_t'],128) : frozenset(['SSE2']),
    (types['int16_t'],128) : frozenset(['SSE2']),
    (types['int8_t'],128) : frozenset(['SSE2']),
    (types['float'],256) : frozenset(['AVX']),
    (types['double'],256) : frozenset(['AVX']),
    (types['int64_t'],256) : frozenset(['AVX2']),
    (types['int32_t'],256) : frozenset(['AVX2']),
    (types['int16_t'],256) : frozenset(['AVX2']),
    (types['int8_t'],256) : frozenset(['AVX2']),
    (types['float'],512) : frozenset(['AVX512F']),
    (types['double'],512) : frozenset(['AVX512F']),
    (types['int64_t'],512) : frozenset(['AVX512F']),
    (types['int32_t'],512) : frozenset(['AVX512F']),
    (types['int16_t'],512) : frozenset(['AVX512BW']),
    (types['int8_t'],512) : frozenset(['AVX512BW'])}

class _Intr:
    __slots__ = 'intrinsics','name'

    def __init__(self,intrinsics,name):
        self.intrinsics = intrinsics
        self.name = name

    def __call__(self,type,size=1,scalar=None):
        if scalar is None: scalar = size == 1
        intr = self.intrinsics[(self.name,type,size,scalar)]
        #if not (intr['cpuid'] <= self.implied_flags):
        #    raise Exception('{} might not be available here'.format(intr['name']))
        return intr['name']

_V_CONFIGS = []
for w in t_widths:
    for t in wrapped_types:
        _V_CONFIGS.append((t,w//t.size))

def feature_guard(req):
    return ' && '.join('defined(SUPPORT_{})'.format(r.replace('.','_')) for r in req)

class TmplState:
    def __init__(self,intrinsics,intr_names,intr_by_name,outfile):
        self.implied_flag_stack = []
        self.implied_flags = frozenset()
        self.locals = {
            'have_avx512_mask':self._have_avx512_mask,
            'supported':self._supported,
            'float_type':_float_type,
            'suffix':_suffix,
            'raw_type':_raw_type,
            'mask_type':_mask_type,
            'min_support':self._min_support,
            'intr_param':_intr_param,
            'cmp_const':_cmp_const,
            '_iter_val_':None,
            'recur':(lambda f,*args: f(f,*args)),
            'V_CONFIGS': _V_CONFIGS,
            'types': types,
            'cvt_supported': self._cvt_supported,
            'cvt': self._cvt,
            'req_and': self._req_and,
            'AGGREGATE_START': self._aggregate_start,
            'AGGREGATE_END': self._aggregate_end}
        for name in intr_names:
            self.locals[name] = _Intr(intrinsics,name)
        self.intrinsics = intrinsics
        self.intr_by_name = intr_by_name
        self.outfile = outfile
        self._aggregate_output = None
        self._agg_f_stack_size = 0

    @staticmethod
    def _req_and(a,*bs):
        while bs:
            b,*bs = bs
            a_set = isinstance(a,(set,frozenset))
            b_set = isinstance(b,(set,frozenset))
            if a_set:
                if b_set:
                    a = a.union(b)
                else:
                    a = a if b else False
            elif b_set:
                a = b if a else False
            else:
                a = a and b
        return a

    def _check_agg_stack_exit(self):
        if self._aggregate_output is not None and self._agg_f_stack_size == len(self.implied_flag_stack):
            raise ValueError('aggregate output must end before exitting the scope where it started')

    def if_guard(self,flags):
        self.implied_flag_stack.append(flags)
        self.implied_flags = self.implied_flags.union(flags)

        if self._aggregate_output is None:
            print('#if '+feature_guard(flags),file=self.outfile)

    def elif_guard(self,flags):
        self._check_agg_stack_exit()

        self.implied_flag_stack[-1] = flags
        self.implied_flags = frozenset().union(*self.implied_flag_stack)

        if self._aggregate_output is None:
            print('#elif '+feature_guard(flags),file=self.outfile)
        else:
            raise TypeError('aggregate elif is not implemented yet')

    def else_guard(self):
        self._check_agg_stack_exit()

        self.implied_flag_stack[-1] = frozenset()
        self.implied_flags = frozenset().union(*self.implied_flag_stack)

        if self._aggregate_output is None:
            print('#else',file=self.outfile)
        else:
            raise TypeError('aggregate else is not implemented yet')

    def endif_guard(self):
        self._check_agg_stack_exit()

        self.implied_flag_stack.pop()
        self.implied_flags = frozenset().union(*self.implied_flag_stack)

        if self._aggregate_output is None:
            print('#endif',file=self.outfile)

    def _aggregate_start(self):
        """Aggregate the output.

        Instead of immediateley outputting lines, store them in buffers and
        group them by #if requirements.
        """
        if self._aggregate_output is not None:
            raise ValueError('aggregate output has already started')
        self._aggregate_output = defaultdict(io.StringIO)
        self._agg_f_stack_size = len(self.implied_flag_stack)

    def _aggregate_end(self):
        if self._aggregate_output is None:
            raise ValueError('aggregate output has not started')
        if self._agg_f_stack_size != len(self.implied_flag_stack):
            # this currently only tests flag guard scope
            raise ValueError('aggregate output must end in the same scope as the start')

        for req,buff in self._aggregate_output.items():
            if req:
                print('#if '+feature_guard(req),file=self.outfile)
                self.outfile.write(buff.getvalue())
                print('#endif',file=self.outfile)
            else:
                self.outfile.write(buff.getvalue())
        self._aggregate_output = None

    def write(self,value):
        if self._aggregate_output is not None:
            self._aggregate_output[self.implied_flags].write(value)
        else:
            self.outfile.write(value)

    def exec(self,code):
        # it's actually important that the same dictionary is passed as globals
        # and locals, since closures don't work with "compile" (and consequently
        # exec), but lambdas can still access global variables
        exec(code,self.locals,self.locals)

    def eval(self,code):
        # same thing applies as with "exec"
        return eval(code,self.locals,self.locals)

    def _have_avx512_mask(self,type,size):
        req = []
        if type.size <= 16:
            req.append('AVX512BW')
        else:
            req.append('AVX512F')
        if type.size*size < 512:
            req.append('AVX512VL')

        return frozenset(req) - self.implied_flags

    def _supported(self,idesc,type,size=1,scalar=None):
        if scalar is None: scalar = size == 1
        intr = self.intrinsics.get((idesc.name,type,size,scalar))
        if intr is None: return None
        return intr['cpuid'] - self.implied_flags

    def _min_support(self,type,size):
        return MIN_SUPPORT[(type,type.size*size)] - self.implied_flags

    def _cvt_supported(self,type1,size1,type2,size2,scalar=False):
        intr = self.intr_by_name.get(cvt_intr_name(type1,size1*type1.size,type2,size2*type2.size,scalar))
        if intr is None: return None
        return intr['cpuid'] - self.implied_flags

    def _cvt(self,type1,size1,type2,size2,scalar=False):
        return cvt_intr_name(type1,size1*type1.size,type2,size2*type2.size,scalar)

re_for = re.compile(r'^for\s+(.+)\s+in\s+(.+)$')
re_cfor = re.compile(r'^for\s+([^;]+)\s*;\s*([^;]+)\s*;\s*([^;]+)$')
re_if = re.compile(r'^(el|)if\s+(.*)$')

re_subs = re.compile(r'\$([a-zA-Z_][a-zA-Z0-9_]*|\{[^\}]*\})')

def syntax_error(linenum):
    raise Exception('syntax error on line {}'.format(linenum))

ParsedLine = namedtuple('ParsedLine',('type','data','linenum','filename'))

def tmpl_to_string(x):
    if isinstance(x,CType): return x.name
    return str(x)

class CppLine:
    def __init__(self,parts):
        self.parts = parts

    def __call__(self,state):
        for i,p in enumerate(self.parts):
            print(tmpl_to_string(state.eval(p)) if i % 2 else p,end='',file=state)
        #print(file=state)

class Condition:
    """An if-elif-else chain.

    If the condition evaluates to a set, it is treated as a set of requirements.
    If the set is empty, it is treated as a "true" value. Otherwise, instead of
    only emitting the block whose condition is true, the decision is delegated
    to the C++ preprocessor by emitting an #if-#elif-#else chain. Blocks whose
    conditions are always false, and blocks that follow blocks whose conditions
    are always true, are still omitted.
    """
    def __init__(self,ifs,else_):
        self.ifs = ifs
        self.else_ = else_

    def __call__(self,state):
        opened = False
        for cond,body in self.ifs:
            req = state.eval(cond)
            is_set = isinstance(req,(set,frozenset))
            if req or is_set:
                pushed = False
                if req and is_set:
                    (state.elif_guard if opened else state.if_guard)(req)
                    opened = True
                elif opened:
                    state.else_guard()
                for line in body:
                    line(state)
                if is_set != bool(req):
                    break
        else:
            if self.else_ is not None:
                if opened:
                    state.else_guard()
                for line in self.else_:
                    line(state)
        if opened:
            state.endif_guard()

class For:
    def __init__(self,varassign,iterexpr,body):
        self.varassign = varassign
        self.iterexpr = iterexpr
        self.body = body

    def __call__(self,state):
        for val in state.eval(self.iterexpr):
            state.locals['_iter_val_'] = val
            state.exec(self.varassign)
            for line in self.body:
                line(state)

class ClassicFor:
    def __init__(self,init,test,advance,body):
        self.init = init
        self.test = test
        self.advance = advance
        self.body = body

    def __call__(self,state):
        state.exec(self.init)
        while state.eval(self.test):
            for line in self.body:
                line(state)
            state.exec(self.advance)

class Expr:
    def __init__(self,val):
        self.val = val

    def __call__(self,state):
        state.exec(self.val)

def parse_line(line,linenum,filename):
    if not line.startswith('//$'):
        return ParsedLine('cpp',line,linenum,filename)

    line = line[3:].strip()

    if line == 'else':
        return ParsedLine('else',None,linenum,filename)
    if line == 'endif':
        return ParsedLine('endif',None,linenum,filename)
    if line == 'endfor':
        return ParsedLine('endfor',None,linenum,filename)
    if m := re_cfor.match(line):
        return ParsedLine('cfor',m.group(1,2,3),linenum,filename)
    if m := re_for.match(line):
        return ParsedLine('for',m.group(1,2),linenum,filename)
    if m := re_if.match(line):
        return ParsedLine('elif' if m.group(1) else 'if',m.group(2),linenum,filename)

    return ParsedLine('py',line,linenum,filename)

parse_type_descr = {
    'if' : '"if"',
    'elif' : '"elif"',
    'else' : '"else"',
    'for' : '"for"',
    'endfor' : '"endfor"',
    'py' : 'python statement',
    'EOF' : 'end of file'}

def unexpected_line(expect,line):
    msg = 'unexpected {}, expected {}'.format(
        parse_type_descr[line.type],
        expect)
    if line.type != 'EOF':
        msg += ' at line {}'.format(line.linenum)
    raise Exception(msg)

def compile_(code,mode,line):
    nodes = ast.parse(code,line.filename,mode)
    for n in ast.walk(nodes):
        n.lineno = line.linenum
        n.end_lineno = line.linenum
    return compile(nodes,line.filename,mode)

def collect_body():
    body = []
    while True:
        line = yield
        if line.type == 'cpp':
            parts = re_subs.split(line.data)
            for i,p in enumerate(parts):
                if i % 2:
                    parts[i] = compile_(p[1:-1] if p.startswith('{') else p,'eval',line)
            body.append(CppLine(parts))
        elif line.type == 'for':
            fline,fbody = yield from collect_body()
            if fline.type != 'endfor':
                unexpected_line('endfor',fline)
            body.append(For(
                compile_(line.data[0] + ' = _iter_val_','exec',line),
                compile_(line.data[1],'eval',line),
                fbody))
        elif line.type == 'cfor':
            fline,fbody = yield from collect_body()
            if fline.type != 'endfor':
                unexpected_line('endfor',fline)
            body.append(ClassicFor(
                compile_(line.data[0],'exec',line),
                compile_(line.data[1],'eval',line),
                compile_(line.data[2],'exec',line),
                fbody))
        elif line.type == 'if':
            cond = compile_(line.data,'eval',line)
            ifs = []
            else_ = None
            while True:
                line,ibody = yield from collect_body()
                if cond is None:
                    else_ = ibody

                    if line.type == 'endif':
                        break
                    else:
                        unexpected_line('"else"',line)
                else:
                    ifs.append((cond,ibody))
                    if line.type == 'elif':
                        cond = compile_(line.data,'eval',line)
                    elif line.type == 'else':
                        cond = None
                    elif line.type == 'endif':
                        break
                    else:
                        unexpected_line('"elif"/"else"/"endif"',line)

            body.append(Condition(ifs,else_))
        elif line.type == 'py':
            body.append(Expr(compile_(line.data,'exec',line)))
        else:
            return line,body


def handle_input_top():
    line,body = yield from collect_body()
    if line.type != 'EOF':
        raise Exception('unexpected {}'.format(parse_type_descr[line.type]))
    yield body

def generate(data_file,input_file,output_file):
    with open(data_file,'r') as input:
        intrinsics = json.load(input)

    for name,item in intrinsics.items():
        item['cpuid'] = frozenset(item['cpuid'])
        item['name'] = name

    funcs = {}
    supported = set()

    def add_intr(basename,name,t,w,s):
        intr = intrinsics.get(name)
        if intr:
            basename = rename_if_keyword(basename)
            funcs[(basename,t,w//t.size,s)] = intr
            supported.add(basename)

    for op,genname in INTR_OPS:
        for w in t_widths:
            for t in types.values():
                for s in [True,False]:
                    for basename,name in genname(t,w,op,s):
                        add_intr(basename,name,t,w,s)

    for op in REG_INTR_OPS:
        for t in v_types.values():
            add_intr(op,'{}_{}_{}'.format(mm_prefix(t.size),op,t.code),t,t.size,True)

    for op,tmpl in MASK_INTR_OPS:
        if tmpl is None: tmpl = '_{}_mask{{}}'.format(op)
        for t in m_types.values():
            intr = intrinsics.get(tmpl.format(t.size))
            if intr:
                basename = rename_if_keyword(op)
                funcs[(basename,t,1,True)] = intr
                supported.add(basename)

    handler = handle_input_top()
    next(handler)
    with open(input_file) as ifile:
        for i,line in enumerate(ifile):
            handler.send(parse_line(line,i+1,input_file))
    body = handler.send(ParsedLine('EOF',None,None,input_file))

    with open(output_file,'w') as ofile:
        state = TmplState(funcs,supported,intrinsics,ofile)
        for line in body:
            line(state)


def create_simd_hpp(base_dir,build_temp,force,dry_run):
    simd_out = os.path.join(build_temp,'simd.hpp')
    tmpl_in = os.path.join(base_dir,'src','simd.hpp.in')
    data_in = os.path.join(base_dir,'src','intrinsics.json')
    if force or newer(data_in,simd_out) or newer(tmpl_in,simd_out) or newer(__file__,simd_out):
        log.info('creating {0} from {1}'.format(simd_out,tmpl_in))
        if not dry_run:
            generate(data_in,tmpl_in,simd_out)


if __name__ == '__main__':
    if len(sys.argv) != 4:
        print('usage: generate_simd.py data_file template output',file=sys.stderr)
        exit(1)
    generate(*sys.argv[1:])
