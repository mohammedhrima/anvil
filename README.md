# anvil

A tiny scripting language for automating shell workflows — build/test/release
tasks, glue scripts, anything you'd otherwise write in bash. You declare
**commands** and run them, react to success/failure, capture their output, loop
over files, and drive it all from an interactive prompt.

```
# hello.an
Cmd(["echo hello from anvil"]).on(
    success = { okput("it ran") },
    failure = { errput("it failed") }
)
```

```
$ anvil hello.an
hello from anvil
it ran
```

---

## Build & run

```
make                 # builds build/<os>/anvil and symlinks it to ~/.local/bin/anvil
make clean
anvil <file.an>      # run a script top-to-bottom
```

Dependencies: a **C++23** compiler (clang) and **readline** — install it with
`brew install readline` (macOS) or `apt install libreadline-dev` (Debian/Ubuntu).
On macOS the Makefile expects Homebrew's readline under `/opt/homebrew`.

Source layout:

```
src/anvil.hpp   # shared types (Type, Token, Node, ThreadPool) + cross-file declarations
src/lexer.cpp   # tokenizer
src/parser.cpp  # AST nodes + the parser
src/exec.cpp    # running shell commands, capturing their output
src/interp.cpp  # the evaluator + builtins
src/anvil.cpp   # globals + main
Makefile
anvil.an        # a small example
```

---

## Tutorial

### Comments
`#` starts a comment that runs to the end of the line.

```
# this whole line is ignored
output("hi")   # trailing comments work too
```

### Values

- **String** — `"double quoted"`. Read literally to the next `"`; there are **no
  escape sequences** (`\n`, `\"` are literal characters), so a string can't
  contain a `"`.
- **Number** — integer literals like `42`. Integer math only (no floats).
- **Array** — `["a", "b", "c"]`. Elements are any expression; extra/empty commas
  are ignored. (There is no indexing — see *Not yet* below.)
- **Command** and **Record** — described in their own sections.

### Variables

Assign with `=`. Variables live in one global scope and can be reassigned.

```
name = "world"
count = 3
list = [1, 2, 3]
```

### Operators

| Operator | Meaning |
|---|---|
| `+` | **numeric add** when *both* sides are numbers, otherwise **string concatenation** |
| `-` `*` `/` | numeric only (integer; divide-by-zero yields `0`) |
| `.` | field access on a record, and `.on(...)` on a command |
| `=` | assignment |

Precedence, highest first: `.` → `*` `/` → `+` `-` → `=`.

```
output(2 + 3)          # 5      (both numbers -> add)
output("a" + "b")      # ab     (concatenation)
output("count = " + 3) # count = 3   (number joined onto a string)
output(10 - 2 * 3)     # 4
```

### Printing (colored output)

Five builtins, each prints its single argument followed by a newline:

| Builtin | Color | Stream |
|---|---|---|
| `output(x)` | default | stdout |
| `okput(x)` | green | stdout |
| `errput(x)` | red | stderr |
| `warnput(x)` | yellow | stderr |
| `infoput(x)` | cyan | stdout |

```
output("plain")
okput("success!")
errput("something broke")
```

### Commands (`Cmd`) and running them

`Cmd(...)` builds a command value. Each **argument** is an **array**:

- An array of **strings** is space-joined into **one** shell command:
  `Cmd(["ls", "-l"])` runs `ls -l`.
- **Several** array arguments become **several** commands that run
  **concurrently**: `Cmd(["make a"], ["make b"])`.
- Commands run through `/bin/sh`, so pipes, `$(...)`, redirects and `&&` work
  inside the string.

A command **runs** when:
- it appears as a statement on its own, **or**
- you attach `.on(...)` (see below), **or**
- you type its name at the `loop` prompt.

Assigning a command **without** `.on` just **stores** it (it does not run) — handy
for composition:

```
build = Cmd(["make"])          # stored, not run
build                          # <- this runs it
```

**Composition** — an array of command *values* is combined into one command:

```
build = Cmd(["make app"])
test  = Cmd(["ctest"])
all   = Cmd([build, test])     # all runs both (concurrently) when invoked
```

(An array must be all strings or all commands — not mixed.)

### Reacting with `.on`, and records

`.on` runs the command, then runs the `success` block if every command exited 0,
otherwise the `failure` block:

```
Cmd(["clang --version"]).on(
    success = { okput("clang present") },
    failure = { errput("clang missing") }
)
```

Running a command produces a **record** with these fields:

| Field | Meaning |
|---|---|
| `.flag` | `"success"` if all exited 0, else `"error"` |
| `.code` | exit code (a number; first non-zero if there were several) |
| `.out` | captured **stdout** |
| `.err` | captured **stderr** |

Use `.on()` with no blocks to just run and capture:

```
res = Cmd(["echo hi"]).on()
output(res.flag)     # success
output(res.out)      # hi
output(res.code)     # 0
```

`.on` also works on a record you already have (it dispatches on `.flag`):

```
res = Cmd(["make"]).on()
res.on(
    success = { okput("built") },
    failure = { errput(res.err) }     # print the captured stderr
)
```

### Loops

`for <var> in <array> { ... }` iterates an array, binding `var` each time:

```
for name in ["world", "anvil"] {
    output("hello " + name)
}
```

### Functions

`fn name(params) { body }` defines a function; call it as `name(args)`. Argument
count must match. Functions run for their side effects (there is no `return`).

```
fn greet(who) {
    Cmd(["echo hi " + who]).on(
        success = { okput("greeted " + who) },
        failure = { errput("failed " + who) }
    )
}

greet("anvil")
```

### The interactive prompt (`loop`)

`loop` opens a REPL. At the prompt:

- Type a **function name** and space-separated args → calls it:
  `greet anvil` runs `greet("anvil")`.
- Type a **stored command's** name → runs it.
- Type **anything else** → runs it as a raw shell command.
- `reload` → re-read the script from disk and start over.
- `Ctrl-D` → quit.

```
fn build() { Cmd(["make"]).on(success = { okput("ok") }, failure = { errput("no") }) }
loop
```
```
(anvil) me@host anvil > build
ok
(anvil) me@host anvil > reload
```

---

## Builtins reference

**Output** — `output`, `okput`, `errput`, `warnput`, `infoput` (see *Printing*).

**`setThreadPool(n)`** — set how many commands run at once (default: CPU cores).

**`setenv(name, value)`** — set an environment variable for the process; every
command run afterwards inherits it.

```
setenv("URA_LIB", "src/ura-lib")
```

**`enableDebug()`** — turn on debug tracing (prints tokens, the parse tree, and
each evaluation step). Off by default.

**`walk(dir)` / `walk(dir, ext)` / `walk(dir, ext, skip)`** — recursively list
files under `dir`, returning an **array of paths**. `ext` keeps only paths ending
with it; `skip` is an array of folder names to prune. Symlinks are not followed.

```
for f in walk("src", ".c", ["build", "vendor"]) {
    output(f)
}
```

**`read(path)`** → record `{flag, out}` — `out` is the file's contents (`flag` is
`error` if it couldn't be opened).

**`write(path, content)`** → record `{flag}` — writes `content` to `path`.

**`diff(a, b, ignores)`** → record `{flag}` — `success` if strings `a` and `b` are
equal after **removing every line that contains** any string in the `ignores`
array (and ignoring trailing newlines). Good for comparing output while skipping
volatile lines.

```
diff(got.out, read("expected.txt").out, ["timestamp", "pid"]).on(
    success = { okput("match") },
    failure = { errput("differs") }
)
```

**`replace(s, old, new)`** → the string `s` with every occurrence of `old`
replaced by `new`.

```
ll = replace(path, ".ura", ".ll")
```

---

## A fuller example

A tiny test runner: compile each input, compare output to a reference, skipping a
volatile header line, and print a summary.

```
setThreadPool(4)
ignores = ["# time"]

fn run_tests(dir) {
    pass = 0
    fail = 0
    for input in walk(dir, ".in", ["build"]) {
        got = Cmd(["./app < " + input]).on()
        want = read(replace(input, ".in", ".out"))
        diff(got.out, want.out, ignores).on(
            success = { pass = pass + 1  okput("PASS " + input) },
            failure = { fail = fail + 1  errput("FAIL " + input) }
        )
    }
    output("Passed: " + pass + "  Failed: " + fail)
}

run_tests("tests")
loop
```

---

## Not yet (by design, so far)

These do **not** exist in the language today:

- No `if` / `else` and no comparison operators (`==`, `<`, …) — branch with `.on`
  (on a command's or record's `flag`) instead.
- No `while` — only `for … in`.
- No array indexing (`arr[i]`).
- No `return` from functions.
- Strings have no escape sequences; numbers are integers only.
