#include "Python.h"

// hawkey
#include "src/errno.h"
#include "src/package_internal.h"
#include "src/packageset.h"
#include "src/repo.h"
#include "src/sack_internal.h"
#include "src/util.h"

// pyhawkey
#include "exception-py.h"
#include "hawkey-pysys.h"
#include "iutil-py.h"
#include "package-py.h"
#include "repo-py.h"
#include "sack-py.h"

typedef struct {
    PyObject_HEAD
    HySack sack;
    PyObject *custom_package_class;
    PyObject *custom_package_val;
} _SackObject;

PyObject *
new_package(PyObject *sack, Id id)
{
    _SackObject *self;

    if (!sackObject_Check(sack)) {
	PyErr_SetString(PyExc_TypeError, "Expected a _hawkey.Sack object.");
	return NULL;
    }
    self = (_SackObject*)sack;
    PyObject *arglist;
    if (self->custom_package_class || self->custom_package_val) {
	arglist = Py_BuildValue("(Oi)O", sack, id, self->custom_package_val);
    } else {
	arglist = Py_BuildValue("((Oi))", sack, id);
    }
    if (arglist == NULL)
	return NULL;
    PyObject *package;
    if (self->custom_package_class) {
	package = PyObject_CallObject(self->custom_package_class, arglist);
    } else {
	package = PyObject_CallObject((PyObject*)&package_Type, arglist);
    }
    return package;
}

HySack
sackFromPyObject(PyObject *o)
{
    if (!sackObject_Check(o)) {
	PyErr_SetString(PyExc_TypeError, "Expected a _hawkey.Sack object.");
	return NULL;
    }
    return ((_SackObject *)o)->sack;
}

int
sack_converter(PyObject *o, HySack *sack_ptr)
{
    HySack sack = sackFromPyObject(o);
    if (sack == NULL)
	return 0;
    *sack_ptr = sack;
    return 1;
}

/* helpers */
static PyObject *
repo_enabled(_SackObject *self, PyObject *reponame, int enabled)
{
    char *cname = PyString_AsString(reponame);
    if (cname == NULL)
	return NULL;
    hy_sack_repo_enabled(self->sack, cname, enabled);
    Py_RETURN_NONE;
}

/* functions on the type */

static void
sack_dealloc(_SackObject *o)
{
    if (o->sack)
	hy_sack_free(o->sack);
    Py_TYPE(o)->tp_free(o);
}

static PyObject *
sack_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    _SackObject *self = (_SackObject *)type->tp_alloc(type, 0);

    if (self) {
	self->sack = NULL;
	self->custom_package_class = NULL;
	self->custom_package_val = NULL;
    }
    return (PyObject *)self;
}

static int
sack_init(_SackObject *self, PyObject *args, PyObject *kwds)
{
    PyObject *custom_class = NULL;
    PyObject *custom_val = NULL;
    const char *cachedir = NULL;
    const char *arch = NULL;
    char *kwlist[] = {"cachedir", "arch", "pkgcls", "pkginitval", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "|ssOO", kwlist,
				     &cachedir, &arch,
				     &custom_class, &custom_val))
	return -1;
    self->sack = hy_sack_create(cachedir, arch);
    if (self->sack == NULL) {
	switch (hy_get_errno()) {
	case HY_E_IO:
	    PyErr_SetString(PyExc_IOError,
			    "Failed creating working files for the Sack.");
	    break;
	case HY_E_ARCH:
	    PyErr_SetString(HyExc_Arch, "Unrecognized arch for the sack.");
	    break;
	default:
	    assert(0);
	}
	return -1;
    }

    if (custom_class && custom_class != Py_None) {
	if (!PyType_Check(custom_class)) {
	    PyErr_SetString(PyExc_TypeError, "Expected a class object.");
	    return -1;
	}
	Py_INCREF(custom_class);
	self->custom_package_class = custom_class;
    }
    if (custom_val && custom_val != Py_None) {
	Py_INCREF(custom_val);
	self->custom_package_val = custom_val;

    }
    return 0;
}

/* getsetters */

static PyObject *
get_cache_path(_SackObject *self, void *unused)
{
    const char *cstr = hy_sack_get_cache_path(self->sack);
    if (cstr == NULL)
	Py_RETURN_NONE;
    return PyString_FromString(cstr);
}

static int
set_installonly(_SackObject *self, PyObject *obj, void *unused)
{
    if (!PySequence_Check(obj)) {
	PyErr_SetString(PyExc_AttributeError, "Expected a sequence.");
	return -1;
    }

    const int len = PySequence_Length(obj);
    const char *strings[len + 1];

    for (int i = 0; i < len; ++i) {
	PyObject *item = PySequence_GetItem(obj, i);
	strings[i] = PyString_AsString(item);
	Py_DECREF(item);
	if (strings[i] == NULL)
	    return -1;
    }
    strings[len] = NULL;
    hy_sack_set_installonly(self->sack, strings);

    return 0;
}

static PyGetSetDef sack_getsetters[] = {
    {"cache_path",	(getter)get_cache_path, NULL, NULL, NULL},
    {"installonly",	NULL, (setter)set_installonly, NULL, NULL},
    {NULL}			/* sentinel */
};

/* object methods */

static PyObject *
_knows(PyObject *unused, PyObject *args, PyObject *kwds)
{
    HySack sack;
    const char *name;
    int name_only = 0, icase = 0;

    char *kwlist[] = {"sack", "name", "name_only", "icase", NULL};
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "O&s|ii", kwlist,
				     sack_converter, &sack, &name, &name_only,
				     &icase))
	return 0;
    int flags = 0;
    if (name_only)
	flags |= HY_NAME_ONLY;
    if (icase)
	flags |= HY_ICASE;
    return PyInt_FromLong(sack_knows(sack, name, flags));
}

static PyObject *
create_cmdline_repo(_SackObject *self, PyObject *unused)
{
    hy_sack_create_cmdline_repo(self->sack);
    Py_RETURN_NONE;
}

static PyObject *
create_package(_SackObject *self, PyObject *solvable_id)
{
    Id id  = PyInt_AsLong(solvable_id);
    if (id <= 0) {
	PyErr_SetString(PyExc_TypeError, "Expected a positive integer.");
	return NULL;
    }
    return new_package((PyObject*)self, id);
}

static PyObject *
add_cmdline_package(_SackObject *self, PyObject *fn_obj)
{
    HyPackage cpkg;
    PyObject *pkg;
    const char *fn = PyString_AsString(fn_obj);

    if (fn == NULL)
	return NULL;
    cpkg = hy_sack_add_cmdline_package(self->sack, fn);
    if (cpkg == NULL) {
	PyErr_SetString(PyExc_IOError, "Can not load an .rpm file");
	return NULL;
    }
    pkg = new_package((PyObject*)self, package_id(cpkg));
    hy_package_free(cpkg);
    return pkg;
}

static PyObject *
add_excludes(_SackObject *self, PyObject *seq)
{
    if (!PySequence_Check(seq)) {
	PyErr_SetString(PyExc_TypeError, "Expected a sequence.");
	return NULL;
    }
    HySack sack = self->sack;
    HyPackageSet pset = pyseq_to_packageset(seq, sack);
    hy_sack_add_excludes(sack, pset);
    hy_packageset_free(pset);
    Py_RETURN_NONE;
}

static PyObject *
disable_repo(_SackObject *self, PyObject *reponame)
{
    return repo_enabled(self, reponame, 0);
}

static PyObject *
enable_repo(_SackObject *self, PyObject *reponame)
{
    return repo_enabled(self, reponame, 1);
}

static PyObject *
list_arches(_SackObject *self, PyObject *unused)
{
    const char **arches = hy_sack_list_arches(self->sack);
    PyObject *list;
    if (!arches) {
	PyErr_SetString(HyExc_Runtime, "Arches not initialized");
	return NULL;
    }
    list = strlist_to_pylist(arches);
    hy_free(arches);
    return list;
}

static PyObject *
load_system_repo(_SackObject *self, PyObject *args, PyObject *kwds)
{
    char *kwlist[] = {"repo", "build_cache", "load_filelists", "load_presto",
		      NULL};

    HyRepo crepo = NULL;
    int build_cache = 0, unused_1 = 0, unused_2 = 0;
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "|O&iii", kwlist,
				     repo_converter, &crepo,
				     &build_cache, &unused_1, &unused_2))


	return 0;

    int flags = 0;
    if (build_cache)
	flags |= HY_BUILD_CACHE;
    switch (hy_sack_load_system_repo(self->sack, crepo, flags)) {
    case 0:
	Py_RETURN_NONE;
    default:
	PyErr_SetString(PyExc_IOError, "load_system_repo() failed.");
	return NULL;
    }
}

static PyObject *
load_yum_repo(_SackObject *self, PyObject *args, PyObject *kwds)
{
    char *kwlist[] = {"repo", "build_cache", "load_filelists", "load_presto",
		      NULL};

    HyRepo crepo = NULL;
    int build_cache = 0, load_filelists = 0, load_presto = 0;
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "O&|iii", kwlist,
				     repo_converter, &crepo,
				     &build_cache, &load_filelists, &load_presto))
	return 0;

    int flags = 0;
    int ret = 0;
    if (build_cache)
	flags |= HY_BUILD_CACHE;
    if (load_filelists)
	flags |= HY_LOAD_FILELISTS;
    if (load_presto)
	flags |= HY_LOAD_PRESTO;
    if (hy_sack_load_yum_repo(self->sack, crepo, flags))
	ret = hy_get_errno();
    switch (ret) {
    case 0:
	Py_RETURN_NONE;
    case HY_E_IO:
	PyErr_SetString(PyExc_IOError, "Can not read repomd file.");
	return NULL;
    default:
	return PyErr_Format(HyExc_Runtime, "load_yum_repo() failed: %d.", ret);
    }
}

static Py_ssize_t
len(_SackObject *self)
{
    return hy_sack_count(self->sack);
}

static struct
PyMethodDef sack_methods[] = {
    {"_knows",		(PyCFunction)_knows, METH_KEYWORDS|METH_VARARGS,
     NULL},
    {"create_cmdline_repo", (PyCFunction)create_cmdline_repo, METH_NOARGS,
     NULL},
    {"create_package", (PyCFunction)create_package, METH_O,
     NULL},
    {"add_cmdline_package", (PyCFunction)add_cmdline_package, METH_O,
     NULL},
    {"add_excludes", (PyCFunction)add_excludes, METH_O,
     NULL},
    {"disable_repo", (PyCFunction)disable_repo, METH_O,
     NULL},
    {"enable_repo", (PyCFunction)enable_repo, METH_O,
     NULL},
    {"list_arches", (PyCFunction)list_arches, METH_NOARGS,
     NULL},
    {"load_system_repo", (PyCFunction)load_system_repo,
     METH_VARARGS | METH_KEYWORDS, NULL},
    {"load_yum_repo", (PyCFunction)load_yum_repo, METH_VARARGS | METH_KEYWORDS,
     NULL},
    {NULL}                      /* sentinel */
};

PySequenceMethods sack_sequence = {
    (lenfunc)len,		/* sq_length */
};

PyTypeObject sack_Type = {
    PyObject_HEAD_INIT(NULL)
    0,				/*ob_size*/
    "_hawkey.Sack",		/*tp_name*/
    sizeof(_SackObject),	/*tp_basicsize*/
    0,				/*tp_itemsize*/
    (destructor) sack_dealloc,  /*tp_dealloc*/
    0,				/*tp_print*/
    0,				/*tp_getattr*/
    0,				/*tp_setattr*/
    0,				/*tp_compare*/
    0,				/*tp_repr*/
    0,				/*tp_as_number*/
    &sack_sequence,		/*tp_as_sequence*/
    0,				/*tp_as_mapping*/
    0,				/*tp_hash */
    0,				/*tp_call*/
    0,				/*tp_str*/
    0,				/*tp_getattro*/
    0,				/*tp_setattro*/
    0,				/*tp_as_buffer*/
    Py_TPFLAGS_DEFAULT|Py_TPFLAGS_BASETYPE,	/*tp_flags*/
    "Sack object",		/* tp_doc */
    0,				/* tp_traverse */
    0,				/* tp_clear */
    0,				/* tp_richcompare */
    0,				/* tp_weaklistoffset */
    PyObject_SelfIter,		/* tp_iter */
    0,                         	/* tp_iternext */
    sack_methods,		/* tp_methods */
    0,				/* tp_members */
    sack_getsetters,		/* tp_getset */
    0,				/* tp_base */
    0,				/* tp_dict */
    0,				/* tp_descr_get */
    0,				/* tp_descr_set */
    0,				/* tp_dictoffset */
    (initproc)sack_init,	/* tp_init */
    0,				/* tp_alloc */
    sack_new,			/* tp_new */
    0,				/* tp_free */
    0,				/* tp_is_gc */
};
