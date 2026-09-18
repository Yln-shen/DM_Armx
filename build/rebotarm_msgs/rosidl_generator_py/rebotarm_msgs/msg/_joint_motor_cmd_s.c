// generated from rosidl_generator_py/resource/_idl_support.c.em
// with input from rebotarm_msgs:msg/JointMotorCmd.idl
// generated code does not contain a copyright notice
#define NPY_NO_DEPRECATED_API NPY_1_7_API_VERSION
#include <Python.h>
#include <stdbool.h>
#ifndef _WIN32
# pragma GCC diagnostic push
# pragma GCC diagnostic ignored "-Wunused-function"
#endif
#include "numpy/ndarrayobject.h"
#ifndef _WIN32
# pragma GCC diagnostic pop
#endif
#include "rosidl_runtime_c/visibility_control.h"
#include "rebotarm_msgs/msg/detail/joint_motor_cmd__struct.h"
#include "rebotarm_msgs/msg/detail/joint_motor_cmd__functions.h"

ROSIDL_GENERATOR_C_IMPORT
bool builtin_interfaces__msg__time__convert_from_py(PyObject * _pymsg, void * _ros_message);
ROSIDL_GENERATOR_C_IMPORT
PyObject * builtin_interfaces__msg__time__convert_to_py(void * raw_ros_message);

ROSIDL_GENERATOR_C_EXPORT
bool rebotarm_msgs__msg__joint_motor_cmd__convert_from_py(PyObject * _pymsg, void * _ros_message)
{
  // check that the passed message is of the expected Python class
  {
    char full_classname_dest[49];
    {
      char * class_name = NULL;
      char * module_name = NULL;
      {
        PyObject * class_attr = PyObject_GetAttrString(_pymsg, "__class__");
        if (class_attr) {
          PyObject * name_attr = PyObject_GetAttrString(class_attr, "__name__");
          if (name_attr) {
            class_name = (char *)PyUnicode_1BYTE_DATA(name_attr);
            Py_DECREF(name_attr);
          }
          PyObject * module_attr = PyObject_GetAttrString(class_attr, "__module__");
          if (module_attr) {
            module_name = (char *)PyUnicode_1BYTE_DATA(module_attr);
            Py_DECREF(module_attr);
          }
          Py_DECREF(class_attr);
        }
      }
      if (!class_name || !module_name) {
        return false;
      }
      snprintf(full_classname_dest, sizeof(full_classname_dest), "%s.%s", module_name, class_name);
    }
    assert(strncmp("rebotarm_msgs.msg._joint_motor_cmd.JointMotorCmd", full_classname_dest, 48) == 0);
  }
  rebotarm_msgs__msg__JointMotorCmd * ros_message = _ros_message;
  {  // mode
    PyObject * field = PyObject_GetAttrString(_pymsg, "mode");
    if (!field) {
      return false;
    }
    assert(PyLong_Check(field));
    ros_message->mode = (uint8_t)PyLong_AsUnsignedLong(field);
    Py_DECREF(field);
  }
  {  // use_pos
    PyObject * field = PyObject_GetAttrString(_pymsg, "use_pos");
    if (!field) {
      return false;
    }
    assert(PyBool_Check(field));
    ros_message->use_pos = (Py_True == field);
    Py_DECREF(field);
  }
  {  // use_vel
    PyObject * field = PyObject_GetAttrString(_pymsg, "use_vel");
    if (!field) {
      return false;
    }
    assert(PyBool_Check(field));
    ros_message->use_vel = (Py_True == field);
    Py_DECREF(field);
  }
  {  // use_kp
    PyObject * field = PyObject_GetAttrString(_pymsg, "use_kp");
    if (!field) {
      return false;
    }
    assert(PyBool_Check(field));
    ros_message->use_kp = (Py_True == field);
    Py_DECREF(field);
  }
  {  // use_kd
    PyObject * field = PyObject_GetAttrString(_pymsg, "use_kd");
    if (!field) {
      return false;
    }
    assert(PyBool_Check(field));
    ros_message->use_kd = (Py_True == field);
    Py_DECREF(field);
  }
  {  // use_tau
    PyObject * field = PyObject_GetAttrString(_pymsg, "use_tau");
    if (!field) {
      return false;
    }
    assert(PyBool_Check(field));
    ros_message->use_tau = (Py_True == field);
    Py_DECREF(field);
  }
  {  // use_vlim
    PyObject * field = PyObject_GetAttrString(_pymsg, "use_vlim");
    if (!field) {
      return false;
    }
    assert(PyBool_Check(field));
    ros_message->use_vlim = (Py_True == field);
    Py_DECREF(field);
  }
  {  // pos
    PyObject * field = PyObject_GetAttrString(_pymsg, "pos");
    if (!field) {
      return false;
    }
    assert(PyFloat_Check(field));
    ros_message->pos = PyFloat_AS_DOUBLE(field);
    Py_DECREF(field);
  }
  {  // vel
    PyObject * field = PyObject_GetAttrString(_pymsg, "vel");
    if (!field) {
      return false;
    }
    assert(PyFloat_Check(field));
    ros_message->vel = PyFloat_AS_DOUBLE(field);
    Py_DECREF(field);
  }
  {  // kp
    PyObject * field = PyObject_GetAttrString(_pymsg, "kp");
    if (!field) {
      return false;
    }
    assert(PyFloat_Check(field));
    ros_message->kp = PyFloat_AS_DOUBLE(field);
    Py_DECREF(field);
  }
  {  // kd
    PyObject * field = PyObject_GetAttrString(_pymsg, "kd");
    if (!field) {
      return false;
    }
    assert(PyFloat_Check(field));
    ros_message->kd = PyFloat_AS_DOUBLE(field);
    Py_DECREF(field);
  }
  {  // tau
    PyObject * field = PyObject_GetAttrString(_pymsg, "tau");
    if (!field) {
      return false;
    }
    assert(PyFloat_Check(field));
    ros_message->tau = PyFloat_AS_DOUBLE(field);
    Py_DECREF(field);
  }
  {  // vlim
    PyObject * field = PyObject_GetAttrString(_pymsg, "vlim");
    if (!field) {
      return false;
    }
    assert(PyFloat_Check(field));
    ros_message->vlim = PyFloat_AS_DOUBLE(field);
    Py_DECREF(field);
  }
  {  // stamp
    PyObject * field = PyObject_GetAttrString(_pymsg, "stamp");
    if (!field) {
      return false;
    }
    if (!builtin_interfaces__msg__time__convert_from_py(field, &ros_message->stamp)) {
      Py_DECREF(field);
      return false;
    }
    Py_DECREF(field);
  }

  return true;
}

ROSIDL_GENERATOR_C_EXPORT
PyObject * rebotarm_msgs__msg__joint_motor_cmd__convert_to_py(void * raw_ros_message)
{
  /* NOTE(esteve): Call constructor of JointMotorCmd */
  PyObject * _pymessage = NULL;
  {
    PyObject * pymessage_module = PyImport_ImportModule("rebotarm_msgs.msg._joint_motor_cmd");
    assert(pymessage_module);
    PyObject * pymessage_class = PyObject_GetAttrString(pymessage_module, "JointMotorCmd");
    assert(pymessage_class);
    Py_DECREF(pymessage_module);
    _pymessage = PyObject_CallObject(pymessage_class, NULL);
    Py_DECREF(pymessage_class);
    if (!_pymessage) {
      return NULL;
    }
  }
  rebotarm_msgs__msg__JointMotorCmd * ros_message = (rebotarm_msgs__msg__JointMotorCmd *)raw_ros_message;
  {  // mode
    PyObject * field = NULL;
    field = PyLong_FromUnsignedLong(ros_message->mode);
    {
      int rc = PyObject_SetAttrString(_pymessage, "mode", field);
      Py_DECREF(field);
      if (rc) {
        return NULL;
      }
    }
  }
  {  // use_pos
    PyObject * field = NULL;
    field = PyBool_FromLong(ros_message->use_pos ? 1 : 0);
    {
      int rc = PyObject_SetAttrString(_pymessage, "use_pos", field);
      Py_DECREF(field);
      if (rc) {
        return NULL;
      }
    }
  }
  {  // use_vel
    PyObject * field = NULL;
    field = PyBool_FromLong(ros_message->use_vel ? 1 : 0);
    {
      int rc = PyObject_SetAttrString(_pymessage, "use_vel", field);
      Py_DECREF(field);
      if (rc) {
        return NULL;
      }
    }
  }
  {  // use_kp
    PyObject * field = NULL;
    field = PyBool_FromLong(ros_message->use_kp ? 1 : 0);
    {
      int rc = PyObject_SetAttrString(_pymessage, "use_kp", field);
      Py_DECREF(field);
      if (rc) {
        return NULL;
      }
    }
  }
  {  // use_kd
    PyObject * field = NULL;
    field = PyBool_FromLong(ros_message->use_kd ? 1 : 0);
    {
      int rc = PyObject_SetAttrString(_pymessage, "use_kd", field);
      Py_DECREF(field);
      if (rc) {
        return NULL;
      }
    }
  }
  {  // use_tau
    PyObject * field = NULL;
    field = PyBool_FromLong(ros_message->use_tau ? 1 : 0);
    {
      int rc = PyObject_SetAttrString(_pymessage, "use_tau", field);
      Py_DECREF(field);
      if (rc) {
        return NULL;
      }
    }
  }
  {  // use_vlim
    PyObject * field = NULL;
    field = PyBool_FromLong(ros_message->use_vlim ? 1 : 0);
    {
      int rc = PyObject_SetAttrString(_pymessage, "use_vlim", field);
      Py_DECREF(field);
      if (rc) {
        return NULL;
      }
    }
  }
  {  // pos
    PyObject * field = NULL;
    field = PyFloat_FromDouble(ros_message->pos);
    {
      int rc = PyObject_SetAttrString(_pymessage, "pos", field);
      Py_DECREF(field);
      if (rc) {
        return NULL;
      }
    }
  }
  {  // vel
    PyObject * field = NULL;
    field = PyFloat_FromDouble(ros_message->vel);
    {
      int rc = PyObject_SetAttrString(_pymessage, "vel", field);
      Py_DECREF(field);
      if (rc) {
        return NULL;
      }
    }
  }
  {  // kp
    PyObject * field = NULL;
    field = PyFloat_FromDouble(ros_message->kp);
    {
      int rc = PyObject_SetAttrString(_pymessage, "kp", field);
      Py_DECREF(field);
      if (rc) {
        return NULL;
      }
    }
  }
  {  // kd
    PyObject * field = NULL;
    field = PyFloat_FromDouble(ros_message->kd);
    {
      int rc = PyObject_SetAttrString(_pymessage, "kd", field);
      Py_DECREF(field);
      if (rc) {
        return NULL;
      }
    }
  }
  {  // tau
    PyObject * field = NULL;
    field = PyFloat_FromDouble(ros_message->tau);
    {
      int rc = PyObject_SetAttrString(_pymessage, "tau", field);
      Py_DECREF(field);
      if (rc) {
        return NULL;
      }
    }
  }
  {  // vlim
    PyObject * field = NULL;
    field = PyFloat_FromDouble(ros_message->vlim);
    {
      int rc = PyObject_SetAttrString(_pymessage, "vlim", field);
      Py_DECREF(field);
      if (rc) {
        return NULL;
      }
    }
  }
  {  // stamp
    PyObject * field = NULL;
    field = builtin_interfaces__msg__time__convert_to_py(&ros_message->stamp);
    if (!field) {
      return NULL;
    }
    {
      int rc = PyObject_SetAttrString(_pymessage, "stamp", field);
      Py_DECREF(field);
      if (rc) {
        return NULL;
      }
    }
  }

  // ownership of _pymessage is transferred to the caller
  return _pymessage;
}
