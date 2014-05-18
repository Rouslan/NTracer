import sys
import os.path

from distutils import sysconfig
from distutils.core import setup,Command
from distutils.extension import Extension
from distutils.command.build_ext import build_ext
from distutils.errors import DistutilsSetupError
from distutils.dir_util import mkpath
from distutils import log
from distutils.dep_util import newer


base_dir = os.path.dirname(os.path.realpath(__file__))
sys.path.insert(0,os.path.join(base_dir,'support'))

import version
import generate_simd


GCC_OPTIMIZE_COMPILE_ARGS = [
    '-O3',
    '-fmerge-all-constants',
    '-funsafe-loop-optimizations',
    '-ffast-math',
    '-fsched2-use-superblocks',
    '-fstrict-enums',
    '-fno-enforce-eh-specs',
    '-fnothrow-opt',
    '-march=native',
#    '-DPROFILE_CODE',
    '--param','large-function-growth=500',
    '--param','inline-unit-growth=1000']
GCC_EXTRA_COMPILE_ARGS = [
    '-std=c++11',
    '-fvisibility=hidden',
    '-Wno-format',
    '-Wno-invalid-offsetof',
    '-Werror=return-type']

# At the time of this writing, no version of MSVC has enough C++11 support to
# compile this package
MSVC_OPTIMIZE_COMPILE_ARGS = ['/Ox','/fp:fast','/volatile:iso','/Za']

DEFAULT_OPTIMIZED_DIMENSIONS = frozenset(range(3,9))


TRACER_TEMPLATE = """
#include <Python.h>
#include "fixed_geometry.hpp"

typedef fixed::item_store<{0},real> module_store;

#define MODULE_NAME tracer{0}
#include "ntracer_body.hpp"
"""


def check_min(x):
    if x < 3:
        raise DistutilsSetupError('dimension cannot be less than 3')
        
def parse_int(x):
    try:
        return int(x,10)
    except ValueError:
        raise DistutilsSetupError('invalid format for ranges')

def parse_ranges(x):
    items = set()
    parts = x.split(',')
    for p in parts:
        if (not p) or p.isspace(): continue
        p = p.partition('-')
        start = parse_int(p[0])
        check_min(start)
        end = p[2].strip()
        if end:
            end = parse_int(end)
            check_min(end)
            if end < start: start,end = end,start
            items.update(xrange(start,end+1))
        else:
            items.add(start)
            
    return items


class CustomBuildExt(build_ext):
    user_options = build_ext.user_options + [
        ('optimize-dimensions=',None,
         'which dimensionalities will have optimized versions of the main tracer module')]
    
    def initialize_options(self):
        build_ext.initialize_options(self)
        self.optimize_dimensions = None
        
    def special_tracer_file(self,n):
        return os.path.join(self.build_temp,'tracer{0}.cpp'.format(n))
        
    def special_tracer_ext(self,n):
        n = str(n)
        return Extension(
            'tracer'+n,
            [self.special_tracer_file(n),'src/py_common.cpp','src/simd.cpp'],
            depends=['src/simd.hpp.in','src/ntracer_body.hpp',
                'src/py_common.hpp','src/pyobject.hpp','src/geometry.hpp',
                'src/fixed_geometry.hpp','src/tracer.hpp','src/light.hpp',
                'src/render.hpp','src/camera.hpp','src/compatibility.hpp',
                'src/v_array.hpp','src/instrumentation.hpp',
                'src/index_list.hpp'],
            include_dirs=['src'])
    
    def finalize_options(self):
        build_ext.finalize_options(self)
        
        self.optimize_dimensions = (parse_ranges(self.optimize_dimensions)
            if self.optimize_dimensions is not None
            else DEFAULT_OPTIMIZED_DIMENSIONS)
        
        for d in self.optimize_dimensions:
            self.extensions.append(self.special_tracer_ext(d))

        # needed for simd.hpp (derived from simd.hpp.in)
        self.include_dirs.append(self.build_temp)
        self.include_dirs.append('src')

    def add_optimization(self):
        c = self.compiler.compiler_type
        if c == 'msvc':
            for e in self.extensions:
                e.extra_compile_args = MSVC_OPTIMIZE_COMPILE_ARGS
            return True
        if c == 'unix' or c == 'cygwin':
            cc = os.path.basename(sysconfig.get_config_var('CXX'))
            if 'gcc' in cc or 'g++' in cc or 'clang' in cc:
                for e in self.extensions:
                    e.extra_compile_args = GCC_EXTRA_COMPILE_ARGS[:]
                    e.extra_compile_args += GCC_OPTIMIZE_COMPILE_ARGS
                    if not self.debug:
                        e.extra_link_args = ['-s']
                return True
            
        return False

    def build_extensions(self):
        if not self.add_optimization():
            self.warn("don't know how to set optimization flags for this compiler")
        
        mkpath(self.build_temp,dry_run=self.dry_run)
        if self.optimize_dimensions:
            for d in self.optimize_dimensions:
                f = self.special_tracer_file(d)
                if self.force or not os.path.exists(f):
                    log.info('creating {0} from template'.format(f))
                    if not self.dry_run:
                        with open(f,'w') as out:
                            out.write(TRACER_TEMPLATE.format(d))
        
        simd_out = os.path.join(self.build_temp,'simd.hpp')
        simd_in = os.path.join(base_dir,'src','simd.hpp.in')
        if self.force or newer(simd_in,simd_out):
            log.info('creating {0} from {1}'.format(simd_out,simd_in))
            if not self.dry_run:
                generate_simd.generate(
                    os.path.join(base_dir,'src','intrinsics.json'),
                    simd_in,
                    simd_out)
        
        build_ext.build_extensions(self)


long_description = open('README.rst').read()
long_description = long_description[0:long_description.find('\n\n\n')]


float_format = {
    'unknown' : '0',
    'IEEE, little-endian' : '1',
    'IEEE, big-endian' : '2'}[float.__getformat__('float')]
byteorder = {'little' : '1','big' : '2'}[sys.byteorder]

setup(name='ntracer',
    author='Rouslan Korneychuk',
    author_email='rouslank@msn.com',
    url='https://github.com/Rouslan/NTracer',
    version=version.get_version(base_dir) or 'unversioned',
    packages=['ntracer','ntracer.tests'],
    scripts=['scripts/hypercube.py','scripts/polytope.py'],
    ext_package='ntracer',
    ext_modules=[
        Extension(
            'render',
            ['src/render.cpp','src/py_common.cpp'],
            depends=['src/simd.hpp.in','src/py_common.hpp','src/render.hpp',
                'src/pyobject.hpp','src/compatibility.hpp',
                'src/index_list.hpp'],
            define_macros=[('FORMAT_OTHER','0'),('FORMAT_IEEE_LITTLE','1'),
                ('FORMAT_IEEE_BIG','2'),('FLOAT_NATIVE_FORMAT',float_format),
                ('BYTEORDER_LITTLE','1'),('BYTEORDER_BIG','2'),
                ('NATIVE_BYTEORDER',byteorder)]),
        Extension(
            'tracern',
            ['src/tracern.cpp','src/py_common.cpp','src/simd.cpp'],
            depends=['src/simd.hpp.in','src/ntracer_body.hpp',
                'src/py_common.hpp','src/pyobject.hpp','src/geometry.hpp',
                'src/var_geometry.hpp','src/tracer.hpp','src/light.hpp',
                'src/render.hpp','src/camera.hpp','src/compatibility.hpp',
                'src/v_array.hpp','src/instrumentation.hpp',
                'src/index_list.hpp'])],
#    extras_require={'PygameRenderer': ['pygame']},
    description='A fast hyper-spacial ray-tracing library',
    long_description=long_description,
    license='MIT',
    classifiers=[
        'Development Status :: 4 - Beta',
        'License :: OSI Approved :: MIT License',
        'Operating System :: OS Independent',
        'Programming Language :: C++',
        'Programming Language :: Python',
        'Programming Language :: Python :: 2',
        'Programming Language :: Python :: 2.7',
        'Programming Language :: Python :: 3',
        'Programming Language :: Python :: 3.0',
        'Programming Language :: Python :: 3.1',
        'Programming Language :: Python :: 3.2',
        'Programming Language :: Python :: 3.3',
        'Programming Language :: Python :: 3.4',
        'Topic :: Multimedia :: Graphics :: 3D Rendering',
        'Topic :: Scientific/Engineering :: Mathematics'],
    cmdclass={'build_ext' : CustomBuildExt})
