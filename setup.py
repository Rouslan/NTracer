import os.path

from distutils import sysconfig
from distutils.core import setup,Command
from distutils.extension import Extension
from distutils.command.build_ext import build_ext
from distutils.errors import DistutilsSetupError
from distutils.dir_util import mkpath
from distutils import log

try:
    from distutils.command.build_py import build_py_2to3 as build_py
except ImportError:
    from distutils.command.build_py import build_py


GCC_OPTIMIZE_COMPILE_ARGS = ['-O3','-fmerge-all-constants','-funsafe-loop-optimizations','-ffast-math','-fstrict-enums','-fno-enforce-eh-specs','-fnothrow-opt','-march=native']
GCC_EXTRA_COMPILE_ARGS = ['-std=c++11','-fvisibility=hidden','-Wno-format','-Wno-invalid-offsetof']

# At the time of this writing, no version of MSVC has enough C++11 support to
# compile this package
MSVC_OPTIMIZE_COMPILE_ARGS = ['/Ox','/fp:fast','/volatile:iso','/Za']

DEFAULT_OPTIMIZED_DIMENSIONS = frozenset(range(3,9))


TRACER_TEMPLATE = """
#include "fixed_geometry.hpp"

typedef fixed::repr<{0}> repr;

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
    user_opyions = build_ext.user_options + [
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
            [self.special_tracer_file(n),'src/py_common.cpp'],
            depends=['src/ntracer.hpp','src/ntracer_body.hpp',
                'src/py_common.hpp','src/pyobject.hpp','src/geometry.hpp',
                'src/fixed_geometry.hpp','src/tracer.hpp','src/light.hpp',
                'src/scene.hpp','src/camera.hpp'],
            include_dirs=['src'],
            libraries=['SDL'])
    
    def find_sdl(self,path):
        if os.path.isfile(os.path.join(path,'SDL.h')):
            return path
        
        path = os.path.join(path,'SDL')
        if os.path.isfile(os.path.join(path,'SDL.h')):
            return path
        
        return None
    
    def finalize_options(self):
        build_ext.finalize_options(self)
        
        self.optimize_dimensions = parse_ranges(self.optimize_dimensions) if self.optimize_dimensions is not None else DEFAULT_OPTIMIZED_DIMENSIONS
        
        for d in self.optimize_dimensions:
            self.extensions.append(self.special_tracer_ext(d))
        
        # try to find the SDL include directory
        for p in self.include_dirs:
            sp = self.find_sdl(p)
            if sp is not None:
                if sp is not p:
                    self.include_dirs.append(sp)
                break
        else:
            for p in ['/usr/include','/usr/local/include']:
                sp = self.find_sdl(p)
                if sp is not None:
                    self.include_dirs.append(sp)
                    break
            else:
                raise DistutilsSetupError('Unable to locate "SDL.h". Use --include-dirs to specify the location of the SDL header files.')

    def add_optimization(self):
        c = self.compiler.compiler_type
        if c == 'msvc':
            if not self.debug:
                for e in self.extensions:
                    e.extra_compile_args = MSVC_OPTIMIZE_COMPILE_ARGS
            return True
        if c == 'unix' or c == 'cygwin':
            cc = os.path.basename(sysconfig.get_config_var('CXX'))
            if 'gcc' in cc or 'g++' in cc or 'clang' in cc:
                for e in self.extensions:
                    e.extra_compile_args = GCC_EXTRA_COMPILE_ARGS[:]
                    if not self.debug:
                        e.extra_compile_args += GCC_OPTIMIZE_COMPILE_ARGS
                        e.extra_link_args = ['-s']
                return True
            
        return False

    def build_extensions(self):
        if not self.add_optimization():
            self.warn("don't know how to set optimization flags for this compiler")
        
        if self.optimize_dimensions:
            mkpath(self.build_temp,dry_run=self.dry_run)
            for d in self.optimize_dimensions:
                f = self.special_tracer_file(d)
                if not os.path.exists(f):
                    log.info('creating {0} from template'.format(f))
                    if not self.dry_run:
                        with open(f,'w') as out:
                            out.write(TRACER_TEMPLATE.format(d))
            
        build_ext.build_extensions(self)


long_description = open('README.txt').read()
long_description = long_description[0:long_description.find('\n\n')]

setup(name='ntracer',
    version='0.1',
    packages=['ntracer'],
    scripts=['scripts/hypercube.py'],
    ext_package='ntracer',
    ext_modules=[
        Extension(
            'render',
            ['src/render.cpp','src/py_common.cpp'],
            depends=['src/py_common.hpp','src/scene.hpp','src/pyobject.hpp'],
            libraries=['SDL']),
        Extension(
            'tracern',
            ['src/tracern.cpp','src/py_common.cpp'],
            depends=['src/ntracer.hpp','src/ntracer_body.hpp',
                'src/py_common.hpp','src/pyobject.hpp','src/geometry.hpp',
                'src/var_geometry.hpp','src/tracer.hpp','src/light.hpp',
                'src/scene.hpp','src/camera.hpp'])],
    requires=['pygame'],
    description='A fast hyper-spacial ray-tracing library',
    long_description=long_description,
    license='MIT',
    classifiers=[
        'Development Status :: 2 - Pre-Alpha',
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
        'Topic :: Multimedia :: Graphics :: 3D Rendering',
        'Topic :: Scientific/Engineering :: Mathematics'],
    cmdclass={
        'build_py':build_py,
        'build_ext' : CustomBuildExt})
