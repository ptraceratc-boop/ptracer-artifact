(function (global, factory) {
	typeof exports === 'object' && typeof module !== 'undefined' ? module.exports = factory() :
	typeof define === 'function' && define.amd ? define(factory) :
	(global.PermaProxy = factory());
}(this, (function () { 'use strict';

function permaProxy(container, name) {
  return new Proxy({}, {
    getPrototypeOf: function getPrototypeOf(_) {
      return Reflect.getPrototypeOf(container[name]);
    },
    setPrototypeOf: function setPrototypeOf(_, prototype) {
      return Reflect.setPrototypeOf(container[name], prototype);
    },
    isExtensible: function isExtensible(_) {
      return Reflect.isExtensible(container[name]);
    },
    preventExtensions: function preventExtensions(_) {
      return Reflect.preventExtensions(container[name]);
    },
    getOwnPropertyDescriptor: function getOwnPropertyDescriptor(_, property) {
      return Reflect.getOwnPropertyDescriptor(container[name], property);
    },
    defineProperty: function defineProperty(_, property, descriptor) {
      return Reflect.defineProperty(container[name], property, descriptor);
    },
    get: function get(_, property) {
      var value = Reflect.get(container[name], property);
      if (typeof value === 'function') {
        value = value.bind(container[name]);
      }
      return value;
    },
    set: function set(_, property, value) {
      return Reflect.set(container[name], property, value);
    },
    has: function has(_, property) {
      return Reflect.has(container[name], property);
    },
    deleteProperty: function deleteProperty(_, property) {
      return Reflect.delete(container[name], property);
    },
    ownKeys: function ownKeys(_) {
      return Reflect.ownKeys(container[name]);
    },
    apply: function apply(_, that, args) {
      return Reflect.apply(container[name], that, args);
    },
    construct: function construct(_, args, newTarget) {
      return Reflect.construct(container[name], args, newTarget);
    }
  });
}

return permaProxy;

})));
