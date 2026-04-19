# GridLang Libraries Reference

Agent-optimized reference. Function signatures, parameters, return values, and minimal examples.

---

## Math (`using Math`)

### Constants

| Name | Value |
|------|-------|
| `e` | Euler's number |
| `pi` | 3.14159... |
| `tau` | 6.28318... |

### Arithmetic

```
ceil(x) -> Int
floor(x) -> Int
exp(x) -> Float
log(x) -> Float
log(x, base) -> Float
sqrt(x) -> Float
sum(x1, ..., xn) -> Float
divmod(x, y) -> [quotient, remainder]
```

### Statistical

```
average(x1, ..., xn) -> Float
median(x1, ..., xn) -> Float
```

#### Stats Distributions

```
Stats.normalCDF(x) -> Float
Stats.normalPDF(x) -> Float
Stats.normalquantile(x) -> Float

Stats.TDistCDF(x, df) -> Float
Stats.TDistPDF(x, df) -> Float
Stats.TDistInverse(p, df) -> Float

Stats.FDistCDF(x, df1, df2) -> Float
Stats.FDistPDF(x, df1, df2) -> Float
Stats.FDistInverse(p, df1, df2) -> Float

Stats.exponentialCDF(x, lambda) -> Float
Stats.exponentialPDF(x, lambda) -> Float
Stats.exponentialInverse(p, lambda) -> Float
```

### Trigonometric

```
sin(x) -> Float      cos(x) -> Float      tan(x) -> Float
asin(x) -> Float     acos(x) -> Float     atan(x) -> Float
```

### Vectors

```
var v2 = Vector(x, y)
var v3 = Vector(x, y, z)
```

**Operators:** `+`, `-`, `*` (scalar), `/` (scalar)

**Properties:** `.x`, `.y`, `.z`, `.length`, `.unit`

**Methods:** `.dot(v) -> Float`, `.cross(v) -> Vector`

---

## HTTP (`using HTTP`)

Requires `using HTTP` declaration at top of program.

### HTTP.get

```
HTTP.get(url) -> response
HTTP.get(url, headers) -> response
HTTP.get(url, headers, options) -> response
```

**Response:** `[ status, statusCode, headers, data ]`

**Example:**
```
using HTTP
var resp = HTTP.get("https://api.example.com/data")
if resp.statusCode == 200 do
    var body = resp.data
    print(body)
end
```

### JSON

```
JSON.read(jsonString) -> value
JSON.read(jsonString, [ noExceptions=true ]) -> value
JSON.write(value) -> String
```

### URL

```
var u = URL("https://example.com", [ path="/folder1", query=[ id="123" ] ])
```

**Properties:** `.protocol`, `.host`, `.port`, `.path`, `.query`, `.fragment`

### GFM (GitHub-Flavored Markdown)

```
GFM.write(markdown) -> String        // returns HTML
GFM.setCheckbox(markdown, line, checked) -> String
```

### XMLElementType

Returned from `Data.import` on XML data.

**Properties:** `.tag`, `.text`, `.attributes`, `.children`

**Methods:**
```
.getChildByTag(tag) -> XMLElementType
.getChildrenByTag(tag) -> Array<XMLElementType>
.copy() -> XMLElementType
```

---

## UI (`using UI`)

### Core Functions

```
UI.show(control)                        // place control in display hierarchy
UI.run()                                // run event loop
UI.stop()                               // stop event loop
UI.dialog(text)                         // simple message dialog
UI.dialog(schema, data, options)        // form dialog -> struct | null
UI.dialog(control, data, options)       // custom dialog -> DialogResult
UI.get(id) -> control                   // get control by ID
UI.setCommands(arrayOfCommands)         // set top command bar
UI.setSendTo(arrayOfCommands)           // set Send To menu
UI.setProgramTitle(title)               // set header title
UI.setFragment(fragment)                // set URL fragment
UI.getProgramURL(params, options) -> String
UI.navigate(url, options)               // options: { newTab: true }
UI.addTimer(intervalMS, func) -> timerID
UI.removeTimer(timerID)
UI.processEvents()                      // yield during long operations
UI.waitForEvents()
UI.print(control)                       // print dialog
```

> `UI.create(controlClass)` is DEPRECATED. Use constructors.

### Dialog Options

| Property | Values |
|----------|--------|
| `.type` | `"ok"`, `"okCancel"`, `"yesNo"`, `"yesNoCancel"`, `"cancel"`, `"wizardStart"`, `"wizard"`, `"wizardFinish"` |
| `.title` | dialog title string |
| `.columns` | subset of schema columns to show |
| `.onaction` | function; return `DialogResult.continue` to keep open |

Returns `null` on Cancel, struct on form dialog, `DialogResult` on other types.

### Command Structure

```
[ label="Save", function onaction () -> doSave(), keyBinding="Control+s" ]
[ label="Link", href="https://example.com" ]
```

### Control Constructors

| Constructor | Purpose |
|-------------|---------|
| `TextControlType(options)` | Display text |
| `ButtonControlType(options)` | Clickable button |
| `CanvasControlType(options)` | HTML canvas drawing |
| `CanvasGridControlType(options)` | Grid of glyphs |
| `DropdownControlType(options)` | Dropdown selection |
| `FormControlType(options)` | Vertical form with fields |
| `LayoutGridControlType(options)` | Grid layout for children |
| `ListboxControlType(options)` | List of items |
| `HorizontalTabsControlType(options)` | Tab navigation |
| `ImageControlType(options)` | Image display |
| `MenuControlType(options)` | Popup menu |
| `BoardControlType(options)` | 2D board with cards |
| `DialControlType(options)` | Single value display |
| `Scene2DControlType(options)` | 2D animation scene |
| `TableControlType(options)` | Table with columns/sorting/selection |

### Common Control Properties

```
.data                                   // control data
.background  .border  .margin  .padding
.width  .height                         // CSS dims, supports "min:X; max:Y"
.hidden                                 // bool
.id                                     // string
.local                                  // arbitrary storage object
.class                                  // read-only
.datatype                               // read-only
```

### Common Events

```
.onclick  .onaction  .onchange  .oncontextmenu
.onfragmentchange(fragment)
```

### LayoutGrid

```
var grid = LayoutGridControlType([
    cols = 2,
    rows = 2,
    colWidth = ["200px", "auto"],
    rowHeight = ["40px", "auto"],
    content = [
        [ col=0, row=0, control=headerCtrl ],
        [ col=0, row=1, control=sidebarCtrl ],
        [ col=1, row=0, height=2, control=mainCtrl ],
    ]
])
```

**colWidth values:** `"auto"`, `"100px"`, `"min:X; max:Y"`

**Column behaviors:** `"default"`, `"resize"`, `"collapse"`, `"openClose"`, `"hide"`

**Breakpoints:** `.sm` (640px), `.md` (768px), `.lg` (1024px), `.xl` (1280px)

### Listbox

```
var list = ListboxControlType([
    data = arrayOfStructs,              // expects .id, .name, .icon, .desc
    selectionMode = "single",           // "none", "single", "multi"
    style = "list",                     // or "palette"
])
```

### Form

```
var form = FormControlType([
    content = [
        [ id="name", label="Name", type=String ],
        [ id="age", label="Age", type=Int32 ],
        [ id="color", label="Color", type=MyEnum ],
    ],
    data = [ name="John", age=30 ]
])
```

### Dialog with Form (Common Pattern)

```
schema InputRecord [
    var name:String
    var age:Int32
]
var result = UI.dialog(InputRecord, [ name="", age=0 ])
if result do
    print("Got: " + result.name)
end
```

---

## Charts (`using Charts`)

### Basic Usage

```
using Charts
using UI

var chartCtrl = UI.create("chart2D")
chartCtrl.data = [3, 2, 3, 4, 7]
chartCtrl.design = [
    type = "line",
    xAxis = [ data="array:index" ],
    yAxis = [ data="array:value" ]
]
UI.show(chartCtrl)
UI.run()
```

### Chart Types

`"line"`, `"bar"`, `"area"`, `"xy"` (scatter)

### Data References

| Source | X syntax | Y syntax |
|--------|----------|----------|
| Array | `"array:index"` | `"array:value"` |
| Table column | `"column:columnID"` | `"column:columnID"` |
| Struct of tables | `"column:tableName/columnID"` | `"column:tableName/columnID"` |
| Constants | `data=["Spring", "Summer", "Fall", "Winter"]` | -- |

### Multiple Series

```
design = [
    series = [
        [ type="line", xAxis=[data="column:date"], yAxis=[data="column:sales"] ],
        [ type="bar", xAxis=[data="column:date"], yAxis=[data="column:profit"] ],
    ]
]
```

### Axis Options

`.data`, `.type` (`"categories"` | `"continuous"`), `.min`, `.max`

---

## Email (`using Email`)

```
Email.send(from, to, subject, body) -> true | error
Email.send(from, to, subject, body, [ connection=connectionObj ]) -> true | error
```

- Requires mail service connection in program owner's account.
- `from` must be from a domain with configured connection.

---

## XLS (`using XLS`)

### Create Workbook

```
var wb = XLSWorkbookType()
```

**Properties:** `.sheets`, `.xlsxFileData` (binary .xlsx)

### Methods

```
wb.getCell(sheet, cellAddr) -> value        // sheet is 1-based
wb.setCell(sheet, cellAddr, value)
wb.getTable(range) -> Table                 // first row = header
wb.setTable(sheet, table)
wb.insertSheet(info)
wb.insertSheet(info, index)
wb.setSheetInfo(sheet, info)                // info: { name: "Sheet1" }
wb.getFill(sheet, cell) -> color
wb.setFill(sheet, range, color)
```

**Cell addresses:** `"A1"`, `"Sheet1!A1:D5"`, or `{ row=1, col=1 }`

### Export Pattern

```
var exportDS = Data.open("Export", "xls")
exportDS.setValue(xls)
exportDS.setName("MyFile.xlsx")
UI.navigate(exportDS.getDownloadURL(), [ newTab=true ])
```

---

## User (`using User`)

### User Dataset

```
using Data
var userDS = Data.open("@" + System.username.id)
```

**Fields:** `.id`, `.name`, `.owner`, `.groups`, `.membership`, `.preferences`, `.profile`, `.accounts`

### Functions

```
// Director access required
User.createUser(desc) -> user
User.deleteUser(username)
User.getUsers(options) -> Array

// General
User.inGroup(groupID) -> Boolean
User.checkLicense(licenseID) -> Boolean

// Connections
User.createConnection(type, data) -> connection
User.getConnection(connectionID, options) -> connection
User.findConnection(service, data) -> connection
User.setConnection(connectionID, data)
User.deleteConnection(connectionID)

// Config (Director access for set)
User.getConfig() -> config
User.setConfig(config)

// Key codes
User.addKeyCode(keyCode)
User.createKeyCode(offeringID)
```

### GridNameType

`System.username` returns this type.

**Properties:** `.id`, `.name`

**Special values:** `"overlord"`, `"$architects"`, `"$directors"`, `"$operators"`

---

## Data (`using Data`)

### Core Functions

```
Data.open(path) -> Dataset
Data.open(name, type) -> Dataset            // type: "xls", "table", etc.
Data.import(data, format) -> value          // format: "xml", "csv", etc.
```

### Dataset Types

`Array`, `Canvas`, `Container`, `Database`, `Folder`, `Generic`, `Process`, `Queue`, `Table`, `XLS`

### Common Dataset Methods

```
ds.getValue() -> value
ds.setValue(data)
ds.setName(name)
ds.getDownloadURL() -> String
```

---

## System (no import needed)

```
System.apiVersion -> String
System.programID -> String
System.processID -> String
System.username -> GridNameType              // .id, .name
System.user -> Object
System.args -> value                        // entry point arguments
System.programMode -> String                // "normal", "entrypoint", "job"
System.types -> Array                       // all defined types
```
