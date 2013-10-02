#!/usr/bin/python

import itertools
import functools
import argparse
import pygame
from ntracer import NTracer, Renderer


MOVE_SENSITIVITY = 0.01
ROTATE_SENSITIVITY = 0.002

UI_FOREGROUND = 100,100,100
UI_BACKGROUND = 255,255,255
UI_BASE_SIZE = 15
UI_TEXT_SPACE = 80
UI_ALPHA_NORMAL = 128
UI_ALPHA_HOVER = 200


O_LEFT = 0
O_RIGHT = 1
O_UP = 2
O_DOWN = 3

M_NORMAL = 0
M_HOVER = 1
M_PRESSED = 2

SLIDER_EVENT = pygame.USEREVENT + 1
SLIDER_DELAY = 50


def make_arrowhead(size,orient):
    surf = pygame.Surface(size)
    surf.fill(UI_BACKGROUND)
    surf.lock()
    try:
        area = pygame.Rect(1,1,size[0]-2,size[1]-2)
        pygame.draw.rect(surf,UI_FOREGROUND,area,1)
        area.inflate_ip(-5,-5)
        points = [
            [area.midleft,area.topright,area.bottomright], # O_LEFT
            [area.bottomleft,area.topleft,area.midright], # O_RIGHT
            [area.bottomleft,area.midtop,area.bottomright], # O_UP
            [area.topleft,area.midbottom,area.topright] # O_DOWN
            ][orient]
        pygame.draw.aalines(surf,UI_FOREGROUND,True,points)
    finally:
        surf.unlock()

    return surf


class Slider:
    arrows = [None] * 4

    @classmethod
    def arrow(cls,orient):
        if not cls.arrows[orient]:
            cls.arrows[orient] = make_arrowhead((UI_BASE_SIZE,UI_BASE_SIZE),orient)
        return cls.arrows[orient]

    def __init__(self,text,pos,on_slide):
        self.pos = pos
        self.text = UI.make_text_block(text,(UI_TEXT_SPACE - 2,UI_BASE_SIZE))
        self.text.set_alpha(UI_ALPHA_NORMAL)
        self.on_slide = on_slide

    def draw(self):
        screen.blit(self.text,(self.pos[0] + UI_BASE_SIZE + 1,self.pos[1]))


class SliderButton:
    def __init__(self,slider):
        self.slider = slider

    @property
    def area(self):
        return pygame.Rect(self.pos,(UI_BASE_SIZE,UI_BASE_SIZE))

    def draw(self,mode):
        a = Slider.arrow(self.orient)
        alpha = UI_ALPHA_NORMAL
        flags = 0
        if mode == M_PRESSED:
            alpha = 255
            flags = pygame.BLEND_MULT
        elif mode == M_HOVER:
            alpha = UI_ALPHA_HOVER

        a.set_alpha(alpha)
        screen.blit(a,self.pos,special_flags=flags)

    def on_press(self):
        UI.begin_tick()

    def on_release(self):
        UI.tick()
        UI.end_tick()

class LeftSliderButton(SliderButton):
    orient = O_LEFT

    @property
    def pos(self):
        return self.slider.pos

    def on_tick(self,t):
        self.slider.on_slide(-t)


class RightSliderButton(SliderButton):
    orient = O_RIGHT

    @property
    def pos(self):
        return (self.slider.pos[0] + UI_BASE_SIZE + UI_TEXT_SPACE,self.slider.pos[1])

    def on_tick(self,t):
        self.slider.on_slide(t)


class Label:
    def __init__(self,text,pos):
        self.text = UI.make_text_block(text,(UI_BASE_SIZE*2 + 2 + UI_TEXT_SPACE,UI_BASE_SIZE))
        self.text.set_alpha(UI_ALPHA_NORMAL)
        self.pos = pos

    def draw(self):
        screen.blit(self.text,self.pos)


class UI:
    buttons = []
    components = []
    active = None
    hover = False
    pressed = False
    lasttime = None

    _font = None

    @classmethod
    def font(cls):
        if not cls._font:
            cls._font = pygame.font.SysFont('None',UI_BASE_SIZE)
        return cls._font

    @classmethod
    def make_text_block(cls,text,size):
        font = cls.font()
        fsurf = font.render(text,True,UI_FOREGROUND,UI_BACKGROUND)
        block = pygame.Surface(size)
        block.fill(UI_BACKGROUND)
        block.blit(
            fsurf,
            (   (size[0] - fsurf.get_width()) // 2,
                (size[1] - font.get_ascent()) // 2))
        return block

    @classmethod
    def on_mousemove(cls,e):
        if cls.active:
            hover = cls.active.area.collidepoint(e.pos)
            if cls.hover != hover:
                cls.hover = hover
                cls.redraw_active()
            if cls.hover or cls.pressed:
                return

        cls.active = None
        for c in cls.buttons:
            if c.area.collidepoint(e.pos):
                cls.active = c
                cls.hover = True
                cls.redraw_active()
                break

    @classmethod
    def on_mousedown(cls,e):
        if e.button == 1 and cls.active and not cls.pressed:
            cls.pressed = True
            cls.redraw_active()
            cls.active.on_press()

    @classmethod
    def on_mouseup(cls,e):
        if e.button == 1 and cls.active and cls.pressed:
            cls.pressed = False
            cls.redraw_active()
            cls.active.on_release()
            

    @classmethod
    def on_active(cls,e):
        if (not e.gain) and cls.active and cls.pressed:
            cls.pressed = False
            cls.redraw_active()
            cls.active.on_release()

    @classmethod
    def full_draw(cls):
        for c in cls.components:
            c.draw()

        for b in cls.buttons:
            mode = M_NORMAL
            if cls.active == b:
                mode = M_HOVER
                if cls.pressed:
                    mode = M_PRESSED
            b.draw(mode)

    @classmethod
    def redraw_active(cls):
        area = cls.active.area
        screen.blit(current_img,area,area)
        mode = M_NORMAL
        if cls.pressed: mode = M_PRESSED
        elif cls.hover: mode = M_HOVER
        cls.active.draw(mode)
        pygame.display.update(area)

    @classmethod
    def begin_tick(cls):
        # set_timer has the side-effect of initializing the timer subsystem,
        # which get_ticks doesn't do
        pygame.time.set_timer(SLIDER_EVENT,SLIDER_DELAY)

        cls.lasttime = pygame.time.get_ticks()

    @classmethod
    def end_tick(cls):
        cls.lasttime = None
        pygame.time.set_timer(SLIDER_EVENT,0)
        pygame.event.clear(SLIDER_EVENT)

    @classmethod
    def ticking(cls):
        return cls.lasttime is not None

    @classmethod
    def tick(cls,reset=False):
        t = pygame.time.get_ticks()
        cls.active.on_tick(cls.lasttime - t)
        cls.lasttime = t

        if reset:
            pygame.event.clear(SLIDER_EVENT)
            pygame.time.set_timer(SLIDER_EVENT,SLIDER_DELAY)

    @classmethod
    def slider(cls,text,pos,on_slide):
        s = Slider(text,pos,on_slide)
        cls.components.append(s)
        cls.buttons.append(LeftSliderButton(s))
        cls.buttons.append(RightSliderButton(s))

    @classmethod
    def label(cls,text,pos):
        cls.components.append(Label(text,pos))


def d_symbol(d):
    return 'XYZ'[d] if d < 3 else 'D' + str(d+1)



argp = argparse.ArgumentParser(description='''Navigate around a hypercube.
Note: the term "dimension" is used here in the geometric sense. e.g.: to say a
given space has a dimension of three, is the same as saying it is
three-dimensional or it has three dimensions.''')
argp.add_argument('-d','--dimension',default=3,type=int,metavar='N',help='the dimension of the hypercube and the navigable space')
args = argp.parse_args()
dimension = args.dimension

pygame.display.init()
pygame.font.init()

pygame.display.set_caption('ntracer')
screen = pygame.display.set_mode((640,480),pygame.RESIZABLE)

current_img = pygame.Surface(screen.get_size())
next_img = pygame.Surface(screen.get_size())

started = False

def translate(d):
    def inner(t):
        global started
        camera.origin += camera.axes[d] * (t * MOVE_SENSITIVITY)
        if not started:
            begin_render()
            started = True
    return inner

UI.label('Slide',(15,15))
for i in range(dimension):
    UI.slider(d_symbol(i),(15,35+20*i),translate(i))


def rotate(d1,d2):
    def inner(t):
        global started
        m = ntracer.Matrix.rotation(camera.axes[d1],camera.axes[d2],t * ROTATE_SENSITIVITY)
        for i in range(len(camera.axes)):
            camera.axes[i] = m * camera.axes[i]
        camera.normalize()

        if not started:
            begin_render()
            started = True
    return inner

UI.label('Turn',(15,50+20*dimension))
for i,dd in enumerate(itertools.combinations(range(dimension),2)):
    UI.slider(
        '{0} -> {1}'.format(*map(d_symbol,dd)),
        (15,70+20*(i+dimension)),
        rotate(*dd))


ntracer = NTracer(dimension)
camera = ntracer.Camera()
camera.translate(ntracer.Vector.axis(2,-5))
r = Renderer()
scene = ntracer.BoxScene()

def begin_render():
    scene.set_camera(camera)
    r.begin_render(next_img,scene)

begin_render()

while True:
    e = pygame.event.wait()    
    if e.type == pygame.MOUSEMOTION:
        UI.on_mousemove(e)
    elif e.type == pygame.MOUSEBUTTONDOWN:
        UI.on_mousedown(e)
    elif e.type == pygame.MOUSEBUTTONUP:
        UI.on_mouseup(e)
    elif e.type == SLIDER_EVENT:
        UI.tick()
    elif e.type == pygame.USEREVENT:
        current_img,next_img = next_img,current_img
        screen.blit(current_img,(0,0))
        UI.full_draw()
        pygame.display.flip()
        if UI.ticking():
            UI.tick(True)
            begin_render()
        else:
            started = False
    elif e.type == pygame.VIDEORESIZE:
        r.abort_render()
        pygame.event.clear(pygame.USEREVENT);
        screen = pygame.display.set_mode(e.size,pygame.RESIZABLE)
        
        # the surfaces can be very large, so we free the memory first
        del current_img
        del next_img
        current_img = pygame.Surface(e.size)
        next_img = pygame.Surface(e.size)
        begin_render()
    elif e.type == pygame.QUIT:
        break
    