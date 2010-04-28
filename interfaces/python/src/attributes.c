/* vim:set ts=2 sw=2 sts=2 et: */
/* 
   IGraph library.
   Copyright (C) 2006  Gabor Csardi <csardi@rmki.kfki.hu>
   MTA RMKI, Konkoly-Thege Miklos st. 29-33, Budapest 1121, Hungary
   
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.
   
   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
   
   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc.,  51 Franklin Street, Fifth Floor, Boston, MA 
   02110-1301 USA

*/

#include <Python.h>
#include "attributes.h"
#include "common.h"
#include "convert.h"

int igraphmodule_i_attribute_struct_init(igraphmodule_i_attribute_struct *attrs) {
  int i;
  for (i=0; i<3; i++) {
    attrs->attrs[i] = PyDict_New();
    if (PyErr_Occurred())
      return 1;
    RC_ALLOC("dict", attrs->attrs[i]);
  }
  attrs->vertex_name_index = 0;
  return 0;
}

void igraphmodule_i_attribute_struct_destroy(igraphmodule_i_attribute_struct *attrs) {
  int i;
  for (i=0; i<3; i++) {
    if (attrs->attrs[i]) {
      RC_DEALLOC("dict", attrs->attrs[i]);
      Py_DECREF(attrs->attrs[i]);
    }
  }
  if (attrs->vertex_name_index) {
    RC_DEALLOC("dict", attrs->vertex_name_index);
    Py_DECREF(attrs->vertex_name_index);
  }
}

int igraphmodule_i_attribute_struct_index_vertex_names(
    igraphmodule_i_attribute_struct *attrs, igraph_bool_t force) {
  Py_ssize_t n = 0;
  PyObject *name_list, *key, *value;

  if (attrs->vertex_name_index && !force)
    return 0;

  if (attrs->vertex_name_index == 0) {
    attrs->vertex_name_index = PyDict_New();
    if (attrs->vertex_name_index == 0) {
      return 1;
    }
  } else
    PyDict_Clear(attrs->vertex_name_index);

  name_list = PyDict_GetItemString(attrs->attrs[1], "name");
  if (name_list == 0)
    return 0;    /* no name attribute */

  n = PyList_Size(name_list) - 1;
  while (n >= 0) {
    key = PyList_GET_ITEM(name_list, n);    /* we don't own a reference to key */
    value = PyInt_FromLong(n);              /* we do own a reference to value */
    if (value == 0)
      return 1;
    PyDict_SetItem(attrs->vertex_name_index, key, value);
    /* PyDict_SetItem did an INCREF for both the key and a value, therefore we
     * have to drop our reference on value */
    Py_DECREF(value);

    n--;
  }

  return 0;
}

void igraphmodule_i_attribute_struct_invalidate_vertex_name_index(
    igraphmodule_i_attribute_struct *attrs) {
  if (attrs->vertex_name_index == 0)
    return;

  Py_DECREF(attrs->vertex_name_index);
  attrs->vertex_name_index = 0;
}

void igraphmodule_invalidate_vertex_name_index(igraph_t *graph) {
  igraphmodule_i_attribute_struct_invalidate_vertex_name_index(ATTR_STRUCT(graph));
}

int igraphmodule_get_vertex_id_by_name(igraph_t *graph, PyObject* o, long int* vid) {
  igraphmodule_i_attribute_struct* attrs = ATTR_STRUCT(graph);
  PyObject* o_vid;

  if (igraphmodule_i_attribute_struct_index_vertex_names(attrs, 0))
    return 1;

  o_vid = PyDict_GetItem(attrs->vertex_name_index, o);
  if (o_vid == NULL) {
    PyObject* s = PyObject_Repr(o);
    
    if (s) {
      PyErr_Format(PyExc_ValueError, "no such vertex: %s", PyString_AS_STRING(s));
      Py_DECREF(s);
    } else {
      PyErr_Format(PyExc_ValueError, "no such vertex: %p", o);
    }
    return 1;
  }

  if (!PyInt_Check(o_vid)) {
    PyErr_SetString(PyExc_ValueError, "non-numeric vertex ID assigned to vertex name. This is most likely a bug.");
    return 1;
  }

  *vid = PyInt_AsLong(o_vid);

  return 0;
}

/* Attribute handlers for the Python interface */

/* Initialization */ 
static int igraphmodule_i_attribute_init(igraph_t *graph, igraph_vector_ptr_t *attr) {
  igraphmodule_i_attribute_struct* attrs;
  long int i, n;
  
  attrs=(igraphmodule_i_attribute_struct*)calloc(1, sizeof(igraphmodule_i_attribute_struct));
  if (!attrs)
    IGRAPH_ERROR("not enough memory to allocate attribute hashes", IGRAPH_ENOMEM);
  if (igraphmodule_i_attribute_struct_init(attrs)) {
    PyErr_Clear();
    free(attrs);
    IGRAPH_ERROR("not enough memory to allocate attribute hashes", IGRAPH_ENOMEM);
  }
  graph->attr=(void*)attrs;

  /* See if we have graph attributes */
  if (attr) {
    PyObject *dict=attrs->attrs[0], *value;
    char *s;
    n = igraph_vector_ptr_size(attr);
    for (i=0; i<n; i++) {
      igraph_attribute_record_t *attr_rec;
      attr_rec = VECTOR(*attr)[i];
      switch (attr_rec->type) {
      case IGRAPH_ATTRIBUTE_NUMERIC:
        value=PyFloat_FromDouble((double)VECTOR(*(igraph_vector_t*)attr_rec->value)[0]);
        break;
      case IGRAPH_ATTRIBUTE_STRING:
        igraph_strvector_get((igraph_strvector_t*)attr_rec->value, 0, &s);
        if (s == 0) value=PyString_FromString("");
        else value=PyString_FromString(s);
        break;
      default:
        IGRAPH_WARNING("unsupported attribute type (not string and not numeric)");
        value=0;
        break;
      }
      if (value) {
        if (PyDict_SetItemString(dict, attr_rec->name, value)) {
          Py_DECREF(value);
          igraphmodule_i_attribute_struct_destroy(attrs);
          free(graph->attr); graph->attr = 0;
          IGRAPH_ERROR("failed to add attributes to graph attribute hash",
                       IGRAPH_FAILURE);
        }
        Py_DECREF(value);
        value=0;
      }
    }
  }

  return IGRAPH_SUCCESS;
}

/* Destruction */
static void igraphmodule_i_attribute_destroy(igraph_t *graph) {
  igraphmodule_i_attribute_struct* attrs;
 
  /* printf("Destroying attribute table\n"); */
  if (graph->attr) {
    attrs=(igraphmodule_i_attribute_struct*)graph->attr;
    igraphmodule_i_attribute_struct_destroy(attrs);
    free(attrs);
  }
}

/* Copying */
static int igraphmodule_i_attribute_copy(igraph_t *to, const igraph_t *from,
  igraph_bool_t ga, igraph_bool_t va, igraph_bool_t ea) {
  igraphmodule_i_attribute_struct *fromattrs, *toattrs;
  PyObject *key, *value, *newval, *o=NULL;
  igraph_bool_t copy_attrs[3] = { ga, va, ea };
  int i, j;
  Py_ssize_t pos = 0;
 
  if (from->attr) {
    fromattrs=ATTR_STRUCT(from);
    /* what to do with the original value of toattrs? */
    toattrs=(igraphmodule_i_attribute_struct*)calloc(1, sizeof(igraphmodule_i_attribute_struct));
    if (!toattrs)
      IGRAPH_ERROR("not enough memory to allocate attribute hashes", IGRAPH_ENOMEM);
    if (igraphmodule_i_attribute_struct_init(toattrs)) {
      PyErr_Clear();
      free(toattrs);
      IGRAPH_ERROR("not enough memory to allocate attribute hashes", IGRAPH_ENOMEM);
    }
    to->attr=toattrs;

    for (i=0; i<3; i++) {
      if (!copy_attrs[i])
        continue;

      if (!PyDict_Check(fromattrs->attrs[i])) {
        toattrs->attrs[i]=fromattrs->attrs[i];
        Py_XINCREF(fromattrs->attrs[i]);
        continue;
      }
      
      pos = 0;
      while (PyDict_Next(fromattrs->attrs[i], &pos, &key, &value)) {
        /* value is only borrowed, so copy it */
        if (i>0) {
          newval=PyList_New(PyList_GET_SIZE(value));
          for (j=0; j<PyList_GET_SIZE(value); j++) {
            o=PyList_GetItem(value, j);
            Py_INCREF(o);
            PyList_SetItem(newval, j, o);
          }
        } else {
          newval=value;
          Py_INCREF(newval);
        }
        PyDict_SetItem(toattrs->attrs[i], key, newval);
        Py_DECREF(newval); /* compensate for PyDict_SetItem */
      }
    }
  }
  return IGRAPH_SUCCESS;
}

/* Adding vertices */
static int igraphmodule_i_attribute_add_vertices(igraph_t *graph, long int nv, igraph_vector_ptr_t *attr) {
  /* Extend the end of every value in the vertex hash with nv pieces of None */
  PyObject *key, *value, *dict;
  long int i, j, k, l;
  igraph_attribute_record_t *attr_rec;
  igraph_bool_t *added_attrs=0;
  Py_ssize_t pos = 0;

  if (!graph->attr) return IGRAPH_SUCCESS;
  if (nv<=0) return IGRAPH_SUCCESS;

  if (attr) {
    added_attrs = (igraph_bool_t*)calloc((size_t)igraph_vector_ptr_size(attr),
                                         sizeof(igraph_bool_t));
    if (!added_attrs)
      IGRAPH_ERROR("can't add vertex attributes", IGRAPH_ENOMEM);
    IGRAPH_FINALLY(free, added_attrs);
  }

  dict=ATTR_STRUCT_DICT(graph)[ATTRHASH_IDX_VERTEX];
  if (!PyDict_Check(dict)) 
    IGRAPH_ERROR("vertex attribute hash type mismatch", IGRAPH_EINVAL);

  while (PyDict_Next(dict, &pos, &key, &value)) {
    if (!PyString_Check(key))
      IGRAPH_ERROR("vertex attribute hash key is not a string", IGRAPH_EINVAL);
    if (!PyList_Check(value))
      IGRAPH_ERROR("vertex attribute hash member is not a list", IGRAPH_EINVAL);
    /* Check if we have specific values for the given attribute */
    attr_rec=0;
    if (attr) {
      j=igraph_vector_ptr_size(attr);
      for (i=0; i<j; i++) {
        attr_rec=VECTOR(*attr)[i];
        if (!strcmp(attr_rec->name, PyString_AS_STRING(key))) {
          added_attrs[i]=1;
          break;
        }
        attr_rec=0;
      }
    }
    /* If we have specific values for the given attribute, attr_rec contains
     * the appropriate vector. If not, it is null. */
    if (attr_rec) {
      for (i=0; i<nv; i++) {
        char *s;
        PyObject *o;
        switch (attr_rec->type) {
        case IGRAPH_ATTRIBUTE_NUMERIC:
          o=PyFloat_FromDouble((double)VECTOR(*(igraph_vector_t*)attr_rec->value)[i]);
          break;
        case IGRAPH_ATTRIBUTE_STRING:
          igraph_strvector_get((igraph_strvector_t*)attr_rec->value, i, &s);
          o=PyString_FromString(s);
          break;
        default:
          IGRAPH_WARNING("unsupported attribute type (not string and not numeric)");
          o=0;
          break;
        }
        if (o) {
          if (PyList_Append(value, o) == -1)
            IGRAPH_ERROR("can't extend a vertex attribute hash member", IGRAPH_FAILURE);
          else Py_DECREF(o);
        }
      }

      /* Invalidate the vertex name index if needed */
      if (!strcmp(attr_rec->name, "name"))
        igraphmodule_i_attribute_struct_invalidate_vertex_name_index(ATTR_STRUCT(graph));
    } else {
      for (i=0; i<nv; i++) {
        if (PyList_Append(value, Py_None) == -1) {
          IGRAPH_ERROR("can't extend a vertex attribute hash member", IGRAPH_FAILURE);
        }
      }
    }
  }

  /* Okay, now we added the new attribute values for the already existing
   * attribute keys. Let's see if we have something left */
  if (attr) {
    l=igraph_vector_ptr_size(attr);
    j=igraph_vcount(graph)-nv;
    /* j contains the number of vertices EXCLUDING the new ones! */
    for (k=0; k<l; k++) {
      if (added_attrs[k]) continue;
      attr_rec=(igraph_attribute_record_t*)VECTOR(*attr)[k];

      value=PyList_New(j + nv);
      if (!value) {
        IGRAPH_ERROR("can't add attributes", IGRAPH_ENOMEM);
      }

      for (i=0; i<j; i++) {
        Py_INCREF(Py_None);
        PyList_SET_ITEM(value, i, Py_None);
      }

      for (i=0; i<nv; i++) {
        char *s;
        PyObject *o;
        switch (attr_rec->type) {
        case IGRAPH_ATTRIBUTE_NUMERIC:
          o=PyFloat_FromDouble((double)VECTOR(*(igraph_vector_t*)attr_rec->value)[i]);
          break;
        case IGRAPH_ATTRIBUTE_STRING:
          igraph_strvector_get((igraph_strvector_t*)attr_rec->value, i, &s);
          o=PyString_FromString(s);
          break;
        default:
          IGRAPH_WARNING("unsupported attribute type (not string and not numeric)");
          o=0;
          break;
        }
        if (o) PyList_SET_ITEM(value, i+j, o);
      }

      /* Invalidate the vertex name index if needed */
      if (!strcmp(attr_rec->name, "name"))
        igraphmodule_i_attribute_struct_invalidate_vertex_name_index(ATTR_STRUCT(graph));

      PyDict_SetItemString(dict, attr_rec->name, value);
      Py_DECREF(value);   /* compensate for PyDict_SetItemString */
    }
    free(added_attrs);
    IGRAPH_FINALLY_CLEAN(1);
  }

  return IGRAPH_SUCCESS;
}

/* Permuting vertices */
static int igraphmodule_i_attribute_permute_vertices(const igraph_t *graph,
    igraph_t *newgraph, const igraph_vector_t *idx) {
  long int n, i;
  PyObject *key, *value, *dict, *newdict, *newlist, *o;
  Py_ssize_t pos=0;
  
  dict=ATTR_STRUCT_DICT(graph)[ATTRHASH_IDX_VERTEX];
  if (!PyDict_Check(dict)) return 1;

  newdict=PyDict_New();
  if (!newdict) return 1;

  n=igraph_vector_size(idx);
  pos=0;

  while (PyDict_Next(dict, &pos, &key, &value)) {
    newlist=PyList_New(n);
    for (i=0; i<n; i++) {
      o=PyList_GetItem(value, VECTOR(*idx)[i]);
      if (!o) {
        PyErr_Clear();
        return 1;
      }
      Py_INCREF(o);
      PyList_SET_ITEM(newlist, i, o);
    }
    PyDict_SetItem(newdict, key, newlist);
    Py_DECREF(newlist);
  }

  dict = ATTR_STRUCT_DICT(newgraph)[ATTRHASH_IDX_VERTEX];
  ATTR_STRUCT_DICT(newgraph)[ATTRHASH_IDX_VERTEX]=newdict;
  Py_DECREF(dict);

  /* Invalidate the vertex name index */
  igraphmodule_i_attribute_struct_invalidate_vertex_name_index(ATTR_STRUCT(newgraph));

  return 0;
}

/* Adding edges */
static int igraphmodule_i_attribute_add_edges(igraph_t *graph, const igraph_vector_t *edges, igraph_vector_ptr_t *attr) {
  /* Extend the end of every value in the edge hash with ne pieces of None */
  PyObject *key, *value, *dict;
  Py_ssize_t pos=0;
  long int i, j, k, l, ne;
  igraph_bool_t *added_attrs=0;
  igraph_attribute_record_t *attr_rec;

  ne=igraph_vector_size(edges)/2;
  if (!graph->attr) return IGRAPH_SUCCESS;
  if (ne<=0) return IGRAPH_SUCCESS;
  
  if (attr) {
    added_attrs = (igraph_bool_t*)calloc((size_t)igraph_vector_ptr_size(attr),
                                         sizeof(igraph_bool_t));
    if (!added_attrs)
      IGRAPH_ERROR("can't add vertex attributes", IGRAPH_ENOMEM);
    IGRAPH_FINALLY(free, added_attrs);
  }

  dict=ATTR_STRUCT_DICT(graph)[ATTRHASH_IDX_EDGE];
  if (!PyDict_Check(dict)) 
    IGRAPH_ERROR("edge attribute hash type mismatch", IGRAPH_EINVAL);
  while (PyDict_Next(dict, &pos, &key, &value)) {
    if (!PyString_Check(key))
      IGRAPH_ERROR("edge attribute hash key is not a string", IGRAPH_EINVAL);
    if (!PyList_Check(value))
      IGRAPH_ERROR("edge attribute hash member is not a list", IGRAPH_EINVAL);

    /* Check if we have specific values for the given attribute */
    attr_rec=0;
    if (attr) {
      j=igraph_vector_ptr_size(attr);
      for (i=0; i<j; i++) {
        attr_rec=VECTOR(*attr)[i];
        if (!strcmp(attr_rec->name, PyString_AS_STRING(key))) {
          added_attrs[i]=1;
          break;
        }
        attr_rec=0;
      }
    }
    /* If we have specific values for the given attribute, attr_rec contains
     * the appropriate vector. If not, it is null. */
    if (attr_rec) {
      for (i=0; i<ne; i++) {
        char *s;
        PyObject *o;
        switch (attr_rec->type) {
        case IGRAPH_ATTRIBUTE_NUMERIC:
          o=PyFloat_FromDouble((double)VECTOR(*(igraph_vector_t*)attr_rec->value)[i]);
          break;
        case IGRAPH_ATTRIBUTE_STRING:
          igraph_strvector_get((igraph_strvector_t*)attr_rec->value, i, &s);
          o=PyString_FromString(s);
          break;
        default:
          IGRAPH_WARNING("unsupported attribute type (not string and not numeric)");
          o=0;
          break;
        }
        if (o) {
          if (PyList_Append(value, o) == -1)
            IGRAPH_ERROR("can't extend an edge attribute hash member", IGRAPH_FAILURE);
          else Py_DECREF(o);
        }
      }
    } else {
      for (i=0; i<ne; i++) {
        if (PyList_Append(value, Py_None) == -1) {
          IGRAPH_ERROR("can't extend an edge attribute hash member", IGRAPH_FAILURE);
        }
      }
    }
  }
  
  /*pos=0;
  while (PyDict_Next(dict, &pos, &key, &value)) {
    printf("key: "); PyObject_Print(key, stdout, Py_PRINT_RAW); printf("\n");
    printf("value: "); PyObject_Print(value, stdout, Py_PRINT_RAW); printf("\n");
  }*/
  
  /* Okay, now we added the new attribute values for the already existing
   * attribute keys. Let's see if we have something left */
  if (attr) {
    l=igraph_vector_ptr_size(attr);
    j=igraph_ecount(graph)-ne;
    /* j contains the number of edges EXCLUDING the new ones! */
    for (k=0; k<l; k++) {
      if (added_attrs[k]) continue;
      attr_rec=(igraph_attribute_record_t*)VECTOR(*attr)[k];

      value=PyList_New(j+ne);
      if (!value) {
        IGRAPH_ERROR("can't add attributes", IGRAPH_ENOMEM);
      }

      for (i=0; i<j; i++) {
        Py_INCREF(Py_None);
        PyList_SET_ITEM(value, i, Py_None);
      }

      for (i=0; i<ne; i++) {
        char *s;
        PyObject *o;
        switch (attr_rec->type) {
        case IGRAPH_ATTRIBUTE_NUMERIC:
          o=PyFloat_FromDouble((double)VECTOR(*(igraph_vector_t*)attr_rec->value)[i]);
          break;
        case IGRAPH_ATTRIBUTE_STRING:
          igraph_strvector_get((igraph_strvector_t*)attr_rec->value, i, &s);
          o=PyString_FromString(s);
          break;
        default:
          IGRAPH_WARNING("unsupported attribute type (not string and not numeric)");
          o=0;
          break;
        }
        if (o) PyList_SET_ITEM(value, i+j, o);
      }

      PyDict_SetItemString(dict, attr_rec->name, value);
      Py_DECREF(value);   /* compensate for PyDict_SetItemString */
    }
    free(added_attrs);
    IGRAPH_FINALLY_CLEAN(1);
  }

  return IGRAPH_SUCCESS;
}

/* Deleting edges, currently unused */
/*
static void igraphmodule_i_attribute_delete_edges(igraph_t *graph, const igraph_vector_t *idx) {
  long int n, i, ndeleted=0;
  PyObject *key, *value, *dict, *o;
  Py_ssize_t pos=0;
  
  dict=ATTR_STRUCT_DICT(graph)[ATTRHASH_IDX_EDGE];
  if (!PyDict_Check(dict)) return;

  n=igraph_vector_size(idx);
  for (i=0; i<n; i++) {
    if (!VECTOR(*idx)[i]) {
      ndeleted++;
      continue;
    }

    pos=0;
    while (PyDict_Next(dict, &pos, &key, &value)) {
      o=PyList_GetItem(value, i);
      if (!o) {
        PyErr_Clear();
        return;
      }
      Py_INCREF(o);
      PyList_SetItem(value, VECTOR(*idx)[i]-1, o);
    }
  }
  
  pos=0;
  while (PyDict_Next(dict, &pos, &key, &value)) {
    n=PySequence_Size(value);
    if (PySequence_DelSlice(value, n-ndeleted, n) == -1) return;
  }
  
  return;
}
*/

/* Permuting edges */
static int igraphmodule_i_attribute_permute_edges(const igraph_t *graph,
    igraph_t *newgraph, const igraph_vector_t *idx) { 
  long int n, i;
  PyObject *key, *value, *dict, *newdict, *newlist, *o;
  Py_ssize_t pos=0;

  dict=ATTR_STRUCT_DICT(graph)[ATTRHASH_IDX_EDGE];
  if (!PyDict_Check(dict)) return 1;

  newdict=PyDict_New();
  if (!newdict) return 1;

  n=igraph_vector_size(idx);
  pos=0;

  while (PyDict_Next(dict, &pos, &key, &value)) {
    newlist=PyList_New(n);
    for (i=0; i<n; i++) {
      o=PyList_GetItem(value, VECTOR(*idx)[i]);
      if (!o) {
        PyErr_Clear();
        return 1;
      }
      Py_INCREF(o);
      PyList_SET_ITEM(newlist, i, o);
    }
    PyDict_SetItem(newdict, key, newlist);
    Py_DECREF(newlist);
  }

  dict = ATTR_STRUCT_DICT(newgraph)[ATTRHASH_IDX_EDGE];
  ATTR_STRUCT_DICT(newgraph)[ATTRHASH_IDX_EDGE]=newdict;
  Py_DECREF(dict);

  return 0;
}

/* Getting attribute names and types */
static int igraphmodule_i_attribute_get_info(const igraph_t *graph,
					     igraph_strvector_t *gnames,
					     igraph_vector_t *gtypes,
					     igraph_strvector_t *vnames,
					     igraph_vector_t *vtypes,
					     igraph_strvector_t *enames,
					     igraph_vector_t *etypes) {
  igraph_strvector_t *names[3] = { gnames, vnames, enames };
  igraph_vector_t *types[3] = { gtypes, vtypes, etypes };
  long int i, j, k, l, m;
  
  for (i=0; i<3; i++) {
    igraph_strvector_t *n = names[i];
    igraph_vector_t *t = types[i];
    PyObject *dict = ATTR_STRUCT_DICT(graph)[i];
    PyObject *keys;
    PyObject *values;
    PyObject *o=0;
    keys=PyDict_Keys(dict);
    if (!keys) IGRAPH_ERROR("Internal error in PyDict_Keys", IGRAPH_FAILURE);
 
    if (n) {
      j=igraphmodule_PyList_to_strvector_t(keys, n);
      if (j) return j;
    }
    if (t) {
      k=PyList_Size(keys);
      igraph_vector_init(t, k);
      for (j=0; j<k; j++) {
        int is_numeric = 1;
        int is_string = 1;
        values=PyDict_GetItem(dict, PyList_GetItem(keys, j));
        if (PyList_Check(values)) {
          m=PyList_Size(values);
          for (l=0; l<m && is_numeric; l++) {
            o=PyList_GetItem(values, l);
            if (o != Py_None && !PyNumber_Check(o))
              is_numeric=0;
          }
          for (l=0; l<m && is_string; l++) {
            o=PyList_GetItem(values, l);
            if (o != Py_None && !PyString_Check(o) && !PyUnicode_Check(o))
              is_string=0;
          }
        } else {
          if (values != Py_None && !PyNumber_Check(values))
            is_numeric=0;
          if (values != Py_None && !PyString_Check(values) && !PyUnicode_Check(values))
            is_string=0;
        }
        if (is_numeric)
          VECTOR(*t)[j] = IGRAPH_ATTRIBUTE_NUMERIC;
        else if (is_string)
          VECTOR(*t)[j] = IGRAPH_ATTRIBUTE_STRING;
        else
          VECTOR(*t)[j] = IGRAPH_ATTRIBUTE_PY_OBJECT;
      }
    }
    
    Py_DECREF(keys);
  }
 
  return 0;
}

/* Checks whether the graph has a graph/vertex/edge attribute with the given name */
igraph_bool_t igraphmodule_i_attribute_has_attr(const igraph_t *graph,
						igraph_attribute_elemtype_t type,
						const char* name) {
  long int attrnum;
  PyObject *o, *dict;
  switch (type) {
  case IGRAPH_ATTRIBUTE_GRAPH:  attrnum=ATTRHASH_IDX_GRAPH;  break;
  case IGRAPH_ATTRIBUTE_VERTEX: attrnum=ATTRHASH_IDX_VERTEX; break;
  case IGRAPH_ATTRIBUTE_EDGE:   attrnum=ATTRHASH_IDX_EDGE;   break;
  default: return 0; break;
  }
  dict = ATTR_STRUCT_DICT(graph)[attrnum];
  o = PyDict_GetItemString(dict, name);
  return o != 0;
}

/* Returns the type of a given attribute */
int igraphmodule_i_attribute_get_type(const igraph_t *graph,
				      igraph_attribute_type_t *type,
				      igraph_attribute_elemtype_t elemtype,
				      const char *name) {
  long int attrnum, i, j;
  int is_numeric, is_string;
  PyObject *o, *dict;
  switch (elemtype) {
  case IGRAPH_ATTRIBUTE_GRAPH:  attrnum=ATTRHASH_IDX_GRAPH;  break;
  case IGRAPH_ATTRIBUTE_VERTEX: attrnum=ATTRHASH_IDX_VERTEX; break;
  case IGRAPH_ATTRIBUTE_EDGE:   attrnum=ATTRHASH_IDX_EDGE;   break;
  default: IGRAPH_ERROR("No such attribute type", IGRAPH_EINVAL); break;
  }
  dict = ATTR_STRUCT_DICT(graph)[attrnum];
  o = PyDict_GetItemString(dict, name);
  if (o == 0) IGRAPH_ERROR("No such attribute", IGRAPH_EINVAL);
  is_numeric = is_string = 1;
  if (attrnum>0) {
    if (!PyList_Check(o)) IGRAPH_ERROR("attribute hash type mismatch", IGRAPH_EINVAL);
    if (!PyList_Size(o))  IGRAPH_ERROR("attribute hash type mismatch", IGRAPH_EINVAL);
    j = PyList_Size(o);
    for (i=0; i<j && is_numeric; i++) {
      PyObject *item = PyList_GET_ITEM(o, i);
      if (item != Py_None && !PyNumber_Check(item)) is_numeric=0;
    }
    for (i=0; i<j && is_string; i++) {
      PyObject *item = PyList_GET_ITEM(o, i);
      if (item != Py_None && !PyString_Check(item) && !PyUnicode_Check(item))
        is_string=0;
    }
  } else {
    if (o != Py_None && !PyNumber_Check(o)) is_numeric=0;
    if (o != Py_None && !PyString_Check(o) && !PyUnicode_Check(o))
      is_string=0;
  }
  if (is_numeric)
    *type = IGRAPH_ATTRIBUTE_NUMERIC;
  else if (is_string)
    *type = IGRAPH_ATTRIBUTE_STRING;
  else
    *type = IGRAPH_ATTRIBUTE_PY_OBJECT;
  return 0;
}

/* Getting numeric graph attributes */
int igraphmodule_i_get_numeric_graph_attr(const igraph_t *graph,
					  const char *name, igraph_vector_t *value) {
  PyObject *dict, *o, *result;
  dict = ATTR_STRUCT_DICT(graph)[ATTRHASH_IDX_GRAPH];
  /* No error checking, if we get here, the type has already been checked by previous
     attribute handler calls... hopefully :) Same applies for the other handlers. */
  o = PyDict_GetItemString(dict, name);
  if (!o) IGRAPH_ERROR("No such attribute", IGRAPH_EINVAL);
  IGRAPH_CHECK(igraph_vector_resize(value, 1));
  if (o == Py_None) {
    VECTOR(*value)[0] = IGRAPH_NAN;
    return 0;
  }
  result = PyNumber_Float(o);
  if (result) {
    VECTOR(*value)[0] = PyFloat_AsDouble(o);
    Py_DECREF(result);
  } else IGRAPH_ERROR("Internal error in PyFloat_AsDouble", IGRAPH_EINVAL); 

  return 0;
}

/* Getting string graph attributes */
int igraphmodule_i_get_string_graph_attr(const igraph_t *graph,
					 const char *name, igraph_strvector_t *value) {
  PyObject *dict, *o, *result;
  dict = ATTR_STRUCT_DICT(graph)[ATTRHASH_IDX_GRAPH];
  o = PyDict_GetItemString(dict, name);
  if (!o) IGRAPH_ERROR("No such attribute", IGRAPH_EINVAL);
  IGRAPH_CHECK(igraph_strvector_resize(value, 1));
  if (PyUnicode_Check(o)) {
    result = PyUnicode_AsEncodedString(o, "utf-8", "xmlcharrefreplace");
  } else {
    result = PyObject_Str(o);
  }
  if (result) {
    IGRAPH_CHECK(igraph_strvector_set(value, 0, PyString_AsString(result)));
    Py_DECREF(result);
  } else IGRAPH_ERROR("Internal error in PyObject_Str", IGRAPH_EINVAL); 

  return 0;
}

/* Getting numeric vertex attributes */
int igraphmodule_i_get_numeric_vertex_attr(const igraph_t *graph,
					   const char *name,
					   igraph_vs_t vs,
					   igraph_vector_t *value) {
  PyObject *dict, *list, *result, *o;
  igraph_vector_t newvalue;

  dict = ATTR_STRUCT_DICT(graph)[ATTRHASH_IDX_VERTEX];
  list = PyDict_GetItemString(dict, name);
  if (!list) IGRAPH_ERROR("No such attribute", IGRAPH_EINVAL);

  if (igraph_vs_is_all(&vs)) {
    if (igraphmodule_PyObject_float_to_vector_t(list, &newvalue))
      IGRAPH_ERROR("Internal error", IGRAPH_EINVAL);
    igraph_vector_update(value, &newvalue);
    igraph_vector_destroy(&newvalue);
  } else {
    igraph_vit_t it;
    long int i=0;
    IGRAPH_CHECK(igraph_vit_create(graph, vs, &it));
    IGRAPH_FINALLY(igraph_vit_destroy, &it);
    IGRAPH_CHECK(igraph_vector_resize(value, IGRAPH_VIT_SIZE(it)));
    while (!IGRAPH_VIT_END(it)) {
      long int v=IGRAPH_VIT_GET(it);
      o = PyList_GetItem(list, v);
      if (o != Py_None) {
        result = PyNumber_Float(o);
        VECTOR(*value)[i] = PyFloat_AsDouble(result);
        Py_XDECREF(result);
      } else VECTOR(*value)[i] = IGRAPH_NAN;
      IGRAPH_VIT_NEXT(it);
      i++;
    }
    igraph_vit_destroy(&it);
    IGRAPH_FINALLY_CLEAN(1);
  }

  return 0;
}

/* Getting string vertex attributes */
int igraphmodule_i_get_string_vertex_attr(const igraph_t *graph,
					  const char *name,
					  igraph_vs_t vs,
					  igraph_strvector_t *value) {
  PyObject *dict, *list, *result;
  igraph_strvector_t newvalue;

  dict = ATTR_STRUCT_DICT(graph)[ATTRHASH_IDX_VERTEX];
  list = PyDict_GetItemString(dict, name);
  if (!list) IGRAPH_ERROR("No such attribute", IGRAPH_EINVAL);

  if (igraph_vs_is_all(&vs)) {
    if (igraphmodule_PyList_to_strvector_t(list, &newvalue))
      IGRAPH_ERROR("Internal error", IGRAPH_EINVAL);
    igraph_strvector_destroy(value);
    *value=newvalue;
  } else {
    igraph_vit_t it;
    long int i=0;
    IGRAPH_CHECK(igraph_vit_create(graph, vs, &it));
    IGRAPH_FINALLY(igraph_vit_destroy, &it);
    IGRAPH_CHECK(igraph_strvector_resize(value, IGRAPH_VIT_SIZE(it)));
    while (!IGRAPH_VIT_END(it)) {
      long int v=IGRAPH_VIT_GET(it);
      result = PyList_GetItem(list, v);
      if (PyUnicode_Check(result)) {
        result = PyUnicode_AsEncodedString(result, "utf-8", "xmlcharrefreplace");
      } else {
        result = PyObject_Str(result);
      }
      if (result == 0) {
        IGRAPH_ERROR("Internal error in PyObject_Str", IGRAPH_EINVAL);
      }
      igraph_strvector_set(value, i, PyString_AsString(result));
      Py_XDECREF(result);
      IGRAPH_VIT_NEXT(it);
      i++;
    }
    igraph_vit_destroy(&it);
    IGRAPH_FINALLY_CLEAN(1);
  }

  return 0;
}

/* Getting numeric edge attributes */
int igraphmodule_i_get_numeric_edge_attr(const igraph_t *graph,
					 const char *name,
					 igraph_es_t es,
					 igraph_vector_t *value) {
  PyObject *dict, *list, *result, *o;
  igraph_vector_t newvalue;

  dict = ATTR_STRUCT_DICT(graph)[ATTRHASH_IDX_EDGE];
  list = PyDict_GetItemString(dict, name);
  if (!list) IGRAPH_ERROR("No such attribute", IGRAPH_EINVAL);

  if (igraph_es_is_all(&es)) {
    if (igraphmodule_PyObject_float_to_vector_t(list, &newvalue))
      IGRAPH_ERROR("Internal error", IGRAPH_EINVAL);
    igraph_vector_update(value, &newvalue);
    igraph_vector_destroy(&newvalue);
  } else {
    igraph_eit_t it;
    long int i=0;
    IGRAPH_CHECK(igraph_eit_create(graph, es, &it));
    IGRAPH_FINALLY(igraph_eit_destroy, &it);
    IGRAPH_CHECK(igraph_vector_resize(value, IGRAPH_EIT_SIZE(it)));
    while (!IGRAPH_EIT_END(it)) {
      long int v=IGRAPH_EIT_GET(it);
      o = PyList_GetItem(list, v);
      if (o != Py_None) {
        result = PyNumber_Float(o);
        VECTOR(*value)[i] = PyFloat_AsDouble(result);
        Py_XDECREF(result);
      } else VECTOR(*value)[i] = IGRAPH_NAN;
      IGRAPH_EIT_NEXT(it);
      i++;
    }
    igraph_eit_destroy(&it);
    IGRAPH_FINALLY_CLEAN(1);
  }

  return 0;
}

/* Getting string edge attributes */
int igraphmodule_i_get_string_edge_attr(const igraph_t *graph,
					const char *name,
					igraph_es_t es,
					igraph_strvector_t *value) {
  PyObject *dict, *list, *result;
  igraph_strvector_t newvalue;

  dict = ATTR_STRUCT_DICT(graph)[ATTRHASH_IDX_EDGE];
  list = PyDict_GetItemString(dict, name);
  if (!list) IGRAPH_ERROR("No such attribute", IGRAPH_EINVAL);

  if (igraph_es_is_all(&es)) {
    if (igraphmodule_PyList_to_strvector_t(list, &newvalue))
      IGRAPH_ERROR("Internal error", IGRAPH_EINVAL);
    igraph_strvector_destroy(value);
    *value=newvalue;
  } else {
    igraph_eit_t it;
    long int i=0;
    IGRAPH_CHECK(igraph_eit_create(graph, es, &it));
    IGRAPH_FINALLY(igraph_eit_destroy, &it);
    IGRAPH_CHECK(igraph_strvector_resize(value, IGRAPH_EIT_SIZE(it)));
    while (!IGRAPH_EIT_END(it)) {
      long int v=IGRAPH_EIT_GET(it);
      result = PyList_GetItem(list, v);
      if (PyUnicode_Check(result)) {
        result = PyUnicode_AsEncodedString(result, "utf-8", "xmlcharrefreplace");
      } else {
        result = PyObject_Str(result);
      }
      if (result == 0) {
        IGRAPH_ERROR("Internal error in PyObject_Str", IGRAPH_EINVAL);
      }
      igraph_strvector_set(value, i, PyString_AsString(result));
      Py_XDECREF(result);
      IGRAPH_EIT_NEXT(it);
      i++;
    }
    igraph_eit_destroy(&it);
    IGRAPH_FINALLY_CLEAN(1);
  }

  return 0;
}

static igraph_attribute_table_t igraphmodule_attribute_table = {
  igraphmodule_i_attribute_init,
  igraphmodule_i_attribute_destroy,
  igraphmodule_i_attribute_copy,
  igraphmodule_i_attribute_add_vertices,
  igraphmodule_i_attribute_permute_vertices,
  0,  /* TODO */
  igraphmodule_i_attribute_add_edges,
  igraphmodule_i_attribute_permute_edges,
  0,  /* TODO */
  igraphmodule_i_attribute_get_info,
  igraphmodule_i_attribute_has_attr,
  igraphmodule_i_attribute_get_type,
  igraphmodule_i_get_numeric_graph_attr,
  igraphmodule_i_get_string_graph_attr,
  igraphmodule_i_get_numeric_vertex_attr,
  igraphmodule_i_get_string_vertex_attr,
  igraphmodule_i_get_numeric_edge_attr,
  igraphmodule_i_get_string_edge_attr,
};

void igraphmodule_initialize_attribute_handler(void) {
  igraph_i_set_attribute_table(&igraphmodule_attribute_table);
}
