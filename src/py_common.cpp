#include "py_common.hpp"
#include "pyobject.hpp"


const char *no_delete_msg = "This attribute cannot be deleted";
const char *not_init_msg = "This object has not been initialized. Its __init__ method must be called first.";
const char *unspecified_err_msg = "unspecified error";
const char *no_keywords_msg = "keyword arguments are not accepted";
const char *init_on_derived_msg = "__init__ cannot be used directly on a derived type";
const char *not_implemented_msg = "This method is not implemented";


get_arg::get_arg_base::get_arg_base(PyObject *args,PyObject *kwds,Py_ssize_t param_len,PyObject **names,const char *fname)
        : args(args), kwds(kwds), names(names), fname(fname), kcount(0) {
    assert(args != NULL && PyTuple_Check(args));
    assert(kwds == NULL || PyDict_Check(kwds));

    Py_ssize_t given = PyTuple_GET_SIZE(args);
    if(kwds) given += PyDict_Size(kwds);
    if(UNLIKELY(given > param_len)) {
        PyErr_Format(PyExc_TypeError, "%s%s takes at most %zd argument%s (%zd given)",
            fname ? fname : "function",
            fname ? "()" : "",
            param_len,
            (param_len == 1) ? "" : "s",
            given);
        throw py_error_set();
    }
}

PyObject *get_arg::get_arg_base::operator()(Py_ssize_t i,get_arg::arg_type type) {
    assert(type != KEYWORD_ONLY || names);

    if(i < PyTuple_GET_SIZE(args)) {
        if(type == KEYWORD_ONLY) {
            PyErr_Format(PyExc_TypeError,"\"%U\" is a keyword-only argument",names[i]);
            throw py_error_set();
        }

        PyObject *r = PyTuple_GET_ITEM(args,i);
        if(UNLIKELY(names && kwds && PyDict_GetItem(kwds,names[i]))) {
            PyErr_Format(PyExc_TypeError,"got multiple values for keyword argument \"%U\"",names[i]);
            throw py_error_set();
        }
        return r;
    }
    if(names && kwds) {
        PyObject *r = PyDict_GetItem(kwds,names[i]);
        if(r) {
            ++kcount;
            return r;
        }
    }

    if(UNLIKELY(type == REQUIRED)) {
        if(names) PyErr_Format(PyExc_TypeError,"a value for keyword argument \"%U\" is required",names[i]);
        else PyErr_Format(PyExc_TypeError,"a value for positional argument # %zd is required",i);
        throw py_error_set();
    }

    return nullptr;
}

void get_arg::get_arg_base::finished() {
    // are there more keywords than we used?
    if(UNLIKELY(kwds && kcount < PyDict_Size(kwds))) {
        PyObject *key;
        Py_ssize_t pos = 0;
        while(PyDict_Next(kwds,&pos,&key,NULL)){
            if(!PyUnicode_Check(key)) {
                PyErr_SetString(PyExc_TypeError,"keywords must be strings");
                throw py_error_set();
            }

            if(names) {
                for(PyObject **name = names; *name; ++name) {
                    int r = PyUnicode_Compare(key,*name);
                    if(r == 0) goto match;
                    if(r == -1 && PyErr_Occurred()) throw py_error_set();
                }
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

#if !defined(Py_LIMITED_API) && PY_VERSION_HEX >= 0x03070000
get_arg::fast_get_arg_base::fast_get_arg_base(PyObject *const *args,Py_ssize_t nargs,PyObject *kwnames,Py_ssize_t param_len,PyObject **names,const char *fname)
    : args(args), nargs(nargs), kwnames(kwnames), param_len(param_len), names(names), fname(fname), kcount(0)
{
    assert(kwnames == NULL || PyTuple_Check(kwnames));

    Py_ssize_t tot_args = nargs + (kwnames ? PyTuple_GET_SIZE(kwnames) : 0);

    if(UNLIKELY(tot_args > param_len)) {
        PyErr_Format(PyExc_TypeError, "%s%s takes at most %zd argument%s (%zd given)",
            fname ? fname : "function",
            fname ? "()" : "",
            param_len,
            (param_len == 1) ? "" : "s",
            tot_args);
        throw py_error_set();
    }
}

PyObject *get_arg::fast_get_arg_base::operator()(Py_ssize_t i,get_arg::arg_type type) {
    assert(type != KEYWORD_ONLY || names);

    PyObject *kval = nullptr;
    if(kwnames) {
        for(int j=0; j<PyTuple_GET_SIZE(kwnames); ++j) {
            int cr = PyUnicode_Compare(names[i],PyTuple_GET_ITEM(kwnames,j));
            if(cr == 0) {
                kval = args[nargs+j];
                break;
            } else if(cr == -1 && PyErr_Occurred()) throw py_error_set();
        }
    }

    if(i < nargs) {
        if(type == KEYWORD_ONLY) {
            PyErr_Format(PyExc_TypeError,"\"%U\" is a keyword-only argument",names[i]);
            throw py_error_set();
        }

        if(UNLIKELY(kval)) {
            PyErr_Format(PyExc_TypeError,"got multiple values for keyword argument \"%U\"",names[i]);
            throw py_error_set();
        }
        return args[i];
    }
    if(kval) {
        ++kcount;
        return kval;
    }

    if(UNLIKELY(type == REQUIRED)) missing_arg(i);

    return nullptr;
}

void get_arg::fast_get_arg_base::missing_arg(Py_ssize_t index) const {
    if(names) PyErr_Format(PyExc_TypeError,"a value for keyword argument \"%U\" is required",names[index]);
    else PyErr_Format(PyExc_TypeError,"a value for positional argument # %zd is required",index);
    throw py_error_set();
}

void get_arg::fast_get_arg_base::finished() {
    // are there more keywords than we used?
    if(UNLIKELY(kwnames && kcount < PyTuple_GET_SIZE(kwnames))) {
        for(int i=0; i<PyTuple_GET_SIZE(kwnames); ++i) {
            if(names) {
                for(int j=0; j<param_len; ++j) {
                    int r = PyUnicode_Compare(names[j],PyTuple_GET_ITEM(kwnames,i));
                    if(r == 0) goto match;
                    if(r == -1 && PyErr_Occurred()) throw py_error_set();
                }
            }

            PyErr_Format(PyExc_TypeError,"'%U' is an invalid keyword argument for %s%s",
                PyTuple_GET_ITEM(kwnames,i),
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
#endif

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
            Py_ssize_t needed = PyTuple_GET_SIZE(args); // (len(args) - 1) commas and a terminating NUL
            for(Py_ssize_t i = 0; i < PyTuple_GET_SIZE(args); ++i) {
                assert(PyTuple_GET_ITEM(args,i)->ob_type && PyTuple_GET_ITEM(args,i)->ob_type->tp_name);
                needed += strlen(PyTuple_GET_ITEM(args,i)->ob_type->tp_name);
            }

            char *msg = reinterpret_cast<char*>(py::malloc(needed));
            char *cur = msg;

            for(Py_ssize_t i = 0; i < PyTuple_GET_SIZE(args); ++i) {
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

PyObject *immutable_copy_func(PyObject *self,PyObject*) noexcept {
    Py_INCREF(self);
    return self;
}
