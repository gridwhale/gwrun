# GridLang Example Programs

Complete, runnable programs demonstrating common GridLang patterns. Use these as templates when writing GridLang applications.

---

## 1. Console Hello World

```
// Simple console program: read user input and respond.
var name = input("Enter your name: ")
print("Hello, " + name + "!")
print("Today is " + String(DateTime(), "mm/dd/yyyy"))
```

Pattern: Basic console I/O with `print()`, `input()`, and date formatting.

---

## 1A. End-to-End Console Program With `gw`

Source file `hello.grid`:

```
var name = System.args.name || ""
if name == "" do
    name = input("What is your name? ")
end
print("Hello, " + name + "!")
```

Create, write, and compile:

```powershell
gw program create --name Hello --output json
gw program write <programID> --file hello.grid --output json
gw program compile <programID> --output json
```

Run as a one-shot program with arguments:

```powershell
Set-Content -Path args.json -Value '{"name":"Ada"}' -NoNewline -Encoding ascii
gw program run <programID> --json-file args.json --output json
```

For interactive process entry points, define an `entrypoint` function and start
it as `PROGRAMID.entryPoint`:

```
function HelloWorld () entrypoint
do
    var name = System.args.name || ""
    if name == "" do
        name = input("What is your name? ")
    end
    print("Hello, " + name + "!")
end
```

```powershell
Set-Content -Path args.json -Value '{}' -NoNewline -Encoding ascii
gw process start <programID>.HelloWorld --json-file args.json --output json
gw process view <processID> --seq 0 --output json
gw process input <processID> --text Ada --seq-file input.seq.json --output json
gw process view <processID> --seq-file next.seq.json --output json
```

Pattern: Use `System.args` for caller-supplied arguments, `input()` for missing
interactive values, and `gw program` wrappers to avoid shell JSON mistakes.

---

## 2. Clinical Trial Data Table with Filtering and Sorting

```
// Define a schema for clinical lab results and query the data.

schema LabResult [
    key subjectID:String
    var site:String
    var visitDate:DateTime
    var analyte:String
    var labValue:Float64
    var unit:String
    var flagged:Bool
]

var results = Table(LabResult, [
    ["SUBJ-001", "Boston",    DateTime(15,1,2026), "ALT",        42.0, "U/L",   false],
    ["SUBJ-002", "Boston",    DateTime(16,1,2026), "ALT",        98.5, "U/L",   true],
    ["SUBJ-003", "London",    DateTime(15,1,2026), "Creatinine", 1.1,  "mg/dL", false],
    ["SUBJ-004", "London",    DateTime(17,1,2026), "ALT",        55.0, "U/L",   false],
    ["SUBJ-005", "Tokyo",     DateTime(18,1,2026), "Creatinine", 2.4,  "mg/dL", true],
    ["SUBJ-006", "Tokyo",     DateTime(18,1,2026), "ALT",        37.0, "U/L",   false],
    ["SUBJ-007", "Boston",    DateTime(20,1,2026), "Creatinine", 0.9,  "mg/dL", false],
])

// Filter to flagged results only
var flagged = results.filter(.flagged == true)
print("Flagged results: " + String(flagged.length))

// Sort by lab value descending
var bySeverity = results.sorted(-.labValue)

// Filter by site and analyte
var bostonALT = results.filter(.site == "Boston" and .analyte == "ALT")
print("Boston ALT results: " + String(bostonALT.length))
```

Pattern: Schema definition, table constructor with inline data, column expression filtering with `.field` syntax, chained operations, descending sort with `-`.

---

## 3. Group-By Summarization with Site Metrics

```
// Summarize enrollment counts and average lab values by site.

schema Enrollment [
    key recordID:String
    var site:String
    var arm:String
    var labValue:Float64
]

var data = Table(Enrollment, [
    ["R001", "Boston", "Treatment", 45.2],
    ["R002", "Boston", "Treatment", 52.8],
    ["R003", "Boston", "Placebo",   38.1],
    ["R004", "London", "Treatment", 61.0],
    ["R005", "London", "Placebo",   41.5],
    ["R006", "London", "Placebo",   39.9],
    ["R007", "Tokyo",  "Treatment", 58.3],
    ["R008", "Tokyo",  "Treatment", 44.7],
    ["R009", "Tokyo",  "Placebo",   36.0],
])

// Count subjects and average lab values per site
var siteSummary = data.groupBy(.site).summarize([
    SubjectCount = count(.recordID),
    AvgLabValue  = average(.labValue),
    MaxLabValue  = max(.labValue),
])

for key, row in siteSummary do
    print("{0}: {1} subjects, avg={2}".format(
        row.site, row.SubjectCount, String(row.AvgLabValue, "#.##")
    ))
end

// Summarize by treatment arm
var armSummary = data.groupBy(.arm).summarize([
    N        = count(.recordID),
    MeanLab  = average(.labValue),
])
```

Pattern: `.groupBy()` with `.summarize()`, aggregation functions (`count`, `average`, `max`), iterating summarized results with `for key, row in`.

---

## 4. Array Operations: Map, Filter, Reduce

```
// Process arrays of numeric data with functional operations.

var readings = [23.1, 45.6, 12.0, 67.8, 34.2, 89.1, 5.5, 51.0]

// Filter to values above threshold
var elevated = readings.filter(fn(x) -> x > 40.0)
print("Elevated readings: " + String(elevated.length))

// Scale all values
var normalized = readings.map(fn(x) -> x / 100.0)

// Compute total
var total = readings.reduce(fn(acc, x) -> acc + x)
print("Total: " + String(total))

// Built-in aggregations
print("Mean: " + String(readings.average()))
print("Max: " + String(readings.max()))
print("Min: " + String(readings.min()))

// Sort descending and take top 3
var top3 = readings.sorted(-1).sliced(0, 3)
print("Top 3: " + String(top3))

// Generate array from function
var indices = Array(10, fn(i) -> i * i)
```

Pattern: Anonymous functions with `fn(x) -> ...`, `.filter()`, `.map()`, `.reduce()` on arrays, built-in stats methods, `Array()` constructor with generator.

---

## 5. GUI Text Display with Click Handler

```
// GUI app that displays text and responds to a command bar action.

using UI

var display = UI.create("text")
display.data = "Click 'Greet' in the command bar"
display.align = "center middle"
display.fontSize = "24pt"

var clickCount = 0

UI.setCommands([
    [ label="Greet", function command () do
        clickCount = clickCount + 1
        display.data = "Hello! Clicked {0} time(s).".format(clickCount)
    end ],
    [ label="Reset", function command () do
        clickCount = 0
        display.data = "Counter reset."
    end ],
])

UI.show(display)
UI.run()
```

Pattern: `using UI`, creating a text control, `UI.setCommands()` for command bar buttons, mutable state in click handlers, `UI.show()` and `UI.run()`.

---

## 6. GUI Layout Grid with Multiple Controls

```
// Dashboard layout with a table and a chart side by side.

using UI

schema SalesRow [
    key region:String
    var q1:Float64
    var q2:Float64
    var q3:Float64
    var q4:Float64
]

var salesData = Table(SalesRow, [
    ["North", 120.5, 135.0, 142.3, 158.0],
    ["South",  98.2, 105.8, 110.1, 122.4],
    ["East",  145.0, 138.7, 155.2, 160.9],
    ["West",  110.3, 128.4, 131.0, 145.5],
])

var grid = UI.create("grid")
grid.rows = "1fr"
grid.columns = "1fr 1fr"

// Left panel: data table
var tableCtrl = UI.create("table")
tableCtrl.data = salesData

// Right panel: bar chart
var chartCtrl = UI.create("chart2D")
chartCtrl.data = salesData
chartCtrl.design = "bar"

grid[0, 0] = tableCtrl
grid[0, 1] = chartCtrl

UI.show(grid)
UI.run()
```

Pattern: Grid layout with `rows`/`columns`, table control bound to data, chart control with `design` property, positioning children with `grid[row, col]`.

---

## 7. GUI Form Dialog for Data Entry

```
// Use UI.dialog to collect structured input from the user.

using UI

schema PatientEntry [
    var subjectID:String as "Subject ID"
    var site:String as "Study Site"
    var visitDate:DateTime as "Visit Date"
    var weight:Float64 as "Weight (kg)"
    var adverse:Bool as "Adverse Event?"
]

var display = UI.create("text")
display.data = "Click 'Add Patient' to enter data"
display.align = "center middle"
display.fontSize = "18pt"

UI.setCommands([
    [ label="Add Patient", function command () do
        var defaults = PatientEntry([
            subjectID  = "",
            site       = "Boston",
            visitDate  = DateTime(),
            weight     = 70.0,
            adverse    = false,
        ])
        var result = UI.dialog(PatientEntry, defaults)
        if result != null do
            display.data = "Entered: {0} at {1}, weight={2}kg".format(
                result.subjectID, result.site, String(result.weight, "#.#")
            )
        end
    end ],
])

UI.show(display)
UI.run()
```

Pattern: `UI.dialog(schema, defaults)` for modal form input, schema `as` labels for field display names, null check on dialog result (user may cancel).

---

## 8. HTTP Entry Point: Lab Value Classifier

```
// HTTP server endpoint that classifies a lab value as normal or abnormal.
// Accessed via POST to /srv/PROGRAMID/ClassifyLab

function ClassifyLab (analyte:String, value:Float64, unit:String) entrypoint
do
    var ranges = [
        ALT       = [ low=7.0,   high=56.0,  unit="U/L" ],
        Creatinine = [ low=0.6,  high=1.2,   unit="mg/dL" ],
        Hemoglobin = [ low=12.0, high=17.5,  unit="g/dL" ],
    ]

    var ref = ranges[analyte]
    if ref == null do
        return [ status="error", message="Unknown analyte: " + analyte ]
    end

    var classification = if value < ref.low -> "LOW"
        else if value > ref.high -> "HIGH"
        else -> "NORMAL"

    return [
        analyte        = analyte,
        value          = value,
        unit           = unit,
        referenceLow   = ref.low,
        referenceHigh  = ref.high,
        classification = classification,
    ]
end
```

Pattern: `entrypoint` keyword for HTTP endpoints, typed parameters, struct literals for lookup data, if-expression for inline conditionals, returning structured data.

---

## 9. Chart Display: Line Chart from Time Series

```
// Display a line chart of lab values over time.

using UI

schema TimePoint [
    key visitDay:Int32 as "Study Day"
    var treatmentArm:Float64 as "Treatment"
    var placeboArm:Float64 as "Placebo"
]

var timeSeries = Table(TimePoint, [
    [0,   45.0, 44.8],
    [7,   42.3, 45.1],
    [14,  38.7, 44.5],
    [28,  35.2, 43.9],
    [42,  31.8, 44.2],
    [56,  28.5, 43.7],
    [84,  25.1, 44.0],
])

var chart = UI.create("chart2D")
chart.data = timeSeries
chart.design = "line"

UI.show(chart)
UI.run()
```

Pattern: Line chart with `design = "line"`, multi-series data (each non-key column becomes a series), schema `as` labels used for legend names.

---

## 10. XLS Import/Export

```
// Import clinical data from Excel, process it, and export results.

using XLS
using Data

// Import from Excel file
var raw = XLS.import("trial_results.xlsx", [sheet="LabData"])

// Clean and filter
var cleaned = raw.filter(.labValue != null and .labValue > 0)

// Compute summary
var summary = cleaned.groupBy(.site).summarize([
    N         = count(.subjectID),
    MeanValue = average(.labValue),
    StdDev    = stddev(.labValue),
])

// Export processed data back to Excel
XLS.export(summary, "site_summary.xlsx")

// Also export as structured string for logging
for key, row in summary do
    print("{0}: N={1}, Mean={2}, SD={3}".format(
        row.site, row.N,
        String(row.MeanValue, "#.##"),
        String(row.StdDev, "#.##")
    ))
end
```

Pattern: `using XLS` for Excel I/O, `XLS.import()` with sheet parameter, filtering nulls, groupBy/summarize pipeline, `XLS.export()` for output.

---

## 11. HTTP Entry Point: Multi-Site Enrollment Report

```
// Server endpoint that returns enrollment statistics across sites.
// Accepts a study ID and returns aggregated counts.

schema EnrollmentRecord [
    key recordID:String
    var studyID:String
    var site:String
    var arm:String
    var status:String
    var enrollDate:DateTime
]

var enrollments = Table(EnrollmentRecord, [
    ["E001", "TRIAL-042", "Boston", "Treatment", "Active",    DateTime(10,1,2026)],
    ["E002", "TRIAL-042", "Boston", "Placebo",   "Active",    DateTime(11,1,2026)],
    ["E003", "TRIAL-042", "London", "Treatment", "Completed", DateTime(8,1,2026)],
    ["E004", "TRIAL-042", "London", "Treatment", "Active",    DateTime(12,1,2026)],
    ["E005", "TRIAL-042", "Tokyo",  "Placebo",   "Withdrawn", DateTime(9,1,2026)],
    ["E006", "TRIAL-099", "Boston", "Treatment", "Active",    DateTime(15,1,2026)],
])

function GetEnrollmentReport (studyID:String) entrypoint
do
    var studyData = enrollments.filter(.studyID == studyID)

    if studyData.length == 0 do
        return [ status="error", message="No data for study " + studyID ]
    end

    var bySite = studyData.groupBy(.site).summarize([
        Total     = count(.recordID),
        Active    = count(.status == "Active"),
        Completed = count(.status == "Completed"),
    ])

    var byArm = studyData.groupBy(.arm).summarize([
        Total = count(.recordID),
    ])

    return [
        studyID    = studyID,
        totalN     = studyData.length,
        bySite     = bySite,
        byArm      = byArm,
    ]
end
```

Pattern: Entry point with table data, filtering by parameter, multiple groupBy summaries, returning nested structured results (structs containing tables).

---

## 12. Console Data Pipeline: Adverse Event Analysis

```
// Console program that analyzes adverse events from a clinical trial.
// Demonstrates the full pipeline: define, populate, filter, summarize, report.

schema AdverseEvent [
    key aeID:String
    var subjectID:String
    var site:String
    var term:String
    var severity:String
    var related:Bool
    var onsetDay:Int32
]

var events = Table(AdverseEvent, [
    ["AE001", "SUBJ-001", "Boston", "Headache",    "Mild",     false, 3],
    ["AE002", "SUBJ-001", "Boston", "Nausea",      "Moderate", true,  7],
    ["AE003", "SUBJ-002", "Boston", "Fatigue",     "Mild",     true,  5],
    ["AE004", "SUBJ-003", "London", "Headache",    "Mild",     false, 2],
    ["AE005", "SUBJ-004", "London", "Rash",        "Severe",   true,  14],
    ["AE006", "SUBJ-005", "Tokyo",  "Nausea",      "Moderate", true,  8],
    ["AE007", "SUBJ-005", "Tokyo",  "Dizziness",   "Mild",     false, 10],
    ["AE008", "SUBJ-006", "Tokyo",  "Hepatotoxicity","Severe", true,  21],
])

// Total and related counts
var totalAE = events.length
var relatedAE = events.filter(.related == true)
print("Total AEs: " + String(totalAE))
print("Drug-related AEs: " + String(relatedAE.length))

// Severity breakdown
var bySeverity = events.groupBy(.severity).summarize([
    Count = count(.aeID),
])
print("\n-- By Severity --")
for key, row in bySeverity do
    print("  {0}: {1}".format(row.severity, row.Count))
end

// Serious (severe + related) events requiring attention
var serious = events.filter(.severity == "Severe" and .related == true)
print("\n-- Serious Related Events --")
for key, row in serious do
    print("  {0}: {1} at {2} (day {3})".format(
        row.subjectID, row.term, row.site, row.onsetDay
    ))
end

// Site with most events
var bySite = events.groupBy(.site).summarize([
    Count = count(.aeID),
])
var highestSite = bySite.atMax(.Count)
print("\nSite with most AEs: " + highestSite.site + " (" + String(highestSite.Count) + ")")
```

Pattern: Full console data pipeline -- schema, table, multiple filters, groupBy/summarize, `.atMax()` for finding extremes, formatted console output with `.format()`.

---

## 13. Custom Functions

```
// Functions use do/end for multi-statement bodies.
// Pure expression functions use -> (no do/end needed).

function double (x) -> x * 2

function clamp (value, lo, hi) do
    if value < lo do
        return lo
    end
    if value > hi do
        return hi
    end
    return value
end

function stats (arr) do
    var sum = 0.0
    var n = 0
    for v in arr do
        sum = sum + v
        n = n + 1
    end
    var mean = sum / n

    var sqDiffSum = 0.0
    for v in arr do
        sqDiffSum = sqDiffSum + (v - mean) ^ 2
    end
    var stddev = Math.sqrt(sqDiffSum / n)

    return [ mean=mean, stddev=stddev, n=n ]
end

using Math
var data = [12.5, 18.3, 9.1, 22.7, 15.0]
var result = stats(data)
print("Mean: " + String(result.mean))
print("StdDev: " + String(result.stddev))
print("Clamped: " + String(clamp(150, 0, 100)))
print("Doubled: " + String(double(21)))
```

Pattern: Expression functions with `->`, multi-statement functions with `do`/`end`, returning structs from functions, manual iteration for aggregation (use `.length` for array size).

---

## 14. Class with Methods and State

```
// Classes use [ ] delimiters. Members declared with var. Access via this.

class Counter [
    var name:String
    var value:Int32 = 0
    var history:Array = Array()

    constructor (name:String) do
        this.name = name
    end

    function increment (amount:Int32) do
        this.value = this.value + amount
        this.history.append(this.value)
    end

    function decrement (amount:Int32) do
        this.value = this.value - amount
        this.history.append(this.value)
    end

    function report () do
        print(this.name + " = " + String(this.value))
        print("  History: " + String(this.history))
    end
]

var a = Counter("Alpha")
a.increment(10)
a.increment(5)
a.decrement(3)
a.report()

var b = Counter("Beta")
b.increment(100)
b.report()
```

Pattern: Class with `[ ]` body, `var` member declarations with defaults, `constructor` for initialization, `this.` for all member access, multiple independent instances.

---

## 15. HTTP GET Request

```
// Fetch data from an external API using the HTTP library.

using HTTP

var resp = HTTP.get("https://api.example.com/data")
if resp.statusCode == 200 do
    print("Success: " + String(resp.data))
else do
    print("Error: " + String(resp.statusCode))
end
```

Pattern: `using HTTP` declaration required, `HTTP.get(url)` returns response struct with `.statusCode` and `.data` fields.
