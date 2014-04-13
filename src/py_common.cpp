#include "py_common.hpp"
#include "pyobject.hpp"


const char *no_delete_msg = "This attribute cannot be deleted";
const char *not_init_msg = "This object has not been initialized. Its __init__ method must be called first.";
const char *unspecified_err_msg = "unspecified error";
const char *no_keywords_msg = "keyword arguments are not accepted";
const char *init_on_derived_msg = "__init__ cannot be used directly on a derived type";
const char *not_implemented_msg = "This method is not implemented";


#if PY_MAJOR_VERSION < 3 || (PY_MARJOR_VERSION == 3 && PY_MINOR_VERSION < 3)
Py_ssize_t PyObject_LengthHint(PyObject *o,Py_ssize_t defaultlen) {
    Py_ssize_t r = PyObject_Size(o);
    if(r >= 0) return r;
    
    if(!PyErr_ExceptionMatches(PyExc_TypeError)) return -1;
    PyErr_Clear();
    return defaultlen;
}
#endif

get_arg::get_arg(PyObject *args,PyObject *kwds,Py_ssize_t arg_len,const char **names,const char *fname)
        : args(args), kwds(kwds), names(names), fname(fname), arg_index(0), tcount(0), kcount(0) {
    assert(args != NULL && PyTuple_Check(args));
    assert(kwds == NULL || PyDict_Check(kwds));
    
    Py_ssize_t given = PyTuple_GET_SIZE(args);
    if(kwds) given += PyDict_Size(kwds);
    if(UNLIKELY(given > arg_len)) {
        PyErr_Format(PyExc_TypeError, "%s%s takes at most %zd argument%s (%zd given)",
            fname ? fname : "function",
            fname ? "()" : "",
            arg_len,
            (arg_len == 1) ? "" : "s",
            given);
        throw py_error_set();
    }
}

PyObject *get_arg::operator()(bool required) {
    const char *name = names[arg_index++];
    if(tcount < PyTuple_GET_SIZE(args)) {
        PyObject *r = PyTuple_GET_ITEM(args,tcount++);
        if(UNLIKELY(name && kwds && PyDict_GetItemString(kwds,name))) {
            PyErr_Format(PyExc_TypeError,"got multiple values for keyword argument \"%s\"",name);
            throw py_error_set();
        }
        return r;
    }
    if(name && kwds) {
        PyObject *r = PyDict_GetItemString(kwds,name);
        if(r) {
            ++kcount;
            return r;
        }
    }

    if(UNLIKELY(required)) {
        if(name) PyErr_Format(PyExc_TypeError,"a value for keyword argument \"%s\" is required",name);
        else PyErr_Format(PyExc_TypeError,"a value for positional argument # %zd is required",tcount);
        throw py_error_set();
    }

    return NULL;
}

void get_arg::finished() {
    // are there more keywords than we used?
    if(UNLIKELY(kwds && kcount > PyDict_Size(kwds))) {
        PyObject *key;
        Py_ssize_t pos = 0;
        while(PyDict_Next(kwds,&pos,&key,NULL)){
            if(!PyUnicode_Check(key)) {
                PyErr_SetString(PyExc_TypeError,"keywords must be strings");
                throw py_error_set();
            }
#if PY_MAJOR_VERSION >= 3 && PY_MINOR_VERSION >= 3
            const char *kstr = PyUnicode_AsUTF8(key);
            if(!kstr) throw py_error_set();
#else
            PyObject *kstr_obj = PyUnicode_AsUTF8String(key);
            if(!kstr_obj) throw py_error_set();
            
            struct deleter {
                PyObject *ptr;
                deleter(PyObject *ptr) : ptr(ptr) {}
                ~deleter() { Py_DECREF(ptr); }
            } _(kstr_obj);
            
            const char *kstr = PyBytes_AS_STRING(kstr_obj);
#endif
            for(const char **name = names; *name; ++name) {
                if(strcmp(kstr,*name) == 0) goto match;
            }
                
            PyErr_Format(PyExc_TypeError,"'%U' is an invalid keyword argument for %s%s",
                key,
                fname ? fname : "this function",
                fname ? "()" : "");
            throw py_error_set();
            
        match:
            ;
        }
        
        // should never reach here
        assert(false);
    }
}

long narrow(long x,long max,long min) {
    if(UNLIKELY(x > max || x < min)) {
        if(min == 0 && x < 0) PyErr_SetString(PyExc_TypeError,"value cannot be negative");
        else PyErr_SetString(PyExc_OverflowError,"value is out of range");
        throw py_error_set();
    }
    return x;
}

void NoSuchOverload(PyObject *args) {
    const char *const format = "no overload takes (%s)";
    if(PyTuple_Check(args)) {
        if(PyTuple_GET_SIZE(args)) {
            unsigned int needed = PyTuple_GET_SIZE(args); // (len(args) - 1) commas and a terminating NUL
            for(unsigned int i = 0; i < PyTuple_GET_SIZE(args); ++i) {
                assert(PyTuple_GET_ITEM(args,i)->ob_type && PyTuple_GET_ITEM(args,i)->ob_type->tp_name);
                needed += strlen(PyTuple_GET_ITEM(args,i)->ob_type->tp_name);
            }

            char *msg = reinterpret_cast<char*>(py::malloc(needed));
            char *cur = msg;

            for(unsigned int i = 0; i < PyTuple_GET_SIZE(args); ++i) {
                if(i) *cur++ = ',';
                const char *other = PyTuple_GET_ITEM(args,i)->ob_type->tp_name;
                while(*other) *cur++ = *other++;
            }
            *cur = 0;

            PyErr_Format(PyExc_TypeError,format,msg);
            py::free(msg);
        } else {
            PyErr_SetString(PyExc_TypeError,"no overload takes 0 arguments");
        }
    } else {
        PyErr_Format(PyExc_TypeError,format,args->ob_type->tp_name);
    }
}
