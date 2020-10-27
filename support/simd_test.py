import subprocess
import os.path
import itertools

from setuptools import Command
from distutils.errors import DistutilsSetupError
from distutils import log
from distutils.util import split_quoted
from distutils.ccompiler import new_compiler,CCompiler

import generate_simd

# this assumes this file is in the "support" folder
base_dir = os.path.dirname(os.path.dirname(os.path.realpath(__file__)))

COMMON_GCC_CPP_FLAGS = [
    '-Wno-ignored-attributes',
    '-std=c++17',
    '-Wall']

class test_simd(Command):
    """Test the SIMD wrapper.

    This tests that for every valid and supported combination of SIMD
    feature-sets, simd.hpp compiles, and if supported by the current CPU, that a
    test program using it, runs without error.
    """

    description = "Test the SIMD wrapper (requires GCC-compatible compiler)"

    user_options = [
        ('build-base=','b',"base directory for build library"),
        ('build-temp=','t',"temporary build directory"),
        ('emu-cmd=',None,'if specified, will use this command to run test executables')
    ]

    def initialize_options(self):
        self.emu_cmd = None
        self.build_temp = None

    def finalize_options(self):
        self.set_undefined_options('build',
            ('build_temp',)*2)
        if self.emu_cmd: self.emu_cmd = split_quoted(self.emu_cmd)

    def run(self):
        generate_simd.create_simd_hpp(base_dir,self.build_temp,self.force,self.dry_run)

        compiler = new_compiler(compiler='unix',verbose=self.verbose,dry_run=self.dry_run,force=self.force)
        compiler.add_include_dir(self.build_temp)
        compiler.add_include_dir('src')

        cpu_features = self._get_cpuid_flags(compiler)

        compiler.add_library('stdc++')
        compiler.add_library('m')

        i = 0
        for fe,fl in [
            ('sse','sse'),
            ('sse2','sse2'),
            ('sse3','sse3'),
            ('ssse3','ssse3'),
            ('sse4.1','sse4.1'),
            ('sse4.2','sse4.2'),
            ('avx','avx'),
            ('avx2','avx2'),
            ('avx512_f','avx512f')]:
            self._test_one(cpu_features,compiler,i,{fe},[fl])
            i += 1

        exts = [
            ('avx512_pf','avx512pf'),
            ('avx512_er','avx512er'),
            ('avx512_cd','avx512cd'),
            ('avx512_vl','avx512vl'),
            ('avx512_bw','avx512bw'),
            ('avx512_dq','avx512dq')]

        for n in range(len(exts)):
            for combo in itertools.combinations(exts,n+1):
                features = {'avx512_f'}
                flags = ['avx512f']
                for fe,fl in combo:
                    features.add(fe)
                    flags.append(fl)
                self._test_one(cpu_features,compiler,i,features,flags)
                i += 1

    def _test_command_list(self,name):
        cmd = [os.path.join(self.build_temp,name)]
        if self.emu_cmd:
            cmd = self.emu_cmd + cmd
        return cmd

    def _test_one(self,cpu_features,compiler,index,features,flags):
        log.info('testing for ({}):'.format(','.join(features)))
        exc_name = 'simd_test{}'.format(index)

        objs = compiler.compile(
            ['src/simd_test.cpp'],
            output_dir=self.build_temp,
            extra_postargs=COMMON_GCC_CPP_FLAGS + ['-m' + f for f in flags])
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
                self._test_command_list(exc_name),
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


    def _get_cpuid_flags(self,compiler):
        objs = compiler.compile(['src/feature_test.c'],output_dir=self.build_temp)
        compiler.link(
            CCompiler.EXECUTABLE,
            objs,
            'feature_test',
            output_dir=self.build_temp,
            build_temp=self.build_temp,
            target_lang='c')

        if self.dry_run:
            return []

        try:
            result = subprocess.run(
                self._test_command_list('feature_test'),
                stdout=subprocess.PIPE,
                encoding='utf-8')
        except Exception as e:
            msg = 'unable to run "feature_test" program'
            if self.force:
                self.warn(msg)
                return []
            raise DistutilsSetupError(msg)

        return frozenset(f for f in result.stdout.split('\n') if f)
