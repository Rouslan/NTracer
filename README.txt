==========================================
NTracer
==========================================
A fast hyper-spacial ray-tracing library
------------------------------------------

**Important:** this library makes extensive use of features exclusive to C++11.
At the time of this writing, the only compilers that can build this library are
GCC >= 4.7 and Clang >= 3.0.

NTracer is a simple ray-tracer that can work with scenes (currently limited to a
scene containing one hyper-cube) with an arbitrary number of dimensions.

The renderer can use an arbitrary number of threads and by default uses as many
threads as there are processing cores. For small dimensionalities (by default,
eight or fewer dimensions), the library uses specialized routines with the
number of dimensions hard-coded, which offer better performance by avoiding the
looping and heap allocation that the generic versions require.

The main goal is to aid in the visualization of higher-dimensional space. Python
and Pygame were chosen as the library's interface to make experimenting with
user interfaces and navigation schemes as easy as possible. The included script,
hypercube.py, offers a working example.


Dealing with Higher-Dimensional Space
==========================================

Regardless of the number of dimensions of the scene, the images produced by the
ray-tracer are always taken from the view-point of a three-dimensional observer.
A two-dimensional image is produced by projecting rays from a single point, onto
a grid corresponding to the pixels of the image. The consequence of this is that
the observer can only see a three-dimensional slice of the entire scene with a
single image. The reason for this can be understood by imagining a lower-
dimensional analog. If a being existed in a two-dimensional universe, it would
only be able to see in two dimensions and have three degrees of freedom (two 
translation and one rotation components). If the being were plucked from its
universe and placed before a three-dimensional object, it would only be able to
see a two-dimensional slice of the object at any given time. To see the rest of
the object, the being would have to either translate or rotate itself in a way
that exploits one of the newly acquired degrees of freedom (one new translation
and two new rotation components).

Another consequence of having more than three dimensions is it no longer makes
sense to rotate about a single axis. Thus the static member function
``Matrix.rotation``, which creates a rotation matrix, requires two vectors to
describe a plane of rotation.
