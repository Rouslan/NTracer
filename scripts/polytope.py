#!/usr/bin/python

from __future__ import unicode_literals,print_function

import math
import fractions
import pygame
import argparse
import os.path
import sys
import subprocess
from itertools import combinations
from ntracer import NTracer,Material,Renderer,build_composite_scene,CUBE


ROT_SENSITIVITY = 0.005
WHEEL_INCREMENT = 8


def excepthook(type,value,traceback):
    if isinstance(value,Exception):
        print('error: '+str(value),file=sys.stderr)
    else:
        sys.__excepthook__(type,value,traceback)
    
sys.excepthook = excepthook


def schlafli_component(x):
    x = x.partition('/')
    p = int(x[0],10)
    if p < 3: raise argparse.ArgumentTypeError('a component cannot be less than 3')
    
    if not x[2]: return fractions.Fraction(p)
    
    s = int(x[2],10)
    if s < 1: raise argparse.ArgumentTypeError('for component p/q: q cannot be less than 1')
    if s >= p: raise argparse.ArgumentTypeError('for component p/q: q must be less than p')
    if fractions.gcd(s,p) != 1: raise argparse.ArgumentTypeError('for component p/q: p and q must be co-prime')
    return fractions.Fraction(p,s)

def positive_int(x):
    x = int(x,10)
    if x < 1: raise argparse.ArgumentTypeError('a positive number is required')
    return x

def screen_size(x):
    w,_,h = x.partition('x')
    w = int(w,10)
    h = int(h,10)
    if w < 1 or h < 1: raise argparse.ArgumentTypeError('invalid screen size')
    return w,h

def fov_type(x):
    x = float(x)
    if x <= 0 or x >= 180: raise argparse.ArgumentTypeError('fov must be between 0 and 180 degrees')
    return x/180*math.pi

parser = argparse.ArgumentParser(
    description='Display a regular polytope given its Schl\u00e4fli symbol.')
parser.add_argument('schlafli',metavar='N',type=schlafli_component,nargs='+',help='the Schl\u00e4fli symbol components')
parser.add_argument('-o','--output',metavar='PATH',help='save an animation to PATH instead of displaying the polytope')
parser.add_argument('-t','--type',metavar='TYPE',default='h264',
    help='Specifies output type when --output is used. If TYPE is "png", the '+
        'output is a series of PNG images. For any other value, it is used '+
        'as the video codec for ffmpeg.')
parser.add_argument('-f','--frames',metavar='F',type=positive_int,default=160,help='when creating an animation, the number of frames to create')
parser.add_argument('-s','--screen',metavar='WIDTHxHEIGHT',type=screen_size,default=(800,600),help='screen size')
parser.add_argument('-a','--fov',metavar='FOV',type=fov_type,default=0.8,help='field of vision in degrees')
parser.add_argument('-d','--cam-dist',metavar='DIST',type=float,default=4,
    help='How far the view-port is from the center of the polytope. The '+
        'value is a multiple of the outer raidius of the polytope.')
args = parser.parse_args()


material = Material((1,0.5,0.5))
nt = NTracer(max(len(args.schlafli)+1,3))


def higher_dihedral_supplement(schlafli,ds):
    a = math.pi*schlafli.denominator/schlafli.numerator
    return 2*math.asin(math.sin(math.acos(1/(math.tan(ds/2)*math.tan(a))))*math.sin(a))

def almost_equal(a,b):
    return (a-b).absolute() < 0.1

def radial_vector(angle):
    return nt.Vector.axis(0,math.sin(angle)) + nt.Vector.axis(1,math.cos(angle))


class Instance:
    def __init__(self,shape,position,orientation=nt.Matrix.identity()):
        self.shape = shape
        self.position = position
        self.orientation = orientation
        self.inv_orientation = orientation.inverse()
    
    def translated(self,position=nt.Vector(),orientation=nt.Matrix.identity()):
        return (
            position + (orientation * self.position),
            orientation * self.orientation)
    
    def tesselate(self,*args):
        return self.shape.tesselate(*self.translated(*args))
    
    def tesselate_outer(self,*args):
        return self.shape.tesselate_outer(*self.translated(*args))
    
    def any_point(self,*args):
        return self.shape.any_point(*self.translated(*args))
    
    def contains(self,p):
        return self.shape.contains(self.inv_orientation * (p - self.position))


def star_component(x):
    return (x.numerator - 1) > x.denominator > 1

class Line:
    def __init__(self,position):
        self.position = position


class Polygon:
    radius = 1
    
    def __init__(self,schlafli):
        self.star = star_component(schlafli)
        convex_ds = 2 * math.pi / schlafli.numerator
        self.dihedral_s = convex_ds * schlafli.denominator
        self.parts = [Line(radial_vector(i * convex_ds)) for i in range(schlafli.numerator)]
        
        self._outer_radius = 1/math.cos(convex_ds/2)
        self.base_points = [self._outer_radius * radial_vector((i+0.5) * convex_ds) for i in range(schlafli.numerator)]
        
        if self.star:
            # star polygon
            
            self._outer_radius = math.tan(convex_ds)*math.tan(convex_ds/2) + 1
            self.outer_points = [self._outer_radius * radial_vector(i * convex_ds) for i in range(schlafli.numerator)]

    def points(self,position,orientation,pset=None):
        if pset is None: pset = self.base_points
        return (orientation * bp + position for bp in pset)
    
    def tesselate(self,position,orientation):
        points = list(self.points(position,orientation))
        r = [points[0:3]]
        for i in range(len(points)-3):
            r.append([points[0],points[i+2],points[i+3]])
        return r
    
    def tesselate_outer(self,position,orientation):
        points = list(self.points(position,orientation))
        opoints = list(self.points(position,orientation,self.outer_points))
        return [[opoints[i],points[i-1],points[i]] for i in range(len(points))]
    
    def any_point(self,position,orientation):
        return next(self.points(position,orientation))
    
    def contains(self,p):
        return any(almost_equal(p,test_p) for test_p in self.base_points)
    
    def hull(self,position=nt.Vector(),orientation=nt.Matrix.identity()):
        tris = [nt.TrianglePrototype(tri,material) for tri in self.tesselate(position,orientation)]
        if self.star: tris.extend(nt.TrianglePrototype(tri,material) for tri in
            self.tesselate_outer(position,orientation))
        return tris
    
    def outer_radius(self):
        return self._outer_radius
    
    def outer_radius_square(self):
        return self._outer_radius*self._outer_radius


# Cells are enlarged ever so slightly to prevent the view frustum from being
# wedged exactly between two adjacent primitives, which, do to limited
# precision, can cause that volume to appear to vanish.
fuzz_scale = nt.Matrix.scale(1.00001)
class PolyTope:
    def __init__(self,dihedral_s,face_radius,star,parts=None):
        self.dihedral_s = dihedral_s
        self.radius = math.tan((math.pi - dihedral_s)/2) * face_radius
        self.star = star
        self.parts = parts or []
        
    def propogate_faces(self,potentials):
        new_p = set()
        
        for instance,p in potentials:
            dir = (instance.orientation * p.position).unit()
            
            reflect = nt.Matrix.reflection(dir)

            turn = nt.Matrix.rotation(
                instance.position.unit(),
                dir,
                self.dihedral_s)
            
            new_p |= self.add_face(Instance(
                instance.shape,
                turn * instance.position,
                fuzz_scale * turn * reflect * instance.orientation))
        return new_p
    
    def add_face(self,instance):
        for p in self.parts:
            if almost_equal(instance.position,p.position): return set()

        self.parts.append(instance)
        
        return set((instance,p) for p in instance.shape.parts)
    
    def tesselate(self,position,orientation):
        point1 = self.parts[0].any_point(position,orientation)
        tris = []
        inv_orientation = orientation.inverse()
        for part in self.parts[1:]:
            if not part.contains(inv_orientation * (point1 - position)):
                new_t = part.tesselate(position,orientation)
                for t in new_t: t.append(point1)
                tris += new_t
        
        return tris
    
    def tesselate_outer(self,position,orientation):
        assert self.parts[0].shape.star
        
        outer_r = self.outer_radius()
        tris = []
        for p in self.parts:
            cusp = p.position * outer_r + position
            tris.extend(t + [cusp] for t in p.tesselate(position,orientation))
            
        return tris
    
    def hull(self,position=nt.Vector(),orientation=nt.Matrix.identity()):
        tris = []
        for p in self.parts:
            tris += (p.tesselate_outer if p.shape.star else p.tesselate)(position,orientation)
        return [nt.TrianglePrototype(tri,material) for tri in tris]
    
    def any_point(self,position,orientation):
        return self.parts[0].any_point(position,orientation)
    
    def contains(self,p):
        return any(part.contains(p) for part in self.parts)
    
    def outer_radius_square(self):
        return self.radius*self.radius + self.parts[0].shape.outer_radius_square()
    
    def outer_radius(self):
        return math.sqrt(self.outer_radius_square())


def compose(part,order,schlafli):
    if schlafli.numerator * (math.pi - part.dihedral_s) >= math.pi * 2 * schlafli.denominator:
        exit("Component #{0} ({1}) is invalid because the angles of the parts add up to 360\u00b0 or\nmore and thus can't be folded inward".format(order,schlafli))
    higher = PolyTope(
        higher_dihedral_supplement(schlafli,part.dihedral_s),
        part.radius,
        star_component(schlafli))
    potentials = higher.add_face(Instance(part,nt.Vector.axis(order,higher.radius)))
    while potentials:
        potentials = higher.propogate_faces(potentials)
    return higher


def process_movement():
    global x_move, y_move, w_move
    
    if x_move or y_move or w_move:
        h = math.sqrt(x_move*x_move + y_move*y_move + w_move*w_move)
        a2 = camera.axes[0]*(x_move/h) + camera.axes[1]*(-y_move/h)
        if w_move: a2 += camera.axes[3] * (w_move / h)
        camera.transform(nt.Matrix.rotation(
            camera.axes[2],
            a2,
            h * ROT_SENSITIVITY))
        camera.normalize()
        camera.origin = camera.axes[2] * cam_distance
        scene.set_camera(camera)
        
        x_move = 0
        y_move = 0
        w_move = 0
        
        run()


def run():
    global running
    
    running = True
    render.begin_render(screen,scene)


def announce_frame():
    if args.type == 'png':
        print('drawing frame {0}/{1}'.format(frame+1,args.frames))



if nt.dimension >= 3 and args.schlafli[0] == 4 and all(c == 3 for c in args.schlafli[1:]):
    cam_distance = -math.sqrt(nt.dimension) * args.cam_dist
    scene = nt.BoxScene()
else:
    print('building geometry...')
    p = Polygon(args.schlafli[0])
    for i,s in enumerate(args.schlafli[1:]):
        p = compose(p,i+2,s)

    hull = p.hull()
    print('done')

    cam_distance = -math.sqrt(p.outer_radius_square()) * args.cam_dist

    print('partitioning scene...')
    scene = build_composite_scene(nt,hull)
    print('done')
    
    del p

camera = nt.Camera()
camera.translate(nt.Vector.axis(2,cam_distance))
scene.set_camera(camera)
scene.set_fov(args.fov)

render = Renderer()

pygame.display.init()

if args.output is not None:
    kwds = {'depth':24}
    if args.type != 'png':
        kwds['masks'] = (0xff,0xff00,0xff0000,0)
        pipe = subprocess.Popen(['ffmpeg',
                '-y',
                '-f','rawvideo',
                '-vcodec','rawvideo',
                '-s','{0}x{1}'.format(*args.screen),
                '-pix_fmt','rgb24',
                '-r','60',
                '-i','-',
                '-an',
                '-vcodec',args.type,
                '-crf','10',
                args.output],
            stdin=subprocess.PIPE)

    try:
        surf = pygame.Surface(args.screen,**kwds)

        incr = 2*math.pi / args.frames
        h = 1/math.sqrt(nt.dimension-1)

        frame = 0
        announce_frame()
        render.begin_render(surf,scene)

        while True:
            e = pygame.event.wait()
            if e.type == pygame.USEREVENT:
                frame += 1
                if args.type != 'png':
                    print(
                        buffer(surf.get_buffer()),
                        file=pipe.stdin,
                        sep='',
                        end='')
                else:
                    pygame.image.save(
                        surf,
                        os.path.join(args.output,'frame{0:04}.png'.format(frame)))
                if frame >= args.frames: break
                
                a2 = camera.axes[0]*h + camera.axes[1]*h
                for i in range(nt.dimension-3): a2 += camera.axes[i+3]*h
                camera.transform(nt.Matrix.rotation(camera.axes[2],a2,incr))
                camera.normalize()
                camera.origin = camera.axes[2] * cam_distance
                announce_frame()
                scene.set_camera(camera)
                render.begin_render(surf,scene)
            elif e.type == pygame.QUIT:
                render.abort_render()
                break
    finally:
        if args.type != 'png':
            pipe.stdin.close()
    
    if args.type != 'png':
        sys.exit(pipe.wait())
else:
    screen = pygame.display.set_mode(args.screen)

    running = False
    run()

    x_move = 0
    y_move = 0
    w_move = 0

    while True:
        e = pygame.event.wait()
        if e.type == pygame.MOUSEMOTION:
            if e.buttons[0]:
                x_move += e.rel[0]
                y_move += e.rel[1]
                if not running:
                    process_movement()
        elif e.type == pygame.MOUSEBUTTONDOWN:
            if nt.dimension > 3:
                if e.button == 4 or e.button == 5:
                    if e.button == 4:
                        w_move += WHEEL_INCREMENT
                    else:
                        w_move -= WHEEL_INCREMENT
                    if not running:
                        process_movement()
        elif e.type == pygame.USEREVENT:
            running = False
            pygame.display.flip()
            process_movement()
        elif e.type == pygame.KEYDOWN:
            if e.key == pygame.K_c:
                x,y = pygame.mouse.get_pos()
                fovI = (2 * math.tan(scene.fov/2)) / screen.get_width()

                print(camera.origin)
                print((camera.axes[2] + camera.axes[0] * (fovI * (x - screen.get_width()/2)) - camera.axes[1] * (fovI * (y - screen.get_height()/2))).unit())
        elif e.type == pygame.QUIT:
            render.abort_render()
            break

