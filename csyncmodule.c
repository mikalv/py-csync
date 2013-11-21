#include <Python.h>
#include <structmember.h>
#include <csync/csync.h>
#undef NDEBUG
#include <assert.h>

//#include "csync_private.h"
// ctx->status & CSYNC_STATUS_INIT


/** Exceptions. */

//static PyObject *CSyncError;



/**
 * The CSync Python type.
 */

// Object struct
typedef struct {
    PyObject_HEAD
    // Type-specific fields go here:
    CSYNC *ctx;
    PyObject *callback;
    PyObject *auth_callback;
    /*
    struct {
        PyObject *auth;
        PyObject *file_progress;
        PyObject *overall_progress;
    } callbacks;
    */
} CSync;
//} CSyncObject;


// Constructor (allocate)
static PyObject *
_py_csync_new (PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    CSync *self;

    self = (CSync*) type->tp_alloc (type, 0);
    if (! self)
        return NULL;

    // clear all members
    self->ctx = NULL;
    self->callback = NULL;
    self->auth_callback = NULL;

    return (PyObject*) self;
}


// Constructor (initialize / construct)
// q: object will not be constructed if this fails with -1?
static int
_py_csync_init (CSync *self, PyObject *args, PyObject *kwargs)
{
    static char *kwlist[] = { "local", "remote", NULL};
    const char *local, *remote;

    if (! PyArg_ParseTupleAndKeywords (args, kwargs, "|ss",
                                       kwlist, &local, &remote))
        return -1;

    assert (self->ctx == NULL);
    if (csync_create (&self->ctx, local, remote) != 0)
        return -1;

    int rv;
    rv = csync_set_userdata (self->ctx, self);
    assert (rv==0);

    return 0;
    // @todo must free local&remote? (or keep'em on self)
}


// Destructor
static void
_py_csync_dealloc (CSync *self)
{
    assert (self->ctx);
    int rv = csync_destroy (self->ctx);
    assert (rv==0);
    // Q: howto report error? throw exception
    // A? PyErr_SetString(PyExc_AttributeError, "first");

    self->ob_type->tp_free ((PyObject*) self);
}


// Misc functions


// Python callback is passed: (string prompt, bool echo, bool verify)
// And returns password string on success, or None on failure.
// Q: what to return?
static int
_py_auth_callback_wrapper (const char *prompt,
                           char       *buffer,
                           size_t      buffer_len,
                           int         echo,
                           int         verify,
                           void       *userdata)
{
    CSync *self;
    PyObject *arglist;
    PyObject *result;
//    printf ("  echo:\t %d\n", echo);
//    printf ("  verify:%d\n", verify);

    self = (CSync*) userdata;
    assert (self);

    arglist = Py_BuildValue ("(sii)", prompt, echo, verify);
    result = PyObject_CallObject (self->auth_callback, arglist);
    Py_XDECREF (arglist);
    if (! result) {
        // Python error / exception in callback
        // howto pass back to interpereter
        // PyWarn?
        fprintf (stderr, "\nFIXME Python error in auth-callback"
                         "\nFIXME Do not know how to handle that ...\n");
        assert (false);
    }

    if (result == Py_None)
        assert(0);
    if (! PyString_Check (result))
        assert(0);

    int rv;
    char *strbuf;
    Py_ssize_t sz;
    rv = PyString_AsStringAndSize (result, &strbuf, &sz);
    assert (rv==0);
    assert (sz < buffer_len);
    if (sz > 0)
        strcpy (buffer, strbuf);

    Py_DECREF (result);
    return 0;
}


static int
_py_treewalk_visit_func (TREE_WALK_FILE *file, void *userdata)
{
    printf ("visitor: %p\n", userdata);
    return 1;
}




/**
 * CSync methods.
 */

// just for testing
static PyObject *
py_csync_test (CSync *self, PyObject *args)
{
    PyObject *arglist;
    PyObject *result;

    arglist = Py_BuildValue ("()");
    result = PyObject_CallObject (self->callback, arglist);
    Py_XDECREF (arglist);
    if (! result) return NULL;    // pass error back
    Py_DECREF(result);
    Py_RETURN_NONE;

//    result = PyObject_CallObject (self->callback, args);
//    return result;

    // use result ...
    // Py_DECREF(result);
    // Py_RETURN_NONE;
}

/*
// @todo None => no callback
static PyObject *
py_csync_set_callback (CSync *self, PyObject *args)
{
    PyObject *tmp;

    if (! PyArg_ParseTuple (args, "O:set_callback", &tmp))
        return NULL;
    if (! PyCallable_Check (tmp)) {
        PyErr_SetString (PyExc_TypeError, "parameter must be callable");
        return NULL;
    }

    Py_INCREF (tmp);
    Py_XDECREF (self->callback);
    self->callback = tmp;
    Py_RETURN_NONE;
}
*/


static PyObject *
py_csync_walk_local_tree (CSync *self, PyObject *args)
{
    PyObject *visitor;
    int filter;

    if (! PyArg_ParseTuple (args, "Oi", &visitor, &filter))
        return NULL;
    if (! PyCallable_Check (visitor)) {
        PyErr_SetString (PyExc_TypeError, "parameter must be callable");
        return NULL;
    }

    int rv;

    filter = 0x1ff;
    rv = csync_walk_local_tree (self->ctx, _py_treewalk_visit_func, filter);
//    rv = csync_walk_remote_tree (self->ctx, _py_treewalk_visit_func, filter);
    printf ("rv = %d\n", rv);
    assert (rv==0);

//    if (CSYNC_STATUS_IS_OK (rv))

    Py_RETURN_NONE;
}


static PyObject *
py_csync_set_auth_callback (CSync *self, PyObject *args)
{
    PyObject *tmp;

    if (! PyArg_ParseTuple (args, "O", &tmp))
        return NULL;
    if (! PyCallable_Check (tmp)) {
        PyErr_SetString (PyExc_TypeError, "parameter must be callable");
        return NULL;
    }

    int rv = csync_set_auth_callback (self->ctx, _py_auth_callback_wrapper);
    assert (rv == 0);

    Py_INCREF (tmp);    // ParseTuple returns borrowed ref, so must inc before storing in self
    Py_XDECREF (self->auth_callback);
    self->auth_callback = tmp;
    Py_RETURN_NONE;
}


static PyObject *
py_csync_get_auth_callback (CSync *self)
{
    Py_XINCREF (self->auth_callback);
    return self->auth_callback;
}


static PyObject *
py_csync_init (CSync *self)
{
    int rv = csync_init (self->ctx);
    // csync_errno_to_status
//    printf ("init rv = %d\n", rv);
//    printf ("status: %d\n", csync_get_status (self->ctx));
//    printf ("status str: %s\n", csync_get_status_string (self->ctx));
    assert (rv == 0);
    Py_RETURN_NONE;
}


static PyObject *
py_csync_add_exclude_list (CSync *self, PyObject *args)
{
    int rv;
    const char *path;

    if (! PyArg_ParseTuple (args, "s", &path))
        return NULL;

    // error if file does not exists. howto get error code / str?
//    rv = csync_add_exclude_list (self->ctx, "/tmp/a/xignore");
    rv = csync_add_exclude_list (self->ctx, path);
    assert (rv==0);

    Py_RETURN_NONE;
}


static PyObject *
py_csync_disable_statedb (CSync *self)
{
    int rv = csync_disable_statedb (self->ctx);
    assert (rv==0);
    Py_RETURN_NONE;
}

static PyObject *
py_csync_enable_statedb (CSync *self)
{
    int rv = csync_enable_statedb (self->ctx);
    assert (rv==0);
    Py_RETURN_NONE;
}


static PyObject *
py_csync_get_config_dir (CSync *self)
{
    const char *path = csync_get_config_dir (self->ctx);
    assert (path);
    return PyString_FromString (path);
}


// @note segfault if init() is not called first!
// @note segfault if statedb disabled (might return NULL? but howto report errors?)
static PyObject *
py_csync_get_statedb_file (CSync *self)
{
    const char *path = csync_get_statedb_file (self->ctx);
    assert (path);
    return PyString_FromString (path);
}


static PyObject *
py_csync_is_statedb_disabled (CSync *self)
{
    int enabled = csync_is_statedb_disabled (self->ctx);
    if (enabled)
        Py_RETURN_TRUE;
    else
        Py_RETURN_FALSE;
}


static PyObject *
py_csync_propagate (CSync *self)
{
    int rv = csync_propagate (self->ctx);
    assert (rv==0);
    Py_RETURN_NONE;
}


static PyObject *
py_csync_reconcile (CSync *self)
{
    int rv = csync_reconcile (self->ctx);
    assert (rv==0);
    Py_RETURN_NONE;
}


static PyObject *
py_csync_update (CSync *self)
{
    int rv = csync_update (self->ctx);
    assert (rv==0);
    Py_RETURN_NONE;
}


static PyObject *
py_csync_commit (CSync *self)
{
    int rv = csync_commit (self->ctx);
    assert (rv==0);
    Py_RETURN_NONE;
}


static PyObject *
py_csync_set_config_dir (CSync *self, PyObject *args)
{
    int rv;
    const char *path;
    if (! PyArg_ParseTuple (args, "s", &path))
        return NULL;

    rv = csync_set_config_dir (self->ctx, path);
    assert (rv==0);
    Py_RETURN_NONE;
}



/*
// ImportError: build/lib.linux-x86_64-2.7/csync.so: undefined symbol:
// csync_remove_config_dir
static PyObject *
py_csync_remove_config_dir (CSync *self)
{
    int rv = csync_remove_config_dir (self->ctx);
    assert (rv==0);
    Py_RETURN_NONE;
}
*/


// XXX missing from my build
// has get_status() but doc says: Used for special modes or debugging
/*
static PyObject *
py_csync_status_ok (CSync *self)
{
    bool status = csync_status_ok (self->ctx);
    if (status)
        Py_RETURN_TRUE;
    else
        Py_RETURN_FALSE;
}
*/



/**
 * CSync members, methods and type structure.
 */

// Members list
// CSync_members
static PyMemberDef CSyncMembers[] = {
//    {"first", T_OBJECT_EX, offsetof(Noddy, first), 0, "first name"},
//    {"number", T_INT, offsetof(Noddy, number), 0, "noddy number"},
    { NULL },
};


// Method list
// CSync_methods
static PyMethodDef CSyncMethods[] = {
    { "init",               (PyCFunction) py_csync_init,                METH_NOARGS,    "docstring" },
    { "add_exclude_list",   (PyCFunction) py_csync_add_exclude_list,    METH_VARARGS,   "docstring" },
    { "disable_statedb",    (PyCFunction) py_csync_disable_statedb,     METH_NOARGS,    "docstring" },
    { "enable_statedb",     (PyCFunction) py_csync_enable_statedb,      METH_NOARGS,    "docstring" },
    { "is_statedb_disabled",(PyCFunction) py_csync_is_statedb_disabled, METH_NOARGS,    "docstring" },
    { "get_statedb_file",   (PyCFunction) py_csync_get_statedb_file,    METH_NOARGS,    "docstring" },
    { "update",             (PyCFunction) py_csync_update,              METH_NOARGS,    "docstring" },
    { "propagate",          (PyCFunction) py_csync_propagate,           METH_NOARGS,    "docstring" },
    { "reconcile",          (PyCFunction) py_csync_reconcile,           METH_NOARGS,    "docstring" },
    { "commit",             (PyCFunction) py_csync_commit,              METH_NOARGS,    "docstring" },
    { "set_config_dir",     (PyCFunction) py_csync_set_config_dir,      METH_VARARGS,   "docstring" },
    { "get_config_dir",     (PyCFunction) py_csync_get_config_dir,      METH_NOARGS,    "docstring" },
//    { "remove_config_dir",  (PyCFunction) py_csync_remove_config_dir,   METH_NOARGS,    "docstring" },
    { "set_auth_callback",  (PyCFunction) py_csync_set_auth_callback,   METH_VARARGS,   "docstring" },
    { "get_auth_callback",  (PyCFunction) py_csync_get_auth_callback,   METH_NOARGS,    "docstring" },
    { "walk_local_tree",    (PyCFunction) py_csync_walk_local_tree,     METH_VARARGS,   "docstring" },

    // just testing ...
//    { "set_callback",       (PyCFunction) py_csync_set_callback,        METH_VARARGS,   "docstring" },
    { "test", (PyCFunction) py_csync_test, METH_VARARGS, "docstring" },

    // @todo boolean statedb member?
//    { "status_ok", (PyCFunction) py_csync_status_ok, METH_NOARGS, "docstring" },
    { NULL },
};


// Type struct
static PyTypeObject CSyncType = {
    PyObject_HEAD_INIT(NULL)
    0,                              /* ob_size */
    "csync.CSync",                  /* tp_name */
    sizeof(CSync),                  /* tp_basicsize */
    0,                              /* tp_itemsize */
    (destructor) _py_csync_dealloc, /* tp_dealloc */
    0,                              /* tp_print */
    0,                              /* tp_getattr */
    0,                              /* tp_setattr */
    0,                              /* tp_compare */
    0,                              /* tp_repr */
    0,                              /* tp_as_number */
    0,                              /* tp_as_sequence */
    0,                              /* tp_as_mapping */
    0,                              /* tp_hash */
    0,                              /* tp_call */
    0,                              /* tp_str */
    0,                              /* tp_getattro */
    0,                              /* tp_setattro */
    0,                              /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,       /* tp_flags */
    "CSync objects",                /* tp_doc */
    0,                              /* tp_traverse */
    0,                              /* tp_clear */
    0,                              /* tp_richcompare */
    0,                              /* tp_weaklistoffset */
    0,                              /* tp_iter */
    0,                              /* tp_iternext */
    CSyncMethods,                   /* tp_methods */
    CSyncMembers,                   /* tp_members */
    0,                              /* tp_getset */
    0,                              /* tp_base */
    0,                              /* tp_dict */
    0,                              /* tp_descr_get */
    0,                              /* tp_descr_set */
    0,                              /* tp_dictoffset */
    (initproc) _py_csync_init,      /* tp_init */
    0,                              /* tp_alloc */
    _py_csync_new,                  /* tp_new */
};





/**
 * Module methods.
 */

// @note no argument => required version is same as build version
// @todo take required version in tuple? (not int)
static PyObject *
py_csync_version (PyObject *self, PyObject *args)
{
    int reqver = -1;
    if (! PyArg_ParseTuple (args, "|i", &reqver))
        return NULL;
    if (reqver == -1)
        reqver = CSYNC_VERSION_INT (LIBCSYNC_VERSION_MAJOR, LIBCSYNC_VERSION_MINOR, LIBCSYNC_VERSION_MICRO);

//    const char* verstr = csync_version (reqver);
//    if (verstr == NULL) Py_RETURN_NONE;
//    return PyString_FromString (NULL);

    return Py_BuildValue ("s", csync_version (reqver));  // NULL -> NoneType
}




/** Module init. */

// Module methods
// @todo lower case?
static PyMethodDef CSyncModuleMethods[] = {
    { "version", py_csync_version, METH_VARARGS, "Get the csync version string." },
    { NULL }
};


// Only non-static function. Should be named "init<modulename>".
PyMODINIT_FUNC
initcsync (void)
{
    PyObject *mod;

    CSyncType.tp_new = PyType_GenericNew;
    if (PyType_Ready(&CSyncType) < 0)
        return;
    
    mod = Py_InitModule3 ("csync", CSyncModuleMethods, "docstring");
    if (! mod) return;

    // Register type
    Py_INCREF (&CSyncType);
    PyModule_AddObject (mod, "CSync", (PyObject*) &CSyncType);

    /** Constants */
    // @todo check rv
    PyModule_AddObject (mod, "VERSION", Py_BuildValue ("(iii)", LIBCSYNC_VERSION_MAJOR, LIBCSYNC_VERSION_MINOR, LIBCSYNC_VERSION_MICRO));
    //PyModule_AddStringConstant (mod, "VERSION_STRING", CSYNC_STRINGIFY(LIBCSYNC_VERSION));
    PyModule_AddIntConstant (mod, "VERSION_MAJOR", LIBCSYNC_VERSION_MAJOR);
    PyModule_AddIntConstant (mod, "VERSION_MINOR", LIBCSYNC_VERSION_MINOR);
    PyModule_AddIntConstant (mod, "VERSION_MICRO", LIBCSYNC_VERSION_MICRO);
    PyModule_AddStringConstant (mod, "CONF_DIR", CSYNC_CONF_DIR);
    PyModule_AddStringConstant (mod, "CONF_FILE", CSYNC_CONF_FILE);
    PyModule_AddStringConstant (mod, "EXCLUDE_FILE", CSYNC_EXCLUDE_FILE);
    PyModule_AddStringConstant (mod, "LOCK_FILE", CSYNC_LOCK_FILE);
#ifdef CSYNC_LOG_FILE
    PyModule_AddStringConstant (mod, "LOG_FILE", CSYNC_LOG_FILE);
#endif
    // PyModule_AddIntMacro     # uses same name as C macro


    // Register custom exceptions
//    CSyncError = PyErr_NewException ("csync.Error", NULL, NULL);
//    Py_INCREF (CSyncError);
//    PyModule_AddObject (mod, "Error", CSyncError);
}
