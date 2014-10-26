
import weakref
import pygame
import platform

import ntracer.render


IS_PYTHON3 = int(platform.python_version_tuple()[0]) >= 3

def channels_from_surface(surface):
    """Create a list of :py:class:`.render.Channel` objects that match the
    pixel format of ``surface``.
    
    Note: indexed color modes (all 8-bit color modes are indexed) are not
    supported.
    
    :param surface: An instance of `pygame.Surface
        <http://www.pygame.org/docs/ref/surface.html#pygame.Surface>`_.
    
    """
    channels = []
    
    bs = surface.get_bytesize()
    if bs == 1:
        raise TypeError('indexed color modes are not supported')
    
    def_shift = (bs - 1) * 8
    pyg = [(8-l,def_shift+l-s,m,c) for l,s,m,c in zip(surface.get_losses(),surface.get_shifts(),surface.get_masks(),'RGBA')]
    pyg.sort(key=lambda x: x[1])
    offset = 0
    for size,o,m,c in pyg:
        assert o >= offset
        
        if not size: continue
        
        if o > offset:
            channels.append(ntracer.render.Channel(o - offset,0,0,0))
        channels.append(ntracer.render.Channel(
            size,
            c == 'R',
            c == 'G',
            c == 'B',
            c == 'A'))
        offset = o + size
    
    assert offset <= bs*8
        
    return channels


# "Bases" is added manually instead of using :show-inheritance: because it
# doesn't link the base to its documentation for some reason
class PygameRenderer(ntracer.render.CallbackRenderer):
    """Bases: :py:class:`.render.CallbackRenderer`
    
    A renderer for Pygame surfaces.

    The scene is drawn onto a `pygame.Surface
    <http://www.pygame.org/docs/ref/surface.html#pygame.Surface>`_ object and
    upon completion, sends a Pygame event of type :py:attr:`ON_COMPLETE` with a
    ``source`` attribute set to the instance of the renderer, and ``surface``
    and ``scene`` attributes with the surface and scene that were passed to
    :py:meth:`begin_render`.

    Note: the renderer does not honor clipping areas or subsurface boundaries
    (it will always draw onto the entire surface) and indexed color modes (all
    8-bit color modes are indexed) are not supported.
    
    :param integer threads: The number of threads to use. If zero, the number
        of threads will equal the number of processing cores of the machine.
    
    """
    
    #: The Pygame event type that will be sent when rendering is finished.
    #:
    #: This is set to ``pygame.USEREVENT`` initially, but can be set to any
    #: value between ``pygame.USEREVENT`` and ``pygame.NUMEVENTS`` to avoid
    #: conflicts with other producers of events.
    ON_COMPLETE = pygame.USEREVENT
    
    instances = weakref.WeakSet()
    
    def __init__(self,threads=0):
        super(PygameRenderer,self).__init__(threads)
        PygameRenderer.instances.add(self)
        self.last_channels = (None,None)
    
    def begin_render(self,surface,scene):
        """Begin rendering ``scene`` onto ``dest``.

        If the renderer is already running, an exception is thrown instead.
        Upon starting, the scene will be locked for writing.

        :param pygame.Surface dest: A surface to draw onto.
        :param scene: The scene to draw.
        :type scene: :py:class:`.render.Scene`
        
        """
        def on_complete(x):
            pygame.event.post(pygame.event.Event(
                self.ON_COMPLETE,
                source=self,
                scene=scene,
                surface=surface))
        
        py_format = (surface.get_bitsize(),surface.get_masks())
        if py_format != self.last_channels[0]:
            self.last_channels = (py_format,channels_from_surface(surface))

        super(PygameRenderer,self).begin_render(
            # for some reason, get_view returns a read-only buffer under Python
            # 2.7 on Windows with Pygame 1.9.2a0
            surface.get_view() if IS_PYTHON3 and hasattr(surface,'get_view') else surface.get_buffer(),
            ntracer.render.ImageFormat(
                surface.get_width(),
                surface.get_height(),
                self.last_channels[1],
                surface.get_pitch(),
                pygame.get_sdl_byteorder() == pygame.LIL_ENDIAN),
            scene,
            on_complete)


# When PyGame shuts down, it destroys all surface objects regardless of
# reference counts, so any threads working with surfaces need to be stopped
# first.
def _quit_func():
    for inst in PygameRenderer.instances:
        inst.abort_render()

pygame.register_quit(_quit_func)
