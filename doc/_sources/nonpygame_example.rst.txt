Using without Pygame
====================

The following code will load a 3D mesh, render it, and save it to a file, using
`Pillow <http://python-imaging.github.io/>`_ instead of
`Pygame <http://www.pygame.org>`_.

.. code:: python

    from PIL import Image
    from ntracer import NTracer,ImageFormat,Channel,BlockingRenderer
    from ntracer import wavefront_obj


    width = 640
    height = 480

    format = ImageFormat(width,height,[
        Channel(8,1,0,0),
        Channel(8,0,1,0),
        Channel(8,0,0,1),
        Channel(8,0,0,0)])

    nt = NTracer(3)

    data = wavefront_obj.load_obj('monkey.obj')
    scene = nt.build_composite_scene(data)

    camera = nt.Camera()
    camera.translate(nt.Vector.axis(2,-5))
    scene.set_camera(camera)

    render = BlockingRenderer()

    buffer = bytearray(width * height * format.bytes_per_pixel)
    render.render(buffer,format,scene)

    mode = 'RGBX'
    im = Image.frombuffer(mode,(width,height),buffer,'raw',mode,0,1)

    im.convert('RGB').save('monkey.png')


Here is a breakdown of the code:

.. code:: python

    width = 640
    height = 480

    format = ImageFormat(width,height,[
        Channel(8,1,0,0),
        Channel(8,0,1,0),
        Channel(8,0,0,1),
        Channel(8,0,0,0)])

Since Pygame is not being used, the image format needs to be specified manually,
using :py:class:`.render.ImageFormat`.
Here we specify that the image will be 640 by 480 pixels, with each pixel
containing 8 bits for the red component (``1×R + 0×G + 0×B``) followed by 8 bits
for the green component (``0×R + 1×G + 0×B``) followed by 8 bits for the blue
component (``0×R + 0×G + 1×B``) followed by 8 bits of padding. The reason for
the padding will be explained later.

.. code:: python

    nt = NTracer(3)

    data = wavefront_obj.load_obj('monkey.obj')
    scene = nt.build_composite_scene(data)

    camera = nt.Camera()
    camera.translate(nt.Vector.axis(2,-5))
    scene.set_camera(camera)

For an explanation of this code, see :doc:`example`.

.. code:: python

    render = BlockingRenderer()

The two main renderers are :py:class:`.render.BlockingRenderer` and
:py:class:`.render.CallbackRenderer` (:py:class:`.pygame_render.PygameRenderer`
derives from :py:class:`.render.CallbackRenderer`). While both renderers can use
any number of threads, only :py:class:`.render.CallbackRenderer` works
asynchronously, allowing the thread that started the render to continue while
the renderer is busy. :py:class:`.render.BlockingRenderer` will use the invoking
thread as one of its worker threads and won't return until renderering is
finished.

Since the main thread doesn't need to do anything else in this example, we use
:py:class:`.render.BlockingRenderer`.

.. code:: python

    buffer = bytearray(width * height * format.bytes_per_pixel)
    render.render(buffer,format,scene)

We create a ``bytearray`` big enough to hold the image data and render the scene
onto it. There is nothing special about the ``bytearray`` class. Any object that
can be written to and supports the buffer interface can be drawn onto.

.. code:: python

    mode = 'RGBX'
    im = Image.frombuffer(mode,(width,height),buffer,'raw',mode,0,1)

We then use `frombuffer
<http://pillow.readthedocs.org/en/latest/reference/Image.html#PIL.Image.frombuffer>`_
to turn the array into an instance of ``Image``. There are other ways to load
image data into an ``Image`` class but this is the most efficient because this
way the data is used directly instead of being copied. This also means that to
update the ``Image`` object, all we have to do is draw onto the same
``bytearray`` object again.

Only certain formats can be used by ``frombuffer`` without copying, which is why
we use ``RGBX`` and not just ``RGB`` and consequently why we needed padding in
our pixel format above.

In case you're wondering what the last two number passed to ``frombuffer`` mean:
the zero means there is no padding between lines and the one means the lines of
the image are stored from top to bottom (a value of -1 would indicate lines
being stored bottom to top).

.. code:: python

    im.convert('RGB').save('monkey.png')

Finally we save the image to a PNG file. To do so, we have to first convert the
image to ``RGB``.
