import sys
import os
import os.path
import re
import subprocess

from setuptools import setup,Extension
from distutils import sysconfig

# at the time of this writing, there is no setuptools.command.build module
from distutils.command.build import build

from setuptools.command.build_ext import build_ext
from distutils.errors import DistutilsSetupError
from distutils.dir_util import mkpath
from distutils import log
from distutils.util import split_quoted, strtobool, get_platform
from distutils.dep_util import newer_group
from distutils.spawn import find_executable
from distutils.file_util import copy_file

try:
    import distutils.cygwinccompiler
except ImportError:
    pass
else:
    # Mingw32CCompiler will link with the MSVC runtime library, which is not
    # just unnecessary, but will cause the libraries to fail to run. It also
    # doesn't recognize the latest versions anyway, and throws an exception.

    distutils.cygwinccompiler.get_msvcr = lambda: []

try:
    from sphinx.setup_command import BuildDoc
except ImportError:
    BuildDoc = None


base_dir = os.path.dirname(os.path.realpath(__file__))
sys.path.insert(0,os.path.join(base_dir,'support'))

import version
import generate_simd
import simd_test
import arg_helper

IS_64_BIT = sys.maxsize > 2**32

GCC_OPTIMIZE_COMPILE_ARGS = [
    '-O3',
    '-funsafe-loop-optimizations',
    '-ffast-math',
    '-fstrict-enums',
    '-fno-enforce-eh-specs',
    '-fnothrow-opt',
    '-march=native',
    '-fdelete-dead-exceptions']
GCC_EXTRA_COMPILE_ARGS = [
    '-std=c++17',
    '-fabi-version=0',
    '-fvisibility=hidden',
    '-fno-rtti',
    '-Wno-format',
    '-Wno-format-security',
    '-Wno-invalid-offsetof',

    # setuptools has the equivalent to this enabled by default when compiling
    # with MSVC, so we might as well enable it with GCC too, to know where to
    # add explicit casts
    '-Wconversion',

    # using SIMD types in templates causes the __may_alias__ attribute to be
    # dropped, but since we do all type punning through unions, this isn't a
    # problem
    '-Wno-ignored-attributes',

    # in the future, we may be able to mark classes as "trivially relocatable"
    # which would make suppressing this warning unnecessary
    '-Wno-class-memaccess',

    # I get "iteration 4611686018427387903 invokes undefined behavior" warnings
    # when compiling fixed_geometry versions, but I don't see any way the
    # pertinent loops could reach such high values.
    '-Wno-aggressive-loop-optimizations',

    '-Werror=return-type']

CLANG_OPTIMIZE_COMPILE_ARGS = [
    '-O3',
    '-ffast-math',
    '-fstrict-enums',
    '-march=native']
CLANG_EXTRA_COMPILE_ARGS = [
    '-std=c++17',
    '-fvisibility=hidden',
    '-fno-rtti',
    '-Wno-format',
    '-Wno-format-security',
    '-Wno-invalid-offsetof',
    '-Werror=return-type']

MSVC_OPTIMIZE_COMPILE_ARGS = ['/Ox','/fp:fast','/volatile:iso','/std:c++latest']

DEFAULT_OPTIMIZED_DIMENSIONS = frozenset(range(3,9))


TRACER_TEMPLATE = """
#include "py_common.hpp"
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
            items.update(range(start,end+1))
        else:
            items.add(start)

    return items


extra_user_options = [
    ('optimize-dimensions=',None,
     'which dimensionalities will have optimized versions of the main tracer module'),
    ('cpp-opts=',None,
     'extra command line arguments for the compiler'),
    ('ld-opts=',None,
     'extra command line arguments for the linker'),
    ('cpp-neg-opts=',None,
     'compiler command line arguments to omit'),
    ('ld-neg-opts=',None,
     'liner command line arguments to omit'),
    ('copy-mingw-deps=',None,
     'when the compiler type is "mingw32", copy the dependent MinGW DLLs to the built package (default true)'),
    ('test-cpu-flags',None,
     'if compiling with MSVC, determine SSE/AVX support from current CPU and add corresponding compile flags (default true)'),
    ('emu-cmd=',None,
     'if specified, and test-cpu-flags is true, use this command to run SSE/AVX test executable')]

class CustomBuild(build):
    user_options = build.user_options + extra_user_options

    def initialize_options(self):
        build.initialize_options(self)
        self.optimize_dimensions = None
        self.cpp_opts = ''
        self.ld_opts = ''
        self.cpp_neg_opts = ''
        self.ld_neg_opts = ''
        self.copy_mingw_deps = None
        self.test_cpu_flags = None
        self.emu_cmd = None

    def finalize_options(self):
        build.finalize_options(self)

        self.optimize_dimensions = (parse_ranges(self.optimize_dimensions)
            if self.optimize_dimensions is not None
            else DEFAULT_OPTIMIZED_DIMENSIONS)

        self.cpp_opts = split_quoted(self.cpp_opts)
        self.ld_opts = split_quoted(self.ld_opts)
        self.cpp_neg_opts = frozenset(split_quoted(self.cpp_neg_opts))
        self.ld_neg_opts = frozenset(split_quoted(self.ld_neg_opts))
        self.copy_mingw_deps = strtobool(self.copy_mingw_deps) if self.copy_mingw_deps is not None else True
        self.test_cpu_flags = strtobool(self.test_cpu_flags) if self.test_cpu_flags is not None else True
        if self.emu_cmd: self.emu_cmd = split_quoted(self.emu_cmd)


def get_dll_list(f):
    dlls = []
    p = re.compile(br'\s*DLL Name: (.+)')
    for line in f:
        m = p.match(line)
        if m:
            dlls.append(m.group(1).decode())
    return dlls

def make_negatable_compiler(compiler,neg_opts):
    class Wrapper(type(compiler)):
        def __init__(self):
            # don't call super().__init__
            pass

        def spawn(self,cmd):
            super().spawn([cmd[0]]+[arg for arg in cmd[1:] if arg not in neg_opts])
    newc = Wrapper()
    newc.__dict__ = compiler.__dict__
    return newc

class CustomBuildExt(build_ext):
    user_options = build_ext.user_options + extra_user_options

    def initialize_options(self):
        build_ext.initialize_options(self)
        self.optimize_dimensions = None
        self.cpp_opts = None
        self.ld_opts = None
        self.cpp_neg_opts = None
        self.ld_neg_opts = None
        self.copy_mingw_deps = None
        self.test_cpu_flags = None
        self.emu_cmd = None

    def special_tracer_file(self,n):
        return os.path.join(self.build_temp,'tracer{0}.cpp'.format(n))

    def special_tracer_ext(self,n):
        n = str(n)
        return Extension(
            'tracer'+n,
            ['src/py_common.cpp'],
            depends=['src/simd.hpp.in','src/ntracer_body.hpp',
                'src/py_common.hpp','src/pyobject.hpp','src/geometry.hpp',
                'src/fixed_geometry.hpp','src/tracer.hpp','src/light.hpp',
                'src/render.hpp','src/camera.hpp','src/compatibility.hpp',
                'src/v_array.hpp','src/instrumentation.hpp',
                'src/geom_allocator.hpp'],
            include_dirs=['src'])

    def finalize_options(self):
        self.set_undefined_options('build',
            ('optimize_dimensions',)*2,
            ('cpp_opts',)*2,
            ('ld_opts',)*2,
            ('cpp_neg_opts',)*2,
            ('ld_neg_opts',)*2,
            ('copy_mingw_deps',)*2,
            ('test_cpu_flags',)*2,
            ('emu_cmd',)*2)

        # we can't add to self.extensions after running
        # build_ext.finalize_options because the items are modified by it
        old_ext_mods = self.distribution.ext_modules
        self.distribution.ext_modules = new_ext_mods = old_ext_mods[:]
        try:
            for d in self.optimize_dimensions:
                new_ext_mods.append(self.special_tracer_ext(d))

            build_ext.finalize_options(self)
        finally:
            self.distribution.ext_modules = old_ext_mods

        # needed for simd.hpp (derived from simd.hpp.in)
        self.include_dirs.append(self.build_temp)
        self.include_dirs.append('src')

        # these have to be added after calling build_ext.finalize_options
        # because they rely on self.build_temp, which is set by
        # build_ext.finalize_options
        for d,ext in zip(self.optimize_dimensions,self.extensions[-len(self.optimize_dimensions):]):
            ext.sources.insert(0,self.special_tracer_file(d))

    def add_optimization(self):
        c = self.compiler.compiler_type
        if c == 'msvc':
            cpp_args = MSVC_OPTIMIZE_COMPILE_ARGS
            ld_args = []
            if self.test_cpu_flags and get_platform() in ('win32','win-amd64'):
                cpp_args = cpp_args + simd_test.msvc_flags(
                    simd_test.get_cpuid_flags(self,simd_test.basic_compiler(self,'msvc')))
        elif c in {'unix','cygwin','mingw32'}:
            if c == 'unix':
                cc = os.path.basename(self.compiler.compiler_so[0])
                if 'clang' in cc:
                    args,oargs = CLANG_EXTRA_COMPILE_ARGS,CLANG_OPTIMIZE_COMPILE_ARGS
                elif 'gcc' in cc or 'g++' in cc:
                    args,oargs = GCC_EXTRA_COMPILE_ARGS,GCC_OPTIMIZE_COMPILE_ARGS
                else:
                    return False
            else:
                args,oargs = GCC_EXTRA_COMPILE_ARGS,GCC_OPTIMIZE_COMPILE_ARGS

            py_debug = False#sysconfig.get_config_var('Py_DEBUG') == 1
            if not self.debug:
                # this is normally defined automatically, but not when
                # using the mingw32 compiler option under windows
                cpp_args = args + oargs + ['-DNDEBUG=1']
                ld_args = ['-s']
            elif py_debug:
                cpp_args = args
                ld_args = []
            else:
                cpp_args = args + oargs + ['-DNDEBUG=1']
                ld_args = []
        else:
            return False

        cpp_args = cpp_args + self.cpp_opts
        ld_args = ld_args + self.ld_opts
        for e in self.extensions:
            e.extra_compile_args = cpp_args
            e.extra_link_args = ld_args

        return True

    def mingw_dlls(self):
        if self.dry_run: return []

        objdump = 'objdump'
        strip = 'strip'

        # places to look for DLLs
        exedirs = []
        if sys.platform == 'win32':
            exedir = find_executable('gcc')
            if exedir is None: raise DistutilsSetupError('cannot determine location of gcc.exe')
            exedirs.append(os.path.dirname(exedir))
        else:
            # if we are cross-compiling, the version of gcc used for compilation
            # is not a Windows exe and will not be in the same location as the
            # DLLs, but the environment variable PYTHONHOME may be set to an
            # alternate root directory with the "bin" directory having the DLLs
            phome = os.environ.get('PYTHONHOME')
            if phome is not None:
                exedirs.append(os.path.join(phome,'bin'))

            # we can also check where the directory usually is, if the CC
            # environment variable is set
            cc = os.environ.get('CC')
            if cc:
                m = re.match('^(.+-mingw)-g(?:cc|\\+\\+)\\b([\'"]|)',cc)
                if m:
                    rootname = m.group(1)
                    exedirs.append(os.path.join('/usr',os.path.basename(rootname),'sys-root','mingw','bin'))
                    objdump = rootname + '-objdump'
                    strip = rootname + '-strip'
                    if m.group(2):
                        objdump += m.group(2)
                        strip += m.group(2)

            if not exedirs:
                raise DistutilsSetupError('cannot determine where to look for the required DLLs')

        suffix = sysconfig.get_config_vars('EXT_SUFFIX','SO')
        info = subprocess.run(
            [objdump,'-p',os.path.join(self.build_lib,'ntracer','render'+(suffix[0] or suffix[1]))],
            capture_output=True,
            check=True)
        r = []
        exedir = None
        if len(exedirs) == 1: exedir = exedirs[0]
        for dll in get_dll_list(info.stdout.splitlines()):
            if 'python' in dll: continue
            if exedir is None:
                for d in exedirs:
                    p = os.path.join(d,dll)
                    if os.path.exists(p):
                        r.append(p)
                        exedir = d
                        break
            else:
                p = os.path.join(exedir,dll)
                if os.path.exists(p): r.append(p)

        return strip,r

    def build_extensions(self):
        if not self.add_optimization():
            self.warn("don't know how to set optimization flags for this compiler")

        # On some 32-bit systems, __STDCPP_DEFAULT_NEW_ALIGNMENT__ is
        # erroneously defined as 16 but operator new only aligns to 8 bytes,
        # so on 32-bit systems we replace the global new and delete functions.
        # If we raise the requirement from C++17 to C++20, this can be removed,
        # as the new compilers have this fixed.
        if not IS_64_BIT:
            for e in self.extensions:
                e.sources.append('src/compatibility_32bit.cpp')

        # To be able to omit *any* argument, we have to intercept the arguments
        # right before the compiler object calls spawn. Fortunately, there is a
        # CCompiler.spawn method we can override. However, the method needs to
        # remove different arguments depending on whether we're compiling or
        # linking, and the compiler object cannot be mutated while building
        # extensions, since they may be built in parallel, so we create two
        # wrapper classes and override build_extension in this class to use the
        # different wrappers.

        self.cpp_compiler = make_negatable_compiler(self.compiler,self.cpp_neg_opts)
        self.ld_compiler = make_negatable_compiler(self.compiler,self.ld_neg_opts)

        mkpath(self.build_temp,dry_run=self.dry_run)
        if self.optimize_dimensions:
            for d in self.optimize_dimensions:
                f = self.special_tracer_file(d)
                if self.force or not os.path.exists(f):
                    log.info('creating {0} from template'.format(f))
                    if not self.dry_run:
                        with open(f,'w') as out:
                            out.write(TRACER_TEMPLATE.format(d))

        generate_simd.create_simd_hpp(base_dir,self.build_temp,self.force,self.dry_run)

        # These create ntracer_body_strings.hpp and render_strings.hpp
        # respectively
        arg_helper.create_strings_hpp(base_dir,self.build_temp,'ntracer_body.hpp',self.force,self.dry_run)
        arg_helper.create_strings_hpp(base_dir,self.build_temp,'render.cpp',self.force,self.dry_run)

        build_ext.build_extensions(self)

        if self.copy_mingw_deps and (
                self.compiler.compiler_type == 'mingw32' or
                (self.compiler.compiler_type == 'unix' and 'mingw' in os.path.basename(self.compiler.compiler_so[0]))):
            log.info('copying MinGW-specific dependencies')
            out = os.path.join(self.build_lib,'ntracer')
            link = getattr(os,'link',None) and 'hard'
            strip,dlls = self.mingw_dlls()
            skip_strip = False
            for dll in dlls:
                copy_file(dll,out,update=True,link=link)
                if not skip_strip:
                    try:
                        subprocess.run(
                            [strip,os.path.join(out,os.path.basename(dll))],
                            check=True)
                    except Exception:
                        skip_strip = True

    def build_extension(self,ext):
        # this code is mostly copied from distutils/command/build_ext.py, with
        # the stuff we don't need, removed, and support for removing specific
        # compiler/linker arguments

        sources = list(ext.sources)

        ext_path = self.get_ext_fullpath(ext.name)
        depends = sources + ext.depends
        if not (self.force or newer_group(depends, ext_path, 'newer')):
            log.debug("skipping '%s' extension (up-to-date)", ext.name)
            return
        else:
            log.info("building '%s' extension", ext.name)

        extra_args = ext.extra_compile_args or []

        macros = ext.define_macros[:]
        for undef in ext.undef_macros:
            macros.append((undef,))

        objects = self.cpp_compiler.compile(sources,
            output_dir=self.build_temp,
            macros=macros,
            include_dirs=ext.include_dirs,
            debug=self.debug,
            extra_postargs=extra_args,
            depends=ext.depends)

        self._built_objects = objects[:]

        if ext.extra_objects:
            objects.extend(ext.extra_objects)
        extra_args = ext.extra_link_args or []

        language = ext.language or self.compiler.detect_language(sources)

        self.ld_compiler.link_shared_object(
            objects, ext_path,
            libraries=self.get_libraries(ext),
            library_dirs=ext.library_dirs,
            runtime_library_dirs=ext.runtime_library_dirs,
            extra_postargs=extra_args,
            export_symbols=self.get_export_symbols(ext),
            debug=self.debug,
            build_temp=self.build_temp,
            target_lang=language)


with open('README.rst') as readme:
    long_description = readme.read()

ver = version.get_version(base_dir)

float_format = {
    'unknown' : '0',
    'IEEE, little-endian' : '1',
    'IEEE, big-endian' : '2'}[float.__getformat__('float')]
byteorder = {'little' : '1','big' : '2'}[sys.byteorder]

cmdclass = {
    'build' : CustomBuild,
    'build_ext' : CustomBuildExt,
    'test_simd' : simd_test.test_simd,
    'cpu_features' : simd_test.cpu_features}
command_options = {}

if BuildDoc is not None:
    cmdclass['build_sphinx'] = BuildDoc
    command_options['build_sphinx'] = {
        'version': ('setup.py', '.'.join(ver.split('.')[0:2]) if ver else ''),
        'release': ('setup.py', ver or ''),
        'source_dir': ('setup.py', 'doc')}

setup(name='ntracer',
    author='Rouslan Korneychuk',
    author_email='rouslank@msn.com',
    url='https://github.com/Rouslan/NTracer',
    version=ver or 'unversioned',
    packages=['ntracer','ntracer.tests'],
    package_dir={'': 'lib'},
    scripts=['scripts/hypercube.py','scripts/polytope.py'],
    ext_package='ntracer',
    ext_modules=[
        Extension(
            'render',
            ['src/render.cpp','src/py_common.cpp'],
            depends=['src/simd.hpp.in','src/py_common.hpp','src/render.hpp',
                'src/pyobject.hpp','src/compatibility.hpp',
                'src/geom_allocator.hpp'],
            define_macros=[('FORMAT_OTHER','0'),('FORMAT_IEEE_LITTLE','1'),
                ('FORMAT_IEEE_BIG','2'),('FLOAT_NATIVE_FORMAT',float_format),
                ('BYTEORDER_LITTLE','1'),('BYTEORDER_BIG','2'),
                ('NATIVE_BYTEORDER',byteorder)]),
        Extension(
            'tracern',
            ['src/tracern.cpp','src/py_common.cpp','src/geom_allocator.cpp',
                'src/var_geometry.cpp'],
            depends=['src/simd.hpp.in','src/ntracer_body.hpp',
                'src/py_common.hpp','src/pyobject.hpp','src/geometry.hpp',
                'src/var_geometry.hpp','src/tracer.hpp','src/light.hpp',
                'src/render.hpp','src/camera.hpp','src/compatibility.hpp',
                'src/v_array.hpp','src/instrumentation.hpp',
                'src/geom_allocator.hpp'])],
    extras_require={'PygameRenderer': ['pygame']},
    description='A hyper-spacial ray-tracing library',
    long_description=long_description,
    license='MIT',
    classifiers=[
        'Development Status :: 4 - Beta',
        'License :: OSI Approved :: MIT License',
        'Operating System :: OS Independent',
        'Programming Language :: C++',
        'Programming Language :: Python',
        'Programming Language :: Python :: 3',
        'Programming Language :: Python :: 3.5',
        'Programming Language :: Python :: 3.6',
        'Programming Language :: Python :: 3.7',
        'Programming Language :: Python :: 3.8',
        'Programming Language :: Python :: 3.9',
        'Topic :: Multimedia :: Graphics :: 3D Rendering',
        'Topic :: Scientific/Engineering :: Mathematics'],
    zip_safe=True,
    cmdclass=cmdclass)
