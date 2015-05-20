/*
 * Copyright 2015 Byhiras (Europe) Limited
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "Python.h"

#include "filereader.h"
#include "filewriter.h"

static PyObject *
create_types_func(PyObject *self, PyObject *args)
{
    int rval;
    avro_schema_t schema;
    PyObject *schema_json;
    ConvertInfo info;

    if (!PyArg_ParseTuple(args, "O", &schema_json)) {
        return NULL;
    }

    rval = avro_schema_from_json(PyString_AsString(schema_json), 0, &schema, NULL);

    if (rval != 0 || schema == NULL) {
        PyErr_Format(PyExc_IOError, "Error reading schema: %s", avro_strerror());
        return NULL;
    }

    info.types = PyObject_CallFunctionObjArgs((PyObject *)get_avro_types_type(), NULL);
    if (info.types == NULL) {
        return -1;
    }

    declare_types(&info, schema);

    return info.types;
}

/* FIXME: move to a serializer object */
static PyObject *
serialize_func(PyObject *self, PyObject *args)
{
    int rval;
    char *datum_buffer;
    size_t datum_buffer_size;

    avro_schema_t schema;
    avro_value_t value;
    avro_value_iface_t *iface;
    avro_writer_t datum_writer;

    PyObject *pyvalue;
    PyObject *schema_json;
    PyObject *serialized;

    if (!PyArg_ParseTuple(args, "OO", &pyvalue, &schema_json)) {
        return NULL;
    }
    rval = avro_schema_from_json(PyString_AsString(schema_json),
                                 0, &schema, NULL);
    if (rval != 0 || schema == NULL) {
        PyErr_Format(PyExc_IOError, "Error reading schema: %s",
                     avro_strerror());
        return NULL;
    }
    iface = avro_generic_class_from_schema(schema);
    avro_generic_value_new(iface, &value);
    rval = python_to_avro(NULL, pyvalue, &value);
    if (!rval) {
        /* FIXME: use auto-incrementing buffer */
        datum_buffer_size = 128 * 1024;
        datum_buffer = (char *) avro_malloc(datum_buffer_size);
        if (!datum_buffer) {
            return PyErr_NoMemory();
        }
        datum_writer = avro_writer_memory(datum_buffer, datum_buffer_size);
        if (!datum_writer) {
            avro_free(datum_buffer, datum_buffer_size);
            return PyErr_NoMemory();
        }
        rval = avro_value_write(datum_writer, &value);
    }
    if (rval) {
        avro_value_decref(&value);
        avro_writer_free(datum_writer);
        avro_free(datum_buffer, datum_buffer_size);
        PyErr_SetString(PyExc_RuntimeError, "Serialization error");
        return NULL;
    }
    serialized = Py_BuildValue("s#", datum_buffer,
                               avro_writer_tell(datum_writer));
    avro_value_decref(&value);
    avro_writer_free(datum_writer);
    avro_free(datum_buffer, datum_buffer_size);
    return serialized;
}

static PyMethodDef mod_methods[] = {
    {"create_types", (PyCFunction)create_types_func, METH_VARARGS,
     "Take a JSON schema and return a structure of Python types."
    },
    {"serialize", (PyCFunction)serialize_func, METH_VARARGS,
     "Serialize a record and return the corresponding byte string."
    },
    {NULL}  /* Sentinel */
};


#ifndef PyMODINIT_FUNC  /* declarations for DLL import/export */
#define PyMODINIT_FUNC void
#endif
PyMODINIT_FUNC
init_pyavroc(void)
{
    PyObject* m;

    avroFileReaderType.tp_new = PyType_GenericNew;
    if (PyType_Ready(&avroFileReaderType) < 0) {
        return;
    }

    avroFileWriterType.tp_new = PyType_GenericNew;
    if (PyType_Ready(&avroFileWriterType) < 0) {
        return;
    }

    m = Py_InitModule3("_pyavroc", mod_methods,
                       "Python wrapper around Avro-C");

    Py_INCREF(&avroFileReaderType);
    PyModule_AddObject(m, "AvroFileReader", (PyObject *)&avroFileReaderType);

    Py_INCREF(&avroFileWriterType);
    PyModule_AddObject(m, "AvroFileWriter", (PyObject *)&avroFileWriterType);

    PyModule_AddObject(m, "AvroTypes", (PyObject*)get_avro_types_type());
}
