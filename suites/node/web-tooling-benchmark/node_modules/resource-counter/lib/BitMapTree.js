// @flow
/** @module BitMapTree */

import type { BitMap } from './bitMap.js';

import {
  createBitMap,
  setBit,
  unsetBit,
  allSet,
  allUnset,
  firstUnset,
  isSet
} from './bitMap.js';

type Tree = Tree;

type CallbackAlloc = ({
  counter: number,
  changed: boolean,
  bitMap: BitMap,
  tree: Tree
}) => void;

type CallbackDealloc = ({
  exists: boolean,
  changed: boolean,
  bitMap: BitMap,
  tree: Tree
}) => void;

type CallbackCheck = (boolean | null) => void;

type SnapShot = WeakSet<Tree|BitMap>;

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

  blockSize: number;
  shrink: boolean;
  begin: number;
  depth: number;
  bitMap: BitMap;

  /**
   * Creates a BitMapTree, this is an abstract class.
   * It is not meant to by directly instantiated.
   */
  constructor (
    blockSize: number,
    shrink: boolean,
    begin: number,
    depth: number,
    bitMap?: BitMap
  ) {
    this.blockSize = blockSize;
    this.shrink = shrink;
    this.begin = begin;
    this.depth = depth;
    this.bitMap = bitMap || createBitMap(blockSize);
  }

};

/**
 * Class representing a Leaf of the recursive bitmap tree.
 * This represents the base case of the lazy recursive bitmap tree.
 */
class Leaf extends BitMapTree {

  /**
   * Creates a Leaf
   */
  constructor (
    blockSize: number,
    shrink: boolean,
    begin: number,
    bitMap?: BitMap
  ) {
    super(blockSize, shrink, begin, 0, bitMap);
  }

  /**
   * Allocates a counter and sets the corresponding bit for the bitmap.
   * It will lazily grow parents.
   */
  allocate (
    counter: ?number,
    callback: CallbackAlloc,
    snapshot?: SnapShot
  ): void {
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
      const treeNew = new Node(
        this.blockSize,
        this.shrink,
        this.begin,
        this.depth + 1
      );
      if (snapshot) {
        snapshot.add(treeNew);
        snapshot.add(treeNew.bitMap);
      }
      treeNew.bitMapTrees[0] = this;
      if (allSet(this.bitMap)) {
        setBit(treeNew.bitMap, 0);
      }
      treeNew.allocate(
        counter,
        callback,
        snapshot
      );
    }
  }

  /**
   * Deallocates a counter and unsets the corresponding bit for the bitmap.
   */
  deallocate (
    counter: number,
    callback: CallbackDealloc,
    snapshot?: SnapShot
  ): void {
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
  check (counter: number, callback: CallbackCheck): void {
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

};

/**
 * Class representing a Node of the recursive bitmap tree.
 */
class Node extends BitMapTree {

  bitMapTrees: Array<Tree>;

  /**
   * Creates a Node
   */
  constructor (
    blockSize: number,
    shrink: boolean,
    begin: number,
    depth: number,
    bitMap?: BitMap,
    bitMapTrees?: Array<Tree>
  ) {
    super(blockSize, shrink, begin, depth, bitMap);
    this.bitMapTrees = bitMapTrees || [];
  }

  /**
   * Allocates a counter by allocating the corresponding child.
   * Passes a continuation to the child allocate that will
   * set the current bitmap if the child bitmap is now all set.
   * It will also lazily create the children or parents as necessary.
   */
  allocate (
    counter: ?number,
    callback: CallbackAlloc,
    snapshot?: SnapShot
  ): void {
    let index;
    if (counter == null) {
      index = firstUnset(this.bitMap);
    } else {
      index = Math.floor(
        (counter - this.begin) / (this.blockSize ** this.depth)
      );
    }
    if (index != null && this.bitMapTrees[index]) {
      const index_ = index; // fix the non-null value
      this.bitMapTrees[index].allocate(
        counter,
        ({counter, changed, bitMap: bitMapChild, tree: treeChild}) => {
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
              treeNew = new Node(
                this.blockSize,
                this.shrink,
                this.begin,
                this.depth,
                bitMapNew,
                bitMapTreesNew
              );
              snapshot.add(treeNew);
            }
          }
          callback({
            counter: counter,
            changed: changed,
            bitMap: bitMapNew,
            tree: treeNew
          });
        },
        snapshot
      );
    } else if (index === null || index >= this.blockSize) {
      // grow the tree upwards
      const treeNew = new Node(
        this.blockSize,
        this.shrink,
        this.begin,
        this.depth + 1
      );
      if (snapshot) {
        snapshot.add(treeNew);
        snapshot.add(treeNew.bitMap);
      }
      treeNew.bitMapTrees[0] = this;
      if (allSet(this.bitMap)) {
        setBit(treeNew.bitMap, 0);
      }
      treeNew.allocate(
        counter,
        callback,
        snapshot
      );
    } else {
      // grow the tree downwards
      const beginNew = this.begin + index * (this.blockSize ** this.depth);
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
        treeNew = new Node(
          this.blockSize,
          this.shrink,
          this.begin,
          this.depth,
          this.bitMap,
          bitMapTreesNew
        );
        snapshot.add(treeNew);
      }
      const index_ = index; // fix the non-null value
      treeChild.allocate(
        counter,
        ({counter, changed, bitMap: bitMapChild, tree: treeChild}) => {
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
        },
        snapshot
      );
    }
  }

  /**
   * Deallocates a counter by deallocating the corresponding child.
   * Passes a continuation to the child deallocate that will
   * unset the current bitmap if the child bitmap was previously all set.
   * It can also shrink the tree if the child node is compeletely empty
   * or if the child leaf is completely unset.
   */
  deallocate (
    counter: number,
    callback: CallbackDealloc,
    snapshot?: SnapShot
  ): void {
    const index = Math.floor(
      (counter - this.begin) / (this.blockSize ** this.depth)
    );
    if (this.bitMapTrees[index]) {
      const allSetPrior = allSet(this.bitMapTrees[index].bitMap);
      this.bitMapTrees[index].deallocate(
        counter,
        ({exists, changed, bitMap: bitMapChild, tree: treeChild}) => {
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
                treeNew = new Node(
                  this.blockSize,
                  this.shrink,
                  this.begin,
                  this.depth,
                  bitMapNew,
                  bitMapTreesNew
                );
                snapshot.add(treeNew);
              }
              if (
                this.shrink &&
                (
                  (
                    treeChild instanceof Leaf &&
                    allUnset(bitMapChild, this.blockSize)
                  )
                  ||
                  (
                    treeChild instanceof Node &&
                    Object.keys(treeChild.bitMapTrees).length === 0
                  )
                )
              ) {
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
        },
        snapshot
      );
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
  check (counter: number, callback: CallbackCheck): void {
    const index = Math.floor(
      (counter - this.begin) / (this.blockSize ** this.depth)
    );
    if (this.bitMapTrees[index]) {
      this.bitMapTrees[index].check(counter, (set) => {
        callback(set);
      });
    } else {
      callback(null);
    }
  }

};

export { Leaf, Node };

export type { Tree, CallbackAlloc, CallbackDealloc, CallbackCheck, SnapShot };
