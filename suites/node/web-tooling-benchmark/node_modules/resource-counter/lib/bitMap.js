// @flow
/** @module bitMap */

import BitSet from 'bitset';

type BitMap = BitSet;

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
function createBitMap (blockSize: number): BitMap {
  return new BitSet(new Uint8Array(blockSize / 8 - 1)).flip(0, blockSize - 1);
};

/**
  * Set a bit
  */
function setBit (bitMap: BitMap, i: number): BitMap {
  return bitMap.set(i, 0);
};

/**
  * Unsets a bit
  */
function unsetBit (bitMap: BitMap, i: number): BitMap {
  return bitMap.set(i, 1);
};

/**
  * Checks if the entire bitmap is set
  */
function allSet (bitMap: BitMap): boolean {
  return bitMap.isEmpty();
};

/**
  * Checks if the entire bitmap is unset
  */
function allUnset (bitMap: BitMap, blockSize: number): boolean {
  return bitMap.cardinality() === blockSize;
};

/**
  * Find first set algorithm
  * If null is returned, all items have been set
  */
function firstUnset (bitMap: BitMap): number|null {
  let first = bitMap.ntz();
  if (first === Infinity) {
    first = null;
  }
  return first;
};

/**
  * Checks if a bit is set.
  */
function isSet (bitMap: BitMap, i: number): boolean {
  return !bitMap.get(i);
};

export {
  createBitMap,
  setBit,
  unsetBit,
  allSet,
  allUnset,
  firstUnset,
  isSet
};

export type { BitMap };
