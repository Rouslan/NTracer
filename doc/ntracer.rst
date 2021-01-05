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

.. py:class:: BlockingRenderer([threads=-1])

    A synchronous scene renderer.

    By default, this class uses as many threads are the are processing cores.
    The scene can be drawn on any writable object supporting the buffer
    protocol. :py:meth:`signal_abort` can be called from another thread to quit
    drawing early.

    :param integer threads: The number of threads to use *in addition* to the
        thread from which it's called. If -1, the number of extra threads will
        be one minus the number of processing cores of the machine.

    .. py:method:: signal_abort()

        Signal for the renderer to quit and return immediately.

        If the renderer isn't running, this does nothing.

    .. py:method:: render(dest,format,scene) -> boolean

        Render ``scene`` onto ``dest``.

        If the renderer is already running on another thread, an exception is
        thrown instead. Upon starting, the scene will be locked for writing.

        The return value will be ``True`` unless the renderer quit before
        finishing because of a call to :py:meth:`signal_abort`, in which case
        the return value will be ``False``.

        :param dest: An object supporting the buffer protocol to draw onto.
        :param format: The dimensions and pixel format of ``dest``.
        :param scene: The scene to draw.
        :type format: :py:class:`ImageFormat`
        :type scene: :py:class:`Scene`


.. py:class:: CallbackRenderer([threads=0])

    An asynchronous scene renderer.

    By default, this class uses as many threads are the are processing cores.
    The scene can be drawn on any writable object supporting the buffer protocol
    (such as ``bytearray``) and a callback function is invoked when finished.

    :param integer threads: The number of threads to use. If zero, the number of
        threads will equal the number of processing cores of the machine.

    .. py:method:: abort_render()

        Signal for the renderer to quit and wait until all drawing has stopped
        and the scene has been unlocked.

        The callback function passed to :py:meth:`begin_render` will not be
        called if the renderer doesn't finish drawing.

        If the renderer isn't running, this does nothing.

    .. py:method:: begin_render(dest,format,scene,callback)

        Begin rendering ``scene`` onto ``dest``.

        If the renderer is already running, an exception is thrown instead. Upon
        starting, the scene will be locked for writing.

        :param dest: An object supporting the buffer protocol to draw onto.
        :param format: The dimensions and pixel format of ``dest``.
        :param scene: The scene to draw.
        :param callback: A function taking one parameter to call when rendering
            is done. The parameter will be the renderer itself.
        :type format: :py:class:`ImageFormat`
        :type scene: :py:class:`Scene`


.. py:class:: Channel(bit_size,f_r,f_g,f_b[,f_c=0,tfloat=False])

    A representation of a color channel.

    This is used by :py:class:`ImageFormat` to specify how pixels are stored.

    All colors are computed internally using three 32-bit floating point
    numbers, representing red, green and blue. An instance of ``Channel``
    specifies how to convert a color to a component of the destination format.
    For a given color "c", the output will be :code:`f_r*c.r + f_g*c.g + f_b*c.b
    + f_c` and is clamped between 0 and 1. If ``tfloat`` is false, the value is
    multiplied by 2\ :sup:`bit_size`\ −1 and converted to an integer.

    Instances of this class are read-only.

    :param integer bit_size: The number of bits the channel takes up. If
        ``tfloat`` is false, it can be between 1 and 31. If ``tfloat`` is true
        it must be 32.
    :param number f_r: The red factor.
    :param number f_g: The green factor.
    :param number f_b: The blue factor.
    :param number f_c: A constant to add.
    :param boolean tfloat: Whether the channel is stored as an integer or
        floating point number.

    .. py:attribute:: bit_size

        The number of bits the channel takes up.

    .. py:attribute:: f_r

        The red factor.

    .. py:attribute:: f_g

        The green factor.

    .. py:attribute:: f_b

        The blue factor.

    .. py:attribute:: f_c

        A constant to add.

    .. py:attribute:: tfloat

        Whether the channel is stored as an integer or floating point number.


.. py:class:: ChannelList

    The channels of an :py:class:`ImageFormat` object.

    This class can not be instantiated directly in Python code.

    .. py:method:: __getitem__(index)

        :code:`self.__getitem__(i)` <==> :code:`self[i]`

    .. py:method:: __len__()

        :code:`self.__len__()` <==> :code:`len(self)`


.. py:class:: Color(r,g,b)

    A red-green-blue triplet specifying a color.

    For each component, zero represents its minimum value and one represents its
    maximum value. Therefore, :code:`Color(0,0,0)` represents black,
    :code:`Color(1,1,1)` represents white and :code:`Color(0,0,0.5)` represents
    dark blue.

    Although values outside of 0-1 are allowed, they are clipped to the normal
    range when finally drawn. Such values will, however, affect how reflections
    and transparency are calculated.

    Instances of this class are read-only.

    :param number r: Red component
    :param number g: Green component
    :param number b: Blue component

    .. py:method:: __add__(b)

        Element-wise addition of two colors.

        :code:`self.__add__(y)` <==> :code:`self+y`

    .. py:method:: __div__(b)

        Divide each element by a number or do element-wise division of two
        colors.

        :code:`self.__div__(y)` <==> :code:`self/y`

    .. py:method:: __eq__(b)

        :code:`self.__eq__(y)` <==> :code:`self==y`

    .. py:method:: __getitem__(index)

        Equivalent to :code:`[self.r,self.g,self.b].__getitem__`.

        :code:`self.__getitem__(i)` <==> :code:`self[i]`

    .. py:method:: __len__()

        This always returns 3.

        :code:`self.__len__()` <==> :code:`len(self)`

    .. py:method:: __mul__(b)

        Multiply each element by a number or do element-wise multiplication of
        two colors.

        :code:`self.__mul__(y)` <==> :code:`self*y`

    .. py:method:: __ne__(b)

        :code:`self.__ne__(y)` <==> :code:`self!=y`

    .. py:method:: __neg__()

        Negate each element.

        :code:`self.__neg__()` <==> :code:`-self`

    .. py:method:: __repr__()

        :code:`self.__repr__()` <==> :code:`repr(self)`

    .. py:method:: __rmul__(b)

        This is the same as :py:meth:`__mul__`.

        :code:`self.__rmul__(y)` <==> :code:`y*self`

    .. py:method:: __sub__(b)

        Element-wise subtraction of two colors.

        :code:`self.__sub__(y)` <==> :code:`self-y`

    .. py:method:: apply(f) -> Color

        Return a color with the given function applied to each component.

        Equivalent to: :code:`Color(*map(f,self))`

        :param f: A function or callable object that takes one number and
            returns a number.

    .. py:attribute:: r

        Red component

    .. py:attribute:: g

        Green component

    .. py:attribute:: b

        Blue component


.. py:class:: ImageFormat(width,height,channels[,pitch=0,reversed=False])

    The dimensions and pixel format of an image.

    The pixel format is specified by one or more instances of
    :py:class:`Channel`. Each channel describes how to convert a red-green-blue
    triplet into the associated pixel component and has a bit size. When drawing
    a pixel, a renderer will write each component, one after the other without
    leaving any gaps. However, each pixel will start on a new byte. If the last
    byte is not completely covered by the channels, the remaining bits will be
    set to zero.

    The size of a pixel may not exceed 16 bytes (128 bits).

    Some examples of pixel formats and their associated channel sequences:

    +----------------------------+---------------------------------------------+
    |24-bit RGB                  |:code:`[Channel(8,1,0,0),                    |
    |                            |Channel(8,0,1,0),                            |
    |                            |Channel(8,0,0,1)]`                           |
    +----------------------------+---------------------------------------------+
    |32-bit RGBA with full alpha |:code:`[Channel(8,1,0,0),                    |
    |                            |Channel(8,0,1,0),                            |
    |                            |Channel(8,0,0,1),                            |
    |                            |Channel(8,0,0,0,1)]`                         |
    +----------------------------+---------------------------------------------+
    |16-bit 5-5-5 RGB (the last  |:code:`[Channel(5,1,0,0),                    |
    |bit is unused)              |Channel(5,0,1,0),                            |
    |                            |Channel(5,0,0,1)]`                           |
    +----------------------------+---------------------------------------------+
    |16-bit 5-6-5 RGB            |:code:`[Channel(5,1,0,0),                    |
    |                            |Channel(6,0,1,0),                            |
    |                            |Channel(5,0,0,1)]`                           |
    +----------------------------+---------------------------------------------+
    |the native internal         |:code:`[Channel(32,1,0,0,0,True),            |
    |representation              |Channel(32,0,1,0,0,True),                    |
    |                            |Channel(32,0,0,1,0,True)]`                   |
    +----------------------------+---------------------------------------------+
    |digital |YCrCb| (ITU-R      |:code:`[Channel(8,0.299,0.587,0.114,0.0625), |
    |BT.601 conversion)          |Channel(8,-0.147,-0.289,0.436,0.5),          |
    |                            |Channel(8,0.615,-0.515,-0.1,0.5)]`           |
    +----------------------------+---------------------------------------------+
    |16-bit brightness only      |:code:`[Channel(16,0.299,0.587,0.114)]`      |
    +----------------------------+---------------------------------------------+

    .. |YCrCb| replace:: YC\ :sub:`R`\ C\ :sub:`B`

    :param integer width: The width of the image in pixels.
    :param integer height: The height of the image in pixels.
    :param channels: An iterable containing one or more instances of
        :py:class:`Channel`, describing the bit layout of a pixel.
    :param integer pitch: The number of bytes per row. If zero is passed, it
        will be set to ``width`` times the byte width of one pixel (calculated
        from ``channels``).
    :param boolean reversed: If true, the bytes of each pixel will be written in
        reverse order. This is needed if storing pixels as little-endian words
        and the channels don't fit neatly into bytes.

    .. py:method:: set_channels(new_channels)

        Replace the contents of :py:attr:`channels`.

        :param channels: An iterable containing one or more instances of
            :py:class:`Channel`, describing the bit layout of a pixel.

    .. py:attribute:: width

        The width of the image in pixels.

    .. py:attribute:: height

        The height of the image in pixels.

    .. py:attribute:: channels

        An read-only list-like object containing one or more instances of
        :py:class:`Channel`, describing the bit layout of a pixel.

    .. py:attribute:: pitch

        The number of bytes per row.

    .. py:attribute:: reversed

        If true, the bytes of each pixel will be written in reverse order (like
        a little-endian word).

    .. py:attribute:: bytes_per_pixel

        The byte size of one pixel.

        This is the sum of the bit sizes of the channels, rounded up.

        This attribute is read-only.


.. py:class:: LockedError(*args)

    The exception thrown when attempting to modify a locked scene.


.. py:class:: Material(color[,opacity=1,reflectivity=0,specular_intensity=1,specular_exp=8,specular_color=(1,1,1)])

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


.. py:class:: Scene

    A scene that :py:class:`Renderer` can render.

    Although not exposed to Python code, the scene class has a concept of
    locking. While a renderer is drawing a scene, the scene is locked. While
    locked, a scene cannot be modified. Attempting to do so will raise a
    :py:class:`LockedError` exception.

    This cannot be instantiated in Python code, not even as a base class.

    .. py:method:: calculate_color(x,y,width,height) -> Color

        Get the pixel color at a particular coordinate.

        Coordinate ``0,0`` is the top left pixel and ``width-1,height-1`` is the
        bottom right.

        :param integer x: The horizontal coordinate component.
        :param integer y: The vertical coordinate component.
        :param integer width: The pixel width of the image.
        :param integer height: The pixel height of the image.


.. py:function:: get_optimized_tracern(dimension)

    Return a specialized (read: faster) tracer\ *{dimension}* version if it
    exists, otherwise return :py:mod:`.tracern`.

    The results are cached, so calling this function multiple times with the
    same parameter is fast. The cache does not increase the reference count and
    unloaded modules automatically remove themselves from the cache.



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


.. py:class:: AABB(dimension[,start,end])

    An axis-aligned bounding box.

    This is not a displayable primitive, but is instead meant for spacial
    partitioning of the primitives.

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
            :py:class:`Primitive`/:py:class:`PrimitiveBatch`.

    .. py:method:: intersects_flat(primitive,skip) -> boolean

        Returns True if the box intersects the given simplex, ignoring one axis.

        This method is identical to :py:meth:`intersects` except it only accepts
        instances of :py:class:`TrianglePrototype` and
        :py:class:`TriangleBatchPrototype` and it disregards the axis ``skip``.
        This is equivalent to testing against a simplex that has been extruded
        infinitely far in the positive and negative directions along that axis.
        The simplex or batch of simplexes **must** be flat along that axis (i.e.
        :code:`primitive.boundary.start[skip] == primitive.boundary.end[skip]`
        must be true) for the return value to be correct.

        This method is needed when a simplex is completely embedded in a split
        hyperplane and thus would fail the normal intersection test with any
        bounding box that the hyperplane divides.

        :param primitive: A simplex to test intersection with. It must be an
            instance of :py:class:`TrianglePrototype` or
            :py:class:`TriangleBatchPrototype`, not
            :py:class:`Triangle`/:py:class:`TriangleBatch`.
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

        A vector specifying the maximum extent of the box.

    .. py:attribute:: start

        A vector specifying the minimum extent of the box.


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

        If the scene has been locked by a renderer, this function will raise a
        :py:class:`.render.LockedError` exception instead.

    .. py:method:: set_fov(fov)

        Set the field of vision.

        If the scene has been locked by a renderer, this function will raise a
        :py:class:`.render.LockedError` exception instead.

        :param fov: The new field of vision in radians.

    .. py:atrribute:: dimension

        The dimension of the scene.

        This attribute is read-only.

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


.. py:class:: CompositeScene(boundary,data)

    Bases: :py:class:`.render.Scene`

    A scene that displays the contents of a k-d tree.

    You normally don't need to create this object directly, but instead call
    :py:func:`build_composite_scene`.

    :param boundary: The axis-aligned bounding-box that encloses all the
        primitives of the scene.
    :param data: The root node of a k-d tree.
    :type boundary: :py:class:`AABB`
    :type data: :py:class:`KDNode`

    .. py:method:: add_light(light)

        Add a light to the scene.

        The light will be added to :py:attr:`global_lights` or
        :py:attr:`point_lights` according to its type.

        If the scene has been locked by a renderer, this method will raise a
        :py:class:`.render.LockedError` exception instead.

        :param light: An instance of :py:class:`GlobalLight` or
            :py:class:`PointLight`.

    .. py:method:: get_camera() -> Camera

        Return a copy of the scene's camera.

    .. py:method:: set_ambient_color(color)

        Set the value of :py:attr:`ambient_color`

        If the scene has been locked by a renderer, this method will raise a
        :py:class:`.render.LockedError` exception instead.

        :param color: An instance of :py:class:`.render.Color` or a tuple with
            three numbers.

    .. py:method:: set_background(c1[,c2=c1,c3=c1,axis=1])

        Set the values of :py:attr:`bg1`, :py:attr:`bg2`, :py:attr:`bg3` and
        :py:attr:`bg_gradient_axis`.

        If the scene has been locked by a renderer, this method will raise a
        :py:class:`.render.LockedError` exception instead.

        :param color c1: The new value for :py:attr:`bg1`.
        :param color c2: The new value for :py:attr:`bg2`.
        :param color c3: The new value for :py:attr:`bg3`.
        :param integer axis: The new value for :py:attr:`bg_gradient_axis`. This
            must be a value between 0 and dimension−1.

    .. py:method:: set_camera(camera)

        Set the scene's camera to a copy of the provided value.

        If the scene has been locked by a renderer, this method will raise a
        :py:class:`.render.LockedError` exception instead.

        :param camera: An instance of :py:class:`Camera`.

    .. py:method:: set_camera_light(camera_light)

        Set the value of :py:attr:`camera_light`

        If the scene has been locked by a renderer, this method will raise a
        :py:class:`.render.LockedError` exception instead.

        :param boolean camera_light: The new value.

    .. py:method:: set_fov(fov)

        Set the field of vision.

        If the scene has been locked by a renderer, this method will raise a
        :py:class:`.render.LockedError` exception instead.

        :param number fov: The new field of vision in radians.

    .. py:method:: set_max_reflect_depth(depth)

        Set the value of :py:attr:`max_reflect_depth`.

        If the scene has been locked by a renderer, this method will raise a
        :py:class:`.render.LockedError` exception instead.

        :param integer depth: The new value.

    .. py:method:: set_shadows(shadows)

        Set the value of :py:attr:`shadows`

        If the scene has been locked by a renderer, this method will raise a
        :py:class:`.render.LockedError` exception instead.

        :param boolean shadows: The new value.

    .. py:attribute:: ambient_color

        The color of the ambient light.

        This light reaches all geometry unconditionally.

        The default value is ``Color(0,0,0)``.

        This attribute is read-only. To modify the value, use
        :py:meth:`set_ambient_color`.

    .. py:attribute:: bg_gradient_axis

        The index of the axis along which the three color gradient of the
        background will run.

        The default value is 1, corresponding to the y-axis.

        This attribute is read-only. To modify the value, use
        :py:meth:`set_background`.

    .. py:attribute:: bg1

        The first color of the three color gradient of the background.

        The default value is ``Color(1,1,1)``.

        This attribute is read-only. To modify the value, use
        :py:meth:`set_background`.

    .. py:attribute:: bg2

        The middle color of the three color gradient of the background.

        The default value is ``Color(0,0,0)``.

        This attribute is read-only. To modify the value, use
        :py:meth:`set_background`.

    .. py:attribute:: bg3

        The last color of the three color gradient of the background.

        The default value is ``Color(0,1,1)``.

        This attribute is read-only. To modify the value, use
        :py:meth:`set_background`.

    .. py:attribute:: boundary

        The :py:class:`AABB` that encloses all the primitives of the
        scene.

        This attribute is read-only.

    .. py:attribute:: camera_light

        A boolean specifying whether surfaces will be lit if they face the
        camera.

        This is equivalent to having an instance of :py:class:`GlobalLight` with
        :py:attr:`GlobalLight.color` set to ``Color(1,1,1)`` and
        :py:attr:`GlobalLight.direction` set to the direction that the camera is
        facing, except this light never casts shadows.

        The default value is ``True``.

        This attribute is read-only. To modify the value, use
        :py:meth:`set_camera_light`.

    .. py:atrribute:: dimension

        The dimension of the scene.

        This attribute is read-only.

    .. py:attribute:: fov

        The scene's horizontal field of vision in radians.

        This attribute is read-only. To modify the value, use
        :py:meth:`set_fov`.

    .. py:attribute:: global_lights

        A list-like object containing intances of :py:class:`GlobalLight`.

        See :py:class:`GlobalLightList` for details.

    .. py:attribute:: locked

        A boolean specifying whether or not the scene is locked.

        This attribute is read-only.

    .. py:attribute:: max_reflect_depth

        The maximum number of times a ray is allowed to bounce.

        The default value is 4.

        Recursive reflections require shooting rays multiple times per pixel,
        thus lower values can improve performance at the cost of image quality.
        A value of 0 disables reflections altogether.

        This attribute is read-only. To modify the value, use
        :py:meth:`set_max_reflect_depth`.

    .. py:attribute:: point_lights

        A list-like object containing instances of :py:class:`PointLight`.

        See :py:class:`PointLightList` for details.

    .. py:attribute:: root

        The root node of a k-d tree.

        This attribute is read-only.

    .. py:attribute:: shadows

        A boolean specifying whether objects will cast shadows.

        Note: this only applies to lights explicitly added, not the default
        camera light (see :py:attr:`camera_light`).

        The default value is ``False``.

        This attribute is read-only. To modify the value, use
        :py:meth:`set_shadows`.


.. py:class:: FrozenVectorView

    A read-only sequence of vectors.

    This class cannot be instantiated directly in Python code.

    .. py:method:: __getitem__(index)

        :code:`self.__getitem__(i)` <==> :code:`self[i]`

    .. py:method:: __len__()

        :code:`self.__len__()` <==> :code:`len(self)`


.. py:class:: GlobalLight(direction,color)

    A light whose source is infinitely far from the scene's origin.

    This is an approximation of a distant light source such as the sun.

    :param vector direction: The direction that the light's rays travel (i.e.
        the light will be located at :math:`-\text{direction} \times \infty`).
    :param color: The light's color. This can be an instance of
        :py:class:`.render.Color` or a tuple with three numbers.

    .. py:attribute:: color

        The light's color.

    .. py:attribute:: dimension

        The dimension of :py:attr:`direction`.

    .. py:attribute:: direction

        The direction that the light's rays travel (i.e. the light source will
        be located at :math:`-\text{direction} \times \infty`).


.. py:class:: GlobalLightList

    An array of :py:class:`GlobalLight` objects.

    An instance of this class is tied to a specific :py:class:`CompositeScene`
    instance. Any attempt to modify an instance of this class while the scene is
    locked will cause an exception to be raised.

    Since the order of lights is not important, when deleting an element,
    instead of shifting all subsequent elements back, the gap is filled with the
    last element (unless the last element is the one being deleted).

    This class cannot be instantiated directly in Python code.

    .. py:method:: __getitem__(index)

        :code:`self.__getitem__(i)` <==> :code:`self[i]`

    .. py:method:: __len__()

        :code:`self.__len__()` <==> :code:`len(self)`

    .. py:method:: __setitem__(index,value)

        :code:`self.__setitem__(i,v)` <==> :code:`self[i]=v`

    .. py:method:: append(light)

        Add a light.

    .. py:method:: extend(lights)

        Add lights from an iterable object.


.. py:class:: KDBranch(axis,split[,left=None,right=None])

    Bases: :py:class:`KDNode`

    A k-d tree branch node.

    One of ``left`` and ``right`` may be ``None``, but not both.

    .. note:: In order to minimize the amount of space that k-d tree nodes take
        up in memory (and therefore maximize the speed at which they can be
        traversed), the nodes are not stored internally as Python objects nor
        contain references to their Python representations. Accessing
        :py:attr:`left` or :py:attr:`right` will cause a new Python object to be
        created each time, to encapsulate the child node, therefore e.g. given
        node ``n``: the satement ":code:`n.left is n.left`" will evaluate to
        ``False``.

    :param integer axis: The axis that the split hyper-plane is perpendicular
        to.
    :param number split: The location along the axis where the split occurs.
    :param left: The left node (< split) or ``None``.
    :param right: The right node (>= split) or ``None``.
    :type left: :py:class:`KDNode`
    :type right: :py:class:`KDNode`

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

    :param iterable primitives: An iterable of :py:class:`Primitive` objects. If
        :py:const:`BATCH_SIZE` is greater than ``1``, the iterable can also
        yield instances of :py:class:`PrimitiveBatch`.

    .. py:method:: __getitem__(index)

        Return the ``index``'th primitive or primitive batch.

        Note that the order the primitives/batches are stored in will not
        necessarily match the order given to the constructor.

        :code:`self.__getitem__(i)` <==> :code:`self[i]`

    .. py:method:: __len__()

        :code:`self.__len__()` <==> :code:`len(self)`

    .. py:attribute:: dimension

        The dimension of the primitives.

        All the primitives are required to have the same dimension.


.. py:class:: KDNode

    A k-d tree node.

    This class cannot be instantiated directly in Python code.

    .. py:method:: intersects(origin,direction[,t_near,t_far,source,batch_index]) -> list

        Tests whether a given ray intersects.

        The return value is a list containing an instance of
        :py:class:`RayIntersection` for every intersection that occured.
        Multiple intersections can occur when the ray passes through primitives
        that have an opacity of less than one. If an opaque primitive is
        intersected, it will always be the last element and have the greatest
        distance, but every other element will be in an arbitrary order and may
        contain duplicates (this can happen when a primitive crosses a split
        pane). If no intersection occurs, the return value will be an empty
        list.

        :param vector origin: The origin of the ray.
        :param vector direction: The direction of the ray.
        :param number t_near:
        :param number t_far:
        :param source: A primitive that will not be considered for intersection.
        :param integer batch_index: The index of the primitive inside the
            primitive batch to ignore intersection with. If ``source`` is not an
            instance of :py:class:`PrimitiveBatch`, this value is ignored.
        :type source: :py:class:`Primitive` or :py:class:`PrimitiveBatch`

    .. py:method:: occludes(origin,direction[,distance,t_near,t_far,source,batch_index]) -> tuple

        Test if :code:`origin + direction*distance` is occluded by any
        primitives.

        If an opaque object exists at any point along ``distance``, the return
        value is :code:`(True,None)`. Otherwise the return value is a tuple
        containing ``False`` and a list containing an instance of
        :py:class:`RayIntersection` for every non-opaque primitive found along
        ``distance``. The elements will be in an arbitrary order and may contain
        duplicates (this can happen when a primitive crosses a split pane).

        :param vector origin: The origin of the ray.
        :param vector direction: The direction of the ray.
        :param number distance: How far out to check for intersections.
        :param number t_near:
        :param number t_far:
        :param source: A primitive that will not be considered for intersection.
        :param integer batch_index: The index of the primitive inside the
            primitive batch to ignore intersection with. If ``source`` is not an
            instance of :py:class:`PrimitiveBatch`, this value is ignored.
        :type source: :py:class:`Primitive` or :py:class:`PrimitiveBatch`


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


.. py:class:: PointLight(position,color)

    A light source that emits light uniformly in every direction from a given
    point.

    :py:attr:`color` represents not only the light's color, but its brightness,
    too, thus its ``r`` ``g`` ``b`` components may be much greater than 1.

    The intensity of the light at a given point depends on the distance from
    :py:attr:`position` and is given by the formula:

    .. math::

        \text{color} \times \frac{1}{\text{distance}^{\text{dimension} - 1}}

    :param vector position: The position of the light.
    :param color: The light's color multiplied by its brightness. This can be an
        instance of :py:class:`.render.Color` or a tuple with three numbers.

    .. py:attribute:: color

        The light's color multiplied by its brightness.

    .. py:attribute:: dimension

        The dimension of :py:attr:`position`.

    .. py:attribute:: position

        The position of the light.


.. py:class:: PointLightList

    An array of :py:class:`PointLight` objects.

    An instance of this class is tied to a specific :py:class:`CompositeScene`
    instance. Any attempt to modify an instance of this class while the scene is
    locked will cause an exception to be raised.

    Since the order of lights is not important, when deleting an element,
    instead of shifting all subsequent elements back, the gap is filled with the
    last element (unless the last element is the one being deleted).

    This class cannot be instantiated directly in Python code.

    .. py:method:: __getitem__(index)

        :code:`self.__getitem__(i)` <==> :code:`self[i]`

    .. py:method:: __len__()

        :code:`self.__len__()` <==> :code:`len(self)`

    .. py:method:: __setitem__(index,value)

        :code:`self.__setitem__(i,v)` <==> :code:`self[i]=v`

    .. py:method:: append(light)

        Add a light.

    .. py:method:: extend(lights)

        Add lights from an iterable object.


.. py:class:: Primitive

    A geometric primitive that can be used to construct scenes for the
    ray-tracer.

    Descendants of this class are used in highly optimized multi-threaded C++
    code, thus this class cannot be instantiated directly in Python code, not
    even as a base class for another class.

    .. py:method:: intersects(origin,direction) -> tuple or None

        Tests whether a given ray intersects.

        If the ray intersects with the object, an instance of
        :py:class:`RayIntersection` is returned with the details of the
        intersection. Otherwise, the return value is ``None``.

        :param origin: The origin of the ray.
        :param direction: The direction of the ray.

    .. py:attribute:: material

        The material of the primitive.


.. py:class:: PrimitiveBatch

    A batch of primitives with data rearranged for faster computation.

    .. py:method:: intersects(origin,direction,index) -> tuple or None

        Tests whether a given ray intersects.

        If the ray intersects with the object, an instance of
        :py:class:`RayIntersection` is returned with the details of the
        intersection. Otherwise, the return value is ``None``.

        :param origin: The origin of the ray.
        :param direction: The direction of the ray.
        :param integer index: The index specifying which primitive in the batch
            should not be considered for intersection, or ``-1`` if all
            primitives should be considered.

    .. py:attribute:: material

        A read-only sequence containing the materials of the primitives.


.. py:class:: PrimitivePrototype

    A primitive with extra data needed for quick spacial partitioning.

    This class cannot be instantiated directly in Python code.

    .. py:attribute:: boundary

        The :py:class:`AABB` of the primitive.

    .. py:attribute:: primitive

        The corresponding :py:class:`Primitive` or :py:class:`PrimitiveBatch`.


.. py:class:: RayIntersection(dist,origin,normal,primitive[,batch_index=-1])

    The details of an intersection between a ray and a primitive.

    Instances of this class are read-only.

    :param number dist: The distance between the origin of the ray and the point
        of intersection.
    :param vector origin: The point where the ray intersected the primitive.
    :param vector normal: The normal of the surface of the primitive at the
        point of intersection.
    :param primitive: The :py:class:`Primitive` or :py:class:`PrimitiveBatch`
        that the ray intersected.
    :param integer batch_index: The index indicating which primitive in the
        batch was intersected or ``-1`` if the primitive is not an instance of
        :py:class:`PrimitiveBatch`.

    .. py:attribute:: dist

        The distance between the origin of the ray and the point of
        intersection.

    .. py:attribute:: origin

        The point where the ray intersected the primitive.

    .. py:attribute:: normal

        The normal of the surface of the primitive at the point of intersection.

    .. py:attribute:: primitive

        The :py:class:`Primitive` or :py:class:`PrimitiveBatch` that the ray
        intersected.

    .. py:attribute:: batch_index

        The index indicating which primitive in the batch was intersected.

        If :py:attr:`primitive` is not an instance of :py:class:`PrimitiveBatch`
        then this will have a value of ``-1``.


.. py:class:: Solid(type,position,orientation,material)

    Bases: :py:class:`Primitive`

    A non-flat geometric primitive.

    It is either a hypercube or a hypersphere.

    Instances of this class are read-only.

    :param type: The type of solid: either :py:data:`.wrapper.CUBE` or
        :py:data:`.wrapper.SPHERE`.
    :param vector position: The position of the solid.
    :param orientation: A transformation matrix. The matrix must be invertable.
    :param material: A material to apply to the solid.
    :type orientation: :py:class:`Matrix`
    :type material: :py:class:`.render.Material`

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
    :param orientation: A transformation matrix. The matrix must be invertable.
    :param material: A material to apply to the solid.
    :type orientation: :py:class:`Matrix`
    :type material: :py:class:`.render.Material`

    .. py:attribute:: dimension

        The dimension of the solid.

    .. py:attribute:: inv_orientation

        The inverse of :py:attr:`orientation`

    .. py:attribute:: material

        The material of the primitive.

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
    :param material: A material to apply to the simplex.
    :type material: :py:class:`.render.Material`

    .. py:staticmethod:: from_points(points,material) -> Triangle

        Create a :py:class:`Triangle` object from the vertices of the simplex.

        :param points: A sequence of vectors specifying the vertices. The number
            of vectors must equal their dimension.
        :param material: A material to apply to the simplex.
        :type material: :py:class:`.render.Material`

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


.. py:class:: TriangleBatch(triangles)

    Bases: :py:class:`PrimitiveBatch`

    A batch of simplexes with data rearranged for faster computation.

    Instances of this class are read-only.

    :param triangles: An iterable yielding exactly :py:const:`BATCH_SIZE`
        instances of :py:class:`Triangle`

    .. py:method:: __getitem__(index)

        Extract the ``index``'th simplex.

        :code:`self.__getitem__(i)` <==> :code:`self[i]`

    .. py:method:: __len__()

        Return the number of simplexes in the batch.

        This is always equal :py:const:`BATCH_SIZE`.

        :code:`self.__len__()` <==> :code:`len(self)`


.. py:class:: TriangleBatchPointData

    A read-only sequence of :py:class:`TriangleBatchPointDatum` instances.

    Instances of this class are read-only. This class cannot be instantiated
    directly in Python code.

    .. py:method:: __getitem__(index)

        :code:`self.__getitem__(i)` <==> :code:`self[i]`

    .. py:method:: __len__()

        :code:`self.__len__()` <==> :code:`len(self)`


.. py:class:: TriangleBatchPointDatum

    Instances of this class are read-only. This class cannot be instantiated
    directly in Python code.

    .. py:attribute:: edge_normal

    .. py:attribute:: point

        Vertices of a batch of simplexes.


.. py:class:: TriangleBatchPrototype(t_prototypes)

    Bases: :py:class:`PrimitivePrototype`

    A batch of simplexes with extra data needed for quick spacial partitioning.

    This is the batch equivalent to :py:class:`TrianglePrototype`

    Instances of this class are read-only.

    :param t_prototypes: An iterable yielding exactly :py:const:`BATCH_SIZE`
        instances of :py:class:`TrianglePrototype`.

    .. py:attribute:: dimension

        The dimension of the simplexes.

        This is a single value since the simplexes must all have the same
        dimension.

    .. py:attribute:: face_normal

    .. py:attribute:: point_data


.. py:class:: TrianglePointData

    A read-only sequence of :py:class:`TrianglePointDatum` instances.

    Instances of this class are read-only. This class cannot be instantiated
    directly in Python code.

    .. py:method:: __getitem__(index)

        :code:`self.__getitem__(i)` <==> :code:`self[i]`

    .. py:method:: __len__()

        :code:`self.__len__()` <==> :code:`len(self)`


.. py:class:: TrianglePointDatum

    Instances of this class are read-only. This class cannot be instantiated
    directly in Python code.

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
    :param material: A material to apply to the simplex.
    :type material: :py:class:`.render.Material`

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


.. py:class:: VectorBatch

    A batch of vectors with data rearranged for faster computation.

    Instances of this class are read-only. This class cannot be instantiated
    directly in Python code.

    .. py:method:: __getitem__(index)

        Extract the ``index``'th vector from the batch.

        :code:`self.__getitem__(i)` <==> :code:`self[i]`

    .. py:method:: __len__()

        Return the number of vectors in the batch.

        This should not be confused with the length of the vectors themselves.

        This is always equal to :py:const:`BATCH_SIZE`.

        :code:`self.__len__()` <==> :code:`len(self)`


.. py:function:: build_composite_scene(primitives[,extra_threads=-1,*,update_primitives=False]) -> \
    CompositeScene

    Create a scene from a sequence of :py:class:`PrimitivePrototype` instances.

    If :py:const:`BATCH_SIZE` is greater than ``1``, instances of
    :py:class:`TrianglePrototype` will be automatically merged into instances
    of :py:class:`TriangleBatchPrototype`. If :py:const:`BATCH_SIZE` is equal to
    ``1``, then ``primitives`` cannot contain any instances of
    :py:class:`TriangleBatchPrototype`.

    By default, this will use all available processing cores to build the scene,
    but this can be controlled by passing a non-negative integer to
    ``extra_threads`` (a value of zero would make it run single-threaded). Note
    that fewer threads may be used if the resulting k-d tree is too shallow.

    :param iterable primitives: One or more instances of
        :py:class:`PrimitivePrototype`.
    :param integer extra_threads: How many extra threads to use or -1 to use as
        many extra threads as there are extra processing cores.
    :param boolean update_primitives: If true, primitives must be an instance of
        ``list`` and will be updated to contain the actual primtive prototypes
        used, with the :py:class:`TrianglePrototype` instances added and with
        their un-batched counterparts removed.


.. py:function:: build_kdtree(primitives[,extra_threads=-1,*,update_primitives=False]) -> tuple

    Create a k-d tree from a sequence of :py:class:`PrimitivePrototype`
    instances.

    The return value is a tuple containing an instance of :py:class:`AABB`
    followed by the root node of k-d tree (an instance of :py:class:`KDNode`).
    The :py:class:`AABB` encloses all the primitives from ``primitives``. The
    tuple's values can be passed directly to :py:class:`CompositeScene` (which
    is exactly what :py:func:`build_composite_scene` does).

    If :py:const:`BATCH_SIZE` is greater than ``1``, instances of
    :py:class:`TrianglePrototype` will be automatically merged into instances
    of :py:class:`TriangleBatchPrototype`. If :py:const:`BATCH_SIZE` is equal to
    ``1``, then ``primitives`` cannot contain any instances of
    :py:class:`TriangleBatchPrototype`.

    By default, this will use all available processing cores to build the tree,
    but this can be controlled by passing a non-negative integer to
    ``extra_threads`` (a value of zero would make it run single-threaded). Note
    that fewer threads may be used if the resulting tree is too shallow.

    :param sequence primitives: One or more instances of
        :py:class:`PrimitivePrototype`.
    :param integer extra_threads: How many extra threads to use or -1 to use as
        many extra threads as there are extra processing cores.
    :param boolean update_primitives: If true, primitives must be an instance of
        ``list`` and will be updated to contain the actual primtive prototypes
        used, with the :py:class:`TrianglePrototype` instances added and with
        their un-batched counterparts removed.


.. py:function:: cross(vectors) -> Vector

    A generalized cross product.

    :param vectors: A sequence of linearly independent vectors. The number of
        vectors must be one less than their dimension.


.. py:function:: dot(a,b) -> float

    Compute the dot product of two vectors.


.. py:function:: screen_coord_to_ray(cam,x,y,w,h,fov) -> Vector

    Create the same direction vector for camera ``cam``, that
    :py:class:`BoxScene` and :py:class:`CompositeScene` create when calling
    :py:meth:`.render.Scene.calculate_color`.

    :param cam: The camera.
    :type cam: :py:class:`Camera`
    :param float x: The x coordinate.
    :param float y: The y coordinate.
    :param integer w: The width.
    :param integer h: The height.
    :param float fov: The vertical field of view in radians.


.. py:data:: BATCH_SIZE

    The number of objects that batch objects group.

    On hardware that has SIMD registers, certain operations can be performed
    more efficiently if data from multiple objects is rearranged so that every
    numeric value is followed the equivalent value in the next object, up to the
    number of values that a SIMD register can hold, thus the existence of
    "batch" objects. This constant refers to the number of non-batch objects
    that a batch object is equivalent to, as well as the number of
    floating-point values that the largest SIMD register can hold. Note that
    even if run on a machine that has larger registers, the ray tracer will only
    be able to take advantage of the largest registers of the instruction set
    that this package was compiled for (when compiling from source, the latest
    instruction set that the current machine supports, is chosen by default).

    The source code of this package supports SSE and AVX. If this package is
    compiled for an instruction set that supports neither of these technologies,
    ``BATCH_SIZE`` will be equal to ``1``. In such a case, :py:class:`KDLeaf`
    won't even support instances of :py:class:`PrimitiveBatch` (passing
    instances of :py:class:`PrimitiveBatch` to its constructor will cause and
    exception to be raised) so that the ray tracer can be streamlined. The batch
    classes will still exist, however, for the sake of compatibility.

    Most users will not have to worry about the value of ``BATCH_SIZE`` or batch
    objects since :py:func:`build_composite_scene` (and :py:func:`build_kdtree`)
    will automatically combine instances of :py:class:`TrianglePrototype` into
    :py:class:`TriangleBatchPrototype` when beneficial.



:mod:`wrapper` Module
---------------------

.. py:module:: ntracer.wrapper

.. autoclass:: NTracer(dimension)

.. py:data:: CUBE

    A constant that can be passed to :py:class:`.tracern.Solid`'s constructor
    to create a hypercube.

.. py:data:: SPHERE

    A constant that can be passed to :py:class:`.tracern.Solid`'s constructor
    to create a hypersphere.



:mod:`pygame_render` Module
---------------------------

.. automodule:: ntracer.pygame_render

.. autoclass:: PygameRenderer

    .. automethod:: begin_render

    .. autoattribute:: ON_COMPLETE
        :annotation:

.. autofunction:: channels_from_surface



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

Classes that support pickling result in the exact same output, regardless of
whether the specialized or generic versions were used when pickled. When
unpickled, the specialized versions are used when available.

Note that equivalent types between the generic and specific versions are not
compatible with each other (e.g. an instance ``tracern.Vector`` cannot be added
to an instance of ``tracer3.Vector`` even if they have the same dimension).
