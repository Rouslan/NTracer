#!/usr/bin/python

from __future__ import unicode_literals,print_function

import math
import fractions
import pygame
import argparse
import os.path
import sys
import subprocess
import time
from itertools import combinations,islice
from ntracer import NTracer,Material,ImageFormat,Channel,BlockingRenderer,CUBE
from ntracer.pygame_render import PygameRenderer


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
parser.add_argument('-f','--frames',metavar='F',type=positive_int,default=160,help='when creating an animation or benchmarking, the number of frames to render')
parser.add_argument('-s','--screen',metavar='WIDTHxHEIGHT',type=screen_size,default=(800,600),help='screen size')
parser.add_argument('-a','--fov',metavar='FOV',type=fov_type,default=0.8,help='field of vision in degrees')
parser.add_argument('-d','--cam-dist',metavar='DIST',type=float,default=4,
    help='How far the view-port is from the center of the polytope. The '+
        'value is a multiple of the outer raidius of the polytope.')
parser.add_argument('--benchmark',action='store_true',help='measure the speed of rendering the scene')
args = parser.parse_args()


material = Material((1,0.5,0.5))
nt = NTracer(max(len(args.schlafli)+1,3))


def higher_dihedral_supplement(schlafli,ds):
    a = math.pi*schlafli.denominator/schlafli.numerator
    return 2*math.asin(math.sin(math.acos(1/(math.tan(ds/2)*math.tan(a))))*math.sin(a))

def almost_equal(a,b,threshold=0.1):
    return (a-b).absolute() < threshold

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
    
    def tesselate_inner(self,*args):
        return self.shape.tesselate_inner(*self.translated(*args))
    
    def any_point(self,*args):
        return self.shape.any_point(*self.translated(*args))
    
    def contains(self,p):
        return self.shape.contains(self.inv_orientation * (p - self.position))


def star_component(x):
    return (x.numerator - 1) > x.denominator > 1

class LineSegment:
    star = False
    
    def __init__(self,index,convex_ds,polygon):
        self.index = index
        self.p = polygon
        self.position = radial_vector(index*convex_ds)
    
    def tesselate(self,position,orientation):
        return [
            orientation*self.p.base_points[self.index-1]+position,
            orientation*self.p.base_points[self.index]+position]


class Polygon:
    apothem = 1
    
    def __init__(self,schlafli):
        self.star = star_component(schlafli)
        convex_ds = 2 * math.pi / schlafli.numerator
        self.dihedral_s = convex_ds * schlafli.denominator
        self.parts = [LineSegment(i,convex_ds,self) for i in range(schlafli.numerator)]
        
        self._circumradius = 1/math.cos(convex_ds/2)
        self.base_points = [self._circumradius * radial_vector((i+0.5) * convex_ds) for i in range(schlafli.numerator)]
        
        if self.star:
            self._circumradius = math.tan(convex_ds)*math.tan(convex_ds/2) + 1
            self.outer_points = [self._circumradius * radial_vector(i * convex_ds) for i in range(schlafli.numerator)]

    def points(self,position,orientation,pset=None):
        if pset is None: pset = self.base_points
        return (orientation * bp + position for bp in pset)
    
    def tesselate_inner(self,position,orientation):
        points = list(self.points(position,orientation))
        r = [points[0:3]]
        for i in range(len(points)-3):
            r.append([points[0],points[i+2],points[i+3]])
        return r
    
    def tesselate(self,position,orientation):
        if not self.star:
            return self.tesselate_inner(position,orientation)
        
        points = list(self.points(position,orientation))
        opoints = list(self.points(position,orientation,self.outer_points))
        return [[opoints[i],points[i-1],points[i]] for i in range(len(points))]
    
    def any_point(self,position,orientation):
        return next(self.points(position,orientation))
    
    def contains(self,p):
        return any(almost_equal(p,test_p) for test_p in self.base_points)
    
    def hull(self,position=nt.Vector(),orientation=nt.Matrix.identity()):
        tris = [nt.TrianglePrototype(tri,material) for tri in self.tesselate_inner(position,orientation)]
        if self.star: tris.extend(nt.TrianglePrototype(tri,material) for tri in
            self.tesselate(position,orientation))
        return tris
    
    def circumradius(self):
        return self._circumradius
    
    def circumradius_square(self):
        return self._circumradius*self._circumradius
    
    def line_apothem_square(self):
        return 1


class Plane:
    def __init__(self,nt,position):
        self.normal = position.unit()
        self.d = -position.absolute()
        self._dot = nt.dot
    
    def distance(self,point):
        return self._dot(point,self.normal) + self.d

class Line:
    def __init__(self,nt,p0,v,planes,outer=False):
        self.p0 = p0
        self.v = v
        self.planes = set(planes)
        self.outer = outer
        self._dot = nt.dot
    
    def point_at(self,t):
        return self.p0 + self.v*t
    
    def dist_square(self,point):
        a = point - self.p0
        b = self._dot(a,self.v)
        return a.square() - b*b/self.v.square()
    
    def __repr__(self):
        return 'Line({0!r},{1!r})'.format(self.p0,self.v)


def plane_point_intersection(nt,planes):
    assert nt.dimension == len(planes)
    try:
        return nt.Matrix(p.normal for p in planes).inverse()*nt.Vector(-p.d for p in planes)
    except ValueError:
        return None

def plane_line_intersection(nt,planes):
    assert nt.dimension - 1 == len(planes)
    v = nt.cross(p.normal for p in planes).unit()
    return Line(
        nt,
        nt.Matrix([p.normal for p in planes] + [v]).inverse() * nt.Vector([-p.d for p in planes] + [0]),
        v,
        planes)

def line_intersection(nt,l1,l2):
    d = nt.dot(l1.v,l2.v)
    denom = 1 - d*d
    if not denom: return None
    id = 1/denom
    a = nt.dot(l2.p0 - l1.p0,l1.v)
    b = nt.dot(l1.p0 - l2.p0,l2.v)
    t1 = id*(a + d*b)
    t2 = id*(d*a + b)
    p1 = l1.point_at(t1)
    p2 = l2.point_at(t2)
    if abs(p1-p2) > 0.01: return None
    return (p1 + p2) * 0.5, t1, t2


class Node:
    def __init__(self,pos,planes,outer,alive=True):
        self.pos = pos
        self.planes = planes
        self.outer = outer
        self.neighbors = set() if alive else None
    
    def detach(self):
        for n in self.neighbors:
            n.neighbors.remove(self)
        self.neighbors = None
    
    @property
    def dead(self):
        return self.neighbors is None
    
    def find_cycles(self,length,sequence=None,exclude=None):
        if sequence is None: sequence = [self]
        if len(sequence) < length:
            exclude = exclude.copy() if exclude is not None else set([self])
            for n in self.neighbors:
                if n not in exclude:
                    exclude.add(n)
                    for r in n.find_cycles(length,sequence + [n],exclude):
                        yield r
        else:
            for n in self.neighbors:
                if n is sequence[0] and n.planes.intersection(*(sequence[i].planes for i in range(1,len(sequence)))):
                    yield sequence

def join(a,b):
    if not (a.dead or b.dead):
        a.neighbors.add(b)
        b.neighbors.add(a)

class FuzzyGraph:
    def __init__(self):
        self.nodes = []
    
    def add(self,pos,planes,outer):
        for n in self.nodes:
            if almost_equal(n.pos,pos,0.01):
                n.planes |= planes
                return n
        n = Node(pos,planes,outer)
        self.nodes.append(n)
        return n
    
    def remove_at(self,i):
        self.nodes[i].detach()
        if i+1 != len(self.nodes):
            self.nodes[i] = self.nodes[-1]
        del self.nodes[-1]
    
    def remove(self,pos):
        if isinstance(pos,Node):
            if not pos.dead:
                self.remove_at(self.nodes.index(pos))
        else:
            for i,n in enumerate(self.nodes):
                if almost_equal(n.pos,pos,0.01):
                    self.remove_at(i)
                    break


# Cells are enlarged ever so slightly to prevent the view frustum from being
# wedged exactly between two adjacent primitives, which, do to limited
# precision, can cause that volume to appear to vanish.
fuzz_scale = nt.Matrix.scale(1.00001)
class PolyTope:
    def __init__(self,dimension,schlafli,dihedral_s,face_apothem):
        self.dimension = dimension
        self.schlafli = schlafli
        self.dihedral_s = dihedral_s
        self.apothem = math.tan((math.pi - dihedral_s)/2) * face_apothem
        self.star = star_component(schlafli)
        self.parts = []
    
    @property
    def facet(self):
        return self.parts[0].shape
        
    def propogate_faces(self,potentials):
        new_p = []
        
        for instance,p in potentials:
            dir = (instance.orientation * p.position).unit()
            
            reflect = nt.Matrix.reflection(dir)

            turn = nt.Matrix.rotation(
                instance.position.unit(),
                dir,
                self.dihedral_s)
            
            new_p += self.add_face(Instance(
                instance.shape,
                turn * instance.position,
                fuzz_scale * turn * reflect * instance.orientation))
        return new_p
    
    def add_face(self,instance):
        for p in self.parts:
            if almost_equal(instance.position,p.position): return []

        self.parts.append(instance)
        
        return [(instance,p) for p in instance.shape.parts]
    
    def star_tesselation(self):
        t = getattr(self,'_star_tesselation',None)
        if t is None:
            co_nt = NTracer(self.dimension)
            lines = []
            planes = [Plane(co_nt,co_nt.Vector(islice(part.position,co_nt.dimension))) for part in self.parts]
            las = self.line_apothem_square()
            for pgroup in combinations(planes,co_nt.dimension-1):
                try:
                    line = plane_line_intersection(co_nt,pgroup)
                except ValueError:
                    pass
                else:
                    if line:
                        for lineb in lines:
                            if almost_equal(line.p0,lineb.p0) and almost_equal(line.v,lineb.v):
                                lineb.planes |= line.planes
                                break
                        else:
                            outer_dist = line.dist_square(co_nt.Vector()) - las
                            if outer_dist < 0.1:
                                line.outer = outer_dist > -0.1
                                lines.append(line)

            pmap = {}
            for line in lines:
                pmap[line] = {}

            graph = FuzzyGraph()
            maxr = self.circumradius_square() + 0.1
            for l1,l2 in combinations(lines,2):
                inter = line_intersection(co_nt,l1,l2)
                if inter and inter[0].square() < maxr:
                    n = graph.add(inter[0],l1.planes | l2.planes,l1.outer or l2.outer)
                    pmap[l1][n] = inter[1]
                    pmap[l2][n] = inter[2]

            for line,poss in pmap.items():
                if len(poss) == 0: continue
                if len(poss) == 1:
                    graph.remove(poss[0])
                    continue
                
                poss = sorted(poss.items(),key=(lambda x: x[1]))
                
                if line.outer:
                    for i in range(len(poss)-1):
                        join(poss[i][0],poss[i+1][0])
                elif len(poss) == 2:
                    join(poss[0][0],poss[1][0])
                elif len(poss) > 3:
                    for i in range(2,len(poss)-2):
                        graph.remove(poss[i][0])

                    join(poss[0][0],poss[1][0])
                    join(poss[-1][0],poss[-2][0])
            
            t = []
            self._star_tesselation = t
            for n in islice(graph.nodes,0,len(graph.nodes)-co_nt.dimension):
                for cycle in n.find_cycles(co_nt.dimension):
                    t.append([nt.Vector(tuple(x.pos) + (0,) * (nt.dimension-co_nt.dimension)) for x in cycle] + [nt.Vector()])
                n.detach()

        return t
    
    def tesselate(self,position,orientation):
        if self.star or self.facet.star:
            return [[orientation * p + position for p in tri] for tri in self.star_tesselation()]

        return self.tesselate_inner(position,orientation)
    
    def tesselate_inner(self,position,orientation):
        tris = []
        point1 = self.parts[0].any_point(position,orientation)
            
        inv_orientation = orientation.inverse()
        for part in self.parts[1:]:
            if not part.contains(inv_orientation * (point1 - position)):
                new_t = part.tesselate(position,orientation)
                for t in new_t: t.append(point1)
                tris += new_t
            
        return tris
    
    def hull(self,position=nt.Vector(),orientation=nt.Matrix.identity()):
        tris = []
        for p in self.parts:
            tris += p.tesselate(position,orientation)
        return [nt.TrianglePrototype(tri,material) for tri in tris]
    
    def any_point(self,position,orientation):
        return self.parts[0].any_point(position,orientation)
    
    def contains(self,p):
        return any(part.contains(p) for part in self.parts)
    
    def circumradius_square(self):
        return self.apothem*self.apothem + self.facet.circumradius_square()
    
    def circumradius(self):
        return math.sqrt(self.circumradius_square())
    
    def line_apothem_square(self):
        return self.apothem*self.apothem + self.facet.line_apothem_square()


def compose(part,order,schlafli):
    if schlafli.numerator * (math.pi - part.dihedral_s) >= math.pi * 2 * schlafli.denominator:
        exit("Component #{0} ({1}) is invalid because the angles of the parts add up to 360\u00b0 or\nmore and thus can't be folded inward".format(order,schlafli))
    higher = PolyTope(
        order+1,
        schlafli,
        higher_dihedral_supplement(schlafli,part.dihedral_s),
        part.apothem)
    potentials = higher.add_face(Instance(part,nt.Vector.axis(order,higher.apothem)))
    while potentials:
        potentials = higher.propogate_faces(potentials)
    return higher


jitter = nt.Vector((0,0,0) + (0.0001,) * (nt.dimension-3))
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
        camera.origin = camera.axes[2] * cam_distance + jitter
        scene.set_camera(camera)
        
        x_move = 0
        y_move = 0
        w_move = 0
        
        run()


def run():
    global running
    
    running = True
    render.begin_render(screen,scene)


try:
    timer = time.perf_counter
except AttributeError:
    timer = time.clock
    if args.benchmark and not sys.platform.startswith('win'):
        print('''warning: on multi-core systems, Python\'s high-resolution timer may combine
time spent on all cores, making the reported time spent rendering, much higher
than the actual time''',file=sys.stderr)


class RotatingCamera(object):
    incr = 2 * math.pi / args.frames
    h = 1/math.sqrt(nt.dimension-1)
    
    _timer = staticmethod(timer if args.benchmark else (lambda: 0))
    
    def __enter__(self):
        self.frame = 0
        self.total_time = 0
        return self
    
    def __exit__(self,type,value,tb):
        if type is None and self.total_time:
            print('''rendered {0} frame(s) in {1} seconds
time per frame: {2} seconds
frames per second: {3}'''.format(self.frame,self.total_time,self.total_time/self.frame,self.frame/self.total_time))
    
    def start_timer(self):
        self.t = self._timer()
    
    def end_timer(self):
        self.total_time += self._timer() - self.t
    
    def advance_camera(self):
        self.frame += 1
        if self.frame >= args.frames: return False
            
        a2 = camera.axes[0]*self.h + camera.axes[1]*self.h
        for i in range(nt.dimension-3): a2 += camera.axes[i+3]*self.h
        camera.transform(nt.Matrix.rotation(camera.axes[2],a2,self.incr))
        camera.normalize()
        camera.origin = camera.axes[2] * cam_distance
        scene.set_camera(camera)
        
        return True


if nt.dimension >= 3 and args.schlafli[0] == 4 and all(c == 3 for c in args.schlafli[1:]):
    cam_distance = -math.sqrt(nt.dimension) * args.cam_dist
    scene = nt.BoxScene()
else:
    print('building geometry...')
    timing = timer()
    
    p = Polygon(args.schlafli[0])
    for i,s in enumerate(args.schlafli[1:]):
        p = compose(p,i+2,s)

    hull = p.hull()
    timing = timer() - timing
    print('done in {0} seconds'.format(timing))

    cam_distance = -math.sqrt(p.circumradius_square()) * args.cam_dist

    print('partitioning scene...')
    timing = timer()
    scene = nt.build_composite_scene(hull)
    timing = timer() - timing
    print('done in {0} seconds'.format(timing))
    
    del p
    del hull

camera = nt.Camera()
camera.translate(nt.Vector.axis(2,cam_distance) + jitter)
scene.set_camera(camera)
scene.set_fov(args.fov)


if args.output is not None:
    if args.type != 'png':
        render = BlockingRenderer()
        format = ImageFormat(
            args.screen[0],
            args.screen[1],
            [Channel(16,1,0,0),
             Channel(16,0,1,0),
             Channel(16,0,0,1)])
        
        surf = bytearray(args.screen[0]*args.screen[1]*format.bytes_per_pixel)
        
        pipe = subprocess.Popen(['ffmpeg',
                '-y',
                '-f','rawvideo',
                '-vcodec','rawvideo',
                '-s','{0}x{1}'.format(*args.screen),
                '-pix_fmt','rgb48be',
                '-r','60',
                '-i','-',
                '-an',
                '-vcodec',args.type,
                '-crf','10',
                args.output],
            stdin=subprocess.PIPE)
        
        try:
            with RotatingCamera() as rc:
                while True:
                    rc.start_timer()
                    render.render(surf,format,scene)
                    rc.end_timer()

                    print(surf,file=pipe.stdin,sep='',end='')
                    
                    if not rc.advance_camera(): break

        finally:
            pipe.stdin.close()
            r = pipe.wait()
        sys.exit(r)


    pygame.display.init()
    render = PygameRenderer()
    surf = pygame.Surface(args.screen,depth=24)

    def announce_frame(frame):
        print('drawing frame {0}/{1}'.format(frame+1,args.frames))
    
    with RotatingCamera() as rc:
        announce_frame(0)
        rc.start_timer()
        render.begin_render(surf,scene)
        while True:
            e = pygame.event.wait()
            if e.type == pygame.USEREVENT:
                rc.end_timer()
                
                pygame.image.save(
                    surf,
                    os.path.join(args.output,'frame{0:04}.png'.format(rc.frame)))

                if not rc.advance_camera(): break
                
                announce_frame(rc.frame)
                rc.start_timer()
                render.begin_render(surf,scene)
            elif e.type == pygame.QUIT:
                render.abort_render()
                break

else:
    pygame.display.init()
    render = PygameRenderer()
    screen = pygame.display.set_mode(args.screen)
    
    if args.benchmark:
        with RotatingCamera() as rc:
            rc.start_timer()
            render.begin_render(screen,scene)
            while True:
                e = pygame.event.wait()
                if e.type == pygame.USEREVENT:
                    rc.end_timer()
                    
                    pygame.display.flip()

                    if not rc.advance_camera(): break
                    
                    rc.start_timer()
                    render.begin_render(screen,scene)
                elif e.type == pygame.QUIT:
                    render.abort_render()
                    break
    else:
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

