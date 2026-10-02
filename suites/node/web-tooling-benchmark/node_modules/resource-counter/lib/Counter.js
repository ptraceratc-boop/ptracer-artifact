// @flow
/** @module Counter */

import type { Tree } from './BitMapTree.js';

import { allocate, deallocate, check } from './counterUtil.js';
import { Leaf } from './BitMapTree.js';

/**
 * Class representing allocatable and deallocatable counters.
 * Counters are allocated in sequential manner, this applies to deallocated counters.
 * Once a counter is deallocated, it will be reused on the next allocation.
 * This is a mutable counter, which doesn't use snapshots.
 */
class Counter {

  _begin: number;
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
    this._tree = tree || new Leaf(blockSize, shrink, 0);
  }

  /**
   * Allocates a counter sequentially.
   * If a counter is specified, it will allocate it explicitly and return a
   * changed boolean.
   * @throws {RangeError} - If the explicitly allocated counter is out of bounds.
   */
  allocate (counter: ?number): boolean|number {
    if (counter != null) {
      if (counter < this._begin) {
        throw new RangeError(
          'counter needs to be greater or equal to the beginning offset'
        );
      }
      counter = counter - this._begin;
    }
    const [counterAssigned, changed, treeNew] = allocate(this._tree, counter);
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
  deallocate (counter: number): boolean {
    const [changed, treeNew] = deallocate(this._tree, counter - this._begin);
    this._tree = treeNew;
    return changed;
  }

  /**
   * Checks if a number has been allocated or not.
   */
  check (counter: number): boolean {
    return check(this._tree, counter - this._begin);
  }

}

export default Counter;
