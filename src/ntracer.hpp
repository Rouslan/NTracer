#ifndef ntracer_h
#define ntracer_h

#include <Python.h>


struct vector_obj_base {
    static PyTypeObject pytype;
};

struct camera_obj_base {
    static PyTypeObject pytype;
};

struct matrix_obj_base {
    static PyTypeObject pytype;
};


#endif

