# Once this module is optimized as much as it can be, it should probably be
# rewritten in C++ where it can be made threaded (CPython can only execute one
# instruction at a time)

from future_builtins import filter, map, zip

import sys
import itertools
import operator
from functools import reduce,partial

from ntracer.wrapper import NTracer


DUMP_ASY_REPR = False

MAX_DEPTH = 20

# only split nodes if there are more than this many primitives
SPLIT_THRESHHOLD = 3


# TODO: find optimum values for these. COST_INTERSECTION almost certainly
# depends on dimensionality and will need to be made into a function instead
# of a constant
COST_TRAVERSAL = 0.5
COST_INTERSECTION = 0.5


class CachedProto(object):
    def __init__(self,p):
        self.p = p
        self.realized = None
    
    def realize(self):
        if not self.realized:
            self.realized = self.p.realize()
        return self.realized
    
    @property
    def dimension(self):
        return self.p.dimension
    
    @property
    def aabb_min(self):
        return self.p.aabb_min
    
    @property
    def aabb_max(self):
        return self.p.aabb_max


def product(x):
    return reduce(operator.mul,x)

def split_cost(boundary,axis,l_count,r_count,split):
    cube_range = boundary.end - boundary.start
    l_range = split - boundary.start[axis]
    r_range = boundary.end[axis] - split
    
    # we actually only compute a value that is one half the surface area of
    # each box, but since we only need the ratios between areas, it doesn't
    # make any difference
    area = product(v for i,v in enumerate(cube_range) if i != axis)
    l_area = area
    r_area = area
    
    for items in itertools.combinations((v for i,v in enumerate(cube_range) if i != axis),boundary.dimension-2):
        items = product(items)
        area += items * cube_range[axis]
        l_area += items * l_range
        r_area += items * r_range

    return COST_TRAVERSAL + COST_INTERSECTION * (l_area/area * l_count + r_area/area * r_count)

def find_split(boundary,axis,contain_p,overlap_p):
    pos = None
    best_cost = sys.float_info.max
    
    search_l = sorted(itertools.chain(contain_p,overlap_p),key=lambda p: p.aabb_min[axis])
    search_r = sorted(itertools.chain(contain_p,overlap_p),key=lambda p: p.aabb_max[axis])
    
    il = 1
    ir = 0
    last_split = search_l[0].aabb_min[axis]
    last_il = 0
    while il < len(search_l):
        split = min(search_l[il].aabb_min[axis],search_r[ir].aabb_max[axis])
        
        # Note: this test is not an optimization. Removing it will produce
        # incorrect values for the l_count and r_count parameters.
        if split != last_split:
            if boundary.end[axis] > last_split > boundary.start[axis]:
                cost = split_cost(boundary,axis,last_il,len(search_l)-ir,last_split)
                if cost < best_cost:
                    best_cost = cost
                    pos = last_split
            last_il = il
            last_split = split
            
        if search_l[il].aabb_min[axis] <= search_r[ir].aabb_max[axis]:
            il += 1
        else:
            ir += 1
    
    assert il == len(search_l)
    
    while ir < len(search_l):
        split = search_r[ir].aabb_max[axis]
        if split != last_split:
            if boundary.end[axis] > last_split > boundary.start[axis]:
                cost = split_cost(boundary,axis,len(search_l),len(search_l)-ir,last_split)
                if cost < best_cost:
                    best_cost = cost
                    pos = last_split
            last_split = split
        ir += 1

    compare = product(boundary.end - boundary.start) * len(search_l)
    return pos if best_cost is not None and best_cost < compare else None


def create_leaf(nt,contain_p,overlap_p):
    return nt.KDLeaf(p.realize() for p in itertools.chain(contain_p,overlap_p))


def best_axis(boundary):
    width = -1
    axis = None
    for i,extent in enumerate(zip(boundary.start,boundary.end)):
        new_width = extent[1] - extent[0]
        if new_width > width:
            width = new_width
            axis = i
            
    assert axis is not None
    return axis


if DUMP_ASY_REPR:
    T_IDS = {}
    def shift(p,s):
        return p*0.99 + s*0.01

    def shrink(nt,points):
        center = sum(points,nt.Vector())
        return [shift(p,center) for p in points]

    def v_str(v):
        return '({0})'.format(','.join(map(str,v)))

    def print_box(nt,bound,cp,op):
        start,end = shrink(nt,[bound.start,bound.end])
        print 'draw(box({0},{1}),red);'.format(v_str(start),v_str(end))
        print '//{0},{1}'.format(bound.start,bound.end)
        print 'label("{0}",{1},Z,red);'.format(
            ','.join(str(T_IDS[id(t)]) for t in (cp+op)),
            v_str((bound.start+bound.end)*0.5))


def ortho_flat(p):
    for i in range(p.dimension):
        if p.aabb_min[i] == p.aabb_max[i]: return i
    return None


def overlap_intersects(bound,p,skip,axis):
    if skip is None:
        return bound.intersects(p.p)
    if skip == axis:
        return bound.start[axis] < p.aabb_min[axis] < bound.end[axis]
    return bound.intersects_flat(p.p,skip)


# The primitives are divided into the lists: contain_p and overlap_p.
# Primitives in contain_p are entirely inside boundary, and are much easier to
# partition. The rest of the primitives are in overlap_p.
#
# Primitives should only be part of a side (left or right) if some point within
# the primitive exists where the distance between the plane and the point is
# greater than zero. The exception is if a primitive is completely inside the
# split (hyper)plane, in which case it should be on the right side.
def create_node(nt,depth,boundary,contain_p,overlap_p):
    depth += 1
    axis = best_axis(boundary)
    
    if not (contain_p or overlap_p): return None
    
    if depth >= MAX_DEPTH or len(contain_p) + len(overlap_p) <= SPLIT_THRESHHOLD:
        if DUMP_ASY_REPR: print_box(nt,boundary,contain_p,overlap_p);
        return create_leaf(nt,contain_p,overlap_p)
    
    split = find_split(boundary,axis,contain_p,overlap_p)
    if split is None:
        if DUMP_ASY_REPR: print_box(nt,boundary,contain_p,overlap_p);
        return create_leaf(nt,contain_p,overlap_p)
    
    b_left = boundary.left(axis,split)
    b_right = boundary.right(axis,split)
    
    l_contain_p = []
    r_contain_p = []
    l_overlap_p = []
    r_overlap_p = []
    
    for p in contain_p:
        if p.aabb_min[axis] < split:
            if p.aabb_max[axis] <= split:
                l_contain_p.append(p)
            else:
                l_overlap_p.append(p)
                r_overlap_p.append(p)
        else:
            r_contain_p.append(p)

    for p in overlap_p:
        # If p is flat along any axis, p could be embedded in the hull of
        # "boundary" and intersect neither b_left nor b_right. Thus, an
        # alternate algorithm is used when p is flat along an axis other than
        # "axis", that disregards that axis.
        skip = ortho_flat(p)
        assert isinstance(p.p,nt.TrianglePrototype) or skip is None
        
        if overlap_intersects(b_left,p,skip,axis):
            l_overlap_p.append(p)
            if overlap_intersects(b_right,p,skip,axis):
                r_overlap_p.append(p)
        else:
            r_overlap_p.append(p)
    
    return nt.KDBranch(
        axis,
        split,
        create_node(nt,depth,b_left,l_contain_p,l_overlap_p),
        create_node(nt,depth,b_right,r_contain_p,r_overlap_p))


if DUMP_ASY_REPR:
    def point_data_str(d):
        return '({0})'.format(','.join(str(n) for n in d.point))
    
    def point_data_center(nt,d):
        return sum((pt.point for pt in d),nt.Vector())*(1.0/len(d))


def build_kdtree(nt,primitives):
    if not isinstance(nt,NTracer): nt = NTracer(nt)
    
    if DUMP_ASY_REPR:
        for i,p in enumerate(primitives):
            T_IDS[id(p)] = i+1
            print 'draw({0} -- {1} -- {2} -- cycle);'.format(*map(point_data_str,p.point_data))
            print 'label("{0}",{1},Z);'.format(
                i+1,
                v_str(point_data_center(nt,p.point_data)))
            #for j,pd in enumerate(p.point_data):
            #    print 'draw({0} -- {1},Arrow);'.format(v_str(pd.point),v_str(pd.point+pd.edge_normal))
            #    print 'label("{0}",{1},Z);'.format(j,v_str(pd.point))

    start = nt.Vector(reduce(partial(map,min),(p.aabb_min for p in primitives)))
    end = nt.Vector(reduce(partial(map,max),(p.aabb_max for p in primitives)))
    return start,end,create_node(nt,-1,nt.AABB(start,end),[CachedProto(p) for p in primitives],[])


def build_composite_scene(nt,primitives):
    if not isinstance(nt,NTracer): nt = NTracer(nt)
    
    return nt.CompositeScene(*build_kdtree(nt,primitives))
