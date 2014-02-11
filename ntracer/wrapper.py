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
    
    An instance contains all the same values that :py:mod:`.tracern` has,
    except every class that has a method with a ``dimension`` parameter is
    subclassed so that parameter is filled in automatically. Additionally, the
    constructors for :py:class:`.tracern.Vector` and
    :py:class:`.tracern.Matrix` are modified to accept multiple parameters as
    an alternative to a sequence of values. e.g.:
    :code:`NTracer(3).Vector([1,2,3])` can also be written as
    :code:`NTracer(3).Vector(1,2,3)`.
    
    An instance of ``NTracer`` imports from a specialized (read: faster)
    tracer\ *{dimension}* version if it exists, and falls back to
    :py:mod:`.tracern` otherwise.
    
    Instances of ``NTracer`` are cached, so invoking ``NTracer`` while another
    instance with the same dimension already exists, will simply give you the
    first instance (unless ``force_generic`` is given a value of ``True``).
    
    :param integer dimension: The value that will be automatically given to any
        function/method/constructor that requires a ``dimension`` parameter.
    :param boolean force_generic: If ``True``, :py:mod:`.tracern` will be used
        even if a specialized version exists. This is mainly for testing.
    """
    
    _cache = weakref.WeakValueDictionary()
    
    def __new__(cls,dimension,force_generic=False):
        if not force_generic:
            obj = NTracer._cache.get(dimension)
            if obj is not None: return obj
        
        obj = object.__new__(cls)
        
        if force_generic:
            mod = importlib.import_module('ntracer.tracern')
        else:
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
            'PrimitivePrototype',
            'Solid',
            'SolidPrototype',
            'Triangle',
            'TrianglePrototype',
            'dot',
            'cross']:
            setattr(obj,n,getattr(mod,n))
        
        if not force_generic:
            NTracer._cache[dimension] = obj
            
        return obj

