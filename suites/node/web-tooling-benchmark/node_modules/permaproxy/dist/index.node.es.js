function permaProxy(container, name) {
  return new Proxy({}, {
    getPrototypeOf: _ => {
      return Reflect.getPrototypeOf(container[name]);
    },
    setPrototypeOf: (_, prototype) => {
      return Reflect.setPrototypeOf(container[name], prototype);
    },
    isExtensible: _ => {
      return Reflect.isExtensible(container[name]);
    },
    preventExtensions: _ => {
      return Reflect.preventExtensions(container[name]);
    },
    getOwnPropertyDescriptor: (_, property) => {
      return Reflect.getOwnPropertyDescriptor(container[name], property);
    },
    defineProperty: (_, property, descriptor) => {
      return Reflect.defineProperty(container[name], property, descriptor);
    },
    get: (_, property) => {
      let value = Reflect.get(container[name], property);
      if (typeof value === 'function') {
        value = value.bind(container[name]);
      }
      return value;
    },
    set: (_, property, value) => {
      return Reflect.set(container[name], property, value);
    },
    has: (_, property) => {
      return Reflect.has(container[name], property);
    },
    deleteProperty: (_, property) => {
      return Reflect.delete(container[name], property);
    },
    ownKeys: _ => {
      return Reflect.ownKeys(container[name]);
    },
    apply: (_, that, args) => {
      return Reflect.apply(container[name], that, args);
    },
    construct: (_, args, newTarget) => {
      return Reflect.construct(container[name], args, newTarget);
    }
  });
}

export default permaProxy;
