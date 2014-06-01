==========================================
NTracer
==========================================
A fast hyper-spacial ray-tracing library
------------------------------------------

**Important:** this library makes extensive use of features exclusive to C++11.
At the time of this writing, the only compilers that can build this library are
GCC >= 4.7 and Clang >= 3.1.

NTracer is a simple ray-tracer that can work with scenes with an arbitrary
number of dimensions.

.. figure:: https://rouslan.github.io/NTracer/screenshots/ntracer_6d_thumb.png
    :alt: sceenshot 1
    :target: https://rouslan.github.io/NTracer/screenshots/ntracer_6d.png
    
    A three-dimension slice of a six-dimensional hypercube

.. figure:: https://rouslan.github.io/NTracer/screenshots/ggs120cell_thumb.png
    :alt: screenshot 2
    :target: https://rouslan.github.io/NTracer/screenshots/ggs120cell.png
    
    A three-dimension slice of a `great grand stellated 120-cell
    <http://en.wikipedia.org/wiki/Great_grand_stellated_120-cell>`_

The renderer can use an arbitrary number of threads and by default uses as many
threads as there are processing cores. For small dimensionalities (by default,
eight or fewer dimensions), the library uses specialized routines with the
number of dimensions hard-coded, which offer better performance by avoiding the
looping and heap allocation that the generic versions require.

The main goal is to aid in the visualization of higher-dimensional space.

There is special support for Pygame, but it is not a requirement. However, the
included example scripts, hypercube.py and polytope.py, depend on it.

Documentation is available at http://rouslan.github.io/NTracer/doc.


Dealing with Higher-Dimensional Space
==========================================

Regardless of the number of dimensions of the scene, the images produced by the
ray-tracer are always taken from the view-point of a three-dimensional observer.
A two-dimensional image is produced by projecting rays from a single point, onto
a grid corresponding to the pixels of the image. The consequence of this is that
the observer can only see a three-dimensional slice of the entire scene with a
single image. The reason for this can be understood by imagining a
lower-dimensional analog. If a being existed in a two-dimensional universe, it
would only be able to see in two dimensions and have three degrees of freedom
(two translation and one rotation components). If the being were plucked from
its universe and placed before a three-dimensional object, it would only be able
to see a two-dimensional slice of the object at any given time. To see the rest
of the object, the being would have to either translate or rotate itself in a
way that exploits one of the newly acquired degrees of freedom (one new
translation and two new rotation components).

Having more than three dimensions, it no longer makes sense to rotate about a
single axis. Thus the static member function ``Matrix.rotation``, which creates
a rotation matrix, requires two vectors to describe a plane of rotation.

Normally, cross products can only be computed in three-dimensional space. To
find perpendicular vectors, the function ``cross`` provides a generalized
version, which takes a sequence of D-1 linearly independent vectors, where D is
the dimensionality of the scene.

In three-dimensional space, the surface of a solid object can be constructed out
of triangles. However, it is impossible to fully enclose objects with more than
three dimensions using a finite number of triangles, just as three-dimensional
objects cannot be enclosed using a finite set of lines or points. Therefore,
instead of triangles, n-dimensional simplexes, where n is one less than the
number of dimensions of the scene, are used instead. One may be tempted to point
out that a 3-simplex (three-dimensional simplex, i.e. a tetrahedron) can be
built out of triangles, a 4-simplex can be built out of 3-simplexes and so on,
so one should still be able to use triangles to build higher dimensional
objects, but this is not quite correct. Technically, you can only construct the
hull of a 3-simplex. In four-dimensional space, a line can pass through a
3-simplex without intersecting any of its faces. In fact, orienting a line so it
does intersect a face would be like trying to stab a line of zero thickness with
a needle of zero thickness in three-dimensional space. It only gets worse with
even more dimensions. The simplex class provided by this library (which is
actually named ``Triangle``) is continuous between every point and avoids this
problem.
