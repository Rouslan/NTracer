Basic Usage
=============

The following code will load a 3D mesh and display it.

.. code:: python

    import pygame
    from ntracer import NTracer
    from ntracer.pygame_render import PygameRenderer
    from ntracer import wavefront_obj

    ntracer = NTracer(3)

    data = wavefront_obj.load_obj('monkey.obj')
    scene = ntracer.build_composite_scene(data)

    camera = ntracer.Camera()
    camera.translate(ntracer.Vector.axis(2,-5))
    scene.set_camera(camera)

    pygame.display.init()
    screen = pygame.display.set_mode((800,600))

    render = PygameRenderer()
    render.begin_render(screen,scene)

    while True:
        e = pygame.event.wait()
        if e.type == PygameRenderer.ON_COMPLETE:
            pygame.display.flip()
        if e.type == pygame.QUIT:
            render.abort_render()
            break


Here is a breakdown of the code:

.. code:: python

    import pygame
    from ntracer import NTracer, build_composite_scene
    from ntracer.pygame_render import PygameRenderer
    from ntracer import wavefront_obj
    
First we import what we need.

.. code:: python

    ntracer = NTracer(3)
    
Next we create an :py:class:`.NTracer` object. This is a helper class for
creating multiple objects with the same dimension. It provides the same
interface as the :py:mod:`.tracern` module except all methods and functions
that have a dimension parameter are wrapped so that the number passed to
NTracer's constructor is automatically used for that parameter.

.. code:: python

    data = wavefront_obj.load_obj('monkey.obj')

We then load a mesh called ``monkey.obj``. The data returned is a list of
:py:class:`.PrimitivePrototype` objects. Note that the Wavefront OBJ loader only
loads 3D meshes. It's not very useful in the context of hyperspace, but it's a
very simple format and is included for testing and as an example for creating
geometry.

.. code:: python

    data = ntracer.build_composite_scene(data)
    
We create a :py:class:`.Scene` object from it.
    
.. code:: python

    camera = ntracer.Camera()

We create a new camera which will be the view-port of our scene.

.. code:: python

    camera.translate(ntracer.Vector.axis(2,-5))

We move the camera back 5 units. The vector by which to move the camera is
created using the static method ``axis``, which takes an axis index (0-based)
and a magnitude to create a vector parallel to the given axis with the given
magnitude. We could have used :code:`camera.translate(ntracer.Vector(0,0,-5))`
or even :code:`camera.translate((0,0,-5))` instead, but doing it this way makes
the code dimension-agnostic.

.. code:: python

    scene.set_camera(camera)

We then set the scene's camera to a copy of ``camera``. Note that a scene cannot
be modified while it is being drawn. Attempting to do so will raise an
exception.

.. code:: python

    pygame.display.init()
    screen = pygame.display.set_mode((800,600))

We initialize Pygame and create our window.

.. code:: python

    render = PygameRenderer()
    render.begin_render(screen,scene)

Then a renderer is created and the drawing is started. By default, the renderer
will use as many threads as there are processing cores, but you can specify a
different number of threads in its constructor.

.. code:: python

    while True:
        e = pygame.event.wait()
        if e.type == PygameRenderer.ON_COMPLETE:
            pygame.display.flip()
        elif e.type == pygame.QUIT:
            render.abort_render()
            break

Finally, we have a basic event loop.  When the renderer is finished, it sends an
event of type :py:attr:`PygameRenderer.ON_COMPLETE` (which is equal to
pygame.USEREVENT by default).  The event will have a ``source`` attribute
containing the associated renderer.  Having only one renderer, we don't use it
here.  We flip the display buffer to make our image appear.
:py:meth:`.abort_render` is called to stop drawing early.

