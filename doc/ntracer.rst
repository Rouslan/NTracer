ntracer Package
===============

:mod:`__init__` Module
----------------------

.. automodule:: ntracer.__init__
    :members:
    :undoc-members:
    :show-inheritance:



:mod:`render` Module
--------------------

.. py:module:: ntracer.render

.. py:class:: Color(r,g,b)

    A red-green-blue triplet specifying a color.

    For each component, zero represents its minimum value and one represents its
    maximum value. Therefore, :code:`Color(0,0,0)` represents black,
    :code:`Color(1,1,1)` represents white and :code:`Color(0,0,0.5)` represents
    dark blue.

    Although values outside of 0-1 are allowed, they are clipped to the normal
    range when drawn onto a `pygame.Surface
    <http://www.pygame.org/docs/ref/surface.html#pygame.Surface>`_ object. Such
    values will, however, affect how reflections and transparency are
    calculated.

    Instances of this class are read-only.

    :param number r: Red component
    :param number g: Green component
    :param number b: Blue component

    .. py:attribute:: r

        Red component

    .. py:attribute:: g

        Green component

    .. py:attribute:: b

        Blue component


.. py:class:: Material(color,[opacity=1,reflectivity=0,specular_intensity=1,specular_exp=8,specular_color=(1,1,1)])

    Specifies how light will interact with a primitive.

    :param color: An instance of :py:class:`Color` or a tuple with three
        numbers.
    :param number opacity: A value between 0 and 1 specifying transparency.
    :param number reflectivity: A value between 0 and 1 specifying reflectivity.
    :param number specular_intesity: A value between 0 and 1 specifying the
        intensity of the specular highlight.
    :param number specular_exp: The sharpness of the specular highlight.
    :param specular_color: An instance of :py:class:`Color` or a tuple with
        three numbers specifying the color of the specular highlight.

    .. py:attribute:: color

        The diffuse color of the object.

    .. py:attribute:: opacity

        A value between 0 and 1 specifying transparency.

        0 mean completely transparent. 1 means completely opaque.

    .. py:attribute:: reflectivity

        A value between 0 and 1 specifying reflectivity.

        0 means does not reflect at all. 1 means 100% reflective. The color of
        the reflection is multiplied by :py:attr:`color`, thus if
        :py:attr:`color` is ``(1,0,0)``, the reflection will always be in shades
        of red.

    .. py:attribute:: specular_intensity

        A value between 0 and 1 specifying the maximum intensity of the specular
        highlight.

    .. py:attribute:: specular_exp

        A value greater than 0 specifying the sharpness of the specular
        highlight.

        The higher the value, the smaller the highlight will be. A value of 0
        would cause the specular highlight to cover the entire surface of the
        object at maximum intensity.

    .. py:attribute:: specular

        The color of the specular highlight.


.. py:class:: Renderer([threads=0])

    A multi-threaded scene renderer.

    By default, this class uses as many threads are the are processing cores.
    The scene is drawn onto a `pygame.Surface
    <http://www.pygame.org/docs/ref/surface.html#pygame.Surface>`_ object and
    upon completion, sends a ``pygame.USEREVENT`` message with a ``source``
    attribute set to the instance of the renderer.

    Note, the renderer does not honor clipping areas or subsurface boundaries
    and will always draw onto the entire surface.

    :param integer threads: The number of threads to use. If zero, the number of
        threads will equal the number of processing cores of the machine.

    .. py:method:: abort_render()

        Signal for the renderer to quit and wait until all drawing has stopped
        and the scene has been unlocked.

        If the renderer isn't running, this does nothing.

    .. py:method:: begin_render(dest,scene)

        Begin rendering ``scene`` onto ``dest``.

        If the renderer is already running, an exception is thrown instead. Upon
        starting, the scene will be locked for writing.

        :param pygame.Surface dest: A surface to draw onto.
        :param Scene scene: The scene to draw.

    .. py::staticmethod:: render_sync(dest,scene)

        Render ``scene`` onto ``dest``.

        This will render the scene using a single thread: the one from which it
        gets called. It will not return until it is finished.

        :param pygame.Surface dest: A surface to draw onto.
        :param Scene scene: The scene to draw.


.. py:class:: Scene

    A scene that :py:class:`Renderer` can render.

    Although not exposed to Python code, the scene class has a concept of
    locking. While a renderer is drawing a scene, the scene is locked. While
    locked, a scene cannot be modified. Attempting to do so will raise an
    exception.

    This cannot be instantiated in Python code, not even as a base class.



:mod:`tracern` Module
---------------------

.. py:module:: ntracer.tracern

The ray-tracing and geometry code and objects.

You are unlikely to need to import this module directly. It will be loaded by
:py:class:`.wrapper.NTracer`, which offers a more convenient interface.

Every function that takes a vector, can in addition to taking a
:py:class:`Vector` object, take a tuple of numbers or a matrix row
(:py:class:`MatrixProxy`). This however does not apply to operators (e.g. you
can't add a tuple and a :py:class:`Vector` together).


.. py:class:: AABB(dimension [,start, end])

    An axis-aligned bounding box.

    This is not a displayable primitive, but is instead meant for spacial
    partitioning of the primitives. This is used by :py:mod:`kdtree_builder`.

    :param integer dimension: The dimension of the box.
    :param vector start: The lowest point of the box. It defaults to a vector
        where every element is set to the lowest finite value it can represent.
    :param vector end: The highest point of the box. It defaults to a vector
        where every element is set to the highest finite value it can represent.

    .. py:method:: intersects(primitive) -> boolean

        Returns True if the box intersects the given object.

        The object is only considered intersecting if some part of it is
        *inside* the box. Merely touching the box does not count.

        :param primitive: The object to test intersection with. It must be an
            instance of :py:class:`PrimitivePrototype`, not
            :py:class:`Primitive`.
            
    .. py:method:: intersects_flat(primitive,skip) -> boolean
    
        Returns True if the box intersects the given simplex, ignoring one axis.
        
        This method is identical to :py:meth:`intersects` except it only accepts
        instances of :py:class:`TrianglePrototype` and it disregards the axis
        ``skip``. This is equivalent to testing against a simplex that has been
        extruded infinitely far in the positive and negative directions along
        that axis. The simplex **must** be flat along that axis (i.e.
        :code:`primitive.aabb_min[skip] == primitive.aabb_max[skip]` must be
        true) for the return value to be correct.
        
        This method is needed when a simplex is completely embedded in a split
        hyperplane and thus would fail the normal intersection test with any
        bounding box that the hyperplane divides.
        
        :param primitive: A simplex to test intersection with. It must be an
            instance of :py:class:`TrianglePrototype`, not
            :py:class:`Triangle`.
        :param number skip: The axis to disregard when testing.

    .. py:method:: left(axis,split) -> AABB

        Returns the lower part of this box split at ``split``, along axis
        ``axis``.

        Equivalent to: :code:`AABB(self.start,self.end.set_c(axis,split))`

        :param integer axis: The numeric index of the axis.
        :param number split: The location along the axis of the split. It must
            be inside the box.

    .. py:method:: right(axis,split) -> AABB

        Returns the upper part of this box split at ``split``, along axis
        ``axis``.

        Equivalent to: :code:`AABB(self.start.set_c(axis,split),self.end)`

        :param integer axis: The numeric index of the axis.
        :param number split: The location along the axis of the split. It must
            be inside the box.

    .. py:attribute:: dimension

        The dimension of the box.

    .. py:attribute:: end

        A vector specifying the minimum extent of the box.

    .. py:attribute:: start

        A vector specifying the maximum extent of the box.


.. py:class:: BoxScene(dimension)

    Bases: :py:class:`.render.Scene`

    A very simple scene containing one hypercube.

    The hypercube is centered at the origin and has a width of 2 along every
    axis. It's not much to look at but it renders really fast.

    :param integer dimension: The dimension of the scene.

    .. py:method:: get_camera() -> Camera

        Return a copy of the scene's camera.

    .. py:method:: set_camera(camera)

        Set the scene's camera to a copy of the provided value.

        If the scene has been locked by a :py:class:`.render.Renderer`, this
        function will raise an exception instead.

    .. py:method:: set_fov(fov)

        Set the field of vision.

        If the scene has been locked by a :py:class:`.render.Renderer`, this
        function will raise an exception instead.

        :param fov: The new field of vision in radians.

    .. py:attribute:: fov

        The scene's horizontal field of vision in radians.

        This attribute is read-only. To modify the value, use
        :py:meth:`set_fov`.

    .. py:attribute:: locked

        A boolean specifying whether or not the scene is locked.

        This attribute cannot be modified in Python code.


.. py:class:: Camera(dimension)

    A "camera" that maintains a local set of axes.

    Note that the classes that depend on this expect the axes to be orthogonal
    and unit, but this is not enforced. If, for example, you rotate one axis,
    you will need to rotate the others the same way. The method
    :py:meth:`normalize` is provided to correct small deviations.

    When rendering a scene, only the order of the first three axes matter. The
    first points right, the second up, and the third forward. Rays are cast from
    the origin of the camera onto a 2-dimensional image, forming a 3-dimension
    frustum.

    :param integer dimension: The dimension of the camera.

    .. py:method:: normalize()

        Adjust the values in :py:attr:`axes` so that every value is a unit
        vector and is orthogonal to each other.

        The limited precision in floating point values means that every time the
        axes are transformed, they may deviate slightly in length and from being
        orthogonal. If multiple transformations are applied, the deviation can
        accumulate and become noticeable. This method will correct any deviation
        as long as all the axes remain linearly independent (otherwise the
        effect is undefined).

    .. py:method:: translate(offset)

        Move the camera using the local coordinate space.
        
        Given camera ``c``, this is equivalent to :code:`for i in
        range(c.dimension): c.origin += c.axes[i] * offset[i]`.

        :param vector offset:
        
    .. py:method:: transform(m)
    
        Rotate the camera using matrix ``m``.

    .. py:attribute:: axes

        A sequence of vectors specifying the axes.

    .. py:attribute:: dimension

        The dimension of the camera.

    .. py:attribute:: origin

        A vector specifying the location of the camera.


.. py:class:: CameraAxes

    The axes of a :py:class:`Camera` object.

    This class can not be instantiated directly in Python code.

    .. py:method:: __getitem__(index)

        :code:`self.__getitem__(i)` <==> :code:`self[i]`

    .. py:method:: __len__()

        :code:`self.__len__()` <==> :code:`len(self)`

    .. py:method:: __setitem__(index,value)

        :code:`self.__setitem__(i,v)` <==> :code:`self[i]=v`


.. py:class:: CompositeScene(aabb_min,aabb_max,data)

    Bases: :py:class:`.render.Scene`

    A scene that displays the contents of a k-d tree.
    
    You normally don't need to create this object directly, but instead call
    :py:func:`.kdtree_builder.build_composite_scene`.

    :param vector aabb_min: The minimum extent of the axis-aligned bounding-box
        that encloses all the primitives of the scene.
    :param vector aabb_max: The maximum extent of the axis-aligned bounding-box
        that encloses all the primitives of the scene.
    :param KDNode data: The root node of a k-d tree.

    .. py:method:: get_camera() -> Camera

        Return a copy of the scene's camera.

    .. py:method:: set_camera(camera)

        Set the scene's camera to a copy of the provided value.

        If the scene has been locked by a :py:class:`.render.Renderer`, this
        function will raise an exception instead.

        :param camera: An instance of :py:class:`Camera`.

    .. py:method:: set_fov(fov)

        Set the field of vision.

        If the scene has been locked by a :py:class:`.render.Renderer`, this
        function will raise an exception instead.

        :param number fov: The new field of vision in radians.
        
    .. py:method:: set_max_reflect_depth(depth)

        Set the value of :py:attr:`max_reflect_depth`.

        If the scene has been locked by a :py:class:`.render.Renderer`, this
        function will raise an exception instead.

        :param integer depth: The new value.
        
    .. py:attribute:: aabb_max
    
        The maximum extent of the axis-aligned bounding-box that encloses all
        the primitives of the scene.
        
        This attribute is read-only.
    
    .. py:attribute:: aabb_min
    
        The minimum extent of the axis-aligned bounding-box that encloses all
        the primitives of the scene.
        
        This attribute is read-only.

    .. py:attribute:: fov

        The scene's horizontal field of vision in radians.

        This attribute is read-only. To modify the value, use
        :py:meth:`set_fov`.

    .. py:attribute:: locked

        A boolean specifying whether or not the scene is locked.

        This attribute cannot be modified in Python code.
    
    .. py:attribute:: max_reflect_depth

        The maximum number of times a ray is allowed to bounce.
        
        The default value is 4.
        
        Recursive reflections require shooting rays multiple times per pixel,
        thus lower values can improve performance at the cost of image quality.
        A value of 0 disables reflections altogether.

        This attribute is read-only. To modify the value, use
        :py:meth:`set_fov`.
        
    .. py:attribute:: root
    
        The root node of a k-d tree.
        
        This attribute is read-only.


.. py:class:: FrozenVectorView

    A read-only sequence of vectors.

    This class cannot be instantiated directly in Python code.

    .. py:method:: __getitem__(index)

        :code:`self.__getitem__(i)` <==> :code:`self[i]`

    .. py:method:: __len__()

        :code:`self.__len__()` <==> :code:`len(x)`


.. py:class:: KDBranch(axis,split[,left=None,right=None])

    Bases: :py:class:`KDNode`

    A k-d tree branch node.

    One of ``left`` and ``right`` may be ``None``, but not both.

    :param integer axis: The axis that the split hyper-plane is perpendicular
        to.
    :param number split: The location along the axis where the split occurs.
    :param KDNode left: The left node (< split) or ``None``.
    :param KDNode right: The right node (>= split) or ``None``.

    .. py:attribute:: axis

        The axis that the split hyper-plane is perpendicular to.

    .. py:attribute:: dimension

        The dimension of the branch's nodes.

    .. py:attribute:: left

        The left node (< split) or ``None``.

    .. py:attribute:: right

        The right node (>= split) or ``None``.

    .. py:attribute:: split

        The location along the axis where the split occurs.


.. py:class:: KDLeaf(primitives)

    Bases: :py:class:`KDNode`

    A k-d tree leaf node.

    This acts as a container for one or more primitives.

    Instances of this class are read-only.

    :param iterable primitives: A sequence of :py:class:`Primitive` objects.

    .. py:method:: __getitem__(index)

        :code:`self.__getitem__(i)` <==> :code:`self[i]`

    .. py:method:: __len__()

        :code:`self.__len__()` <==> :code:`len(self)`

    .. py:attribute:: dimension

        The dimension of the primitives.


.. py:class:: KDNode

    A k-d tree node.

    This class cannot be instantiated directly in Python code.

    .. py:method:: intersects(origin,direction[,t_near,t_far,source]) -> list

        Tests whether a given ray intersects.

        The return value is a list containing one tuple for every intersection
        that occured. Multiple intersections can occur when the ray passes
        through primitives that have an opacity of less than one. Each tuple
        will contain the distance between ``origin`` and the intersection, the
        point of intersection, the normal of the surface intersected and the
        intersected primitive itself. If an opaque primitive is intersected, it
        will always be the last element and have the greatest distance, but
        every other element will be in an arbitrary order and may contain
        duplicates (this can happen when a primitive crosses a split pane). If
        no intersection occurs, the return value will be an empty list.

        :param vector origin: The origin of the ray.
        :param vector direction: The direction of the ray.
        :param number t_near:
        :param number t_far:
        :param KDNode source: A node that will not be considered for
            intersection.


.. py:class:: Matrix(dimension,values)

    A square matrix.

    Instances of this class are read-only.

    :param integer dimension: The dimension of the matrix.
    :param values: Either a sequence of ``dimension`` sequences of ``dimension``
        numbers or a sequence of ``dimension``:sup:`2` numbers.

    .. py:method:: __getitem__(index)

        :code:`self.__getitem__(i)` <==> :code:`self[i]`
        
        The elements of ``Matrix`` are its rows.

    .. py:method:: __len__()

        Returns the number of rows.

        This is always equal to :py:attr:`dimension`.

        :code:`self.__len__()` <==> :code:`len(self)`

    .. py:method:: __mul__(b)

        :code:`self.__mul__(y)` <==> :code:`self*y`

    .. py:method:: determinant() -> float

        Compute the determinant of the matrix.

    .. py:method:: inverse() -> Matrix

        Compute the inverse of the matrix.

        If the matrix is singular (cannot be inverted), an exception is thrown.

    .. py:method:: transpose() -> Matrix

        Return the transpose of the matrix.

    .. py:staticmethod:: identity(dimension) -> Matrix

        Create an identity matrix.

        :param dimension: The dimension of the new matrix.
        
    .. py:staticmethod:: reflection(axis) -> Matrix
    
        Create a reflection matrix.
        
        The refection is by a hyperplane perpendicular to ``axis`` that passes
        through the origin.
        
        :param vector axis: The axis to reflect along.

    .. py:staticmethod:: rotation(a,b,theta) -> Matrix

        Create a rotation matrix along the plane defined by the
        linearly-independent vectors: ``a`` and ``b``.

        The ability to unambiguously describe a plane with one vector is only
        present in 3-space, thus instead of taking one perpendicular vector,
        this method takes two coplanar vectors.

        The resulting matrix ``M`` is such that :code:`M * p` is equal to:

        .. math::

            (\mathbf{p} \cdot \mathbf{a})(\mathbf{a}(\cos \theta - 1) -
            \mathbf{b} \sin \theta) + (\mathbf{p} \cdot \mathbf{b})(\mathbf{b}
            (\cos \theta - 1) + \mathbf{a} \sin \theta) + \mathbf{p}

        :param vector a:
        :param vector b:
        :param number theta: The rotation amount in radians.

    .. py:staticmethod:: scale(...) -> Matrix

        Creates a scale matrix.

        This method takes either two numbers or one vector. If numbers are
        supplied, the numbers must be the dimension of the matrix followed by a
        magnitude, and the return value will be a uniform scale matrix. If a
        vector is supplied, a non-uniform scale matrix will be returned, where
        the vector components will correspond to the scaling factors of each
        axis.

    .. py:attribute:: dimension

        The dimension of the matrix.

    .. py:attribute:: values

        All the matrix elements as a flat sequence


.. py:class:: MatrixProxy

    A sequence of matrix values.

    This type is returned by :py:meth:`Matrix.__getitem__` and
    :py:attr:`Matrix.values`.

    This class cannot be instantiated directly in Python code.

    .. py:method:: __getitem__(index)

        :code:`self.__getitem__(i)` <==> :code:`self[i]`

    .. py:method:: __len__()

        :code:`self.__len__()` <==> :code:`len(self)`


.. py:class:: Primitive

    A geometric primitive that can be used to construct scenes for the
    ray-tracer.

    Descendants of this class are used in highly optimized multi-threaded C++
    code, thus this class cannot be instantiated directly in Python code, not
    even as a base class for another class.

    .. py:method:: intersects(origin,direction) -> tuple or None

        Tests whether a given ray intersects.

        If the ray intersects with the object, a tuple is returned with the
        distance between ``origin`` and the intersection, the point of
        intersection and the normal vector of the surface at the intersection.
        Otherwise, the return value is ``None``.

        :param origin: The origin of the ray.
        :param direction: The direction of the ray.
        
    .. py:attribute:: material
    
        The material of the primitive.


.. py:class:: PrimitivePrototype

    A primitive with extra data needed for quick spacial partitioning.

    This class cannot be instantiated directly in Python code.

    .. py:method:: realize() -> Primitive

        Return the corresponding :py:class:`Primitive`.

    .. py:attribute:: aabb_max

        A vector specifying the maximum extent of the primitive's axis aligned
        bounding box.

    .. py:attribute:: aabb_min

        A vector specifying the minimum extent of the primitive's axis aligned
        bounding box.
    
    .. py:attribute:: material
    
        The material of the primitive.


.. py:class:: Solid(type,position,orientation,material)

    Bases: :py:class:`Primitive`

    A non-flat geometric primitive.

    It is either a hypercube or a hypersphere.

    Instances of this class are read-only.

    :param type: The type of solid: either :py:data:`.wrapper.CUBE` or
        :py:data:`.wrapper.SPHERE`.
    :param vector position: The position of the solid.
    :param Matrix orientation: A transformation matrix. The matrix must be
        invertable.
    :param Material material: A material to apply to the solid.

    .. py:attribute:: dimension

        The dimension of the solid.

    .. py:attribute:: inv_orientation

        The inverse of :py:attr:`orientation`

    .. py:attribute:: orientation

        A transformation matrix applied to the solid.

    .. py:attribute:: position

        A vector specifying the position of the solid.

    .. py:attribute:: type

        The type of solid: either :py:data:`.wrapper.CUBE` or
        :py:data:`.wrapper.SPHERE`


.. py:class:: SolidPrototype(type,position,orientation,material)

    Bases: :py:class:`PrimitivePrototype`

    A solid with extra data needed for quick spacial partitioning.

    Instances of this class are read-only.

    :param type: The type of solid: either :py:data:`.wrapper.CUBE` or
        :py:data:`.wrapper.SPHERE`.
    :param vector position: The position of the solid.
    :param Matrix orientation: A transformation matrix. The matrix must be
        invertable.
    :param Material material: A material to apply to the solid.

    .. py:attribute:: dimension

        The dimension of the solid.

    .. py:attribute:: inv_orientation

        The inverse of :py:attr:`orientation`

    .. py:attribute:: orientation

        A transformation matrix applied to the solid.

    .. py:attribute:: position

        A vector specifying the position of the solid.

    .. py:attribute:: type

        The type of solid: either :py:data:`.wrapper.CUBE` or
        :py:data:`.wrapper.SPHERE`.


.. py:class:: Triangle(p1,face_normal,edge_normals,material)

    Bases: :py:class:`Primitive`

    A simplex of codimension 1.

    Note: it is unlikely that you will need to call this constructor directly.
    It requires very specific values that :py:meth:`from_points` can calculate
    for you.

    Despite the name, this is only a triangle in 3-space. Although this serves
    as an analogue to a triangle. Even with more than 3 dimensions, this shape
    is always *flat*. Rays will intersect the body of the simplex and not it's
    perimeter/hull.

    Instances of this class are read-only.

    :param vector p1:
    :param vector face_normal:
    :param iterable edge_normals:
    :param Material material: A material to apply to the simplex.

    .. py:staticmethod:: from_points(points,material) -> Triangle

        Create a :py:class:`Triangle` object from the vertices of the simplex.

        :param points: A sequence of vectors specifying the vertices. The number
            of vectors must equal their dimension.
        :param Material material: A material to apply to the simplex.

    .. py:attribute:: d

        A value that equals :math:`-\text{face\_normal} \cdot \text{p1}`

        This is simply a pre-computed value needed by the ray-tracer.

    .. py:attribute:: dimension

        The dimension of the simplex's coordinates.

        This is always one greater than the dimension of the simplex. It
        corresponds to the dimension of the space it can occupy.

    .. py:attribute:: edge_normals

    .. py:attribute:: face_normal

    .. py:attribute:: p1

        A vertex of the simplex.

        Due to the way the ray-simplex intersection code works, this is the only
        point that's kept.


.. py:class:: TrianglePointData

    A read-only sequence of :py:class:`TrianglePointDatum` instances.

    .. py:method:: __getitem__(index)

        :code:`self.__getitem__(i)` <==> :code:`self[i]`

    .. py:method:: __len__()

        :code:`self.__len__()` <==> :code:`len(self)`


.. py:class:: TrianglePointDatum

    .. py:attribute:: edge_normal

    .. py:attribute:: point

        A simplex vertex.


.. py:class:: TrianglePrototype(points,material)

    Bases: :py:class:`PrimitivePrototype`

    A simplex with extra data needed for quick spacial partitioning.

    Unlike the :py:class:`Triangle` class, this keeps the vertices that make up
    the simplex.

    Instances of this class are read-only.

    :param points: A sequence of vectors specifying the vertices of the simplex.
        The number of vectors must equal their dimension.
    :param Material material: A material to apply to the simplex.

    .. py:attribute:: dimension

        Has the same meaning as :py:attr:`Triangle.dimension`.

    .. py:attribute:: face_normal

    .. py:attribute:: point_data


.. py:class:: Vector(dimension[,values])

    A vector class for representing positions and directions.

    Instances of this class are read-only.

    :param integer dimension: The dimension of the vector.
    :param values: A sequence of ``dimension`` numbers. If omitted, all elements
        of the vector will be zero.

    .. py:method:: __abs__()

        Return the magnitude of the vector.

        This is the same as :py:meth:`absolute`.

        :code:`self.__abs__()` <==> :code:`abs(self)`

    .. py:method:: __add__(b)

        :code:`self.__add__(y)` <==> :code:`self+y`
        
    .. py:method:: __div__(b)
    
        Divide every element of the vector by ``b``.
    
        :code:`self.__div__(y)` <==> :code:`self/y`

    .. py:method:: __eq__(b)

        :code:`self.__eq__(y)` <==> :code:`self==y`

    .. py:method:: __getitem__(index)

        :code:`self.__getitem__(i)` <==> :code:`self[i]`

    .. py:method:: __len__()

        Return the vector size.

        This is always equal to :py:attr:`dimension`.

        :code:`self.__len__()` <==> :code:`len(self)`

    .. py:method:: __mul__(b)

        Multiply every element of the vector with ``b``.

        This is only for scalar products. For vector products, see
        :py:func:`dot` and :py:func:`cross`.

        :code:`self.__mul__(y)` <==> :code:`self*y`

    .. py:method:: __ne__(b)

        :code:`self.__ne__(y)` <==> :code:`self!=y`

    .. py:method:: __neg__()

        :code:`self.__neg__()` <==> :code:`-self`

    .. py:method:: __repr__()

        :code:`self.__repr__()` <==> :code:`repr(self)`

    .. py:method:: __rmul__(b)

        This is the same as :py:meth:`__mul__`.

        :code:`self.__rmul__(y)` <==> :code:`y*self`

    .. py:method:: __str__()

        :code:`self.__str__()` <==> :code:`str(self)`

    .. py:method:: __sub__(b)

        :code:`self.__sub__(y)` <==> :code:`self-y`

    .. py:method:: absolute() -> float

        Return the magnitude of the vector.

        This is the same as :py:meth:`__abs__`.

    .. py:method:: apply(f) -> Vector

        Return a vector with the given function applied to each value.

        Equivalent to: :code:`Vector(self.dimension,map(f,self))`

        :param f: A function or callable object that takes one number and
            returns a number.

    .. py:method:: set_c(index,value) -> Vector

        Return a copy of the vector with the ``index``'th item replaced with
        ``value``.

    .. py:method:: square() -> float

        Return the magnitude squared of the vector.

        Equivalent to: :code:`dot(self,self)`

    .. py:method:: unit() -> Vector

        Return the corresponding unit vector.

        Equivalent to: :code:`self / self.absolute()`

    .. py:staticmethod:: axis(dimension,axis[,length=1]) -> Vector

        Create an axis vector.

        Every element of the vector will be zero, except the element at
        ``axis``, which will be ``length``.

    .. py:attribute:: dimension

        The dimension of the vector.


.. py:function:: cross(vectors) -> Vector

    A generalized cross product.

    :param vectors: A sequence of linearly independent vectors. The number of
        vectors must be one less than their dimensionality.

.. py:function:: dot(a,b) -> float

    Compute the dot product of two vectors.



:mod:`kdtree_builder` Module
----------------------------

.. py:module:: ntracer.kdtree_builder

.. autofunction:: build_kdtree

.. autofunction:: build_composite_scene



:mod:`wrapper` Module
---------------------

.. py:module:: ntracer.wrapper

.. autoclass:: NTracer(dimension)

.. py:data:: CUBE

.. py:data:: SPHERE



:mod:`wavefront_obj` Module
---------------------------

.. automodule:: ntracer.wavefront_obj
    :members: FileFormatError, load_obj
    :undoc-members:



:mod:`tracer3, tracer4, tracer5, ...` Modules
---------------------------------------------

Depending on how this package was built, there will be one or more modules
that start with "tracer" and end in a number (by default, one for every number
between 3 and 8).  These have an interface identical to :py:mod:`.tracern`,
including the ``dimension`` parameter of certain functions, but are
specialized for the dimensionality corresponding to the number in its name.
For functions that require a ``dimension`` parameter, passing a number other
than the one its specialized for, will raise an exception.

By only supporting a single dimensionality, these specialized modules avoid the
looping and heap-allocation that the generic version requires and thus perform
significantly faster.

To save space, the modules corresponding to dimensionalities you don't need, or
even all the specialized modules, can be deleted with no loss of functionality.
This package can also work without the generic version, using only specialized
versions, but you would only be able to use those particular dimensionalities.

Note that equivalent types between the generic and specific versions are not
interchangeable with each other.

