// @flow
/** @module counterUtil */

import type { Tree, SnapShot } from './BitMapTree.js';

function allocate (
  tree: Tree,
  counter: ?number,
  snapshot?: SnapShot
): [
  number,
  boolean,
  Tree
] {
  let changed;
  let treeNew;
  tree.allocate(
    counter,
    ({counter: counter_, changed: changed_, tree: tree_}) => {
      counter = counter_;
      changed = changed_;
      treeNew = tree_;
    },
    snapshot
  );
  // $FlowFixMe: changed is initialised
  return [counter, changed, treeNew];
}

function deallocate (
  tree: Tree,
  counter: number,
  snapshot?: SnapShot
): [
  boolean,
  Tree
] {
  let changed;
  let treeNew;
  tree.deallocate(
    counter,
    ({changed: changed_, tree: tree_}) => {
      changed = changed_;
      treeNew = tree_;
    },
    snapshot
  );
  // $FlowFixMe: changed is initialised
  return [changed, treeNew];
}

function check (tree: Tree, counter: number): boolean {
  let set;
  tree.check(
    counter,
    (set_) => {
      set = set_;
    }
  );
  return !!set;
}

export { allocate, deallocate, check };
