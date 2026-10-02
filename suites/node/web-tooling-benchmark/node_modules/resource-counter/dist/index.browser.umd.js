(function (global, factory) {
	typeof exports === 'object' && typeof module !== 'undefined' ? factory(exports) :
	typeof define === 'function' && define.amd ? define(['exports'], factory) :
	(factory((global['resource-counter'] = {})));
}(this, (function (exports) { 'use strict';

var commonjsGlobal = typeof window !== 'undefined' ? window : typeof global !== 'undefined' ? global : typeof self !== 'undefined' ? self : {};



function unwrapExports (x) {
	return x && x.__esModule && Object.prototype.hasOwnProperty.call(x, 'default') ? x['default'] : x;
}

function createCommonjsModule(fn, module) {
	return module = { exports: {} }, fn(module, module.exports), module.exports;
}

var _iterStep = function(done, value){
  return {value: value, done: !!done};
};

var _iterators = {};

var toString = {}.toString;

var _cof = function(it){
  return toString.call(it).slice(8, -1);
};

// fallback for non-array-like ES3 and non-enumerable old V8 strings

var _iobject = Object('z').propertyIsEnumerable(0) ? Object : function(it){
  return _cof(it) == 'String' ? it.split('') : Object(it);
};

// 7.2.1 RequireObjectCoercible(argument)
var _defined = function(it){
  if(it == undefined)throw TypeError("Can't call method on  " + it);
  return it;
};

// to indexed object, toObject with fallback for non-array-like ES3 strings

var _toIobject = function(it){
  return _iobject(_defined(it));
};

var _library = true;

var _global = createCommonjsModule(function (module) {
// https://github.com/zloirock/core-js/issues/86#issuecomment-115759028
var global = module.exports = typeof window != 'undefined' && window.Math == Math
  ? window : typeof self != 'undefined' && self.Math == Math ? self : Function('return this')();
if(typeof __g == 'number')__g = global; // eslint-disable-line no-undef
});

var _core = createCommonjsModule(function (module) {
var core = module.exports = {version: '2.4.0'};
if(typeof __e == 'number')__e = core; // eslint-disable-line no-undef
});

var _core_1 = _core.version;

var _aFunction = function(it){
  if(typeof it != 'function')throw TypeError(it + ' is not a function!');
  return it;
};

// optional / simple context binding

var _ctx = function(fn, that, length){
  _aFunction(fn);
  if(that === undefined)return fn;
  switch(length){
    case 1: return function(a){
      return fn.call(that, a);
    };
    case 2: return function(a, b){
      return fn.call(that, a, b);
    };
    case 3: return function(a, b, c){
      return fn.call(that, a, b, c);
    };
  }
  return function(/* ...args */){
    return fn.apply(that, arguments);
  };
};

var _isObject = function(it){
  return typeof it === 'object' ? it !== null : typeof it === 'function';
};

var _anObject = function(it){
  if(!_isObject(it))throw TypeError(it + ' is not an object!');
  return it;
};

var _fails = function(exec){
  try {
    return !!exec();
  } catch(e){
    return true;
  }
};

// Thank's IE8 for his funny defineProperty
var _descriptors = !_fails(function(){
  return Object.defineProperty({}, 'a', {get: function(){ return 7; }}).a != 7;
});

var document$1 = _global.document;
var is = _isObject(document$1) && _isObject(document$1.createElement);
var _domCreate = function(it){
  return is ? document$1.createElement(it) : {};
};

var _ie8DomDefine = !_descriptors && !_fails(function(){
  return Object.defineProperty(_domCreate('div'), 'a', {get: function(){ return 7; }}).a != 7;
});

// 7.1.1 ToPrimitive(input [, PreferredType])

// instead of the ES6 spec version, we didn't implement @@toPrimitive case
// and the second argument - flag - preferred type is a string
var _toPrimitive = function(it, S){
  if(!_isObject(it))return it;
  var fn, val;
  if(S && typeof (fn = it.toString) == 'function' && !_isObject(val = fn.call(it)))return val;
  if(typeof (fn = it.valueOf) == 'function' && !_isObject(val = fn.call(it)))return val;
  if(!S && typeof (fn = it.toString) == 'function' && !_isObject(val = fn.call(it)))return val;
  throw TypeError("Can't convert object to primitive value");
};

var dP             = Object.defineProperty;

var f = _descriptors ? Object.defineProperty : function defineProperty(O, P, Attributes){
  _anObject(O);
  P = _toPrimitive(P, true);
  _anObject(Attributes);
  if(_ie8DomDefine)try {
    return dP(O, P, Attributes);
  } catch(e){ /* empty */ }
  if('get' in Attributes || 'set' in Attributes)throw TypeError('Accessors not supported!');
  if('value' in Attributes)O[P] = Attributes.value;
  return O;
};

var _objectDp = {
	f: f
};

var _propertyDesc = function(bitmap, value){
  return {
    enumerable  : !(bitmap & 1),
    configurable: !(bitmap & 2),
    writable    : !(bitmap & 4),
    value       : value
  };
};

var _hide = _descriptors ? function(object, key, value){
  return _objectDp.f(object, key, _propertyDesc(1, value));
} : function(object, key, value){
  object[key] = value;
  return object;
};

var PROTOTYPE = 'prototype';

var $export = function(type, name, source){
  var IS_FORCED = type & $export.F
    , IS_GLOBAL = type & $export.G
    , IS_STATIC = type & $export.S
    , IS_PROTO  = type & $export.P
    , IS_BIND   = type & $export.B
    , IS_WRAP   = type & $export.W
    , exports   = IS_GLOBAL ? _core : _core[name] || (_core[name] = {})
    , expProto  = exports[PROTOTYPE]
    , target    = IS_GLOBAL ? _global : IS_STATIC ? _global[name] : (_global[name] || {})[PROTOTYPE]
    , key, own, out;
  if(IS_GLOBAL)source = name;
  for(key in source){
    // contains in native
    own = !IS_FORCED && target && target[key] !== undefined;
    if(own && key in exports)continue;
    // export native or passed
    out = own ? target[key] : source[key];
    // prevent global pollution for namespaces
    exports[key] = IS_GLOBAL && typeof target[key] != 'function' ? source[key]
    // bind timers to global for call from export context
    : IS_BIND && own ? _ctx(out, _global)
    // wrap global constructors for prevent change them in library
    : IS_WRAP && target[key] == out ? (function(C){
      var F = function(a, b, c){
        if(this instanceof C){
          switch(arguments.length){
            case 0: return new C;
            case 1: return new C(a);
            case 2: return new C(a, b);
          } return new C(a, b, c);
        } return C.apply(this, arguments);
      };
      F[PROTOTYPE] = C[PROTOTYPE];
      return F;
    // make static versions for prototype methods
    })(out) : IS_PROTO && typeof out == 'function' ? _ctx(Function.call, out) : out;
    // export proto methods to core.%CONSTRUCTOR%.methods.%NAME%
    if(IS_PROTO){
      (exports.virtual || (exports.virtual = {}))[key] = out;
      // export proto methods to core.%CONSTRUCTOR%.prototype.%NAME%
      if(type & $export.R && expProto && !expProto[key])_hide(expProto, key, out);
    }
  }
};
// type bitmap
$export.F = 1;   // forced
$export.G = 2;   // global
$export.S = 4;   // static
$export.P = 8;   // proto
$export.B = 16;  // bind
$export.W = 32;  // wrap
$export.U = 64;  // safe
$export.R = 128; // real proto method for `library` 
var _export = $export;

var _redefine = _hide;

var hasOwnProperty = {}.hasOwnProperty;
var _has = function(it, key){
  return hasOwnProperty.call(it, key);
};

// 7.1.4 ToInteger
var ceil  = Math.ceil;
var floor = Math.floor;
var _toInteger = function(it){
  return isNaN(it = +it) ? 0 : (it > 0 ? floor : ceil)(it);
};

// 7.1.15 ToLength
var min       = Math.min;
var _toLength = function(it){
  return it > 0 ? min(_toInteger(it), 0x1fffffffffffff) : 0; // pow(2, 53) - 1 == 9007199254740991
};

var max       = Math.max;
var min$1       = Math.min;
var _toIndex = function(index, length){
  index = _toInteger(index);
  return index < 0 ? max(index + length, 0) : min$1(index, length);
};

// false -> Array#indexOf
// true  -> Array#includes

var _arrayIncludes = function(IS_INCLUDES){
  return function($this, el, fromIndex){
    var O      = _toIobject($this)
      , length = _toLength(O.length)
      , index  = _toIndex(fromIndex, length)
      , value;
    // Array#includes uses SameValueZero equality algorithm
    if(IS_INCLUDES && el != el)while(length > index){
      value = O[index++];
      if(value != value)return true;
    // Array#toIndex ignores holes, Array#includes - not
    } else for(;length > index; index++)if(IS_INCLUDES || index in O){
      if(O[index] === el)return IS_INCLUDES || index || 0;
    } return !IS_INCLUDES && -1;
  };
};

var SHARED = '__core-js_shared__';
var store  = _global[SHARED] || (_global[SHARED] = {});
var _shared = function(key){
  return store[key] || (store[key] = {});
};

var id = 0;
var px = Math.random();
var _uid = function(key){
  return 'Symbol('.concat(key === undefined ? '' : key, ')_', (++id + px).toString(36));
};

var shared = _shared('keys');
var _sharedKey = function(key){
  return shared[key] || (shared[key] = _uid(key));
};

var arrayIndexOf = _arrayIncludes(false);
var IE_PROTO$1     = _sharedKey('IE_PROTO');

var _objectKeysInternal = function(object, names){
  var O      = _toIobject(object)
    , i      = 0
    , result = []
    , key;
  for(key in O)if(key != IE_PROTO$1)_has(O, key) && result.push(key);
  // Don't enum bug & hidden keys
  while(names.length > i)if(_has(O, key = names[i++])){
    ~arrayIndexOf(result, key) || result.push(key);
  }
  return result;
};

// IE 8- don't enum bug keys
var _enumBugKeys = (
  'constructor,hasOwnProperty,isPrototypeOf,propertyIsEnumerable,toLocaleString,toString,valueOf'
).split(',');

// 19.1.2.14 / 15.2.3.14 Object.keys(O)


var _objectKeys = Object.keys || function keys(O){
  return _objectKeysInternal(O, _enumBugKeys);
};

var _objectDps = _descriptors ? Object.defineProperties : function defineProperties(O, Properties){
  _anObject(O);
  var keys   = _objectKeys(Properties)
    , length = keys.length
    , i = 0
    , P;
  while(length > i)_objectDp.f(O, P = keys[i++], Properties[P]);
  return O;
};

var _html = _global.document && document.documentElement;

// 19.1.2.2 / 15.2.3.5 Object.create(O [, Properties])
var IE_PROTO    = _sharedKey('IE_PROTO');
var Empty       = function(){ /* empty */ };
var PROTOTYPE$1   = 'prototype';

// Create object with fake `null` prototype: use iframe Object with cleared prototype
var createDict = function(){
  // Thrash, waste and sodomy: IE GC bug
  var iframe = _domCreate('iframe')
    , i      = _enumBugKeys.length
    , lt     = '<'
    , gt     = '>'
    , iframeDocument;
  iframe.style.display = 'none';
  _html.appendChild(iframe);
  iframe.src = 'javascript:'; // eslint-disable-line no-script-url
  // createDict = iframe.contentWindow.Object;
  // html.removeChild(iframe);
  iframeDocument = iframe.contentWindow.document;
  iframeDocument.open();
  iframeDocument.write(lt + 'script' + gt + 'document.F=Object' + lt + '/script' + gt);
  iframeDocument.close();
  createDict = iframeDocument.F;
  while(i--)delete createDict[PROTOTYPE$1][_enumBugKeys[i]];
  return createDict();
};

var _objectCreate = Object.create || function create(O, Properties){
  var result;
  if(O !== null){
    Empty[PROTOTYPE$1] = _anObject(O);
    result = new Empty;
    Empty[PROTOTYPE$1] = null;
    // add "__proto__" for Object.getPrototypeOf polyfill
    result[IE_PROTO] = O;
  } else result = createDict();
  return Properties === undefined ? result : _objectDps(result, Properties);
};

var _wks = createCommonjsModule(function (module) {
var store      = _shared('wks')
  , Symbol     = _global.Symbol
  , USE_SYMBOL = typeof Symbol == 'function';

var $exports = module.exports = function(name){
  return store[name] || (store[name] =
    USE_SYMBOL && Symbol[name] || (USE_SYMBOL ? Symbol : _uid)('Symbol.' + name));
};

$exports.store = store;
});

var def = _objectDp.f;
var TAG = _wks('toStringTag');

var _setToStringTag = function(it, tag, stat){
  if(it && !_has(it = stat ? it : it.prototype, TAG))def(it, TAG, {configurable: true, value: tag});
};

var IteratorPrototype = {};

// 25.1.2.1.1 %IteratorPrototype%[@@iterator]()
_hide(IteratorPrototype, _wks('iterator'), function(){ return this; });

var _iterCreate = function(Constructor, NAME, next){
  Constructor.prototype = _objectCreate(IteratorPrototype, {next: _propertyDesc(1, next)});
  _setToStringTag(Constructor, NAME + ' Iterator');
};

// 7.1.13 ToObject(argument)

var _toObject = function(it){
  return Object(_defined(it));
};

// 19.1.2.9 / 15.2.3.2 Object.getPrototypeOf(O)
var IE_PROTO$2    = _sharedKey('IE_PROTO');
var ObjectProto = Object.prototype;

var _objectGpo = Object.getPrototypeOf || function(O){
  O = _toObject(O);
  if(_has(O, IE_PROTO$2))return O[IE_PROTO$2];
  if(typeof O.constructor == 'function' && O instanceof O.constructor){
    return O.constructor.prototype;
  } return O instanceof Object ? ObjectProto : null;
};

var ITERATOR       = _wks('iterator');
var BUGGY          = !([].keys && 'next' in [].keys());
var FF_ITERATOR    = '@@iterator';
var KEYS           = 'keys';
var VALUES         = 'values';

var returnThis = function(){ return this; };

var _iterDefine = function(Base, NAME, Constructor, next, DEFAULT, IS_SET, FORCED){
  _iterCreate(Constructor, NAME, next);
  var getMethod = function(kind){
    if(!BUGGY && kind in proto)return proto[kind];
    switch(kind){
      case KEYS: return function keys(){ return new Constructor(this, kind); };
      case VALUES: return function values(){ return new Constructor(this, kind); };
    } return function entries(){ return new Constructor(this, kind); };
  };
  var TAG        = NAME + ' Iterator'
    , DEF_VALUES = DEFAULT == VALUES
    , VALUES_BUG = false
    , proto      = Base.prototype
    , $native    = proto[ITERATOR] || proto[FF_ITERATOR] || DEFAULT && proto[DEFAULT]
    , $default   = $native || getMethod(DEFAULT)
    , $entries   = DEFAULT ? !DEF_VALUES ? $default : getMethod('entries') : undefined
    , $anyNative = NAME == 'Array' ? proto.entries || $native : $native
    , methods, key, IteratorPrototype;
  // Fix native
  if($anyNative){
    IteratorPrototype = _objectGpo($anyNative.call(new Base));
    if(IteratorPrototype !== Object.prototype){
      // Set @@toStringTag to native iterators
      _setToStringTag(IteratorPrototype, TAG, true);
      // fix for some old engines
      if(!_library && !_has(IteratorPrototype, ITERATOR))_hide(IteratorPrototype, ITERATOR, returnThis);
    }
  }
  // fix Array#{values, @@iterator}.name in V8 / FF
  if(DEF_VALUES && $native && $native.name !== VALUES){
    VALUES_BUG = true;
    $default = function values(){ return $native.call(this); };
  }
  // Define iterator
  if((!_library || FORCED) && (BUGGY || VALUES_BUG || !proto[ITERATOR])){
    _hide(proto, ITERATOR, $default);
  }
  // Plug for library
  _iterators[NAME] = $default;
  _iterators[TAG]  = returnThis;
  if(DEFAULT){
    methods = {
      values:  DEF_VALUES ? $default : getMethod(VALUES),
      keys:    IS_SET     ? $default : getMethod(KEYS),
      entries: $entries
    };
    if(FORCED)for(key in methods){
      if(!(key in proto))_redefine(proto, key, methods[key]);
    } else _export(_export.P + _export.F * (BUGGY || VALUES_BUG), NAME, methods);
  }
  return methods;
};

// 22.1.3.4 Array.prototype.entries()
// 22.1.3.13 Array.prototype.keys()
// 22.1.3.29 Array.prototype.values()
// 22.1.3.30 Array.prototype[@@iterator]()
var es6_array_iterator = _iterDefine(Array, 'Array', function(iterated, kind){
  this._t = _toIobject(iterated); // target
  this._i = 0;                   // next index
  this._k = kind;                // kind
// 22.1.5.2.1 %ArrayIteratorPrototype%.next()
}, function(){
  var O     = this._t
    , kind  = this._k
    , index = this._i++;
  if(!O || index >= O.length){
    this._t = undefined;
    return _iterStep(1);
  }
  if(kind == 'keys'  )return _iterStep(0, index);
  if(kind == 'values')return _iterStep(0, O[index]);
  return _iterStep(0, [index, O[index]]);
}, 'values');

// argumentsList[@@iterator] is %ArrayProto_values% (9.4.4.6, 9.4.4.7)
_iterators.Arguments = _iterators.Array;

var TO_STRING_TAG = _wks('toStringTag');

for(var collections = ['NodeList', 'DOMTokenList', 'MediaList', 'StyleSheetList', 'CSSRuleList'], i = 0; i < 5; i++){
  var NAME       = collections[i]
    , Collection = _global[NAME]
    , proto      = Collection && Collection.prototype;
  if(proto && !proto[TO_STRING_TAG])_hide(proto, TO_STRING_TAG, NAME);
  _iterators[NAME] = _iterators.Array;
}

// true  -> String#at
// false -> String#codePointAt
var _stringAt = function(TO_STRING){
  return function(that, pos){
    var s = String(_defined(that))
      , i = _toInteger(pos)
      , l = s.length
      , a, b;
    if(i < 0 || i >= l)return TO_STRING ? '' : undefined;
    a = s.charCodeAt(i);
    return a < 0xd800 || a > 0xdbff || i + 1 === l || (b = s.charCodeAt(i + 1)) < 0xdc00 || b > 0xdfff
      ? TO_STRING ? s.charAt(i) : a
      : TO_STRING ? s.slice(i, i + 2) : (a - 0xd800 << 10) + (b - 0xdc00) + 0x10000;
  };
};

var $at  = _stringAt(true);

// 21.1.3.27 String.prototype[@@iterator]()
_iterDefine(String, 'String', function(iterated){
  this._t = String(iterated); // target
  this._i = 0;                // next index
// 21.1.5.2.1 %StringIteratorPrototype%.next()
}, function(){
  var O     = this._t
    , index = this._i
    , point;
  if(index >= O.length)return {value: undefined, done: true};
  point = $at(O, index);
  this._i += point.length;
  return {value: point, done: false};
});

// getting tag from 19.1.3.6 Object.prototype.toString()
var TAG$1 = _wks('toStringTag');
var ARG = _cof(function(){ return arguments; }()) == 'Arguments';

// fallback for IE11 Script Access Denied error
var tryGet = function(it, key){
  try {
    return it[key];
  } catch(e){ /* empty */ }
};

var _classof = function(it){
  var O, T, B;
  return it === undefined ? 'Undefined' : it === null ? 'Null'
    // @@toStringTag case
    : typeof (T = tryGet(O = Object(it), TAG$1)) == 'string' ? T
    // builtinTag case
    : ARG ? _cof(O)
    // ES3 arguments fallback
    : (B = _cof(O)) == 'Object' && typeof O.callee == 'function' ? 'Arguments' : B;
};

var ITERATOR$1  = _wks('iterator');
var core_isIterable = _core.isIterable = function(it){
  var O = Object(it);
  return O[ITERATOR$1] !== undefined
    || '@@iterator' in O
    || _iterators.hasOwnProperty(_classof(O));
};

var isIterable$2 = core_isIterable;

var isIterable = createCommonjsModule(function (module) {
module.exports = { "default": isIterable$2, __esModule: true };
});

unwrapExports(isIterable);

var ITERATOR$2  = _wks('iterator');
var core_getIteratorMethod = _core.getIteratorMethod = function(it){
  if(it != undefined)return it[ITERATOR$2]
    || it['@@iterator']
    || _iterators[_classof(it)];
};

var core_getIterator = _core.getIterator = function(it){
  var iterFn = core_getIteratorMethod(it);
  if(typeof iterFn != 'function')throw TypeError(it + ' is not iterable!');
  return _anObject(iterFn.call(it));
};

var getIterator$2 = core_getIterator;

var getIterator = createCommonjsModule(function (module) {
module.exports = { "default": getIterator$2, __esModule: true };
});

unwrapExports(getIterator);

var slicedToArray = createCommonjsModule(function (module, exports) {
exports.__esModule = true;



var _isIterable3 = _interopRequireDefault(isIterable);



var _getIterator3 = _interopRequireDefault(getIterator);

function _interopRequireDefault(obj) { return obj && obj.__esModule ? obj : { default: obj }; }

exports.default = function () {
  function sliceIterator(arr, i) {
    var _arr = [];
    var _n = true;
    var _d = false;
    var _e = undefined;

    try {
      for (var _i = (0, _getIterator3.default)(arr), _s; !(_n = (_s = _i.next()).done); _n = true) {
        _arr.push(_s.value);

        if (i && _arr.length === i) break;
      }
    } catch (err) {
      _d = true;
      _e = err;
    } finally {
      try {
        if (!_n && _i["return"]) _i["return"]();
      } finally {
        if (_d) throw _e;
      }
    }

    return _arr;
  }

  return function (arr, i) {
    if (Array.isArray(arr)) {
      return arr;
    } else if ((0, _isIterable3.default)(Object(arr))) {
      return sliceIterator(arr, i);
    } else {
      throw new TypeError("Invalid attempt to destructure non-iterable instance");
    }
  };
}();
});

var _slicedToArray = unwrapExports(slicedToArray);

var classCallCheck = createCommonjsModule(function (module, exports) {
exports.__esModule = true;

exports.default = function (instance, Constructor) {
  if (!(instance instanceof Constructor)) {
    throw new TypeError("Cannot call a class as a function");
  }
};
});

var _classCallCheck = unwrapExports(classCallCheck);

// 19.1.2.4 / 15.2.3.6 Object.defineProperty(O, P, Attributes)
_export(_export.S + _export.F * !_descriptors, 'Object', {defineProperty: _objectDp.f});

var $Object = _core.Object;
var defineProperty$2 = function defineProperty(it, key, desc){
  return $Object.defineProperty(it, key, desc);
};

var defineProperty = createCommonjsModule(function (module) {
module.exports = { "default": defineProperty$2, __esModule: true };
});

unwrapExports(defineProperty);

var createClass = createCommonjsModule(function (module, exports) {
exports.__esModule = true;



var _defineProperty2 = _interopRequireDefault(defineProperty);

function _interopRequireDefault(obj) { return obj && obj.__esModule ? obj : { default: obj }; }

exports.default = function () {
  function defineProperties(target, props) {
    for (var i = 0; i < props.length; i++) {
      var descriptor = props[i];
      descriptor.enumerable = descriptor.enumerable || false;
      descriptor.configurable = true;
      if ("value" in descriptor) descriptor.writable = true;
      (0, _defineProperty2.default)(target, descriptor.key, descriptor);
    }
  }

  return function (Constructor, protoProps, staticProps) {
    if (protoProps) defineProperties(Constructor.prototype, protoProps);
    if (staticProps) defineProperties(Constructor, staticProps);
    return Constructor;
  };
}();
});

var _createClass = unwrapExports(createClass);

function allocate(tree, counter, snapshot) {
  var changed = void 0;
  var treeNew = void 0;
  tree.allocate(counter, function (_ref) {
    var counter_ = _ref.counter,
        changed_ = _ref.changed,
        tree_ = _ref.tree;

    counter = counter_;
    changed = changed_;
    treeNew = tree_;
  }, snapshot);
  // $FlowFixMe: changed is initialised
  return [counter, changed, treeNew];
}
/** @module counterUtil */

function deallocate(tree, counter, snapshot) {
  var changed = void 0;
  var treeNew = void 0;
  tree.deallocate(counter, function (_ref2) {
    var changed_ = _ref2.changed,
        tree_ = _ref2.tree;

    changed = changed_;
    treeNew = tree_;
  }, snapshot);
  // $FlowFixMe: changed is initialised
  return [changed, treeNew];
}

function check(tree, counter) {
  var set = void 0;
  tree.check(counter, function (set_) {
    set = set_;
  });
  return !!set;
}

// most Object methods by ES6 should accept primitives

var _objectSap = function(KEY, exec){
  var fn  = (_core.Object || {})[KEY] || Object[KEY]
    , exp = {};
  exp[KEY] = exec(fn);
  _export(_export.S + _export.F * _fails(function(){ fn(1); }), 'Object', exp);
};

// 19.1.2.14 Object.keys(O)


_objectSap('keys', function(){
  return function keys(it){
    return _objectKeys(_toObject(it));
  };
});

var keys$1 = _core.Object.keys;

var keys = createCommonjsModule(function (module) {
module.exports = { "default": keys$1, __esModule: true };
});

var _Object$keys = unwrapExports(keys);

// 19.1.2.9 Object.getPrototypeOf(O)


_objectSap('getPrototypeOf', function(){
  return function getPrototypeOf(it){
    return _objectGpo(_toObject(it));
  };
});

var getPrototypeOf$1 = _core.Object.getPrototypeOf;

var getPrototypeOf = createCommonjsModule(function (module) {
module.exports = { "default": getPrototypeOf$1, __esModule: true };
});

var _Object$getPrototypeOf = unwrapExports(getPrototypeOf);

var f$1 = _wks;

var _wksExt = {
	f: f$1
};

var iterator$2 = _wksExt.f('iterator');

var iterator = createCommonjsModule(function (module) {
module.exports = { "default": iterator$2, __esModule: true };
});

unwrapExports(iterator);

var _meta = createCommonjsModule(function (module) {
var META     = _uid('meta')
  , setDesc  = _objectDp.f
  , id       = 0;
var isExtensible = Object.isExtensible || function(){
  return true;
};
var FREEZE = !_fails(function(){
  return isExtensible(Object.preventExtensions({}));
});
var setMeta = function(it){
  setDesc(it, META, {value: {
    i: 'O' + ++id, // object ID
    w: {}          // weak collections IDs
  }});
};
var fastKey = function(it, create){
  // return primitive with prefix
  if(!_isObject(it))return typeof it == 'symbol' ? it : (typeof it == 'string' ? 'S' : 'P') + it;
  if(!_has(it, META)){
    // can't set metadata to uncaught frozen object
    if(!isExtensible(it))return 'F';
    // not necessary to add metadata
    if(!create)return 'E';
    // add missing metadata
    setMeta(it);
  // return object ID
  } return it[META].i;
};
var getWeak = function(it, create){
  if(!_has(it, META)){
    // can't set metadata to uncaught frozen object
    if(!isExtensible(it))return true;
    // not necessary to add metadata
    if(!create)return false;
    // add missing metadata
    setMeta(it);
  // return hash weak collections IDs
  } return it[META].w;
};
// add metadata on freeze-family methods calling
var onFreeze = function(it){
  if(FREEZE && meta.NEED && isExtensible(it) && !_has(it, META))setMeta(it);
  return it;
};
var meta = module.exports = {
  KEY:      META,
  NEED:     false,
  fastKey:  fastKey,
  getWeak:  getWeak,
  onFreeze: onFreeze
};
});

var _meta_1 = _meta.KEY;
var _meta_2 = _meta.NEED;
var _meta_3 = _meta.fastKey;
var _meta_4 = _meta.getWeak;
var _meta_5 = _meta.onFreeze;

var defineProperty$4 = _objectDp.f;
var _wksDefine = function(name){
  var $Symbol = _core.Symbol || (_core.Symbol = _library ? {} : _global.Symbol || {});
  if(name.charAt(0) != '_' && !(name in $Symbol))defineProperty$4($Symbol, name, {value: _wksExt.f(name)});
};

var _keyof = function(object, el){
  var O      = _toIobject(object)
    , keys   = _objectKeys(O)
    , length = keys.length
    , index  = 0
    , key;
  while(length > index)if(O[key = keys[index++]] === el)return key;
};

var f$2 = Object.getOwnPropertySymbols;

var _objectGops = {
	f: f$2
};

var f$3 = {}.propertyIsEnumerable;

var _objectPie = {
	f: f$3
};

// all enumerable object keys, includes symbols

var _enumKeys = function(it){
  var result     = _objectKeys(it)
    , getSymbols = _objectGops.f;
  if(getSymbols){
    var symbols = getSymbols(it)
      , isEnum  = _objectPie.f
      , i       = 0
      , key;
    while(symbols.length > i)if(isEnum.call(it, key = symbols[i++]))result.push(key);
  } return result;
};

// 7.2.2 IsArray(argument)

var _isArray = Array.isArray || function isArray(arg){
  return _cof(arg) == 'Array';
};

// 19.1.2.7 / 15.2.3.4 Object.getOwnPropertyNames(O)
var hiddenKeys = _enumBugKeys.concat('length', 'prototype');

var f$5 = Object.getOwnPropertyNames || function getOwnPropertyNames(O){
  return _objectKeysInternal(O, hiddenKeys);
};

var _objectGopn = {
	f: f$5
};

// fallback for IE11 buggy Object.getOwnPropertyNames with iframe and window
var gOPN$1      = _objectGopn.f;
var toString$1  = {}.toString;

var windowNames = typeof window == 'object' && window && Object.getOwnPropertyNames
  ? Object.getOwnPropertyNames(window) : [];

var getWindowNames = function(it){
  try {
    return gOPN$1(it);
  } catch(e){
    return windowNames.slice();
  }
};

var f$4 = function getOwnPropertyNames(it){
  return windowNames && toString$1.call(it) == '[object Window]' ? getWindowNames(it) : gOPN$1(_toIobject(it));
};

var _objectGopnExt = {
	f: f$4
};

var gOPD$1           = Object.getOwnPropertyDescriptor;

var f$6 = _descriptors ? gOPD$1 : function getOwnPropertyDescriptor(O, P){
  O = _toIobject(O);
  P = _toPrimitive(P, true);
  if(_ie8DomDefine)try {
    return gOPD$1(O, P);
  } catch(e){ /* empty */ }
  if(_has(O, P))return _propertyDesc(!_objectPie.f.call(O, P), O[P]);
};

var _objectGopd = {
	f: f$6
};

// ECMAScript 6 symbols shim
var META           = _meta.KEY;
var gOPD           = _objectGopd.f;
var dP$1             = _objectDp.f;
var gOPN           = _objectGopnExt.f;
var $Symbol        = _global.Symbol;
var $JSON          = _global.JSON;
var _stringify     = $JSON && $JSON.stringify;
var PROTOTYPE$2      = 'prototype';
var HIDDEN         = _wks('_hidden');
var TO_PRIMITIVE   = _wks('toPrimitive');
var isEnum         = {}.propertyIsEnumerable;
var SymbolRegistry = _shared('symbol-registry');
var AllSymbols     = _shared('symbols');
var OPSymbols      = _shared('op-symbols');
var ObjectProto$1    = Object[PROTOTYPE$2];
var USE_NATIVE     = typeof $Symbol == 'function';
var QObject        = _global.QObject;
// Don't use setters in Qt Script, https://github.com/zloirock/core-js/issues/173
var setter = !QObject || !QObject[PROTOTYPE$2] || !QObject[PROTOTYPE$2].findChild;

// fallback for old Android, https://code.google.com/p/v8/issues/detail?id=687
var setSymbolDesc = _descriptors && _fails(function(){
  return _objectCreate(dP$1({}, 'a', {
    get: function(){ return dP$1(this, 'a', {value: 7}).a; }
  })).a != 7;
}) ? function(it, key, D){
  var protoDesc = gOPD(ObjectProto$1, key);
  if(protoDesc)delete ObjectProto$1[key];
  dP$1(it, key, D);
  if(protoDesc && it !== ObjectProto$1)dP$1(ObjectProto$1, key, protoDesc);
} : dP$1;

var wrap = function(tag){
  var sym = AllSymbols[tag] = _objectCreate($Symbol[PROTOTYPE$2]);
  sym._k = tag;
  return sym;
};

var isSymbol = USE_NATIVE && typeof $Symbol.iterator == 'symbol' ? function(it){
  return typeof it == 'symbol';
} : function(it){
  return it instanceof $Symbol;
};

var $defineProperty = function defineProperty(it, key, D){
  if(it === ObjectProto$1)$defineProperty(OPSymbols, key, D);
  _anObject(it);
  key = _toPrimitive(key, true);
  _anObject(D);
  if(_has(AllSymbols, key)){
    if(!D.enumerable){
      if(!_has(it, HIDDEN))dP$1(it, HIDDEN, _propertyDesc(1, {}));
      it[HIDDEN][key] = true;
    } else {
      if(_has(it, HIDDEN) && it[HIDDEN][key])it[HIDDEN][key] = false;
      D = _objectCreate(D, {enumerable: _propertyDesc(0, false)});
    } return setSymbolDesc(it, key, D);
  } return dP$1(it, key, D);
};
var $defineProperties = function defineProperties(it, P){
  _anObject(it);
  var keys = _enumKeys(P = _toIobject(P))
    , i    = 0
    , l = keys.length
    , key;
  while(l > i)$defineProperty(it, key = keys[i++], P[key]);
  return it;
};
var $create = function create(it, P){
  return P === undefined ? _objectCreate(it) : $defineProperties(_objectCreate(it), P);
};
var $propertyIsEnumerable = function propertyIsEnumerable(key){
  var E = isEnum.call(this, key = _toPrimitive(key, true));
  if(this === ObjectProto$1 && _has(AllSymbols, key) && !_has(OPSymbols, key))return false;
  return E || !_has(this, key) || !_has(AllSymbols, key) || _has(this, HIDDEN) && this[HIDDEN][key] ? E : true;
};
var $getOwnPropertyDescriptor = function getOwnPropertyDescriptor(it, key){
  it  = _toIobject(it);
  key = _toPrimitive(key, true);
  if(it === ObjectProto$1 && _has(AllSymbols, key) && !_has(OPSymbols, key))return;
  var D = gOPD(it, key);
  if(D && _has(AllSymbols, key) && !(_has(it, HIDDEN) && it[HIDDEN][key]))D.enumerable = true;
  return D;
};
var $getOwnPropertyNames = function getOwnPropertyNames(it){
  var names  = gOPN(_toIobject(it))
    , result = []
    , i      = 0
    , key;
  while(names.length > i){
    if(!_has(AllSymbols, key = names[i++]) && key != HIDDEN && key != META)result.push(key);
  } return result;
};
var $getOwnPropertySymbols = function getOwnPropertySymbols(it){
  var IS_OP  = it === ObjectProto$1
    , names  = gOPN(IS_OP ? OPSymbols : _toIobject(it))
    , result = []
    , i      = 0
    , key;
  while(names.length > i){
    if(_has(AllSymbols, key = names[i++]) && (IS_OP ? _has(ObjectProto$1, key) : true))result.push(AllSymbols[key]);
  } return result;
};

// 19.4.1.1 Symbol([description])
if(!USE_NATIVE){
  $Symbol = function Symbol(){
    if(this instanceof $Symbol)throw TypeError('Symbol is not a constructor!');
    var tag = _uid(arguments.length > 0 ? arguments[0] : undefined);
    var $set = function(value){
      if(this === ObjectProto$1)$set.call(OPSymbols, value);
      if(_has(this, HIDDEN) && _has(this[HIDDEN], tag))this[HIDDEN][tag] = false;
      setSymbolDesc(this, tag, _propertyDesc(1, value));
    };
    if(_descriptors && setter)setSymbolDesc(ObjectProto$1, tag, {configurable: true, set: $set});
    return wrap(tag);
  };
  _redefine($Symbol[PROTOTYPE$2], 'toString', function toString(){
    return this._k;
  });

  _objectGopd.f = $getOwnPropertyDescriptor;
  _objectDp.f   = $defineProperty;
  _objectGopn.f = _objectGopnExt.f = $getOwnPropertyNames;
  _objectPie.f  = $propertyIsEnumerable;
  _objectGops.f = $getOwnPropertySymbols;

  if(_descriptors && !_library){
    _redefine(ObjectProto$1, 'propertyIsEnumerable', $propertyIsEnumerable, true);
  }

  _wksExt.f = function(name){
    return wrap(_wks(name));
  };
}

_export(_export.G + _export.W + _export.F * !USE_NATIVE, {Symbol: $Symbol});

for(var symbols = (
  // 19.4.2.2, 19.4.2.3, 19.4.2.4, 19.4.2.6, 19.4.2.8, 19.4.2.9, 19.4.2.10, 19.4.2.11, 19.4.2.12, 19.4.2.13, 19.4.2.14
  'hasInstance,isConcatSpreadable,iterator,match,replace,search,species,split,toPrimitive,toStringTag,unscopables'
).split(','), i$1 = 0; symbols.length > i$1; )_wks(symbols[i$1++]);

for(var symbols = _objectKeys(_wks.store), i$1 = 0; symbols.length > i$1; )_wksDefine(symbols[i$1++]);

_export(_export.S + _export.F * !USE_NATIVE, 'Symbol', {
  // 19.4.2.1 Symbol.for(key)
  'for': function(key){
    return _has(SymbolRegistry, key += '')
      ? SymbolRegistry[key]
      : SymbolRegistry[key] = $Symbol(key);
  },
  // 19.4.2.5 Symbol.keyFor(sym)
  keyFor: function keyFor(key){
    if(isSymbol(key))return _keyof(SymbolRegistry, key);
    throw TypeError(key + ' is not a symbol!');
  },
  useSetter: function(){ setter = true; },
  useSimple: function(){ setter = false; }
});

_export(_export.S + _export.F * !USE_NATIVE, 'Object', {
  // 19.1.2.2 Object.create(O [, Properties])
  create: $create,
  // 19.1.2.4 Object.defineProperty(O, P, Attributes)
  defineProperty: $defineProperty,
  // 19.1.2.3 Object.defineProperties(O, Properties)
  defineProperties: $defineProperties,
  // 19.1.2.6 Object.getOwnPropertyDescriptor(O, P)
  getOwnPropertyDescriptor: $getOwnPropertyDescriptor,
  // 19.1.2.7 Object.getOwnPropertyNames(O)
  getOwnPropertyNames: $getOwnPropertyNames,
  // 19.1.2.8 Object.getOwnPropertySymbols(O)
  getOwnPropertySymbols: $getOwnPropertySymbols
});

// 24.3.2 JSON.stringify(value [, replacer [, space]])
$JSON && _export(_export.S + _export.F * (!USE_NATIVE || _fails(function(){
  var S = $Symbol();
  // MS Edge converts symbol values to JSON as {}
  // WebKit converts symbol values to JSON as null
  // V8 throws on boxed symbols
  return _stringify([S]) != '[null]' || _stringify({a: S}) != '{}' || _stringify(Object(S)) != '{}';
})), 'JSON', {
  stringify: function stringify(it){
    if(it === undefined || isSymbol(it))return; // IE8 returns string on undefined
    var args = [it]
      , i    = 1
      , replacer, $replacer;
    while(arguments.length > i)args.push(arguments[i++]);
    replacer = args[1];
    if(typeof replacer == 'function')$replacer = replacer;
    if($replacer || !_isArray(replacer))replacer = function(key, value){
      if($replacer)value = $replacer.call(this, key, value);
      if(!isSymbol(value))return value;
    };
    args[1] = replacer;
    return _stringify.apply($JSON, args);
  }
});

// 19.4.3.4 Symbol.prototype[@@toPrimitive](hint)
$Symbol[PROTOTYPE$2][TO_PRIMITIVE] || _hide($Symbol[PROTOTYPE$2], TO_PRIMITIVE, $Symbol[PROTOTYPE$2].valueOf);
// 19.4.3.5 Symbol.prototype[@@toStringTag]
_setToStringTag($Symbol, 'Symbol');
// 20.2.1.9 Math[@@toStringTag]
_setToStringTag(Math, 'Math', true);
// 24.3.3 JSON[@@toStringTag]
_setToStringTag(_global.JSON, 'JSON', true);

_wksDefine('asyncIterator');

_wksDefine('observable');

var symbol$2 = _core.Symbol;

var symbol = createCommonjsModule(function (module) {
module.exports = { "default": symbol$2, __esModule: true };
});

unwrapExports(symbol);

var _typeof_1 = createCommonjsModule(function (module, exports) {
exports.__esModule = true;



var _iterator2 = _interopRequireDefault(iterator);



var _symbol2 = _interopRequireDefault(symbol);

var _typeof = typeof _symbol2.default === "function" && typeof _iterator2.default === "symbol" ? function (obj) { return typeof obj; } : function (obj) { return obj && typeof _symbol2.default === "function" && obj.constructor === _symbol2.default && obj !== _symbol2.default.prototype ? "symbol" : typeof obj; };

function _interopRequireDefault(obj) { return obj && obj.__esModule ? obj : { default: obj }; }

exports.default = typeof _symbol2.default === "function" && _typeof(_iterator2.default) === "symbol" ? function (obj) {
  return typeof obj === "undefined" ? "undefined" : _typeof(obj);
} : function (obj) {
  return obj && typeof _symbol2.default === "function" && obj.constructor === _symbol2.default && obj !== _symbol2.default.prototype ? "symbol" : typeof obj === "undefined" ? "undefined" : _typeof(obj);
};
});

unwrapExports(_typeof_1);

var possibleConstructorReturn = createCommonjsModule(function (module, exports) {
exports.__esModule = true;



var _typeof3 = _interopRequireDefault(_typeof_1);

function _interopRequireDefault(obj) { return obj && obj.__esModule ? obj : { default: obj }; }

exports.default = function (self, call) {
  if (!self) {
    throw new ReferenceError("this hasn't been initialised - super() hasn't been called");
  }

  return call && ((typeof call === "undefined" ? "undefined" : (0, _typeof3.default)(call)) === "object" || typeof call === "function") ? call : self;
};
});

var _possibleConstructorReturn = unwrapExports(possibleConstructorReturn);

// Works with __proto__ only. Old v8 can't work with null proto objects.
/* eslint-disable no-proto */

var check$1 = function(O, proto){
  _anObject(O);
  if(!_isObject(proto) && proto !== null)throw TypeError(proto + ": can't set as prototype!");
};
var _setProto = {
  set: Object.setPrototypeOf || ('__proto__' in {} ? // eslint-disable-line
    function(test, buggy, set){
      try {
        set = _ctx(Function.call, _objectGopd.f(Object.prototype, '__proto__').set, 2);
        set(test, []);
        buggy = !(test instanceof Array);
      } catch(e){ buggy = true; }
      return function setPrototypeOf(O, proto){
        check$1(O, proto);
        if(buggy)O.__proto__ = proto;
        else set(O, proto);
        return O;
      };
    }({}, false) : undefined),
  check: check$1
};

// 19.1.3.19 Object.setPrototypeOf(O, proto)

_export(_export.S, 'Object', {setPrototypeOf: _setProto.set});

var setPrototypeOf$2 = _core.Object.setPrototypeOf;

var setPrototypeOf = createCommonjsModule(function (module) {
module.exports = { "default": setPrototypeOf$2, __esModule: true };
});

unwrapExports(setPrototypeOf);

// 19.1.2.2 / 15.2.3.5 Object.create(O [, Properties])
_export(_export.S, 'Object', {create: _objectCreate});

var $Object$1 = _core.Object;
var create$2 = function create(P, D){
  return $Object$1.create(P, D);
};

var create = createCommonjsModule(function (module) {
module.exports = { "default": create$2, __esModule: true };
});

unwrapExports(create);

var inherits = createCommonjsModule(function (module, exports) {
exports.__esModule = true;



var _setPrototypeOf2 = _interopRequireDefault(setPrototypeOf);



var _create2 = _interopRequireDefault(create);



var _typeof3 = _interopRequireDefault(_typeof_1);

function _interopRequireDefault(obj) { return obj && obj.__esModule ? obj : { default: obj }; }

exports.default = function (subClass, superClass) {
  if (typeof superClass !== "function" && superClass !== null) {
    throw new TypeError("Super expression must either be null or a function, not " + (typeof superClass === "undefined" ? "undefined" : (0, _typeof3.default)(superClass)));
  }

  subClass.prototype = (0, _create2.default)(superClass && superClass.prototype, {
    constructor: {
      value: subClass,
      enumerable: false,
      writable: true,
      configurable: true
    }
  });
  if (superClass) _setPrototypeOf2.default ? (0, _setPrototypeOf2.default)(subClass, superClass) : subClass.__proto__ = superClass;
};
});

var _inherits = unwrapExports(inherits);

var bitset = createCommonjsModule(function (module, exports) {
/**
 * @license BitSet.js v5.0.3 4/3/2018
 * http://www.xarg.org/2014/03/javascript-bit-array/
 *
 * Copyright (c) 2016, Robert Eisele (robert@xarg.org)
 * Dual licensed under the MIT or GPL Version 2 licenses.
 **/
(function(root) {

  var WORD_LENGTH = 32;

  /**
   * The log base 2 of WORD_LENGTH
   * @const
   * @type number
   */
  var WORD_LOG = 5;

  /**
   * Calculates the number of set bits
   *
   * @param {number} v
   * @returns {number}
   */
  function popCount(v) {

    // Warren, H. (2009). Hacker`s Delight. New York, NY: Addison-Wesley

    v -= ((v >>> 1) & 0x55555555);
    v = (v & 0x33333333) + ((v >>> 2) & 0x33333333);
    return (((v + (v >>> 4) & 0xF0F0F0F) * 0x1010101) >>> 24);
  }

  /**
   * Divide a number in base two by B
   *
   * @param {Array} arr
   * @param {number} B
   * @returns {number}
   */
  function divide(arr, B) {

    var r = 0;

    for (var i = 0; i < arr.length; i++) {
      r *= 2;
      var d = (arr[i] + r) / B | 0;
      r = (arr[i] + r) % B;
      arr[i] = d;
    }
    return r;
  }

  /**
   * Parses the parameters and set variable P
   *
   * @param {Object} P
   * @param {string|BitSet|Array|Uint8Array|number=} val
   */
  function parse(P, val) {

    if (val == null) {
      P['data'] = [0, 0, 0, 0, 0, 0, 0, 0, 0, 0];
      P['_'] = 0;
      return;
    }

    if (val instanceof BitSet) {
      P['data'] = val['data'];
      P['_'] = val['_'];
      return;
    }

    switch (typeof val) {

      case 'number':
        P['data'] = [val | 0];
        P['_'] = 0;
        break;

      case 'string':

        var base = 2;
        var len = WORD_LENGTH;

        if (val.indexOf('0b') === 0) {
          val = val.substr(2);
        } else if (val.indexOf('0x') === 0) {
          val = val.substr(2);
          base = 16;
          len = 8;
        }

        P['data'] = [];
        P['_'] = 0;

        var a = val.length - len;
        var b = val.length;

        do {

          var num = parseInt(val.slice(a > 0 ? a : 0, b), base);

          if (isNaN(num)) {
            throw SyntaxError('Invalid param');
          }

          P['data'].push(num | 0);

          if (a <= 0)
            break;

          a -= len;
          b -= len;
        } while (1);

        break;

      default:

        P['data'] = [0];
        var data = P['data'];

        if (val instanceof Array) {

          for (var i = val.length - 1; i >= 0; i--) {

            var ndx = val[i];

            if (ndx === Infinity) {
              P['_'] = -1;
            } else {
              scale(P, ndx);
              data[ndx >>> WORD_LOG] |= 1 << ndx;
            }
          }
          break;
        }

        if (Uint8Array && val instanceof Uint8Array) {

          var bits = 8;

          scale(P, val.length * bits);

          for (var i = 0; i < val.length; i++) {

            var n = val[i];

            for (var j = 0; j < bits; j++) {

              var k = i * bits + j;

              data[k >>> WORD_LOG] |= (n >> j & 1) << k;
            }
          }
          break;
        }
        throw SyntaxError('Invalid param');
    }
  }

  /**
   * Module entry point
   *
   * @constructor
   * @param {string|BitSet|number=} param
   * @returns {BitSet}
   */
  function BitSet(param) {

    if (!(this instanceof BitSet)) {
      return new BitSet(param);
    }
    parse(this, param);
    this['data'] = this['data'].slice();
  }

  function scale(dst, ndx) {

    var l = ndx >>> WORD_LOG;
    var d = dst['data'];
    var v = dst['_'];

    for (var i = d.length; l >= i; l--) {
      d.push(v);
    }
  }

  var P = {
    'data': [], // Holds the actual bits in form of a 32bit integer array.
    '_': 0 // Holds the MSB flag information to make indefinitely large bitsets inversion-proof
  };

  BitSet.prototype = {
    'data': [],
    '_': 0,
    /**
     * Set a single bit flag
     *
     * Ex:
     * bs1 = new BitSet(10);
     *
     * bs1.set(3, 1);
     *
     * @param {number} ndx The index of the bit to be set
     * @param {number=} value Optional value that should be set on the index (0 or 1)
     * @returns {BitSet} this
     */
    'set': function(ndx, value) {

      ndx |= 0;

      scale(this, ndx);

      if (value === undefined || value) {
        this['data'][ndx >>> WORD_LOG] |= (1 << ndx);
      } else {
        this['data'][ndx >>> WORD_LOG] &= ~(1 << ndx);
      }
      return this;
    },
    /**
     * Get a single bit flag of a certain bit position
     *
     * Ex:
     * bs1 = new BitSet();
     * var isValid = bs1.get(12);
     *
     * @param {number} ndx the index to be fetched
     * @returns {number|null} The binary flag
     */
    'get': function(ndx) {

      ndx |= 0;

      var d = this['data'];
      var n = ndx >>> WORD_LOG;

      if (n > d.length) {
        return this['_'] & 1;
      }
      return (d[n] >>> ndx) & 1;
    },
    /**
     * Creates the bitwise NOT of a set. The result is stored in-place.
     *
     * Ex:
     * bs1 = new BitSet(10);
     *
     * bs1.not();
     *
     * @returns {BitSet} this
     */
    'not': function() { // invert()

      var t = this['clone']();
      var d = t['data'];
      for (var i = 0; i < d.length; i++) {
        d[i] = ~d[i];
      }

      t['_'] = ~t['_'];

      return t;
    },
    /**
     * Creates the bitwise AND of two sets. The result is stored in-place.
     *
     * Ex:
     * bs1 = new BitSet(10);
     * bs2 = new BitSet(10);
     *
     * bs1.and(bs2);
     *
     * @param {BitSet} value A bitset object
     * @returns {BitSet} this
     */
    'and': function(value) {// intersection

      parse(P, value);

      var T = this['clone']();
      var t = T['data'];
      var p = P['data'];

      var pl = p.length;
      var p_ = P['_'];
      var t_ = T['_'];

      // If this is infinite, we need all bits from P
      if (t_ !== 0) {
        scale(T, pl * WORD_LENGTH - 1);
      }

      var tl = t.length;
      var l = Math.min(pl, tl);
      var i = 0;

      for (; i < l; i++) {
        t[i] &= p[i];
      }

      for (; i < tl; i++) {
        t[i] &= p_;
      }

      T['_'] &= p_;

      return T;
    },
    /**
     * Creates the bitwise OR of two sets. The result is stored in-place.
     *
     * Ex:
     * bs1 = new BitSet(10);
     * bs2 = new BitSet(10);
     *
     * bs1.or(bs2);
     *
     * @param {BitSet} val A bitset object
     * @returns {BitSet} this
     */
    'or': function(val) { // union

      parse(P, val);

      var t = this['clone']();
      var d = t['data'];
      var p = P['data'];

      var pl = p.length - 1;
      var tl = d.length - 1;

      var minLength = Math.min(tl, pl);

      // Append backwards, extend array only once
      for (var i = pl; i > minLength; i--) {
        d[i] = p[i];
      }

      for (; i >= 0; i--) {
        d[i] |= p[i];
      }

      t['_'] |= P['_'];

      return t;
    },
    /**
     * Creates the bitwise XOR of two sets. The result is stored in-place.
     *
     * Ex:
     * bs1 = new BitSet(10);
     * bs2 = new BitSet(10);
     *
     * bs1.xor(bs2);
     *
     * @param {BitSet} val A bitset object
     * @returns {BitSet} this
     */
    'xor': function(val) { // symmetric difference

      parse(P, val);

      var t = this['clone']();
      var d = t['data'];
      var p = P['data'];

      var t_ = t['_'];
      var p_ = P['_'];

      var i = 0;

      var tl = d.length - 1;
      var pl = p.length - 1;

      // Cut if tl > pl
      for (i = tl; i > pl; i--) {
        d[i] ^= p_;
      }

      // Cut if pl > tl
      for (i = pl; i > tl; i--) {
        d[i] = t_ ^ p[i];
      }

      // XOR the rest
      for (; i >= 0; i--) {
        d[i] ^= p[i];
      }

      // XOR infinity
      t['_'] ^= p_;

      return t;
    },
    /**
     * Creates the bitwise AND NOT (not confuse with NAND!) of two sets. The result is stored in-place.
     *
     * Ex:
     * bs1 = new BitSet(10);
     * bs2 = new BitSet(10);
     *
     * bs1.notAnd(bs2);
     *
     * @param {BitSet} val A bitset object
     * @returns {BitSet} this
     */
    'andNot': function(val) { // difference

      return this['and'](new BitSet(val)['flip']());
    },
    /**
     * Flip/Invert a range of bits by setting
     *
     * Ex:
     * bs1 = new BitSet();
     * bs1.flip(); // Flip entire set
     * bs1.flip(5); // Flip single bit
     * bs1.flip(3,10); // Flip a bit range
     *
     * @param {number=} from The start index of the range to be flipped
     * @param {number=} to The end index of the range to be flipped
     * @returns {BitSet} this
     */
    'flip': function(from, to) {

      if (from === undefined) {

        var d = this['data'];
        for (var i = 0; i < d.length; i++) {
          d[i] = ~d[i];
        }

        this['_'] = ~this['_'];

      } else if (to === undefined) {

        scale(this, from);

        this['data'][from >>> WORD_LOG] ^= (1 << from);

      } else if (0 <= from && from <= to) {

        scale(this, to);

        for (var i = from; i <= to; i++) {
          this['data'][i >>> WORD_LOG] ^= (1 << i);
        }
      }
      return this;
    },
    /**
     * Clear a range of bits by setting it to 0
     *
     * Ex:
     * bs1 = new BitSet();
     * bs1.clear(); // Clear entire set
     * bs1.clear(5); // Clear single bit
     * bs1.clar(3,10); // Clear a bit range
     *
     * @param {number=} from The start index of the range to be cleared
     * @param {number=} to The end index of the range to be cleared
     * @returns {BitSet} this
     */
    'clear': function(from, to) {

      var data = this['data'];

      if (from === undefined) {

        for (var i = data.length - 1; i >= 0; i--) {
          data[i] = 0;
        }
        this['_'] = 0;

      } else if (to === undefined) {

        from |= 0;

        scale(this, from);

        data[from >>> WORD_LOG] &= ~(1 << from);

      } else if (from <= to) {

        scale(this, to);

        for (var i = from; i <= to; i++) {
          data[i >>> WORD_LOG] &= ~(1 << i);
        }
      }
      return this;
    },
    /**
     * Gets an entire range as a new bitset object
     *
     * Ex:
     * bs1 = new BitSet();
     * bs1.slice(4, 8);
     *
     * @param {number=} from The start index of the range to be get
     * @param {number=} to The end index of the range to be get
     * @returns {BitSet|Object} A new smaller bitset object, containing the extracted range
     */
    'slice': function(from, to) {

      if (from === undefined) {
        return this['clone']();
      } else if (to === undefined) {

        to = this['data'].length * WORD_LENGTH;

        var im = Object.create(BitSet.prototype);

        im['_'] = this['_'];
        im['data'] = [0];

        for (var i = from; i <= to; i++) {
          im['set'](i - from, this['get'](i));
        }
        return im;

      } else if (from <= to && 0 <= from) {

        var im = Object.create(BitSet.prototype);
        im['data'] = [0];

        for (var i = from; i <= to; i++) {
          im['set'](i - from, this['get'](i));
        }
        return im;
      }
      return null;
    },
    /**
     * Set a range of bits
     *
     * Ex:
     * bs1 = new BitSet();
     *
     * bs1.setRange(10, 15, 1);
     *
     * @param {number} from The start index of the range to be set
     * @param {number} to The end index of the range to be set
     * @param {number} value Optional value that should be set on the index (0 or 1)
     * @returns {BitSet} this
     */
    'setRange': function(from, to, value) {

      for (var i = from; i <= to; i++) {
        this['set'](i, value);
      }
      return this;
    },
    /**
     * Clones the actual object
     *
     * Ex:
     * bs1 = new BitSet(10);
     * bs2 = bs1.clone();
     *
     * @returns {BitSet|Object} A new BitSet object, containing a copy of the actual object
     */
    'clone': function() {

      var im = Object.create(BitSet.prototype);
      im['data'] = this['data'].slice();
      im['_'] = this['_'];

      return im;
    },
    /**
     * Gets a list of set bits
     *
     * @returns {Array|number}
     */
    'toArray': Math['clz32'] ?
            function() {

              var ret = [];
              var data = this['data'];

              for (var i = data.length - 1; i >= 0; i--) {

                var num = data[i];

                while (num !== 0) {
                  var t = 31 - Math['clz32'](num);
                  num ^= 1 << t;
                  ret.unshift((i * WORD_LENGTH) + t);
                }
              }

              if (this['_'] !== 0)
                ret.push(Infinity);

              return ret;
            } :
            function() {

              var ret = [];
              var data = this['data'];

              for (var i = 0; i < data.length; i++) {

                var num = data[i];

                while (num !== 0) {
                  var t = num & -num;
                  num ^= t;
                  ret.push((i * WORD_LENGTH) + popCount(t - 1));
                }
              }

              if (this['_'] !== 0)
                ret.push(Infinity);

              return ret;
            },
    /**
     * Overrides the toString method to get a binary representation of the BitSet
     *
     * @param {number=} base
     * @returns string A binary string
     */
    'toString': function(base) {

      var data = this['data'];

      if (!base)
        base = 2;

      // If base is power of two
      if ((base & (base - 1)) === 0 && base < 36) {

        var ret = '';
        var len = 2 + Math.log(4294967295/*Math.pow(2, WORD_LENGTH)-1*/) / Math.log(base) | 0;

        for (var i = data.length - 1; i >= 0; i--) {

          var cur = data[i];

          // Make the number unsigned
          if (cur < 0)
            cur += 4294967296 /*Math.pow(2, WORD_LENGTH)*/;

          var tmp = cur.toString(base);

          if (ret !== '') {
            // Fill small positive numbers with leading zeros. The +1 for array creation is added outside already
            ret += '0'.repeat(len - tmp.length - 1);
          }
          ret += tmp;
        }

        if (this['_'] === 0) {

          ret = ret.replace(/^0+/, '');

          if (ret === '')
            ret = '0';
          return ret;

        } else {
          // Pad the string with ones
          ret = '1111' + ret;
          return ret.replace(/^1+/, '...1111');
        }

      } else {

        if ((2 > base || base > 36))
          throw SyntaxError('Invalid base');

        var ret = [];
        var arr = [];

        // Copy every single bit to a new array
        for (var i = data.length; i--; ) {

          for (var j = WORD_LENGTH; j--; ) {

            arr.push(data[i] >>> j & 1);
          }
        }

        do {
          ret.unshift(divide(arr, base).toString(base));
        } while (!arr.every(function(x) {
          return x === 0;
        }));

        return ret.join('');
      }
    },
    /**
     * Check if the BitSet is empty, means all bits are unset
     *
     * Ex:
     * bs1 = new BitSet(10);
     *
     * bs1.isEmpty() ? 'yes' : 'no'
     *
     * @returns {boolean} Whether the bitset is empty
     */
    'isEmpty': function() {

      if (this['_'] !== 0)
        return false;

      var d = this['data'];

      for (var i = d.length - 1; i >= 0; i--) {
        if (d[i] !== 0)
          return false;
      }
      return true;
    },
    /**
     * Calculates the number of bits set
     *
     * Ex:
     * bs1 = new BitSet(10);
     *
     * var num = bs1.cardinality();
     *
     * @returns {number} The number of bits set
     */
    'cardinality': function() {

      if (this['_'] !== 0) {
        return Infinity;
      }

      var s = 0;
      var d = this['data'];
      for (var i = 0; i < d.length; i++) {
        var n = d[i];
        if (n !== 0)
          s += popCount(n);
      }
      return s;
    },
    /**
     * Calculates the Most Significant Bit / log base two
     *
     * Ex:
     * bs1 = new BitSet(10);
     *
     * var logbase2 = bs1.msb();
     *
     * var truncatedTwo = Math.pow(2, logbase2); // May overflow!
     *
     * @returns {number} The index of the highest bit set
     */
    'msb': Math['clz32'] ?
            function() {

              if (this['_'] !== 0) {
                return Infinity;
              }

              var data = this['data'];

              for (var i = data.length; i-- > 0; ) {

                var c = Math['clz32'](data[i]);

                if (c !== WORD_LENGTH) {
                  return (i * WORD_LENGTH) + WORD_LENGTH - 1 - c;
                }
              }
              return Infinity;
            } :
            function() {

              if (this['_'] !== 0) {
                return Infinity;
              }

              var data = this['data'];

              for (var i = data.length; i-- > 0; ) {

                var v = data[i];
                var c = 0;

                if (v) {

                  for (; (v >>>= 1) > 0; c++) {
                  }
                  return (i * WORD_LENGTH) + c;
                }
              }
              return Infinity;
            },
    /**
     * Calculates the number of trailing zeros
     *
     * Ex:
     * bs1 = new BitSet(10);
     *
     * var ntz = bs1.ntz();
     *
     * @returns {number} The index of the lowest bit set
     */
    'ntz': function() {

      var data = this['data'];

      for (var j = 0; j < data.length; j++) {
        var v = data[j];

        if (v !== 0) {

          v = (v ^ (v - 1)) >>> 1; // Set v's trailing 0s to 1s and zero rest

          return (j * WORD_LENGTH) + popCount(v);
        }
      }
      return Infinity;
    },
    /**
     * Calculates the Least Significant Bit
     *
     * Ex:
     * bs1 = new BitSet(10);
     *
     * var lsb = bs1.lsb();
     *
     * @returns {number} The index of the lowest bit set
     */
    'lsb': function() {

      var data = this['data'];

      for (var i = 0; i < data.length; i++) {

        var v = data[i];
        var c = 0;

        if (v) {

          var bit = (v & -v);

          for (; (bit >>>= 1); c++) {

          }
          return WORD_LENGTH * i + c;
        }
      }
      return this['_'] & 1;
    },
    /**
     * Compares two BitSet objects
     *
     * Ex:
     * bs1 = new BitSet(10);
     * bs2 = new BitSet(10);
     *
     * bs1.equals(bs2) ? 'yes' : 'no'
     *
     * @param {BitSet} val A bitset object
     * @returns {boolean} Whether the two BitSets are similar
     */
    'equals': function(val) {

      parse(P, val);

      var t = this['data'];
      var p = P['data'];

      var t_ = this['_'];
      var p_ = P['_'];

      var tl = t.length - 1;
      var pl = p.length - 1;

      if (p_ !== t_) {
        return false;
      }

      var minLength = tl < pl ? tl : pl;
      var i = 0;

      for (; i <= minLength; i++) {
        if (t[i] !== p[i])
          return false;
      }

      for (i = tl; i > pl; i--) {
        if (t[i] !== p_)
          return false;
      }

      for (i = pl; i > tl; i--) {
        if (p[i] !== t_)
          return false;
      }
      return true;
    }
  };

  BitSet['fromBinaryString'] = function(str) {

    return new BitSet('0b' + str);
  };

  BitSet['fromHexString'] = function(str) {

    return new BitSet('0x' + str);
  };

  BitSet['Random'] = function(n) {

    if (n === undefined || n < 0) {
      n = WORD_LENGTH;
    }

    var m = n % WORD_LENGTH;

    // Create an array, large enough to hold the random bits
    var t = [];
    var len = Math.ceil(n / WORD_LENGTH);

    // Create an bitset instance
    var s = Object.create(BitSet.prototype);

    // Fill the vector with random data, uniformally distributed
    for (var i = 0; i < len; i++) {
      t.push(Math.random() * 4294967296 | 0);
    }

    // Mask out unwanted bits
    if (m > 0) {
      t[len - 1] &= (1 << m) - 1;
    }

    s['data'] = t;
    s['_'] = 0;
    return s;
  };

  if (typeof undefined === 'function' && undefined['amd']) {
    undefined([], function() {
      return BitSet;
    });
  } else {
    Object.defineProperty(exports, "__esModule", {'value': true});
    BitSet['default'] = BitSet;
    BitSet['BitSet'] = BitSet;
    module['exports'] = BitSet;
  }

})(commonjsGlobal);
});

var BitSet = unwrapExports(bitset);

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
  var first = bitMap.ntz();
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
var BitMapTree =

/**
 * Creates a BitMapTree, this is an abstract class.
 * It is not meant to by directly instantiated.
 */
function BitMapTree(blockSize, shrink, begin, depth, bitMap) {
  _classCallCheck(this, BitMapTree);

  this.blockSize = blockSize;
  this.shrink = shrink;
  this.begin = begin;
  this.depth = depth;
  this.bitMap = bitMap || createBitMap(blockSize);
};



/**
 * Class representing a Leaf of the recursive bitmap tree.
 * This represents the base case of the lazy recursive bitmap tree.
 */

var Leaf = function (_BitMapTree) {
  _inherits(Leaf, _BitMapTree);

  /**
   * Creates a Leaf
   */
  function Leaf(blockSize, shrink, begin, bitMap) {
    _classCallCheck(this, Leaf);

    return _possibleConstructorReturn(this, (Leaf.__proto__ || _Object$getPrototypeOf(Leaf)).call(this, blockSize, shrink, begin, 0, bitMap));
  }

  /**
   * Allocates a counter and sets the corresponding bit for the bitmap.
   * It will lazily grow parents.
   */


  _createClass(Leaf, [{
    key: 'allocate',
    value: function allocate(counter, callback, snapshot) {
      var index = void 0;
      if (counter == null) {
        index = firstUnset(this.bitMap);
      } else {
        index = counter - this.begin;
      }
      if (index !== null && index < this.blockSize) {
        if (!isSet(this.bitMap, index)) {
          var bitMapNew = void 0;
          var treeNew = void 0;
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
        var _treeNew = new Node(this.blockSize, this.shrink, this.begin, this.depth + 1);
        if (snapshot) {
          snapshot.add(_treeNew);
          snapshot.add(_treeNew.bitMap);
        }
        _treeNew.bitMapTrees[0] = this;
        if (allSet(this.bitMap)) {
          setBit(_treeNew.bitMap, 0);
        }
        _treeNew.allocate(counter, callback, snapshot);
      }
    }

    /**
     * Deallocates a counter and unsets the corresponding bit for the bitmap.
     */

  }, {
    key: 'deallocate',
    value: function deallocate(counter, callback, snapshot) {
      var index = counter - this.begin;
      if (index >= 0 && index < this.blockSize) {
        if (isSet(this.bitMap, index)) {
          var bitMapNew = void 0;
          var treeNew = void 0;
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

  }, {
    key: 'check',
    value: function check(counter, callback) {
      var index = counter - this.begin;
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
  }]);

  return Leaf;
}(BitMapTree);



/**
 * Class representing a Node of the recursive bitmap tree.
 */

var Node = function (_BitMapTree2) {
  _inherits(Node, _BitMapTree2);

  /**
   * Creates a Node
   */
  function Node(blockSize, shrink, begin, depth, bitMap, bitMapTrees) {
    _classCallCheck(this, Node);

    var _this2 = _possibleConstructorReturn(this, (Node.__proto__ || _Object$getPrototypeOf(Node)).call(this, blockSize, shrink, begin, depth, bitMap));

    _this2.bitMapTrees = bitMapTrees || [];
    return _this2;
  }

  /**
   * Allocates a counter by allocating the corresponding child.
   * Passes a continuation to the child allocate that will
   * set the current bitmap if the child bitmap is now all set.
   * It will also lazily create the children or parents as necessary.
   */


  _createClass(Node, [{
    key: 'allocate',
    value: function allocate(counter, callback, snapshot) {
      var _this3 = this;

      var index = void 0;
      if (counter == null) {
        index = firstUnset(this.bitMap);
      } else {
        index = Math.floor((counter - this.begin) / Math.pow(this.blockSize, this.depth));
      }
      if (index != null && this.bitMapTrees[index]) {
        var index_ = index; // fix the non-null value
        this.bitMapTrees[index].allocate(counter, function (_ref) {
          var counter = _ref.counter,
              changed = _ref.changed,
              bitMapChild = _ref.bitMap,
              treeChild = _ref.tree;

          var bitMapNew = _this3.bitMap;
          var treeNew = _this3;
          if (changed) {
            if (!snapshot && allSet(bitMapChild)) {
              setBit(bitMapNew, index_);
            } else if (snapshot && snapshot.has(_this3)) {
              if (allSet(bitMapChild)) {
                if (!snapshot.has(_this3.bitMap)) {
                  bitMapNew = _this3.bitMap.clone();
                  snapshot.add(bitMapNew);
                  _this3.bitMap = bitMapNew;
                }
                setBit(bitMapNew, index_);
              }
              treeNew.bitMapTrees[index_] = treeChild;
            } else if (snapshot) {
              if (allSet(bitMapChild)) {
                bitMapNew = _this3.bitMap.clone();
                snapshot.add(bitMapNew);
                setBit(bitMapNew, index_);
              }
              var bitMapTreesNew = _this3.bitMapTrees.slice();
              bitMapTreesNew[index_] = treeChild;
              treeNew = new Node(_this3.blockSize, _this3.shrink, _this3.begin, _this3.depth, bitMapNew, bitMapTreesNew);
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
        var treeNew = new Node(this.blockSize, this.shrink, this.begin, this.depth + 1);
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
        var beginNew = this.begin + index * Math.pow(this.blockSize, this.depth);
        var depthNew = this.depth - 1;
        var treeChild = void 0;
        if (depthNew === 0) {
          treeChild = new Leaf(this.blockSize, this.shrink, beginNew);
        } else {
          treeChild = new Node(this.blockSize, this.shrink, beginNew, depthNew);
        }
        if (snapshot) {
          snapshot.add(treeChild);
          snapshot.add(treeChild.bitMap);
        }
        var _treeNew2 = void 0;
        if (!snapshot || snapshot.has(this)) {
          this.bitMapTrees[index] = treeChild;
          _treeNew2 = this;
        } else {
          var bitMapTreesNew = this.bitMapTrees.slice();
          bitMapTreesNew[index] = treeChild;
          _treeNew2 = new Node(this.blockSize, this.shrink, this.begin, this.depth, this.bitMap, bitMapTreesNew);
          snapshot.add(_treeNew2);
        }
        var _index_ = index; // fix the non-null value
        treeChild.allocate(counter, function (_ref2) {
          var counter = _ref2.counter,
              changed = _ref2.changed,
              bitMapChild = _ref2.bitMap,
              treeChild = _ref2.tree;

          var bitMapNew = _this3.bitMap;
          if (bitMapChild && allSet(bitMapChild)) {
            if (snapshot && !snapshot.has(_this3.bitMap)) {
              bitMapNew = _this3.bitMap.clone();
              snapshot.add(bitMapNew);
              _treeNew2.bitMap = bitMapNew;
            }
            setBit(bitMapNew, _index_);
          }
          callback({
            counter: counter,
            changed: changed,
            bitMap: bitMapNew,
            tree: _treeNew2
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

  }, {
    key: 'deallocate',
    value: function deallocate(counter, callback, snapshot) {
      var _this4 = this;

      var index = Math.floor((counter - this.begin) / Math.pow(this.blockSize, this.depth));
      if (this.bitMapTrees[index]) {
        var allSetPrior = allSet(this.bitMapTrees[index].bitMap);
        this.bitMapTrees[index].deallocate(counter, function (_ref3) {
          var exists = _ref3.exists,
              changed = _ref3.changed,
              bitMapChild = _ref3.bitMap,
              treeChild = _ref3.tree;

          var bitMapNew = _this4.bitMap;
          var treeNew = _this4;
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
              } else if (snapshot && snapshot.has(_this4)) {
                if (allSetPrior) {
                  if (!snapshot.has(_this4.bitMap)) {
                    bitMapNew = _this4.bitMap.clone();
                    snapshot.add(bitMapNew);
                    _this4.bitMap = bitMapNew;
                  }
                  unsetBit(bitMapNew, index);
                }
                treeNew.bitMapTrees[index] = treeChild;
              } else if (snapshot) {
                if (allSetPrior) {
                  bitMapNew = _this4.bitMap.clone();
                  snapshot.add(bitMapNew);
                  unsetBit(bitMapNew, index);
                }
                var bitMapTreesNew = _this4.bitMapTrees.slice();
                bitMapTreesNew[index] = treeChild;
                treeNew = new Node(_this4.blockSize, _this4.shrink, _this4.begin, _this4.depth, bitMapNew, bitMapTreesNew);
                snapshot.add(treeNew);
              }
              if (_this4.shrink && (treeChild instanceof Leaf && allUnset(bitMapChild, _this4.blockSize) || treeChild instanceof Node && _Object$keys(treeChild.bitMapTrees).length === 0)) {
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

  }, {
    key: 'check',
    value: function check(counter, callback) {
      var index = Math.floor((counter - this.begin) / Math.pow(this.blockSize, this.depth));
      if (this.bitMapTrees[index]) {
        this.bitMapTrees[index].check(counter, function (set) {
          callback(set);
        });
      } else {
        callback(null);
      }
    }
  }]);

  return Node;
}(BitMapTree);

/** @module Counter */

/**
 * Class representing allocatable and deallocatable counters.
 * Counters are allocated in sequential manner, this applies to deallocated counters.
 * Once a counter is deallocated, it will be reused on the next allocation.
 * This is a mutable counter, which doesn't use snapshots.
 */

var Counter = function () {

  /**
   * Creates a counter instance.
   * @throws {RangeError} - If blockSize is not a multiple of 32.
   */
  function Counter() {
    var begin = arguments.length > 0 && arguments[0] !== undefined ? arguments[0] : 0;
    var blockSize = arguments.length > 1 && arguments[1] !== undefined ? arguments[1] : 32;
    var shrink = arguments.length > 2 && arguments[2] !== undefined ? arguments[2] : true;
    var tree = arguments[3];

    _classCallCheck(this, Counter);

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


  _createClass(Counter, [{
    key: 'allocate',
    value: function allocate$$1(counter) {
      if (counter != null) {
        if (counter < this._begin) {
          throw new RangeError('counter needs to be greater or equal to the beginning offset');
        }
        counter = counter - this._begin;
      }

      var _allocate2 = allocate(this._tree, counter),
          _allocate3 = _slicedToArray(_allocate2, 3),
          counterAssigned = _allocate3[0],
          changed = _allocate3[1],
          treeNew = _allocate3[2];

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

  }, {
    key: 'deallocate',
    value: function deallocate$$1(counter) {
      var _deallocate2 = deallocate(this._tree, counter - this._begin),
          _deallocate3 = _slicedToArray(_deallocate2, 2),
          changed = _deallocate3[0],
          treeNew = _deallocate3[1];

      this._tree = treeNew;
      return changed;
    }

    /**
     * Checks if a number has been allocated or not.
     */

  }, {
    key: 'check',
    value: function check$$1(counter) {
      return check(this._tree, counter - this._begin);
    }
  }]);

  return Counter;
}();

var _redefineAll = function(target, src, safe){
  for(var key in src){
    if(safe && target[key])target[key] = src[key];
    else _hide(target, key, src[key]);
  } return target;
};

var _anInstance = function(it, Constructor, name, forbiddenField){
  if(!(it instanceof Constructor) || (forbiddenField !== undefined && forbiddenField in it)){
    throw TypeError(name + ': incorrect invocation!');
  } return it;
};

// call something on iterator step with safe closing on error

var _iterCall = function(iterator, fn, value, entries){
  try {
    return entries ? fn(_anObject(value)[0], value[1]) : fn(value);
  // 7.4.6 IteratorClose(iterator, completion)
  } catch(e){
    var ret = iterator['return'];
    if(ret !== undefined)_anObject(ret.call(iterator));
    throw e;
  }
};

// check on default Array iterator
var ITERATOR$3   = _wks('iterator');
var ArrayProto = Array.prototype;

var _isArrayIter = function(it){
  return it !== undefined && (_iterators.Array === it || ArrayProto[ITERATOR$3] === it);
};

var _forOf = createCommonjsModule(function (module) {
var BREAK       = {}
  , RETURN      = {};
var exports = module.exports = function(iterable, entries, fn, that, ITERATOR){
  var iterFn = ITERATOR ? function(){ return iterable; } : core_getIteratorMethod(iterable)
    , f      = _ctx(fn, that, entries ? 2 : 1)
    , index  = 0
    , length, step, iterator, result;
  if(typeof iterFn != 'function')throw TypeError(iterable + ' is not iterable!');
  // fast case for arrays with default iterator
  if(_isArrayIter(iterFn))for(length = _toLength(iterable.length); length > index; index++){
    result = entries ? f(_anObject(step = iterable[index])[0], step[1]) : f(iterable[index]);
    if(result === BREAK || result === RETURN)return result;
  } else for(iterator = iterFn.call(iterable); !(step = iterator.next()).done; ){
    result = _iterCall(iterator, f, step.value, entries);
    if(result === BREAK || result === RETURN)return result;
  }
};
exports.BREAK  = BREAK;
exports.RETURN = RETURN;
});

var SPECIES  = _wks('species');

var _arraySpeciesConstructor = function(original){
  var C;
  if(_isArray(original)){
    C = original.constructor;
    // cross-realm fallback
    if(typeof C == 'function' && (C === Array || _isArray(C.prototype)))C = undefined;
    if(_isObject(C)){
      C = C[SPECIES];
      if(C === null)C = undefined;
    }
  } return C === undefined ? Array : C;
};

// 9.4.2.3 ArraySpeciesCreate(originalArray, length)


var _arraySpeciesCreate = function(original, length){
  return new (_arraySpeciesConstructor(original))(length);
};

// 0 -> Array#forEach
// 1 -> Array#map
// 2 -> Array#filter
// 3 -> Array#some
// 4 -> Array#every
// 5 -> Array#find
// 6 -> Array#findIndex

var _arrayMethods = function(TYPE, $create){
  var IS_MAP        = TYPE == 1
    , IS_FILTER     = TYPE == 2
    , IS_SOME       = TYPE == 3
    , IS_EVERY      = TYPE == 4
    , IS_FIND_INDEX = TYPE == 6
    , NO_HOLES      = TYPE == 5 || IS_FIND_INDEX
    , create        = $create || _arraySpeciesCreate;
  return function($this, callbackfn, that){
    var O      = _toObject($this)
      , self   = _iobject(O)
      , f      = _ctx(callbackfn, that, 3)
      , length = _toLength(self.length)
      , index  = 0
      , result = IS_MAP ? create($this, length) : IS_FILTER ? create($this, 0) : undefined
      , val, res;
    for(;length > index; index++)if(NO_HOLES || index in self){
      val = self[index];
      res = f(val, index, O);
      if(TYPE){
        if(IS_MAP)result[index] = res;            // map
        else if(res)switch(TYPE){
          case 3: return true;                    // some
          case 5: return val;                     // find
          case 6: return index;                   // findIndex
          case 2: result.push(val);               // filter
        } else if(IS_EVERY)return false;          // every
      }
    }
    return IS_FIND_INDEX ? -1 : IS_SOME || IS_EVERY ? IS_EVERY : result;
  };
};

var getWeak           = _meta.getWeak;
var arrayFind         = _arrayMethods(5);
var arrayFindIndex    = _arrayMethods(6);
var id$1                = 0;

// fallback for uncaught frozen keys
var uncaughtFrozenStore = function(that){
  return that._l || (that._l = new UncaughtFrozenStore);
};
var UncaughtFrozenStore = function(){
  this.a = [];
};
var findUncaughtFrozen = function(store, key){
  return arrayFind(store.a, function(it){
    return it[0] === key;
  });
};
UncaughtFrozenStore.prototype = {
  get: function(key){
    var entry = findUncaughtFrozen(this, key);
    if(entry)return entry[1];
  },
  has: function(key){
    return !!findUncaughtFrozen(this, key);
  },
  set: function(key, value){
    var entry = findUncaughtFrozen(this, key);
    if(entry)entry[1] = value;
    else this.a.push([key, value]);
  },
  'delete': function(key){
    var index = arrayFindIndex(this.a, function(it){
      return it[0] === key;
    });
    if(~index)this.a.splice(index, 1);
    return !!~index;
  }
};

var _collectionWeak = {
  getConstructor: function(wrapper, NAME, IS_MAP, ADDER){
    var C = wrapper(function(that, iterable){
      _anInstance(that, C, NAME, '_i');
      that._i = id$1++;      // collection id
      that._l = undefined; // leak store for uncaught frozen objects
      if(iterable != undefined)_forOf(iterable, IS_MAP, that[ADDER], that);
    });
    _redefineAll(C.prototype, {
      // 23.3.3.2 WeakMap.prototype.delete(key)
      // 23.4.3.3 WeakSet.prototype.delete(value)
      'delete': function(key){
        if(!_isObject(key))return false;
        var data = getWeak(key);
        if(data === true)return uncaughtFrozenStore(this)['delete'](key);
        return data && _has(data, this._i) && delete data[this._i];
      },
      // 23.3.3.4 WeakMap.prototype.has(key)
      // 23.4.3.4 WeakSet.prototype.has(value)
      has: function has(key){
        if(!_isObject(key))return false;
        var data = getWeak(key);
        if(data === true)return uncaughtFrozenStore(this).has(key);
        return data && _has(data, this._i);
      }
    });
    return C;
  },
  def: function(that, key, value){
    var data = getWeak(_anObject(key), true);
    if(data === true)uncaughtFrozenStore(that).set(key, value);
    else data[that._i] = value;
    return that;
  },
  ufstore: uncaughtFrozenStore
};

var dP$2             = _objectDp.f;
var each           = _arrayMethods(0);

var _collection = function(NAME, wrapper, methods, common, IS_MAP, IS_WEAK){
  var Base  = _global[NAME]
    , C     = Base
    , ADDER = IS_MAP ? 'set' : 'add'
    , proto = C && C.prototype
    , O     = {};
  if(!_descriptors || typeof C != 'function' || !(IS_WEAK || proto.forEach && !_fails(function(){
    new C().entries().next();
  }))){
    // create collection constructor
    C = common.getConstructor(wrapper, NAME, IS_MAP, ADDER);
    _redefineAll(C.prototype, methods);
    _meta.NEED = true;
  } else {
    C = wrapper(function(target, iterable){
      _anInstance(target, C, NAME, '_c');
      target._c = new Base;
      if(iterable != undefined)_forOf(iterable, IS_MAP, target[ADDER], target);
    });
    each('add,clear,delete,forEach,get,has,set,keys,values,entries,toJSON'.split(','),function(KEY){
      var IS_ADDER = KEY == 'add' || KEY == 'set';
      if(KEY in proto && !(IS_WEAK && KEY == 'clear'))_hide(C.prototype, KEY, function(a, b){
        _anInstance(this, C, KEY);
        if(!IS_ADDER && IS_WEAK && !_isObject(a))return KEY == 'get' ? undefined : false;
        var result = this._c[KEY](a === 0 ? 0 : a, b);
        return IS_ADDER ? this : result;
      });
    });
    if('size' in proto)dP$2(C.prototype, 'size', {
      get: function(){
        return this._c.size;
      }
    });
  }

  _setToStringTag(C, NAME);

  O[NAME] = C;
  _export(_export.G + _export.W + _export.F, O);

  if(!IS_WEAK)common.setStrong(C, NAME, IS_MAP);

  return C;
};

// 23.4 WeakSet Objects
_collection('WeakSet', function(get){
  return function WeakSet(){ return get(this, arguments.length > 0 ? arguments[0] : undefined); };
}, {
  // 23.4.3.1 WeakSet.prototype.add(value)
  add: function add(value){
    return _collectionWeak.def(this, value, true);
  }
}, _collectionWeak, false, true);

var weakSet$1 = _core.WeakSet;

var weakSet = createCommonjsModule(function (module) {
module.exports = { "default": weakSet$1, __esModule: true };
});

var _WeakSet = unwrapExports(weakSet);

/** @module CounterImmutable */

/**
 * Class representing allocatable and deallocatable counters.
 * Counters are allocated in sequential manner, this applies to deallocated counters.
 * Once a counter is deallocated, it will be reused on the next allocation.
 * This is an immutable counter. It will return a new counter on mutation.
 */
var CounterImmutable = function () {

  /**
   * Creates a counter instance.
   * @throws {RangeError} - If blockSize is not a multiple of 32.
   */
  function CounterImmutable() {
    var begin = arguments.length > 0 && arguments[0] !== undefined ? arguments[0] : 0;
    var blockSize = arguments.length > 1 && arguments[1] !== undefined ? arguments[1] : 32;
    var shrink = arguments.length > 2 && arguments[2] !== undefined ? arguments[2] : true;
    var tree = arguments[3];

    _classCallCheck(this, CounterImmutable);

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


  _createClass(CounterImmutable, [{
    key: 'allocate',
    value: function allocate$$1(counter) {
      if (counter != null) {
        if (counter < this._begin) {
          throw new RangeError('counter needs to be greater or equal to the beginning offset');
        }
        counter = counter - this._begin;
      }

      var _allocate2 = allocate(this._tree, counter, new _WeakSet()),
          _allocate3 = _slicedToArray(_allocate2, 3),
          counterAssigned = _allocate3[0],
          changed = _allocate3[1],
          treeNew = _allocate3[2];

      var counterNew = void 0;
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

  }, {
    key: 'deallocate',
    value: function deallocate$$1(counter) {
      var _deallocate2 = deallocate(this._tree, counter - this._begin, new _WeakSet()),
          _deallocate3 = _slicedToArray(_deallocate2, 2),
          changed = _deallocate3[0],
          treeNew = _deallocate3[1];

      var counterNew = void 0;
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

  }, {
    key: 'check',
    value: function check$$1(counter) {
      return check(this._tree, counter - this._begin);
    }

    /**
     * Takes a callback that performs a set of operations.
     * And only returns the new immutable counter at the end of all operations.
     * This is useful if you want to batch up modfications to the counter.
     */

  }, {
    key: 'transaction',
    value: function transaction(callback) {
      var _this = this;

      var snapshot = new _WeakSet();
      var tree = this._tree;
      var changed = false;
      var counterTransaction = {
        allocate: function allocate$$1(counter) {
          if (counter != null) {
            if (counter < _this._begin) {
              throw new RangeError('counter needs to be greater or equal to the beginning offset');
            }
            counter = counter - _this._begin;
          }

          var _allocate4 = allocate(tree, counter, snapshot),
              _allocate5 = _slicedToArray(_allocate4, 3),
              counterAssigned = _allocate5[0],
              changed_ = _allocate5[1],
              treeNew = _allocate5[2];

          changed = changed_;
          tree = treeNew;
          if (counter == null) {
            return counterAssigned + _this._begin;
          } else {
            return changed;
          }
        },
        deallocate: function deallocate$$1(counter) {
          var _deallocate4 = deallocate(tree, counter - _this._begin, snapshot),
              _deallocate5 = _slicedToArray(_deallocate4, 2),
              changed_ = _deallocate5[0],
              treeNew = _deallocate5[1];

          changed = changed_;
          tree = treeNew;
          return changed;
        },
        check: function check$$1(counter) {
          return check(tree, counter - _this._begin);
        }
      };
      callback(counterTransaction);
      if (changed) {
        return new CounterImmutable(this._begin, this._blockSize, this._shrink, tree);
      } else {
        return this;
      }
    }
  }]);

  return CounterImmutable;
}();

exports['default'] = Counter;
exports.CounterImmutable = CounterImmutable;

Object.defineProperty(exports, '__esModule', { value: true });

})));
