"""Generate a header file containing a data-type for all interned strings used
in a given C++ file"""

import sys
import re
import os.path

from distutils import log
from distutils.dep_util import newer

RE_P = re.compile(r'(?<![a-zA-Z0-9_])P\(([^\)]+)\)')
RE_ACCEPTABLE = re.compile('[a-zA-Z0-9_]+')

def generate(input_file,output_file):
    to_intern = set()
    with open(input_file) as ifile:
        for line in ifile:
            to_intern.update(RE_P.findall(line))
    to_intern = sorted(to_intern)
    for s in to_intern:
        if RE_ACCEPTABLE.fullmatch(s) is None:
            raise ValueError('"{}" contains a character that can\'t be part of an indentifier'.format(s))

    enum = ['_interned_string_'+s for s in to_intern]
    if enum: enum[0] += ' = 0'

    with open(output_file,'w') as ofile:
        ofile.write('''
const char *_interned_raw_strings[] = {{
{raw}}};
enum {{
{enum}}};
struct interned_strings {{
    PyObject *values[{size}];

    bool init() {{
        for(int i=0; i<{size}; ++i) {{
            values[i] = PyUnicode_InternFromString(_interned_raw_strings[i]);
            if(!values[i]) {{
                for(; i>=0; --i) Py_DECREF(values[i]);
                return false;
            }}
        }}
        return true;
    }}

    ~interned_strings() {{
        for(int i=0; i<{size}; ++i) Py_DECREF(values[i]);
    }}
}};

#define P(X) idata->istrings.values[_interned_string_##X]
'''.format(
        size=len(to_intern),
        raw=',\n'.join('"'+s+'"' for s in to_intern),
        enum=',\n'.join(enum)))

def create_strings_hpp(base_dir,build_temp,ifile,force,dry_run):
    ofile = os.path.join(build_temp,os.path.splitext(ifile)[0]+'_strings.hpp')
    ifile = os.path.join(base_dir,'src',ifile)
    if force or newer(ifile,ofile):
        log.info('creating {0} from {1}'.format(ofile,ifile))
        if not dry_run:
            generate(ifile,ofile)

if __name__ == '__main__':
    if len(sys.argv) != 3:
        print('usage: arg_helper.py input output',file=sys.stderr)
        exit(1)
    generate(*sys.argv[1:])
