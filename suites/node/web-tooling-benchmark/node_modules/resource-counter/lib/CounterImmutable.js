// @flow
/** @module CounterImmutable */

import type { Tree } from './BitMapTree.js';

import { allocate, deallocate, check } from './counterUtil.js';
import { Leaf } from './BitMapTree.js';

type CounterTransaction = {
  allocate (?number): number|boolean,
  deallocate (number): boolean,
  check (number): boolean,
};

/**
 * Class representing allocatable and deallocatable counters.
 * Counters are allocated in sequential manner, this applies to deallocated counters.
 * Once a counter is deallocated, it will be reused on the next allocation.
 * This is an immutable counter. It will return a new counter on mutation.
 */
class CounterImmutable {

  _begin: number;
  _blockSize: number;
  _shrink: boolean;
  _tree: Tree;

  /**
   * Creates a counter instance.
   * @throws {RangeError} - If blockSize is not a multiple of 32.
   */
  constructor (
    begin: number = 0,
    blockSize: number = 32,
    shrink: boolean = true,
    tree?: Tree
  ) {
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
  allocate (counter: ?number): [boolean|number, CounterImmutable] {
    if (counter != null) {
      if (counter < this._begin) {
        throw new RangeError(
          'counter needs to be greater or equal to the beginning offset'
        );
      }
      counter = counter - this._begin;
    }
    const [counterAssigned, changed, treeNew] = allocate(
      this._tree,
      counter,
      new WeakSet
    );
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
  deallocate (counter: number): [boolean, CounterImmutable] {
    const [changed, treeNew] = deallocate(
      this._tree,
      counter - this._begin,
      new WeakSet
    );
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
  check (counter: number): boolean {
    return check(this._tree, counter - this._begin);
  }

  /**
   * Takes a callback that performs a set of operations.
   * And only returns the new immutable counter at the end of all operations.
   * This is useful if you want to batch up modfications to the counter.
   */
  transaction (callback: (CounterTransaction) => any): CounterImmutable {
    const snapshot = new WeakSet;
    let tree = this._tree;
    let changed = false;
    const counterTransaction = {
      allocate: (counter) => {
        if (counter != null) {
          if (counter < this._begin) {
            throw new RangeError(
              'counter needs to be greater or equal to the beginning offset'
            );
          }
          counter = counter - this._begin;
        }
        const [counterAssigned, changed_, treeNew] = allocate(tree, counter, snapshot);
        changed = changed_;
        tree = treeNew;
        if (counter == null) {
          return counterAssigned + this._begin;
        } else {
          return changed;
        }
      },
      deallocate: (counter) => {
        const [changed_, treeNew] = deallocate(tree, counter - this._begin, snapshot);
        changed = changed_;
        tree = treeNew;
        return changed;
      },
      check: (counter) => {
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

export default CounterImmutable;

export type { CounterTransaction };
