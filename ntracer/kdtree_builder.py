# Once this module is optimized as much as it can be, it should probably be
# rewritten in C++ where it can be made threaded (CPython can only execute one
# instruction at a time)

import sys
import itertools
import operator
from functools import reduce,partial

from ntracer.wrapper import NTracer


MAX_DEPTH = 20

# only split nodes if there are more than this many primitives
SPLIT_THRESHHOLD = 3


# TODO: find optimum values for these. COST_INTERSECTION almost certainly
# depends on dimensionality and will need to be made into a function instead
# of a constant
COST_TRAVERSAL = 0.5
COST_INTERSECTION = 0.5



def product(x):
    return reduce(operator.mul,x)

def split_cost(boundary,axis,contain_p,overlap_p,split):
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

    r_count = 0
    b_count = 0
    for p in itertools.chain(contain_p,overlap_p):
        if p.aabb_min[axis] >= split:
            r_count += 1
        elif p.aabb_max[axis] > split:
            b_count += 1
    l_count = len(contain_p) + len(overlap_p) - r_count
    r_count += b_count

    return COST_TRAVERSAL + COST_INTERSECTION * (l_area/area * l_count + r_area/area * r_count)


def find_split(boundary,axis,contain_p,overlap_p):
    pos = None
    best_cost = sys.float_info.max
    for p in itertools.chain(contain_p,overlap_p):
        split = p.aabb_min[axis]
        if boundary.end[axis] > split > boundary.start[axis]:
            cost = split_cost(boundary,axis,contain_p,overlap_p,split)
            if cost < best_cost:
                best_cost = cost
                pos = split

        split = p.aabb_max[axis]
        if boundary.end[axis] > split > boundary.start[axis]:
            cost = split_cost(boundary,axis,contain_p,overlap_p,split)
            if cost < best_cost:
                best_cost = cost
                pos = split

    compare = product(boundary.end - boundary.start) * (len(contain_p) + len(overlap_p))
    return pos if best_cost is not None and best_cost < compare else None


def create_leaf(nt,contain_p,overlap_p):
    return nt.KDLeaf(p.realize() for p in itertools.chain(contain_p,overlap_p))


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
    cur_dim = depth % nt.dimension
    
    if not (contain_p or overlap_p): return None
    
    if depth >= MAX_DEPTH or len(contain_p) + len(overlap_p) <= SPLIT_THRESHHOLD:
        return create_leaf(nt,contain_p,overlap_p)
    
    split = find_split(boundary,cur_dim,contain_p,overlap_p)
    if split is None:
        return create_leaf(nt,contain_p,overlap_p)
    
    b_left = boundary.left(cur_dim,split)
    b_right = boundary.right(cur_dim,split)
    
    l_contain_p = []
    r_contain_p = []
    l_overlap_p = []
    r_overlap_p = []
    
    for p in contain_p:
        if p.aabb_min[cur_dim] < split:
            if p.aabb_max[cur_dim] <= split:
                l_contain_p.append(p)
            else:
                l_overlap_p.append(p)
                r_overlap_p.append(p)
        else:
            r_contain_p.append(p)
            
    for p in overlap_p:
        if b_left.intersects(p):
            l_overlap_p.append(p)
            if b_right.intersects(p):
                r_overlap_p.append(p)
        else:
            r_overlap_p.append(p)
    
    return nt.KDBranch(
        split,
        create_node(nt,depth,b_left,l_contain_p,l_overlap_p),
        create_node(nt,depth,b_right,r_contain_p,r_overlap_p))

    
def build_kdtree(nt,primitives):
    if not isinstance(nt,NTracer): nt = NTracer(nt)

    start = nt.Vector(reduce(partial(map,min),(p.aabb_min for p in primitives)))
    end = nt.Vector(reduce(partial(map,max),(p.aabb_max for p in primitives)))
    return create_node(nt,-1,nt.AABB(start,end),primitives,[])
