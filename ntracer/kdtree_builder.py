from future_builtins import map,zip

import sys
import itertools

from ntracer.wrapper import NTracer,CUBE,SPHERE


MAX_DEPTH = 20

# only split nodes if there are more than this many primitives
SPLIT_THRESHHOLD = 3


def plane_distance(nt,p_normal,p_pos,point):
    return nt.dot(point - p_pos,p_normal)

def occurs(nt,dim,split,p):
    if isinstance(p,TrianglePrototype):
        return any(p[dim] < split for p in p.points),any(p[dim] >= split for p in p.points)
    
    assert isinstance(p,nt.Solid) and p.type == SPHERE
    dist = plane_distance(
        nt,
        nt.Vector.axis(dim) * p.orientation.transpose(),
        nt.Vector.axis(dim,split) - p.position)
    return dist < radius,dist > -radius


# The primitives are divided into the lists: contain_p and overlap_p.
# Primitives in contain_p are entirely inside boundary, and are much easier to
# partition. The rest of the primitives are in overlap_p.
def create_node(nt,depth,boundary,contain_p,overlap_p):
    depth += 1
    cur_dim = depth % nt.dimension
    
    if not primitives: return None
    
    if depth >= MAX_DEPTH or len(primitives) <= SPLIT_THRESHHOLD:
        return nt.KDLeaf(p if isinstance(p,nt.Primitive) else p.realize() for p in (contain_p + overlap_p))
    
    split = find_split()
    
    b_left = boundary.left(split)
    b_right = boundary.right(split)
    
    l_contain_p = []
    r_contain_p = []
    l_overlap_p = []
    r_overlap_p = []
    
    for p in contain_p:
        l,r = occurs(nt,cur_dim,split,p)
        if l:
            if r:
                l_overlap_p.append(p)
                r_overlap_p.append(p)
            else:
                l_contain_p.append(p)
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
        create_node(nt,depth,b_left,l_contain_p,l_overlap_p),
        create_node(nt,depth,b_right,r_contain_p,r_overlap_p))

    

def build_kdtree(nt,primitives):
    if not isinstance(nt,NTracer): nt = NTracer(nt)
    return create_node(nt,-1,nt.AABB(),primitives,[])