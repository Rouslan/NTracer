import subprocess
import os.path
import itertools

from setuptools import Command
from distutils.errors import DistutilsSetupError
from distutils import log
from distutils.util import split_quoted, get_platform
from distutils.ccompiler import new_compiler,CCompiler

import generate_simd

# this assumes this file is in the "support" folder
base_dir = os.path.dirname(os.path.dirname(os.path.realpath(__file__)))

COMMON_GCC_CPP_FLAGS = [
    '-Wno-ignored-attributes',
    '-std=c++17',
    '-Wall']

COMMON_MSVC_CPP_FLAGS = ['/std:c++17']

def msvc_flags(features):
    # MSVC has less fine grained control over SSE/AVX version support, so we
    # need to use a combination of the "arch" flag and our own macros
    flags = []
    if 'sse' in features:
        if 'sse2' in features:
            if 'avx' in features:
                if 'avx2' in features:
                    if {'avx512_f','avx512_bw','avx512_cd','avx512_dq','avx512_vl'} <= features:
                        flags.append('/arch:AVX512')
                        for suff in ['pf','er']:
                            if ('avx512_'+suff) in features:
                                flags.append('/DSUPPORT_AVX512'+suff.toupper())
                    else:
                        flags.append('/arch:AVX2')
                        for suff in ['f','pf','er','cd','vl','bw','dq']:
                            if ('avx512_'+suff) in features:
                                flags.append('/DSUPPORT_AVX512'+suff.toupper())
                else:
                    flags.append('/arch:AVX')
            else:
                is_32bit = get_platform() == 'win32'
                if(is_32bit):
                    flags.append('/arch:SSE2')
                if 'sse4.2' in features: flags.append('/DSUPPORT_SSE4_3')
                elif 'sse4.1' in features: flags.append('/DSUPPORT_SSE4_1')
                elif 'ssse3' in features: flags.append('/DSUPPORT_SSSE3')
                elif 'sse3' in features: flags.append('/DSUPPORT_SSE3')
        else:
            flags.append('/arch:SSE')
    return flags

def gcc_flags(features):
    flags = []
    for fe,fl in [('avx512_pf','avx512pf'),
            ('avx512_er','avx512er'),
            ('avx512_cd','avx512cd'),
            ('avx512_vl','avx512vl'),
            ('avx512_bw','avx512bw'),
            ('avx512_dq','avx512dq')]:
        if fe in features:
            flags.append('-m'+fl)
    for fe,fl in [
            ('avx512_f','avx512f'),
            ('avx2','avx2'),
            ('avx','avx'),
            ('sse4.2','sse4.2'),
            ('sse4.1','sse4.1'),
            ('ssse3','ssse3'),
            ('sse3','sse3'),
            ('sse2','sse2'),
            ('sse','sse')]:
        if fe in features:
            flags.append('-m'+fl)
            break
    return flags

def test_exe(cmd,name):
    exe = [os.path.join(cmd.build_temp,name)]
    if cmd.emu_cmd:
        exe = cmd.emu_cmd + exe
    return exe

def get_cpuid_flags(cmd,compiler):
    objs = compiler.compile(['src/feature_test.c'],output_dir=cmd.build_temp)
    exe_name = 'feature_test' + (compiler.exe_extension or '')
    compiler.link(
        CCompiler.EXECUTABLE,
        objs,
        exe_name,
        output_dir=cmd.build_temp,
        build_temp=cmd.build_temp,
        target_lang='c')

    if cmd.dry_run:
        return []

    try:
        result = subprocess.run(
            test_exe(cmd,exe_name),
            stdout=subprocess.PIPE,
            encoding='utf-8')
    except Exception as e:
        msg = 'unable to run "feature_test" program'
        if cmd.force:
            cmd.warn(msg)
            return []
        raise DistutilsSetupError(msg)

    return frozenset(f for f in result.stdout.split('\n') if f)

def basic_compiler(cmd,ctype):
    c = new_compiler(compiler=ctype,verbose=cmd.verbose,dry_run=cmd.dry_run,force=cmd.force)
    c.add_include_dir(cmd.build_temp)
    c.add_include_dir('src')
    return c

class _CmdCommon(Command):
    user_options = [
        ('build-base=','b',"base directory for build library"),
        ('build-temp=','t',"temporary build directory"),
        ('emu-cmd=',None,'if specified, will use this command to run test executables'),
        ('compiler=','c',"specify the compiler type"),
        ('force','f',"forcibly build everything (ignore file timestamps)")
    ]

    def initialize_options(self):
        self.emu_cmd = None
        self.build_temp = None
        self.compiler = None
        self.force = None

    def finalize_options(self):
        self.set_undefined_options('build',
            ('build_temp',)*2,
            ('compiler',)*2,
            ('force',)*2)
        if self.emu_cmd: self.emu_cmd = split_quoted(self.emu_cmd)

class cpu_features(_CmdCommon):
    """Show a list of features available in the current CPU.

    This is done using the CPUID instruction.
    """

    description = "List current CPU features (requires x86 or x86_64 CPU)"

    def run(self):
        compiler = basic_compiler(self,self.compiler)
        cpu_features = get_cpuid_flags(self,compiler)
        print('\n'.join(sorted(cpu_features)))

class test_simd(_CmdCommon):
    """Test the SIMD wrapper.

    This tests that for every valid and supported combination of SIMD
    feature-sets, simd.hpp compiles, and if supported by the current CPU, that a
    test program using it, runs without error.
    """

    description = "Test the SIMD wrapper (requires x86 or x86_64 CPU)"

    def run(self):
        generate_simd.create_simd_hpp(base_dir,self.build_temp,self.force,self.dry_run)

        compiler = basic_compiler(self,self.compiler)
        cpu_features = get_cpuid_flags(self,compiler)

        compiler.add_library('stdc++')
        compiler.add_library('m')

        i = 0
        tfeatures = set()
        for fe in [
            'sse',
            'sse2',
            'sse3',
            'ssse3',
            'sse4.1',
            'sse4.2',
            'avx',
            'avx2',
            'avx512_f']:
            tfeatures.add(fe)
            self._test_one(cpu_features,compiler,i,tfeatures)
            i += 1

        exts = [
            'avx512_pf',
            'avx512_er',
            'avx512_cd',
            'avx512_vl',
            'avx512_bw',
            'avx512_dq']

        for n in range(len(exts)):
            for combo in itertools.combinations(exts,n+1):
                self._test_one(cpu_features,compiler,i,tfeatures.union(combo))
                i += 1

    def _test_one(self,cpu_features,compiler,index,features):
        log.info('testing for ({}):'.format(','.join(features)))
        exc_name = 'simd_test{}'.format(index) + (compiler.exe_extension or '')

        if compiler.compiler_type == 'msvc':
            flags = COMMON_MSVC_CPP_FLAGS + msvc_flags(features)
        else:
            flags = COMMON_GCC_CPP_FLAGS + gcc_flags(features)

        objs = compiler.compile(
            ['src/simd_test.cpp'],
            output_dir=self.build_temp,
            extra_postargs=flags)
        compiler.link(
            CCompiler.EXECUTABLE,
            objs,
            exc_name,
            output_dir=self.build_temp,
            build_temp=self.build_temp,
            target_lang='c++')

        if self.dry_run:
            return

        if not (features <= cpu_features):
            log.info('not running test program due to lack of support in current CPU')
            return

        try:
            result = subprocess.run(
                test_exe(self,exc_name),
                stdout=subprocess.PIPE,
                encoding='utf-8')
        except Exception:
            msg = 'unable to run test program'
            if self.force:
                self.warn(msg)
                return
            raise DistutilsSetupError(msg)

        log.info(result.stdout)
        if result.returncode:
            raise DistutilsSetupError('test failure')
