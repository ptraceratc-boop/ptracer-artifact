# js-resource-counter

Sequentially Allocatable and Deallocatable Resource Counter written in JavaScript. It is useful for tracking resource usage such as inodes and file descriptors. The resource counter is backed by a new lazy recursive perfectly balanced dynamically growing and shrinking fully-persistent bitmap tree data structure. This allows logarithmic allocation and deallocation performance. It's memory usage is better than the alternative deallocated stack + counter method.

Basic Usage
------------

```sh
npm install --save 'resource-counter';
```

```js
import Counter from 'resource-counter';

let c = new Counter;
let first = c.allocate(); // 1
let second = c.allocate(); // 2
let third = c.allocate(); // 3
let fourth = c.allocate(); // 4
c.deallocate(second); // true
c.deallocate(third); // true
console.log(c.allocate() === second); // true
console.log(c.allocate() === third); // true
console.log(c.allocate() === (fourth + 1)); // true

// you can also explicitly set a specific number
// and all subsequent allocations are still sequential
// explicitly allocating number returns a "changed" boolean
c.allocate(100); // true
c.allocate(100); // false

// you can check whether a number was allocated or not
c.check(100); // false
```

There is also an alternate `CounterImmutable` that gives a fully-persistent counter using structure sharing by path-copying strategy.

```js
import { CounterImmutable } from 'resource-counter';

// c is being reassigned on every modification
// however each intermediate c can be used
// if you do use them, then you will now have diverging cs
let c = new CounterImmutable;
let first, second, third, fourth;
[first, c] = c.allocate();
[second, c] = c.allocate();
[third, c] = c.allocate();
[fourth, c] = c.allocate();
[, c] = c.deallocate(second);
[, c] = c.deallocate(third);
let first_, second_, fifth_;
[first_, c] = c.allocate();
[second_, c] = c.allocate();
[fifth_, c] = c.allocate();
console.log(first_ === second); // true
console.log(second_ === third); // true
console.log(fifth_ === (fourth + 1)); // true

// you can also perform a transaction
c = c.transaction((ct) => {
  const number = ct.allocate();
  ct.allocate();
  ct.deallocate(number);
  console.log(number); // 5
});
```

This can be useful if you need to combine `CounterImmutable` with other fully-persistent data structures to create composite data structures.

Documentation
--------------

Documentation is located in the `doc` folder. You can also view the [rendered HTML](http://cdn.rawgit.com/MatrixAI/js-resource-counter/8a6734c/doc/index.html).

Performance behaviour is lazy memory allocation on counter allocation (for both balanced tree growth and explicit counter allocation). This laziness means intermediate tree nodes won't be allocated when explicitly allocating a counter that has intermediate values. For example allocating only 0 and 500, tree nodes won't be created eagerly in anticipation for counter values 1 to 499.

By default both `Counter` and `CounterImmutable` will lazily grow and shrink the tree as necessary. However shrinking adds extra performance overhead for the benefit of more tighter memory usage. If you expect to always use the same set of numbers for allocation and deallocation, it will be faster if you disable shrinking. You can do this by passing the `shrink` parameter as `false`. See the documentation for more.

Development
------------

To build this package for release:

```
npm run build
```

It will run tests, generate documentation and output multiple targets. One for browsers and one for nodejs. See `rollup.config.js` to see the target specification.

If your bundler is aware of the module field in `package.json`, you'll get the ES6 module directly.

Once you've updated the package run this:

```
npm version <update_type>
npm publish
```
