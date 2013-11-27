import importlib
import weakref


CUBE = 1
SPHERE = 2


def vector_wrapper(mod,dim):
    base = mod.Vector
    class Vector(base):
        __slots__ = ()
        
        def __new__(cls,*values):
            if len(values) > 1: return base.__new__(cls,dim,values)
            return base.__new__(cls,dim,*values)
        
        @staticmethod
        def axis(axis,length=1):
            return base.axis(dim,axis,length)
        
    return Vector

def matrix_wrapper(mod,dim):
    base = mod.Matrix
    class Matrix(base):
        __slots__ = ()
        
        def __new__(cls,*values):
            if len(values) > 1: return base.__new__(cls,dim,values)
            return base.__new__(cls,dim,*values)
        
        @staticmethod
        def scale(factor):
            if isinstance(factor,mod.Vector):
                return base.scale(factor)
            return base.scale(dim,factor)
        
        @staticmethod
        def identity():
            return base.identity(dim)
        
    return Matrix

def camera_wrapper(mod,dim):
    base = mod.Camera
    class Camera(base):
        def __new__(cls):
            return base.__new__(cls,dim)

        def __init__(self):
            base.__init__(self,dim)
            
    return Camera

def boxscene_wrapper(mod,dim):
    base = mod.BoxScene
    class BoxScene(base):
        def __new__(cls):
            return base.__new__(cls,dim)
        
    return BoxScene

def aabb_wrapper(mod,dim):
    base = mod.AABB
    class AABB(base):
        def __new__(cls,*args,**kwds):
            return base.__new__(cls,dim,*args,**kwds)

    return AABB


class NTracer(object):
    """A helper class that simplifies the creation of multiple objects with
    the same dimension.
    
    An instance of ``NTracer`` contains the attributes ``Vector``, ``Matrix``, 
    ``Camera`` and ``BoxScene``, which are classes that have the same
    interface as those of ``tracern``, except their constructors and static
    methods don't have ``dimension`` parameters.
    
    An instance of ``NTracer`` imports from a specialized (read: faster)
    tracer*{dimension}* version if it exists, and falls back to ``tracern``
    otherwise.
    
    Instances of ``NTracer`` are cached, so invoking ``NTracer`` while another
    instance with the same dimension already exists, will simply give you the
    first instance.
    
    """
    
    _cache = weakref.WeakValueDictionary()
    
    def __new__(cls,dimension):
        obj = NTracer._cache.get(dimension)
        
        if obj is None:
            obj = object.__new__(cls)
            
            try:
                mod = importlib.import_module('ntracer.tracer{0:d}'.format(dimension))
            except ImportError:
                mod = importlib.import_module('ntracer.tracern')
            
            obj.dimension = dimension
            obj.Vector = vector_wrapper(mod,dimension)
            obj.Matrix = matrix_wrapper(mod,dimension)
            obj.Camera = camera_wrapper(mod,dimension)
            obj.BoxScene = boxscene_wrapper(mod,dimension)
            obj.AABB = aabb_wrapper(mod,dimension)
            
            for n in [
                'CompositeScene',
                'KDNode',
                'KDLeaf',
                'KDBranch',
                'Primitive',
                'Solid',
                'Triangle',
                'TrianglePrototype',
                'dot',
                'cross',
                'axis_intersection']:
                setattr(obj,n,getattr(mod,n))
            
            NTracer._cache[dimension] = obj
            
        return obj

