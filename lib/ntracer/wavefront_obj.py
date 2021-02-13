from . import render
from . import wrapper

class FileFormatError(Exception):
    def __init__(self):
        super(FileFormatError,self).__init__("not a valid wavefront file")


def index1(x):
    return x-1 if x >= 0 else x

def load_obj(file,nt=None):
    if nt is None:
        nt = wrapper.NTracer(3)
    elif nt.dimension != 3:
        raise ValueError('Wavefront .obj files only support 3-dimensional geometry')

    m = render.Material((1,1,1))

    vertices = []
    triangles = []

    with open(file,'r') as input:
        for line in input:
            parts = line.split()
            if len(parts) == 0: continue
            if parts[0] == 'v':
                try:
                    coords = [float(p) for p in parts[1:4]]
                except ValueError:
                    raise FileFormatError()
                vertices.append(nt.Vector(coords))
            elif parts[0] == 'f':
                try:
                    # the neat thing here is we don't have to do anything to
                    # support OBJ's relative indices, since they are negative
                    # indices that work the same as they do for Python lists
                    coords = [vertices[index1(int(i.partition('/')[0],10))] for i in parts[1:]]
                except (ValueError,IndexError):
                    raise FileFormatError()

                for i in range(1,len(coords)-1):
                    triangles.append(nt.TrianglePrototype([coords[0],coords[i],coords[i+1]],m))

    return triangles
