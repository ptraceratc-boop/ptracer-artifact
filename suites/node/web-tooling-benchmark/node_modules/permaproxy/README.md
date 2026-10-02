# PermaProxy

PermaProxy is a proxy pattern for proxying an object mediated through a container.

Suppose you have some internal object exists in a container.
You want to return a reference to this internal object.
But you know that once you return such a reference, that reference may
become invalid, because the container may change its internal reference.
At the same time we cannot just return the container, since we must
return something that behaves (type-wise) like the internal object.
To solve this problem, we create a proxy that looks and acts just like
the internal object.
However it maintains a persistent link that is mediated through the container.
If the container's reference changes, the proxy will point to the updated
reference.
In other words, we have created an abstract reference. Essentially what we
have done is traded pointer referencing for property key name referencing.
Note that there are serious performance considerations to doing this.
Proxies are very slow compared to raw access to the internal object!

For an example usage see:

https://gist.github.com/CMCDragonkai/9db2ca3c5e47f91c894b0690a475c023

Development
-------------

To build this package for release:

```
npm run build
```

It will output multiple targets. One for browsers and two for nodejs. See `rollup.config.js` to see the target specification.

If your bundler is aware of the module field in `package.json`, you'll get the ES6 module directly.

Once you've updated the package run this:

```
npm version <update_type>
npm publish
```
