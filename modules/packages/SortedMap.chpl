/*
 * Copyright 2021-2025 Hewlett Packard Enterprise Development LP
 * Copyright 2004-2019 Cray Inc.
 * Other additional copyright holders may be indicated within.
 *
 * The entirety of this work is licensed under the Apache License,
 * Version 2.0 (the "License"); you may not use this file except
 * in compliance with the License.
 *
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/* Provides the 'sortedMap' type for storing sorted key-value associations.

  sortedMaps are not parallel safe by default, but can be made parallel safe by
  setting the param formal `parSafe` to true in any sortedMap constructor. When
  constructed from another sortedMap, the new sortedMap will inherit
  the parallel safety mode of its originating sortedMap.

  SortedMap supports searching for a certain key, insertion and deletion in O(logN).
*/
module SortedMap {
  import ChapelLocks;
  private use HaltWrappers;
  private use SortedSet;
  private use IO;
  import Sort.{relativeComparator};
  public use Sort only defaultComparator;

  // Lock code lifted from modules/standard/List.chpl.
  @chpldoc.nodoc
  type _lockType = ChapelLocks.chpl_LocalSpinlock;

  @chpldoc.nodoc
  class _LockWrapper {
    var lockVar = new _lockType();

    inline proc lock() {
      lockVar.lock();
    }

    inline proc unlock() {
      lockVar.unlock();
    }
  }

  @chpldoc.nodoc
  proc _checkKeyType(type keyType) {
    if isGenericType(keyType) {
      compilerWarning("creating a sortedMap with key type " +
                      keyType:string);
      if isClassType(keyType) && !isGenericType(keyType:borrowed) {
        compilerWarning("which is a class type with generic management");
      }
      compilerError("sortedMap key type cannot currently be generic");
    }
  }

  @chpldoc.nodoc
  proc _checkValType(type valType) {
    if isGenericType(valType) {
      compilerWarning("creating a sortedMap with value type " +
                      valType:string);
      if isClassType(valType) && !isGenericType(valType:borrowed) {
        compilerWarning("which is a class type with generic management");
      }
      compilerError("sortedMap value type cannot currently be generic");
    }
  }


  /*
    The shared nilable class helps us to find one element with the key
    and without specifying the value
    See `contains`
  */
  @chpldoc.nodoc
  class _valueWrapper {
    var val;
  }

  record sortedMap : writeSerializable {
    /* Type of sortedMap keys. */
    type keyType;
    /* Type of sortedMap values. */
    type valType;

    /* If `true`, this sortedMap will perform parallel safe operations. */
    param parSafe = false;

    /* The comparator used to compare keys */
    var comparator: record = new defaultComparator();

    // TODO: Maybe we want something like record optional for this?
    @chpldoc.nodoc
    type _eltType = (keyType, shared _valueWrapper?);

    /* The underlying implementation */
    @chpldoc.nodoc
    var _set: sortedSet(_eltType, parSafe=false,
                        _keyComparator(comparator.type));

    //TODO: Maybe we should use the lock from the underlying implementation
    @chpldoc.nodoc
    var _lock = if parSafe then new _LockWrapper() else none;

    @chpldoc.nodoc
    inline proc _enter() {
      if parSafe then
        _lock.lock();
    }

    @chpldoc.nodoc
    inline proc _leave() {
      if parSafe then
        _lock.unlock();
    }

    @chpldoc.nodoc
    record _keyComparator: relativeComparator {
      var comparator: record;
      proc compare(a, b) {
        return comparator.compare(a[0], b[0]);
      }
    }

    /*
      Initializes an empty sortedMap containing keys and values of given types.

      :arg keyType: The type of the keys of this sortedMap.
      :arg valType: The type of the values of this sortedMap.
      :arg parSafe: If `true`, this sortedMap will use parallel safe operations.
      :type parSafe: bool
      :arg comparator: The comparator used to compare keys.
    */
    proc init(type keyType, type valType, param parSafe = false,
              comparator: record = new defaultComparator()) {
      _checkKeyType(keyType);
      _checkValType(valType);

      this.keyType = keyType;
      this.valType = valType;
      this.parSafe = parSafe;
      this.comparator = comparator;
      this._eltType = (keyType, shared _valueWrapper(valType)?);

      this._set = new sortedSet(_eltType, false, new _keyComparator(comparator));
    }

    /*
      Initialize this sortedMap with a copy of each of the elements contained in
      the sortedMap `other`. This sortedMap will inherit the `parSafe` value of
      the sortedMap `other`.

      :arg other: An sortedMap to initialize this sortedMap with.
    */
    proc init=(const ref other: sortedMap(?kt, ?vt)) lifetime this < other {
      if !isCopyableType(kt) || !isCopyableType(vt) then
        compilerError("initializing sortedMap with non-copyable type");

      this.keyType = kt;
      this.valType = vt;
      this.parSafe = other.parSafe;
      this.comparator = other.comparator;
      this._eltType = (keyType, shared _valueWrapper(valType)?);

      this._set = other._set;

      init this;
    }

    /*
      Clears the contents of this sortedMap.

      .. warning::

        Clearing the contents of this sortedMap will invalidate all existing
        references to the elements contained in this sortedMap.
    */
    proc ref clear() {
      _enter(); defer _leave();
      _set.clear();
    }

    /*
      The current number of keys contained in this sortedMap.
    */
    inline proc const size {
      _enter(); defer _leave();
      return _size;
    }

    // Return size without acquiring the lock
    @chpldoc.nodoc
    inline proc const _size {
      return _set.size;
    }

    /*
      Returns `true` if this sortedMap contains zero keys.

      :returns: `true` if this sortedMap is empty.
      :rtype: `bool`
    */
    inline proc const isEmpty(): bool {
      return size == 0;
    }

    /*
      Returns `true` if the given key is a member of this sortedMap, and `false`
      otherwise.

      :arg k: The key to test for membership.
      :type k: keyType

      :returns: Whether or not the given key is a member of this sortedMap.
      :rtype: `bool`
    */
    proc const contains(const k: keyType): bool {
      _enter(); defer _leave();
      return _set.contains((k, nil));
    }

    /*
      Updates this sortedMap with the contents of the other, overwriting the values
      for already-existing keys.

      :arg other: The other sortedMap
    */
    proc ref update(other: sortedMap(keyType, valType, ?p)) {
      _enter(); defer _leave();

      if !isCopyableType(keyType) || !isCopyableType(valType) then
        compilerError("updating map with non-copyable type");

      for key in other.keys() {
        _set.remove((key, nil));
        _set.add((key, new shared _valueWrapper(other.getValue(key))?));
      }
    }

    /*
      Get the value mapped to the given key, or add the mapping if key does not
      exist.

      :arg k: The key to access
      :type k: keyType

      :returns: Reference to the value mapped to the given key.
    */
    proc ref this(k: keyType) ref where isDefaultInitializable(valType) {
      _enter(); defer _leave();

      if !_set.contains((k, nil)) then {
        var defaultValue: valType;
        _set.add((k, new shared _valueWrapper(defaultValue)?));
      }

      ref e = _set.instance._getReference((k, nil));

      ref result = e[1]!.val;
      return result;
    }

    @chpldoc.nodoc
    proc const this(k: keyType) const
    where shouldReturnRvalueByValue(valType) && !isNonNilableClass(valType) {
      _enter(); defer _leave();

      // Could halt
      var e = _set.instance._getValue((k, nil));

      const result = e[1]!.val;
      return result;
    }

    @chpldoc.nodoc
    proc const this(k: keyType) const ref
    where !isNonNilableClass(valType) {
      _enter(); defer _leave();

      // Could halt
      var e = _set.instance._getValue((k, nil));

      const ref result = e[1]!.val;
      return result;
    }

    @chpldoc.nodoc
    proc const this(k: keyType)
    where isNonNilableClass(valType) {
      compilerError("Cannot access non-nilable class directly. Use an",
                    " appropriate accessor method instead.");
    }

    /* Get a borrowed reference to the element at position `k`.
     */
    proc ref getBorrowed(k: keyType) where isClass(valType) {
      _enter(); defer _leave();

      // This could halt
      ref element = _set.instance._getReference((k, nil));

      var result = element[1]!.val.borrow();

      return result;
    }

    /* Get a reference to the element at position `k`. This method is not
       available for non-nilable types.
     */
    proc ref getReference(k: keyType) ref
    where !isNonNilableClass(valType) {
      _enter(); defer _leave();

      // This could halt
      ref element = _set.instance._getReference((k, nil));

      ref result = element[1]!.val;

      return result;
    }

    /*
      Get a copy of the element stored at position `k`. This method is only
      available when a sortedMap's `valType` is a non-nilable class.
    */
    proc getValue(k: keyType) const {
      if isOwnedClass(valType) then
        compilerError('getValue cannot be called when a sortedMap value type ',
                      'is an owned class, use getBorrowed instead');

      _enter(); defer _leave();

      var result: _eltType;
      var found: bool;
      (found, result) = _set.lowerBound((k, nil));
      if !found || comparator.compare(result[0], k) != 0 then
        boundsCheckHalt(try! "sortedMap index %? out of bounds".format(k));
      return result[1]!.val;
    }
    /*
      Remove the element at position `k` from the sortedMap and return its value
    */
    proc ref getAndRemove(k: keyType) {
      _enter(); defer _leave();

      var result: _eltType;
      var found: bool;
      (found, result) = _set.lowerBound((k, nil));
      if !found || comparator.compare(result[0], k) != 0 then
        boundsCheckHalt(try! "sortedMap index %? out of bounds".format(k));

      _set.remove((k, nil));

      return result[1]!.val;
    }

    /*
      Iterates over the keys of this sortedMap. This is a shortcut for :iter:`keys`.

      :yields: A reference to one of the keys contained in this sortedMap.
    */
    iter these() const ref {
      for key in this.keys() {
        yield key;
      }
    }

    /*
      Iterates over the keys of this sortedMap.

      :yields: A reference to one of the keys contained in this sortedMap.
    */
    iter keys() const ref {
      foreach kv in _set {
          yield kv[0];
      }
    }

    /*
      Iterates over the key-value pairs of this sortedMap.

      :yields: A tuple whose elements are a copy of one of the key-value
               pairs contained in this map.
    */
    iter items() {
      foreach kv in _set {
        yield (kv[0], kv[1]!.val);
      }
    }

    /*
      Iterates over the values of this sortedMap.

      :yields: A reference to one of the values contained in this sortedMap.
    */
    iter values() ref {
      foreach kv in _set {
        yield kv[1]!.val;
      }
    }

    @chpldoc.nodoc
    proc serialize(writer, ref serializer) throws {
      _enter(); defer _leave();
      var first = true;
      writer.write("{");
      for kv in _set {
        if first {
          first = false;
        } else {
          writer.write(", ");
        }
        writer.write(kv[0], ": ", kv[1]!.val);
      }
      writer.write("}");
    }

    /*
      Adds a key-value pair to the sortedMap. Method returns `false` if the key
      already exists in the sortedMap.

      :arg k: The key to add to the sortedMap
      :type k: keyType

      :arg v: The value that maps to ``k``
      :type v: valueType

      :returns: `true` if `k` was not in the sortedMap and added with value `v`.
               `false` otherwise.
      :rtype: bool
    */
    proc ref add(in k: keyType, in v: valType): bool lifetime this < v {
      _enter(); defer _leave();

      if _set.contains((k, nil)) {
        return false;
      }

      _set.add((k, new shared _valueWrapper(v)?));

      return true;
    }

    /*
      Sets the value associated with a key. Method returns `false` if the key
      does not exist in the sortedMap.

      :arg k: The key whose value needs to change
      :type k: keyType

      :arg v: The desired value to the key ``k``
      :type v: valueType

      :returns: `true` if `k` was in the sortedMap and its value is updated with `v`.
               `false` otherwise.
      :rtype: bool
    */
    proc ref set(k: keyType, in v: valType): bool {
      _enter(); defer _leave();

      if _set.contains((k, nil)) == false {
        return false;
      }

      ref e = _set.instance._getReference((k, nil));
      e[1] = new shared _valueWrapper(v)?;

      return true;
    }

    /* If the sortedMap doesn't contain a value at position `k` add one and
       set it to `v`. If the sortedMap already contains a value at position
       `k`, update it to the value `v`.
     */
    proc ref addOrReplace(in k: keyType, in v: valType) {
      _enter(); defer _leave();
      _set.remove((k, nil));
      _set.add((k, new shared _valueWrapper(v)?));
    }

    /*
      Removes a key-value pair from the sortedMap, with the given key.

      :arg k: The key to remove from the sortedMap

      :returns: `false` if `k` was not in the sortedMap.  `true` if it was and removed.
      :rtype: bool
    */
    proc ref remove(k: keyType): bool {
      _enter(); defer _leave();
      return _set.remove((k, nil));
    }

    /*
      Returns a new 0-based array containing a copy of key-value pairs as
      tuples.

      :return: A new DefaultRectangular array.
      :rtype: [] (keyType, valType)
    */
    proc toArray(): [] (keyType, valType) {
      _enter(); defer _leave();

      if !isCopyableType(keyType) || !isCopyableType(valType) then
        compilerError("toArray requires copyable key and value types");

      var A: [0..#_size] (keyType, valType);

      for (a, item) in zip(A, items()) {
        a = item;
      }

      return A;
    }

    /*
      Returns a new 0-based array containing a copy of keys. Array is sorted using
      the comparator.

      :return: A new DefaultRectangular array.
      :rtype: [] keyType
    */
    proc keysToArray(): [] keyType {
      _enter(); defer _leave();

      if !isCopyableType(keyType) then
        compilerError("keysToArray requires a copyable key type");

      var A: [0..#_size] keyType;
      for (a, k) in zip(A, keys()) {
        a = k;
      }
      return A;
    }

    /*
      Returns a new 0-based array containing a copy of values. Array is not
      guaranteed to be in any particular order.

      :return: A new DefaultRectangular array.
      :rtype: [] valType
    */
    proc valuesToArray(): [] valType {
      _enter(); defer _leave();

      if !isCopyableType(valType) then
        compilerError("valuesToArray requires a copyable value type");

      var A: [0..#_size] valType;
      for (a, v) in zip(A, values()) {
        a = v;
      }
      return A;
    }
  } // end record sortedMap

  /*
    Replace the content of this sortedMap with the other's.

    .. warning::

      This will invalidate any references to elements previously contained in
      `lhs`.

    :arg lhs: The sortedMap to assign to.
    :arg rhs: The sortedMap to assign from.
  */
  operator sortedMap.=(ref lhs: sortedMap(?kt, ?vt, ?ps),
                        const ref rhs: sortedMap(kt, vt, ps)) {

    if !isCopyableType(kt) || !isCopyableType(vt) then
      compilerError("assigning sortedMap with non-copyable type");

    lhs.clear();
    for key in rhs.keys() {
      lhs.add(key, rhs[key]);
    }
  }


  /*
    Returns `true` if the contents of two sortedMaps are the same.

    :arg a: A sortedMap to compare.

    :arg b: A sortedMap to compare.

    :return: `true` if the contents of two sortedMaps are equal.
    :rtype: `bool`
  */
  operator sortedMap.==(const ref a: sortedMap(?kt, ?vt, ?ps),
                         const ref b: sortedMap(kt, vt, ps)): bool {
    if a.size != b.size then return false;
    for (e1, e2) in zip(a.items(), b.items()) {
      if e1 != e2 then return false;
    }
    return true;
  }

  /*
    Returns `true` if the contents of two sortedMaps are not the same.

    :arg a: A sortedMap to compare.

    :arg b: A sortedMap to compare.

    :return: `true` if the contents of two sortedMaps are not equal.
    :rtype: `bool`
  */
  operator sortedMap.!=(const ref a: sortedMap(?kt, ?vt, ?ps),
                         const ref b: sortedMap(kt, vt, ps)): bool {
    return !(a == b);
  }

  /*
    Returns a new sortedMap containing the keys and values in either a or b.
  */
  operator sortedMap.+(a: sortedMap(?keyType, ?valueType, ?parSafe),
                        b: sortedMap(keyType, valueType, parSafe)) {
    return a | b;
  }

  /*
    Sets the left-hand side sortedMap to contain the keys and values in either
    a or b.
   */
  operator sortedMap.+=(ref a: sortedMap(?keyType, ?valueType, ?parSafe),
                         b: sortedMap(keyType, valueType, parSafe)) {
    a |= b;
  }

  /*
    Returns a new sortedMap containing the keys and values in either a or b.
  */
  operator sortedMap.|(a: sortedMap(?keyType, ?valueType, ?parSafe),
                        b: sortedMap(keyType, valueType, parSafe)) {
    var newMap = new sortedMap(keyType, valueType, parSafe, a.comparator);

    newMap |= a;
    newMap |= b;

    return newMap;
  }

  /* Sets the left-hand side map to contain the keys and values in either
     a or b.
   */
  operator sortedMap.|=(ref a: sortedMap(?keyType, ?valueType, ?parSafe),
                         b: sortedMap(keyType, valueType, parSafe)) {
    // add keys/values from b to a if they weren't already in a
    for e in b.items() do a.add(e[0], e[1]);
  }

  /*
    Returns a new sortedMap containing the keys that are in both a and b.
  */
  operator sortedMap.&(a: sortedMap(?keyType, ?valueType, ?parSafe),
                        b: sortedMap(keyType, valueType, parSafe)) {
    var newMap = new sortedMap(keyType, valueType, parSafe, a.comparator);
    for (k, v) in a.items() {
      if b.contains(k) then
        newMap.add(k, v);
    }
    return newMap;
  }

  /* Sets the left-hand side sortedMap to contain the keys that are in both a and b.

    .. warning::

      This will invalidate any references to elements previously contained in
      `lhs`.

   */
  operator sortedMap.&=(ref a: sortedMap(?keyType, ?valueType, ?parSafe),
                         b: sortedMap(keyType, valueType, parSafe)) {
    a = a & b;
  }

  /*
    Returns a new sortedMap containing the keys that are only in a, but not b.
  */
  operator sortedMap.-(a: sortedMap(?keyType, ?valueType, ?parSafe),
                        b: sortedMap(keyType, valueType, parSafe)) {
    var newMap = new sortedMap(keyType, valueType, parSafe, a.comparator);

    for (k, v) in a.items() {
      if !b.contains(k) then
        newMap.add(k, v);
    }

    return newMap;
  }

  /* Sets the left-hand side sortedMap to contain the keys that are in the
     left-hand sortedMap, but not the right-hand sortedMap. */
  operator sortedMap.-=(ref a: sortedMap(?keyType, ?valueType, ?parSafe),
                         b: sortedMap(keyType, valueType, parSafe)) {
    for k in b.keys() {
      a.remove(k);
    }
  }

  /*
    Returns a new sortedMap containing the keys that are in either a or b, but
    not both.

  */
  operator sortedMap.^(a: sortedMap(?keyType, ?valueType, ?parSafe),
                        b: sortedMap(keyType, valueType, parSafe)) {
    var newMap = new sortedMap(keyType, valueType, parSafe, a.comparator);

    for k in a.keys() do
      if !b.contains(k) then newMap[k] = a[k];
    for k in b.keys() do
      if !a.contains(k) then newMap[k] = b[k];
    return newMap;
  }

  /* Sets the left-hand side sortedMap to contain the keys that are in either the
     left-hand sortedMap or the right-hand sortedMap, but not both. */
  operator sortedMap.^=(ref a: sortedMap(?keyType, ?valueType, ?parSafe),
                         b: sortedMap(keyType, valueType, parSafe)) {
    for k in b.keys() {
      if a.contains(k) then a.remove(k);
      else a[k] = b[k];
    }
  }
}
