# must work in CPython 2.7 and 3.0+

import os.path as path
import subprocess
import warnings

def get_version(base_dir):
    version = None
    try:
        # if this is a source distribution
        with open(path.join(base_dir,'PKG-INFO')) as pinfo:
            for line in pinfo:
                property,_,value = line.partition(':')
                if value and property == 'Version':
                    version = value.strip()
                    break
            else:
                assert False,"no version in PKG-INFO for some reason"
    except IOError:
        # if this is a git working tree
        try:
            version = subprocess.check_output(['git','describe','--long','--dirty'])
        except Exception:
            warnings.warn('cannot determine package version')
        else:
            # needed for Python 3
            if not isinstance(version,str):
                version = str(version,'utf-8')
            
            version = version.strip().split('-')
            del version[2] # get rid of the revision hash
            assert version[0][0] == 'v'
            version[0] = version[0][1:] # get rid of the leading "v"
            version[0] = '.'.join(version[0:2])
            del version[1]
            version = '-'.join(version)
            
    return version
