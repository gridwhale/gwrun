# GridLang Language Reference

## Program Types

**Console** -- reads/writes stdin/stdout:
```
print("Hello, world!")
var name = input("What is your name? ")
```

Console programs run top-level statements from top to bottom. When launched
through GridWhale program tools, arguments are available as `System.args`.
When launched as an interactive process through `gw process start`, pass
`PROGRAMID.entryPoint`; GridWhale first initializes the program and then invokes
that entry point.

**GUI** -- uses `UI` library:
```
using UI
var mainCtrl = UI.create("text")
mainCtrl.data = "Hello, world!"
mainCtrl.align = "center middle"
mainCtrl.fontSize = "100pt"
UI.show(mainCtrl)
UI.run()
```

Top bar commands:
```
UI.setCommands([
    [ label="Hello", function command () -> UI.dialog("Hello, world!") ],
])
```

**HTTP Server** -- `entrypoint` functions exposed as endpoints:
```
function Hello (a:String, b:Int32) entrypoint
do
    return Real(a) + b
end
```
URL: `https://gridwhale.com/srv/PROGRAMID/Hello`

Example entry point call shape:
```
function Hello (name:String) entrypoint
do
    return "Hello, " + name + "!"
end
```
Callers pass an object with matching field names, such as:
```
{ "name": "Ada" }
```

### System.args

`System.args` is the argument object supplied by the caller. For a console
program launched with:
```
{ "name": "Ada", "limit": 100 }
```
read fields as:
```
var name = System.args.name
var limit = Int32(System.args.limit)
```
Missing fields evaluate as `null`. Check or default missing fields before use:
```
var name = System.args.name || ""
if name == "" do
    name = input("What is your name? ")
end
```

---

## Variables & Constants

```
var A                    // global, defaults to null
var B = 1                // global with value
def G = 6.6743e-11      // compile-time constant, can reference before definition
```

- Variables inside functions are local.
- Identifiers are **case-insensitive**.
- Cannot reference a variable before its definition (unlike functions).
- `def` constants need no parentheses to access.

---

## Functions

Standard form:
```
function Times2 (x) do
    return x * 2
end
```

Single-expression: `function Times2 (x) -> x * 2`

Anonymous: `fn(x) -> 5*x`

Closures:
```
function MakeTimes (x) -> fn(n) -> x*n
var Times2 = MakeTimes(2)
```

Typed parameters and return:
```
function SquareInt (x:Int32): Int32 -> x * x
```

- `fn` is alias for `function`.
- Pure expression functions: `function foo (x) -> x * 2` (no `do`/`end` needed).
- Multi-statement functions use `do`/`end`.
- Functions can be referenced before declaration.
- Entry points: add `entrypoint` keyword after params (see Program Types).

---

## Control Flow

### If
```
if a > 0 do
    print("OK!")
else if a == 0 do
    print("Zero")
else do
    print("Negative")
end
```

If expression: `var result = if a == 1 -> "OK!" else -> "Bye!"`

### For
```
for i = 1 to 10 do ... end
for i = 1 to 10 step 2 do ... end
for e in myArray do ... end
for i, e in myArray do ... end           // index, element
for k, v in myStruct do ... end          // key, value
for key, row in myTable do ... end
for c in MyEnum do ... end
for i in 1...100 do ... end              // range
```

### While
```
while A > 0 do
    A = A - 1
end
```

**No `break` statement.** Use `return` from a function instead.

For loops that would normally use `break`, wrap the search in a function and
`return`, or use a sentinel variable in the loop condition:
```
var found = false
var i = 0
while i < values.length && !found do
    if values[i] == target do
        found = true
    else do
        i = i + 1
    end
end
```

### Return
```
return value
```

---

## Data Types

### Numbers

Arbitrary precision integers, IEEE 64-bit floats. Auto-converts: `1 / 5` -> `0.2`.

```
4294967296          // big integer
0xFF00              // hex
1_000_000           // underscore separator
1.2345e+2           // float
```

Operators: `+` `-` `*` `/` `%` `^`
Comparison: `==` `!=` `>` `<` `>=` `<=`
Logical operators: `&&` `||` `!`
Division by zero: `10 / 0` -> `nan`

**Conversion functions:**
| Function | Behavior on failure |
|---|---|
| `Int32(x)` | returns 0 |
| `IntIP(x)` | infinite precision integer |
| `Float(x)` | returns nan |
| `Integer(x)` | returns null |
| `Real(x)` | returns null |
| `Number(x)` | most appropriate type |

`Int32(x)` returning 0 can be ambiguous: `Int32("0")` and `Int32("bad")` both
produce 0. Validate input separately when 0 is a meaningful value.

**Math:** `abs(x)`, `max(...)`, `min(...)`, `random()`, `random(from, to)`, `round(x)`, `sign(x)`

### Strings

Double-quoted. Escape quotes by doubling: `"This ""word"" is quoted"`.

Block literals (preserves line breaks):
```
var s = """
multi
line
"""
```

**Subscript:** `s[1]`, `s[-1]`, `s[[1,2,3]]` (slice by indices)
**Concatenation:** `+`

**Formatting:** `String(value)`, `String(value, format)` -- Excel-like format strings.
```
String(17, "0###")                       // "0017"
String(1776, "#,###")                    // "1,776"
String(now, "mm/dd/yyyy")
String(now, "hh:mm:ss AM/PM")
```

**Template:** `"{0} + {1} = {2}".format(2, 2, 4)`

### Console Input

`input(prompt)` writes `prompt` to the console and returns the line of text
entered by the user as a `String`. Blank input returns an empty string. In
remote process mode, a pending `input()` appears in `gw process view` as
`INPUT` with a `prompt`, `type`, and `seq`; pass that `seq` to
`gw process input`.

**Key methods:**
- `.length`, `.find(s)`, `.startsWith(s)`, `.endsWith(s)`
- `.lowercased()`, `.uppercased()`, `.cleaned()`
- `.sliced(pos)`, `.sliced(start, end)`
- `.split()`, `.split(",")`, `.split([type="line"])`
- `.left(n)`, `.right(n)`, `.repeated(n)`, `.edited(pos, string)`
- `Byte(...)`, `Char(...)` -- construct from byte/codepoint values

### Booleans

`true` and `false` are **not integers**. `true == 1` -> false. `1 + true` -> nan.

### Null

`null` means undefined, treated as false. Operations on null have inconsistent behavior -- check before using.

### Arrays

```
var a = [1, 2, 3, 4, 5]
a[0]                     // 1
a[-1]                    // last element
a[[1,2]]                 // slice: [2, 3]
a[1...3]                 // range slice: [2, 3, 4]
```

Fixed-size: `var a[5] = [1,2,3,4,5]`
Multidimensional: `var a[3,3] = [[1,2,3],[4,5,6],[7,8,9]]`
Typed: `var a:array of String`

**Operators:** `+` `-` `*` `/` work element-wise.
**Concatenate:** `[1,2] & [3,4]` -> `[1,2,3,4]`
**Contains:** `value in array`

**Constructors:**
```
Array(n)                                 // n nulls
Array(n, fn(i) -> i*2)                   // computed
Array([from=1, to=5])                    // range-based
```

**Key methods:**
- `.append(elem)`, `.remove(value)`, `.removePos(i)`, `.insertPos(i, elem)`
- `.filter(fn)`, `.map(fn)`, `.reduce(fn)`, `.reduce(Aggregation.sum)`
- `.sorted()`, `.sorted(-1)` (descending), `.shuffled()`, `.unique()`
- `.find(elem)`, `.findAll(elem)`, `.findMax()`, `.findMin()`
- `.sum()`, `.average()`, `.max()`, `.min()`, `.stats()`
- `.sliced(pos)`, `.joined(",")`, `.grouped(fn)`, `.indexed(fn)`
- `.intersect(a)`, `.union(a)`, `.except(a)`, `.diff(original)`
- `.length`, `.dimensions`, `.shape`, `.keys`

### Structures

Named-key ordered collections (like records/objects):

```
var s = [ foo=10, bar="testing" ]
s.foo                    // 10
s["foo"]                 // 10
s[0]                     // alphabetical order access
s[["foo","bar"]]         // multiple fields as array
s[[ foo= ]]             // struct index: subset
```

Function values in structs:
```
var s = [ function times2 (x) -> x * 2 ]
```

**Methods:** `.append(key, value)`, `.at(key)`, `.copy()`, `.removeAt(key)`, `.concat(struct)`

### Dictionaries

Arbitrary-key maps (unlike structs which are string-keyed):

```
var d = Dictionary([
    [ DateTime(14,3,2024), "Pi Day" ],
    [ DateTime(15,3,2024), "Ides of March" ],
])
d[DateTime(14,3,2024)]                  // "Pi Day"
```

**Methods:** `.append(k,v)`, `.at(key)`, `.find(value)`, `.removeAt(key)`, `.copy()`

### Enumerations

```
enum CarColor [White, Black, Red, Silver, Blue]
var c = CarColor.Red
```

With labels: `enum Colors [ White as "Pearl White", Black as "Midnight Black" ]`
Ordinal: `Integer(Colors.White)` -> 0
Iterate: `for c in CarColor do ... end`

### Ranges

```
var r = 1...100
r.contains(50)           // true
57 in r                  // true
```

Properties: `.length`, `.start`, `.end`, `.step`
Methods: `.reversed()`

### Binary

```
Binary(base64String)
Binary(hexDigits, "hex")
```

Methods: `.getAs(Int32, offset)`, `.setAs(Int32, offset, value)`, `.hex()`, `.hashed()`

### DateTime

```
var now = DateTime()
var specific = DateTime(day, month, year)
```

Properties: `.day`, `.month`, `.year`, `.hour`, `.minute`, `.second`
Format: `String(now, "mm/dd/yyyy hh:mm:ss AM/PM")`

### TimeSpan

```
var dur = TimeSpan(10000)                // 10 seconds (milliseconds)
var diff = DateTime() - someDate         // returns TimeSpan
var yesterday = DateTime() - TimeSpan(24*60*60*1000)
```

Properties: `.days`, `.seconds`, `.milliseconds`

---

## Schemas

Define typed record structures for tables:

```
schema StudentRecord [
    key id:String
    var name:String
    var age:Int32
    var major:String as "Major Subject"
]
```

- `key` -- unique key column. `var` -- regular field.
- Labels: `as "Label"`
- Types: `String`, `Int32`, `Float64`, `Number`, `DateTime`, `Bool`, enum types
- `extends` inherits base fields; `isa` checks conformance.
- Constructor: `StudentRecord([ name="John", age=32 ])`
- Dynamic: `Schema(definition)`

---

## Tables

Create with inline or named schema:

```
var students = Table(
    schema [ key ID:String, Name:String, Class:String ],
    [
        [ "S-001", "Abby", "CS 101" ],
        [ "S-005", "Brad", "EE 101" ],
    ]
)
```

Or: `var t = Table(MySchema)`

**Access:**
```
students["S-005"]                        // by key
students[0]                              // by index
students[["Name", "Class"]]             // column subset
```

**Modify:**
```
students["S-005"] = [Class="CS 101"]     // partial update
```

**Column expressions** -- use `.ColName` syntax in filters/sorts:
```
students.filter(.Class == "CS 101")
students.sorted(-.ID)                    // descending
students.summarize([Total=sum(.Amount)])
```

Column expressions use a different expression grammar from normal GridLang
code. Inside table operations such as `.filter(...)`, `.sorted(...)`,
`.groupBy(...)`, and `.summarize(...)`, use SQL-style `and`/`or` and `.column`
syntax. In normal GridLang code, use `&&`, `||`, `!`, and regular variables or
properties.

**Key methods:**
- `.append(row)`, `.removeAt(key)`, `.removePos(i)`, `.setAt(key, row)`
- `.filter(col-expr)`, `.filter(fn(row))`, `.sorted(col-expr)`, `.sorted(-col-expr)`
- `.map(schema, [col=expr])`, `.map(fn(row))` (returns array)
- `.groupBy(col-expr)`, `.summarize([col=expr])`
- `.select(cols, expr)`, `.col(colID)` (column as array)
- `.keyed(colID)`, `.indexed(col-expr)` (returns dictionary)
- `.all(expr)`, `.any(expr)`, `.max(expr)`, `.min(expr)`, `.average(expr)`, `.median(expr)`
- `.first(expr)`, `.atMax(expr)`, `.atMin(expr)`
- `.diff(original)`, `.concatenated(table)`, `.appendColumns(table)`
- `.copy()`, `.sliced(start, end)`, `.unique(expr)`, `.formatted()`
- `.insertColumn(colID)`, `.setColumn(name, values)`, `.makeID()`
- `.length`, `.columns`, `.keys`, `.elementtype`, `.keytype`

---

## Classes

Class body must be enclosed in `[ ]` (preferred) or `begin`/`end`:

```
class MyClass [
    var name:String
    var count:Int32 = 0

    constructor (name) do
        this.name = name
    end

    function hello () do
        this.count = this.count + 1
        print("Hello, " + this.name + "! (call #" + String(this.count) + ")")
    end
]

var obj = MyClass("John")
obj.hello()
```

Methods can also be defined outside the class body:
```
class MyClass [
    var name
    function hello () end
]

function MyClass.hello () do
    print("Hello, " + this.name + "!")
end
```

- Class body **must** use `[ ]` delimiters (or `begin`/`end`).
- Members use `var` keyword, optionally typed with defaults.
- Use `this` to access instance members and methods.
- Can reference class before definition.

---

## Type System

```
typeof(value)                            // concrete type
isa(value, type)                         // type check
```

Static typing: `var a:Int32`, `var a:array of String`
Nullable: `var n:Int32?` -- allows null, use `n || 0` for default.

Hierarchy: `Int32` is-a `Integer` is-a `Number` is-a `Any`

Utilities: `findtype(name)`, `new(type, args)`, `static_cast(type, value)`, `static_typeof(expr)`

---

## Libraries

```
using Math
using UI
using Data
```

System libraries: `Arcology`, `Celestial`, `Charts`, `Code`, `Data`, `Email`, `Graph`, `HTTP`, `Image`, `Math`, `UI`, `User`, `XLS`

Resolution order: system -> owner's `~/lib` -> `/archive/libraries`

---

## System Object

```
System.apiVersion
System.programID
System.processID
System.username          // GridNameType
System.user              // user object
System.args              // entry point arguments
System.programMode       // "normal", "entrypoint", "job"
System.types             // all defined types
```

---

## Errors & Comments

**Errors:** `error("Invalid input")` -- raises error, terminates program. No try/catch.

**Comments:**
```
// line comment
| line comment (alternative)
/* block comment */
```

Some external legacy docs may describe `#` comments. The canonical comment
forms are `//`, `|`, and `/* ... */`.

## Compile Errors

Compile errors are reported as `ProgramName(row,col): message`. Rows and
columns are 1-indexed.
