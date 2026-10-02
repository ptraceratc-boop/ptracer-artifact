import _slicedToArray from 'babel-runtime/helpers/slicedToArray';
import _Object$keys from 'babel-runtime/core-js/object/keys';
import BitSet from 'bitset';
import _WeakSet from 'babel-runtime/core-js/weak-set';

function allocate(tree, counter, snapshot) {
  let changed;
  let treeNew;
  tree.allocate(counter, ({ counter: counter_, changed: changed_, tree: tree_ }) => {
    counter = counter_;
    changed = changed_;
    treeNew = tree_;
  }, snapshot);
  // $FlowFixMe: changed is initialised
  return [counter, changed, treeNew];
}
/** @module counterUtil */

function deallocate(tree, counter, snapshot) {
  let changed;
  let treeNew;
  tree.deallocate(counter, ({ changed: changed_, tree: tree_ }) => {
    changed = changed_;
    treeNew = tree_;
  }, snapshot);
  // $FlowFixMe: changed is initialised
  return [changed, treeNew];
}

function check(tree, counter) {
  let set;
  tree.check(counter, set_ => {
    set = set_;
  });
  return !!set;
}

/** @module bitMap */

// bitset library uses 32 bits numbers internally
// it preemptively adds an extra number whan it detects it's full
// this is why we use Uint8Array and minus 1 from the blocksize / 8
// in order to get exactly the right size
// because of the functions supplied by the bitset library
// we invert the notions of set and unset where
// set is 0 and unset is 1

/**
 * Creates a new bitmap sized according to the block size
 */
function createBitMap(blockSize) {
  return new BitSet(new Uint8Array(blockSize / 8 - 1)).flip(0, blockSize - 1);
}

/**
  * Set a bit
  */
function setBit(bitMap, i) {
  return bitMap.set(i, 0);
}

/**
  * Unsets a bit
  */
function unsetBit(bitMap, i) {
  return bitMap.set(i, 1);
}

/**
  * Checks if the entire bitmap is set
  */
function allSet(bitMap) {
  return bitMap.isEmpty();
}

/**
  * Checks if the entire bitmap is unset
  */
function allUnset(bitMap, blockSize) {
  return bitMap.cardinality() === blockSize;
}

/**
  * Find first set algorithm
  * If null is returned, all items have been set
  */
function firstUnset(bitMap) {
  let first = bitMap.ntz();
  if (first === Infinity) {
    first = null;
  }
  return first;
}

/**
  * Checks if a bit is set.
  */
function isSet(bitMap, i) {
  return !bitMap.get(i);
}

/** @module BitMapTree */

/**
 * Class representing a lazy recursive fully-persistent bitmap tree.
 * Only the leaf bitmaps correspond to counters.
 * Interior bitmaps index their child bitmaps.
 * If an interior bit is set, that means there's no free bits in the child bitmap.
 * If an interior bit is not set, that means there's at least 1 free bit in the child bitmap.
 * The snapshot parameter for allocate and deallocate controls how the persistence works.
 * If a snapshot is passed in to mutation methods and a mutation occurs either by
 * changing the current node or leaf, or creating a new parent or child, then these
 * will always create new nodes or leafs instead of mutating the current node or leaf.
 * If the node or leaf to be copied is already in a snapshot, then it will not bother copying
 * unnecessarily.
 */
class BitMapTree {

  /**
   * Creates a BitMapTree, this is an abstract class.
   * It is not meant to by directly instantiated.
   */
  constructor(blockSize, shrink, begin, depth, bitMap) {
    this.blockSize = blockSize;
    this.shrink = shrink;
    this.begin = begin;
    this.depth = depth;
    this.bitMap = bitMap || createBitMap(blockSize);
  }

}

/**
 * Class representing a Leaf of the recursive bitmap tree.
 * This represents the base case of the lazy recursive bitmap tree.
 */
class Leaf extends BitMapTree {

  /**
   * Creates a Leaf
   */
  constructor(blockSize, shrink, begin, bitMap) {
    super(blockSize, shrink, begin, 0, bitMap);
  }

  /**
   * Allocates a counter and sets the corresponding bit for the bitmap.
   * It will lazily grow parents.
   */
  allocate(counter, callback, snapshot) {
    let index;
    if (counter == null) {
      index = firstUnset(this.bitMap);
    } else {
      index = counter - this.begin;
    }
    if (index !== null && index < this.blockSize) {
      if (!isSet(this.bitMap, index)) {
        let bitMapNew;
        let treeNew;
        if (!snapshot || snapshot.has(this)) {
          bitMapNew = this.bitMap;
          setBit(bitMapNew, index);
          treeNew = this;
        } else {
          bitMapNew = this.bitMap.clone();
          setBit(bitMapNew, index);
          treeNew = new Leaf(this.blockSize, this.shrink, this.begin, bitMapNew);
          snapshot.add(treeNew);
        }
        callback({
          counter: this.begin + index,
          changed: true,
          bitMap: bitMapNew,
          tree: treeNew
        });
      } else {
        callback({
          counter: this.begin + index,
          changed: false,
          bitMap: this.bitMap,
          tree: this
        });
      }
    } else {
      // grow the tree upwards
      const treeNew = new Node(this.blockSize, this.shrink, this.begin, this.depth + 1);
      if (snapshot) {
        snapshot.add(treeNew);
        snapshot.add(treeNew.bitMap);
      }
      treeNew.bitMapTrees[0] = this;
      if (allSet(this.bitMap)) {
        setBit(treeNew.bitMap, 0);
      }
      treeNew.allocate(counter, callback, snapshot);
    }
  }

  /**
   * Deallocates a counter and unsets the corresponding bit for the bitmap.
   */
  deallocate(counter, callback, snapshot) {
    const index = counter - this.begin;
    if (index >= 0 && index < this.blockSize) {
      if (isSet(this.bitMap, index)) {
        let bitMapNew;
        let treeNew;
        if (!snapshot || snapshot.has(this)) {
          bitMapNew = this.bitMap;
          unsetBit(bitMapNew, index);
          treeNew = this;
        } else {
          bitMapNew = this.bitMap.clone();
          unsetBit(bitMapNew, index);
          treeNew = new Leaf(this.blockSize, this.shrink, this.begin, bitMapNew);
          snapshot.add(treeNew);
        }
        callback({
          exists: true,
          changed: true,
          bitMap: bitMapNew,
          tree: treeNew
        });
      } else {
        callback({
          exists: true,
          changed: false,
          bitMap: this.bitMap,
          tree: this
        });
      }
    } else {
      callback({
        exists: false,
        changed: false,
        bitMap: this.bitMap,
        tree: this
      });
    }
  }

  /**
   * Checks if the counter has been set
   */
  check(counter, callback) {
    const index = counter - this.begin;
    if (index >= 0 && index < this.blockSize) {
      if (isSet(this.bitMap, index)) {
        callback(true);
      } else {
        callback(false);
      }
    } else {
      callback(null);
    }
  }

}

/**
 * Class representing a Node of the recursive bitmap tree.
 */
class Node extends BitMapTree {

  /**
   * Creates a Node
   */
  constructor(blockSize, shrink, begin, depth, bitMap, bitMapTrees) {
    super(blockSize, shrink, begin, depth, bitMap);
    this.bitMapTrees = bitMapTrees || [];
  }

  /**
   * Allocates a counter by allocating the corresponding child.
   * Passes a continuation to the child allocate that will
   * set the current bitmap if the child bitmap is now all set.
   * It will also lazily create the children or parents as necessary.
   */
  allocate(counter, callback, snapshot) {
    let index;
    if (counter == null) {
      index = firstUnset(this.bitMap);
    } else {
      index = Math.floor((counter - this.begin) / Math.pow(this.blockSize, this.depth));
    }
    if (index != null && this.bitMapTrees[index]) {
      const index_ = index; // fix the non-null value
      this.bitMapTrees[index].allocate(counter, ({ counter, changed, bitMap: bitMapChild, tree: treeChild }) => {
        let bitMapNew = this.bitMap;
        let treeNew = this;
        if (changed) {
          if (!snapshot && allSet(bitMapChild)) {
            setBit(bitMapNew, index_);
          } else if (snapshot && snapshot.has(this)) {
            if (allSet(bitMapChild)) {
              if (!snapshot.has(this.bitMap)) {
                bitMapNew = this.bitMap.clone();
                snapshot.add(bitMapNew);
                this.bitMap = bitMapNew;
              }
              setBit(bitMapNew, index_);
            }
            treeNew.bitMapTrees[index_] = treeChild;
          } else if (snapshot) {
            if (allSet(bitMapChild)) {
              bitMapNew = this.bitMap.clone();
              snapshot.add(bitMapNew);
              setBit(bitMapNew, index_);
            }
            const bitMapTreesNew = this.bitMapTrees.slice();
            bitMapTreesNew[index_] = treeChild;
            treeNew = new Node(this.blockSize, this.shrink, this.begin, this.depth, bitMapNew, bitMapTreesNew);
            snapshot.add(treeNew);
          }
        }
        callback({
          counter: counter,
          changed: changed,
          bitMap: bitMapNew,
          tree: treeNew
        });
      }, snapshot);
    } else if (index === null || index >= this.blockSize) {
      // grow the tree upwards
      const treeNew = new Node(this.blockSize, this.shrink, this.begin, this.depth + 1);
      if (snapshot) {
        snapshot.add(treeNew);
        snapshot.add(treeNew.bitMap);
      }
      treeNew.bitMapTrees[0] = this;
      if (allSet(this.bitMap)) {
        setBit(treeNew.bitMap, 0);
      }
      treeNew.allocate(counter, callback, snapshot);
    } else {
      // grow the tree downwards
      const beginNew = this.begin + index * Math.pow(this.blockSize, this.depth);
      const depthNew = this.depth - 1;
      let treeChild;
      if (depthNew === 0) {
        treeChild = new Leaf(this.blockSize, this.shrink, beginNew);
      } else {
        treeChild = new Node(this.blockSize, this.shrink, beginNew, depthNew);
      }
      if (snapshot) {
        snapshot.add(treeChild);
        snapshot.add(treeChild.bitMap);
      }
      let treeNew;
      if (!snapshot || snapshot.has(this)) {
        this.bitMapTrees[index] = treeChild;
        treeNew = this;
      } else {
        const bitMapTreesNew = this.bitMapTrees.slice();
        bitMapTreesNew[index] = treeChild;
        treeNew = new Node(this.blockSize, this.shrink, this.begin, this.depth, this.bitMap, bitMapTreesNew);
        snapshot.add(treeNew);
      }
      const index_ = index; // fix the non-null value
      treeChild.allocate(counter, ({ counter, changed, bitMap: bitMapChild, tree: treeChild }) => {
        let bitMapNew = this.bitMap;
        if (bitMapChild && allSet(bitMapChild)) {
          if (snapshot && !snapshot.has(this.bitMap)) {
            bitMapNew = this.bitMap.clone();
            snapshot.add(bitMapNew);
            treeNew.bitMap = bitMapNew;
          }
          setBit(bitMapNew, index_);
        }
        callback({
          counter: counter,
          changed: changed,
          bitMap: bitMapNew,
          tree: treeNew
        });
      }, snapshot);
    }
  }

  /**
   * Deallocates a counter by deallocating the corresponding child.
   * Passes a continuation to the child deallocate that will
   * unset the current bitmap if the child bitmap was previously all set.
   * It can also shrink the tree if the child node is compeletely empty
   * or if the child leaf is completely unset.
   */
  deallocate(counter, callback, snapshot) {
    const index = Math.floor((counter - this.begin) / Math.pow(this.blockSize, this.depth));
    if (this.bitMapTrees[index]) {
      const allSetPrior = allSet(this.bitMapTrees[index].bitMap);
      this.bitMapTrees[index].deallocate(counter, ({ exists, changed, bitMap: bitMapChild, tree: treeChild }) => {
        let bitMapNew = this.bitMap;
        let treeNew = this;
        if (!exists) {
          callback({
            exists: exists,
            changed: changed,
            bitMap: bitMapNew,
            tree: treeNew
          });
        } else {
          if (changed) {
            if (!snapshot && allSetPrior) {
              unsetBit(bitMapNew, index);
            } else if (snapshot && snapshot.has(this)) {
              if (allSetPrior) {
                if (!snapshot.has(this.bitMap)) {
                  bitMapNew = this.bitMap.clone();
                  snapshot.add(bitMapNew);
                  this.bitMap = bitMapNew;
                }
                unsetBit(bitMapNew, index);
              }
              treeNew.bitMapTrees[index] = treeChild;
            } else if (snapshot) {
              if (allSetPrior) {
                bitMapNew = this.bitMap.clone();
                snapshot.add(bitMapNew);
                unsetBit(bitMapNew, index);
              }
              const bitMapTreesNew = this.bitMapTrees.slice();
              bitMapTreesNew[index] = treeChild;
              treeNew = new Node(this.blockSize, this.shrink, this.begin, this.depth, bitMapNew, bitMapTreesNew);
              snapshot.add(treeNew);
            }
            if (this.shrink && (treeChild instanceof Leaf && allUnset(bitMapChild, this.blockSize) || treeChild instanceof Node && _Object$keys(treeChild.bitMapTrees).length === 0)) {
              delete treeNew.bitMapTrees[index];
            }
          }
          callback({
            exists: true,
            changed: changed,
            bitMap: bitMapNew,
            tree: treeNew
          });
        }
      }, snapshot);
    } else {
      callback({
        exists: false,
        changed: false,
        bitMap: this.bitMap,
        tree: this
      });
    }
  }

  /**
   * Checks if the counter has been set
   */
  check(counter, callback) {
    const index = Math.floor((counter - this.begin) / Math.pow(this.blockSize, this.depth));
    if (this.bitMapTrees[index]) {
      this.bitMapTrees[index].check(counter, set => {
        callback(set);
      });
    } else {
      callback(null);
    }
  }

}

/** @module Counter */

/**
 * Class representing allocatable and deallocatable counters.
 * Counters are allocated in sequential manner, this applies to deallocated counters.
 * Once a counter is deallocated, it will be reused on the next allocation.
 * This is a mutable counter, which doesn't use snapshots.
 */
class Counter {

  /**
   * Creates a counter instance.
   * @throws {RangeError} - If blockSize is not a multiple of 32.
   */
  constructor(begin = 0, blockSize = 32, shrink = true, tree) {
    if (blockSize % 32 !== 0) {
      throw new RangeError('Blocksize for Counter must be a multiple of 32');
    }
    this._begin = begin;
    this._tree = tree || new Leaf(blockSize, shrink, 0);
  }

  /**
   * Allocates a counter sequentially.
   * If a counter is specified, it will allocate it explicitly and return a
   * changed boolean.
   * @throws {RangeError} - If the explicitly allocated counter is out of bounds.
   */
  allocate(counter) {
    if (counter != null) {
      if (counter < this._begin) {
        throw new RangeError('counter needs to be greater or equal to the beginning offset');
      }
      counter = counter - this._begin;
    }

    var _allocate = allocate(this._tree, counter),
        _allocate2 = _slicedToArray(_allocate, 3);

    const counterAssigned = _allocate2[0],
          changed = _allocate2[1],
          treeNew = _allocate2[2];

    this._tree = treeNew;
    if (counter == null) {
      return counterAssigned + this._begin;
    } else {
      return changed;
    }
  }

  /**
   * Deallocates a number, it makes it available for reuse.
   */
  deallocate(counter) {
    var _deallocate = deallocate(this._tree, counter - this._begin),
        _deallocate2 = _slicedToArray(_deallocate, 2);

    const changed = _deallocate2[0],
          treeNew = _deallocate2[1];

    this._tree = treeNew;
    return changed;
  }

  /**
   * Checks if a number has been allocated or not.
   */
  check(counter) {
    return check(this._tree, counter - this._begin);
  }

}

/** @module CounterImmutable */

/**
 * Class representing allocatable and deallocatable counters.
 * Counters are allocated in sequential manner, this applies to deallocated counters.
 * Once a counter is deallocated, it will be reused on the next allocation.
 * This is an immutable counter. It will return a new counter on mutation.
 */
class CounterImmutable {

  /**
   * Creates a counter instance.
   * @throws {RangeError} - If blockSize is not a multiple of 32.
   */
  constructor(begin = 0, blockSize = 32, shrink = true, tree) {
    if (blockSize % 32 !== 0) {
      throw new RangeError('Blocksize for Counter must be a multiple of 32');
    }
    this._begin = begin;
    this._blockSize = blockSize;
    this._shrink = shrink;
    this._tree = tree || new Leaf(blockSize, shrink, 0);
  }

  /**
   * Allocates a counter sequentially.
   * If a counter is specified, it will allocate it explicitly and return a
   * changed boolean.
   * @throws {RangeError} - If the explicitly allocated counter is out of bounds.
   */
  allocate(counter) {
    if (counter != null) {
      if (counter < this._begin) {
        throw new RangeError('counter needs to be greater or equal to the beginning offset');
      }
      counter = counter - this._begin;
    }

    var _allocate = allocate(this._tree, counter, new _WeakSet()),
        _allocate2 = _slicedToArray(_allocate, 3);

    const counterAssigned = _allocate2[0],
          changed = _allocate2[1],
          treeNew = _allocate2[2];

    let counterNew;
    if (changed) {
      counterNew = new CounterImmutable(this._begin, this._blockSize, this._shrink, treeNew);
    } else {
      counterNew = this;
    }
    if (counter == null) {
      return [counterAssigned + this._begin, counterNew];
    } else {
      return [changed, counterNew];
    }
  }

  /**
   * Deallocates a number, it makes it available for reuse.
   */
  deallocate(counter) {
    var _deallocate = deallocate(this._tree, counter - this._begin, new _WeakSet()),
        _deallocate2 = _slicedToArray(_deallocate, 2);

    const changed = _deallocate2[0],
          treeNew = _deallocate2[1];

    let counterNew;
    if (changed) {
      counterNew = new CounterImmutable(this._begin, this._blockSize, this._shrink, treeNew);
    } else {
      counterNew = this;
    }
    return [changed, counterNew];
  }

  /**
   * Checks if a number has been allocated or not.
   */
  check(counter) {
    return check(this._tree, counter - this._begin);
  }

  /**
   * Takes a callback that performs a set of operations.
   * And only returns the new immutable counter at the end of all operations.
   * This is useful if you want to batch up modfications to the counter.
   */
  transaction(callback) {
    const snapshot = new _WeakSet();
    let tree = this._tree;
    let changed = false;
    const counterTransaction = {
      allocate: counter => {
        if (counter != null) {
          if (counter < this._begin) {
            throw new RangeError('counter needs to be greater or equal to the beginning offset');
          }
          counter = counter - this._begin;
        }

        var _allocate4 = allocate(tree, counter, snapshot),
            _allocate5 = _slicedToArray(_allocate4, 3);

        const counterAssigned = _allocate5[0],
              changed_ = _allocate5[1],
              treeNew = _allocate5[2];

        changed = changed_;
        tree = treeNew;
        if (counter == null) {
          return counterAssigned + this._begin;
        } else {
          return changed;
        }
      },
      deallocate: counter => {
        var _deallocate4 = deallocate(tree, counter - this._begin, snapshot),
            _deallocate5 = _slicedToArray(_deallocate4, 2);

        const changed_ = _deallocate5[0],
              treeNew = _deallocate5[1];

        changed = changed_;
        tree = treeNew;
        return changed;
      },
      check: counter => {
        return check(tree, counter - this._begin);
      }
    };
    callback(counterTransaction);
    if (changed) {
      return new CounterImmutable(this._begin, this._blockSize, this._shrink, tree);
    } else {
      return this;
    }
  }

}

export { CounterImmutable };
export default Counter;
