/*
 * Copyright (C) 2024-2024 Intel Corporation.
 * SPDX-License-Identifier: MIT
 */

#ifndef _GLUE_WRAPPERS_H_
#define _GLUE_WRAPPERS_H_

#include "../gluepy.h"
#include <ostream>

namespace glue
{
class Wrapped_ADDRINT
{
  public:
    // special macro for wrapper class
    PY_CLASS(Wrapped_ADDRINT, builtins, ADDRINT)
    //       ^T              ^module   ^name

    Wrapped_ADDRINT()                       = default;
    Wrapped_ADDRINT(const Wrapped_ADDRINT&) = default;
    Wrapped_ADDRINT(ADDRINT value) { this->value = value; }

    // special method _ returns a pointer of the value
    ADDRINT* _() { return &value; }

    bool operator==(const Wrapped_ADDRINT& other) const { return value == other.value; }

    static void _register(VM* vm, PyObject* mod, PyObject* type)
    {
        // The following will bind also addr() method to get the address of wrapped object (&value)
        PY_STRUCT_LIKE(Wrapped_ADDRINT)

        // __init__ method
        vm->bind(type, "__init__(self, value=None)",
                 [](VM* vm, ArgsView args)
                 {
                     Wrapped_ADDRINT& self = _py_cast< Wrapped_ADDRINT& >(vm, args[0]);
                     if (vm->None != args[1])
                     {
                         self.value = py_cast< ADDRINT >(vm, args[1]);
                     }
                     return vm->None;
                 });

        // Bind 'value' as a property
        vm->bind_property(type, "value",
                          [](VM* vm, ArgsView args)
                          {
                              Wrapped_ADDRINT& self = _py_cast< Wrapped_ADDRINT& >(vm, args[0]);
                              return py_var(vm, self.value);
                          });

        // Bind 'size' as a property
        vm->bind_property(type, "size", [](VM* vm, ArgsView args) { return py_var(vm, sizeof(ADDRINT)); });
    }

  private:
    ADDRINT value = 0;
};

class Wrapped_BUFFER
{
  public:
    struct Buffer
    {
        uint8_t* value = nullptr;
        size_t size    = 0;
    };

    // special macro for wrapper class
    PY_CLASS(Wrapped_BUFFER, builtins, BUFFER)
    //       ^T              ^module   ^name

    Wrapped_BUFFER()                      = default;
    Wrapped_BUFFER(const Wrapped_BUFFER&) = default;
    Wrapped_BUFFER(Buffer value) { this->value = value; }

    ~Wrapped_BUFFER()
    {
        if (isAllocated)
        {
            delete[] value.value;
        }
    }

    bool operator==(const Wrapped_BUFFER& other) const
    {
        return ((value.value == other.value.value) && (value.size == other.value.size));
    }

    //special method _ returns a pointer of the value
    Buffer* _() { return &value; }

    static void _register(VM* vm, PyObject* mod, PyObject* type)
    {
        PY_STRUCT_LIKE(Wrapped_BUFFER)

        // __init__ method
        vm->bind(type, "__init__(self, size, void_p=None)",
                 [](VM* vm, ArgsView args)
                 {
                     Wrapped_BUFFER& self = py_cast< Wrapped_BUFFER& >(vm, args[0]);
                     auto size            = py_cast< size_t >(vm, args[1]);
                     self.value.size      = size;

                     if (vm->None == args[2])
                     {
                         self.value.value = new uint8_t[size];
                         self.isAllocated = true;
                     }
                     else
                     {
                         self.value.value = py_cast< uint8_t* >(vm, args[2]);
                     }

                     return vm->None;
                 });

        // Bind 'value' as a property
        vm->bind_property(type, "value",
                          [](VM* vm, ArgsView args)
                          {
                              Wrapped_BUFFER& self = py_cast< Wrapped_BUFFER& >(vm, args[0]);
                              return py_var(vm, self.value.value);
                          });

        // Bind 'size' as a property
        vm->bind_property(type, "size",
                          [](VM* vm, ArgsView args)
                          {
                              Wrapped_BUFFER& self = py_cast< Wrapped_BUFFER& >(vm, args[0]);
                              return py_var(vm, self.value.size);
                          });

        // Optional function to explicitly delete the buffer without waiting to python garbage collector
        vm->bind(type, "delete(self)",
                 [](VM* vm, ArgsView args)
                 {
                     Wrapped_BUFFER& self = py_cast< Wrapped_BUFFER& >(vm, args[0]);

                     if (self.isAllocated)
                     {
                         delete[] self.value.value;
                         self.value.value = nullptr;
                         self.isAllocated = false;
                     }

                     return vm->None;
                 });

#define BIND_SET_GET(T, name)                                                               \
    vm->bind(type, "get_" name "(self, offset=0)",                                          \
             [](VM* vm, ArgsView args)                                                      \
             {                                                                              \
                 Wrapped_BUFFER& self = py_cast< Wrapped_BUFFER& >(vm, args[0]);            \
                 uint64_t offset      = py_cast< uint64_t >(vm, args[1]);                   \
                 if ((offset + sizeof(T)) > self.value.size)                                \
                 {                                                                          \
                     vm->ValueError("offset + size of var will exceed the size of BUFFER"); \
                 }                                                                          \
                 void* ptr = self.value.value + offset;                                     \
                 return py_var(vm, *(T*)ptr);                                               \
             });                                                                            \
    vm->bind(type, "set_" name "(self, value, offset=0)",                                   \
             [](VM* vm, ArgsView args)                                                      \
             {                                                                              \
                 Wrapped_BUFFER& self = py_cast< Wrapped_BUFFER& >(vm, args[0]);            \
                 uint64_t offset      = py_cast< uint64_t >(vm, args[2]);                   \
                 if ((offset + sizeof(T)) > self.value.size)                                \
                 {                                                                          \
                     vm->ValueError("offset + size of var will exceed the size of BUFFER"); \
                 }                                                                          \
                 void* ptr = self.value.value + offset;                                     \
                 *(T*)ptr  = py_cast< T >(vm, args[1]);                                     \
                 return vm->None;                                                           \
             });

        BIND_SET_GET(int8_t, "int8")
        BIND_SET_GET(uint8_t, "uint8")
        BIND_SET_GET(int16_t, "int16")
        BIND_SET_GET(uint16_t, "uint16")
        BIND_SET_GET(int32_t, "int32")
        BIND_SET_GET(uint32_t, "uint32")
        BIND_SET_GET(int64_t, "int64")
        BIND_SET_GET(uint64_t, "uint64")
#undef BIND_SET_GET
    }

  private:
    Buffer value;
    bool isAllocated = false;
};

class Wrapped_EXCEPTION_INFO
{
  public:
    // special macro for wrapper class
    PY_CLASS(Wrapped_EXCEPTION_INFO, builtins, EXCEPTION_INFO)
    //       ^T                      ^module   ^name

    Wrapped_EXCEPTION_INFO()                              = default;
    Wrapped_EXCEPTION_INFO(const Wrapped_EXCEPTION_INFO&) = default;
    Wrapped_EXCEPTION_INFO(EXCEPTION_INFO value) { this->value = value; }

    // The operator == should not be called, it only here to not get: error: no match for ‘operator==’
    bool operator==(const Wrapped_EXCEPTION_INFO& other) const
    {
        throw std::logic_error("operator== is not implemented for Wrapped_EXCEPTION_INFO");
    }

    // special method _ returns a pointer of the wrapped value
    EXCEPTION_INFO* _() { return &value; }

    static void _register(VM* vm, PyObject* mod, PyObject* type)
    {
        PY_STRUCT_LIKE(Wrapped_EXCEPTION_INFO)
        vm->bind_default_constructor< Wrapped_EXCEPTION_INFO >(type);
    }

  private:
    EXCEPTION_INFO value;
};

class Wrapped_FXSAVE
{
  public:
    // special macro for wrapper class
    PY_CLASS(Wrapped_FXSAVE, builtins, FXSAVE)
    //       ^T                      ^module   ^name

    Wrapped_FXSAVE()                      = default;
    Wrapped_FXSAVE(const Wrapped_FXSAVE&) = default;
    Wrapped_FXSAVE(FXSAVE value) { this->value = value; }

    // The operator == should not be called, it only here to not get: error: no match for ‘operator==’
    bool operator==(const Wrapped_FXSAVE& other) const
    {
        throw std::logic_error("operator== is not implemented for Wrapped_FXSAVE");
    }

    // special method _ returns a pointer of the wrapped value
    FXSAVE* _() { return &value; }

    static void _register(VM* vm, PyObject* mod, PyObject* type)
    {
        PY_STRUCT_LIKE(Wrapped_FXSAVE)
        vm->bind_default_constructor< Wrapped_FXSAVE >(type);
    }

  private:
    FXSAVE value;
};

struct Wrapped_FPSTATE
{
  public:
    // special macro for wrapper class
    PY_CLASS(Wrapped_FPSTATE, builtins, FPSTATE)
    //       ^T                      ^module   ^name

    Wrapped_FPSTATE()                       = default;
    Wrapped_FPSTATE(const Wrapped_FPSTATE&) = default;
    Wrapped_FPSTATE(FPSTATE value) { this->value = value; }

    // The operator == should not be called, it only here to not get: error: no match for ‘operator==’
    bool operator==(const Wrapped_FPSTATE& other) const
    {
        throw std::logic_error("operator== is not implemented for Wrapped_FPSTATE");
    }

    // special method _ returns a pointer of the wrapped value
    FPSTATE* _() { return &value; }

    static void _register(VM* vm, PyObject* mod, PyObject* type)
    {
        PY_STRUCT_LIKE(Wrapped_FPSTATE)
        vm->bind_default_constructor< Wrapped_FPSTATE >(type);
    }

  private:
    FPSTATE value;
};

} // namespace glue

#endif // _GLUE_WRAPPERS_H_
