# EmberJIT

<p align="center">
  <strong>
    A compact statically typed language runtime with a verified bytecode VM,<br>
    typed control-flow IR, and a baseline x86-64 JIT compiler written in C++23.
  </strong>
</p>

<p align="center">
  <a href="https://github.com/zhbforum/EmberJIT/actions/workflows/ci.yml">
    <img src="https://github.com/zhbforum/EmberJIT/actions/workflows/ci.yml/badge.svg" alt="CI">
  </a>
  <img src="https://img.shields.io/badge/C%2B%2B-23-00599C?logo=c%2B%2B" alt="C++23">
  <img src="https://img.shields.io/badge/CMake-3.25%2B-064F8C?logo=cmake" alt="CMake 3.25+">
  <img src="https://img.shields.io/badge/JIT-Windows%20x64-0078D4" alt="Windows x64 JIT">
  <img src="https://img.shields.io/badge/license-MIT-green" alt="MIT License">
</p>

> **Status:** EmberJIT v0.1 is feature-complete and currently being prepared for its first public release.

EmberJIT is a small end-to-end language implementation built around a bytecode virtual machine and a tiered baseline JIT.

Ember source code is parsed and semantically analyzed into a fully resolved typed program, compiled to verified stack bytecode, and executed by the VM. Runtime profiling identifies hot functions, which can then be lowered into a typed control-flow IR, verified, optimized, compiled to x86-64 machine code, and safely published for native execution.

The project intentionally keeps the language and optimizer compact while treating verification, deterministic tooling, runtime contracts, native ABI behavior, and executable-memory safety as first-class parts of the design.

<p align="center">
  <img src="assets/emberjit-demo.gif" width="900" alt="EmberJIT terminal demo">
</p>

---

## Contents

- [Highlights](#highlights)
- [Quick start](#quick-start)
- [Architecture](#architecture)
- [CLI](#cli)
- [Benchmarking](#benchmarking)
- [Verification and quality gates](#verification-and-quality-gates)
- [Codebase tour](#codebase-tour)
- [Building from source](#building-from-source)
- [v0.1 language scope](#v01-language-scope)
- [v0.1 runtime scope](#v01-runtime-scope)
- [Known limitations](#known-limitations)
- [Design principles](#design-principles)
- [Why EmberJIT exists](#why-emberjit-exists)
- [License](#license)

---

## Highlights

- Static types: `i64`, `f64`, `bool`, and `void`
- Local variables, assignment, functions, calls, recursion, `if` / `else`, and `while`
- Lexing, parsing, semantic analysis, and a fully resolved typed AST
- Stable symbol and function identities after semantic analysis
- Deterministic stack bytecode
- Mandatory bytecode verification before VM execution
- Typed CFG-based intermediate representation
- Mandatory IR verification before native compilation
- Constant folding, constant propagation, CFG simplification, and dead-code elimination
- Runtime invocation profiling and tier dispatch
- Baseline x86-64 native code generation
- Native support for the complete v0.1 type set
- Windows x64 ABI integration
- W^X-style executable-memory publication
- VM fallback when native compilation cannot safely complete
- Deterministic token, AST, bytecode, IR, and native-code inspection
- Built-in VM / cold-JIT / warmed-JIT benchmarking
- MSVC, clang-cl, and Linux Clang CI
- ASan + UBSan, MSVC `/analyze`, clang-format, and warnings-as-errors gates

---

## Quick start

### Build on Windows

EmberJIT requires:

- CMake 3.25 or newer
- a C++23-capable compiler
- Windows x64 for the v0.1 native JIT backend

Clone and build with Visual Studio 2022:

```console
$ git clone https://github.com/zhbforum/EmberJIT.git
$ cd EmberJIT

$ cmake -S . -B build -G "Visual Studio 17 2022" -A x64
$ cmake --build build --config Release --parallel
```

Verify the executable:

```console
$ ./build/Release/ember.exe --version
EmberJIT 0.1.0
```

Run an example:

```console
$ ./build/Release/ember.exe run examples/vm_factorial.ember
120
```

### A small Ember program

```ember
fn main() -> void {
    let n: i64 = 5;
    let result: i64 = 1;

    while n > 1 {
        result = result * n;
        n = n - 1;
    }

    print_i64(result);
    return;
}
```

The same program can be inspected at every major compiler stage:

```console
$ ./build/Release/ember.exe dump-tokens examples/vm_factorial.ember
$ ./build/Release/ember.exe dump-ast examples/vm_factorial.ember
$ ./build/Release/ember.exe dump-typed-ast examples/vm_factorial.ember
$ ./build/Release/ember.exe dump-bytecode examples/vm_factorial.ember
$ ./build/Release/ember.exe dump-ir examples/vm_factorial.ember
$ ./build/Release/ember.exe dump-asm examples/vm_factorial.ember
```

---

## Architecture

EmberJIT is organized as a sequence of explicit compiler and runtime layers rather than one monolithic interpreter or compiler.

Each major stage owns a representation with a specific contract. Verification is used to create trusted boundaries between stages, and later components consume those trusted representations instead of repeatedly re-validating arbitrary input.

<p align="center">
  <img src="assets/emberjit-architecture.svg" alt="EmberJIT verified tiered execution architecture" width="100%">
</p>

<p align="center">
  <sub>Theme-adaptive SVG · Editable source: <a href="assets/emberjit-architecture.excalidraw">Excalidraw diagram</a></sub>
</p>

The most important architectural idea is not simply **VM versus JIT**.

It is the chain of increasingly trusted representations that lead to execution:

```text
Ember source
    │
    ▼
syntax tree
    │
    ▼
resolved typed program
    │
    ▼
stack bytecode
    │
    ▼
bytecode verifier
    │
    ▼
VerifiedProgram
    │
    ├──────────────► bytecode VM
    │
    │                    │
    │                    │ hot function
    │                    ▼
    │              bytecode → IR
    │                    │
    │                    ▼
    │               verified IR
    │                    │
    │                    ▼
    │              optimization
    │                    │
    │                    ▼
    │          verified optimized IR
    │                    │
    │                    ▼
    │             baseline compiler
    │                    │
    │                    ▼
    │             x86-64 emitter
    │                    │
    │                    ▼
    │              machine code
    │                    │
    │                    ▼
    │            RW → RX publication
    │                    │
    │                    ▼
    └─────────────► native execution
```

A failure in the native path does not invalidate the already verified bytecode program.

The VM remains a valid execution tier, and native state is published only after lowering, optimization, code generation, executable-memory creation, and protection changes have completed successfully.

---

### Trusted representations

EmberJIT uses verified wrapper types as architectural boundaries.

Conceptually:

```text
Program
  │
  ▼
Verifier
  │
  ▼
VerifiedProgram
```

and later:

```text
IR Function
    │
    ▼
IR Verifier
    │
    ▼
VerifiedFunction
```

This means downstream components can express stronger preconditions in their APIs.

The bytecode VM executes a `VerifiedProgram`, not arbitrary mutable bytecode.

The native pipeline similarly operates on verifier-checked IR rather than treating every IR object as executable by default.

Optimization passes preserve the same contract:

```text
VerifiedFunction
      │
      ▼
optimization pass
      │
      ▼
verification
      │
      ▼
VerifiedFunction
```

The baseline pipeline is intentionally small:

```text
constant folding
      ↓
constant propagation
      ↓
CFG simplification
      ↓
dead-code elimination
```

Passes operate on verifier-checked IR, and their output is checked again before becoming input to later native stages.

---

### Frontend

The frontend is split into syntax and semantics.

```text
source
  ↓
lexer
  ↓
tokens
  ↓
parser
  ↓
AST
  ↓
semantic analysis
  ↓
typed AST
```

The parser is responsible for syntax.

Semantic analysis then resolves the program into a representation suitable for compilation:

- lexical scopes
- variable declarations and references
- stable symbol identities
- user and host functions
- stable function identities
- forward function calls
- recursion
- argument and return types
- assignments
- conditions
- operators
- literal values
- typed expressions

Downstream stages therefore do not need to repeat source-level name lookup or type checking.

---

### Bytecode VM

The compiler lowers the typed program into deterministic stack bytecode.

For example:

```text
fn #3 () -> void
  local %0: i64
  local %1: i64
  0000  const.i64 5
  0001  store %0
  0002  const.i64 1
  0003  store %1
  0004  load %0
  0005  const.i64 1
  0006  gt.i64
  0007  jump_false 0017
  0008  load %1
  0009  load %0
  0010  mul.i64
  0011  store %1
  0012  load %0
  0013  const.i64 1
  0014  sub.i64
  0015  store %0
  0016  jump 0004
  0017  load %1
  0018  call #0
  0019  return_void
  0020  return_void
```

Bytecode is verified before execution.

The verifier protects the VM from malformed compiler output and establishes structural and type-related invariants expected by both the interpreter and later lowering stages.

The verified bytecode program also remains the semantic baseline for tiered execution.

---

### Typed control-flow IR

Hot bytecode functions are lowered to a typed, non-SSA control-flow IR.

A loop from the factorial example becomes:

```text
block b2:
  v2:i64 = load %0
  v3:i64 = const.i64 1
  v4:bool = gt.i64 v2, v3
  branch_if_false v4, b4, b3

block b3:
  v5:i64 = load %1
  v6:i64 = load %0
  v7:i64 = mul.i64 v5, v6
  store %1, v7
  v8:i64 = load %0
  v9:i64 = const.i64 1
  v10:i64 = sub.i64 v8, v9
  store %0, v10
  branch b2
```

The IR models:

- explicit basic blocks
- typed temporary values
- locals
- branches
- conditional branches
- calls
- returns
- integer operations
- floating-point operations
- boolean operations

The v0.1 IR intentionally does not use SSA or edge arguments.

That keeps the baseline compiler small while still providing an explicit CFG representation suitable for verification and optimization.

---

### Tiered runtime

Runtime tiering is kept outside the core bytecode interpreter.

Each user function is represented by runtime state that can contain:

```text
verified bytecode
      │
      ├── VM execution
      │
      └── native compilation source
                    │
                    ▼
              hotness threshold
                    │
                    ▼
                IR lowering
                    │
                    ▼
               optimization
                    │
                    ▼
            baseline compilation
                    │
                    ▼
            native publication
                    │
                    ▼
               native tier
```

Invocation profiling counts function activity.

When a function reaches the configured hot threshold, the runtime attempts baseline compilation.

Tier dispatch then selects between:

```text
virtualMachine
native
```

depending on whether a valid native implementation has actually been published.

A function does not become native merely because compilation was attempted.

It becomes native only after a usable `NativeCodeHandle` exists.

---

### Native JIT

The v0.1 native backend targets:

```text
Architecture: x86-64
ABI:          Windows x64
Tier:         baseline JIT
```

The baseline compiler supports the complete v0.1 type model:

```text
i64
f64
bool
void
```

It translates verified IR into x86-64 operations using a small internal emitter rather than delegating code generation to an external compiler backend.

The emitter owns:

- general-purpose register encodings
- XMM register encodings
- arithmetic instructions
- comparisons
- control-flow branches
- calls
- stack manipulation
- labels
- relative relocation
- instruction encoding
- final machine-code materialization

Branch displacement and code-size arithmetic are checked before emission is finalized.

The compiler also explicitly constructs its Windows x64 prologue, epilogue, call area, and preserved-register behavior.

---

### Native frame and ABI boundary

Generated code does not receive arbitrary VM implementation objects.

Native execution crosses a deliberately narrow interface based around a `NativeFrame`.

Conceptually:

```text
NativeFrame
├── locals
├── temporary value slots
├── call arguments
├── runtime call context
├── call bridge
└── error state
```

The frame provides machine code with the memory it needs while keeping native execution decoupled from most C++ runtime internals.

The native call boundary has a fixed ABI contract.

The executable-code handle owns the published address and is responsible for converting it to the expected native entry-point signature.

ABI-sensitive assumptions are kept localized rather than scattered throughout the runtime.

---

### Executable memory and W^X

EmberJIT does not allocate permanently writable and executable memory.

Native publication follows a W^X-style lifecycle:

```text
VirtualAlloc
PAGE_READWRITE
      │
      ▼
copy emitted machine code
      │
      ▼
VirtualProtect
PAGE_EXECUTE_READ
      │
      ▼
FlushInstructionCache
      │
      ▼
publish NativeCodeHandle
```

The important property is the publication boundary:

```text
emitted bytes
     │
     ▼
temporary executable allocation
     │
     ├── allocation failure ─────► discard
     ├── write failure ──────────► discard
     ├── protection failure ─────► discard
     ├── cache flush failure ────► discard
     │
     ▼
valid executable handle
     │
     ▼
publish native tier
```

A partially prepared executable allocation is never exposed to runtime dispatch.

Temporary native allocations are released on failure.

The runtime retains the verified VM representation when native publication cannot complete safely.

---

### Inspecting the compiler pipeline

One of EmberJIT's goals is to make the whole pipeline directly inspectable from the CLI.

Consider:

```ember
result = result * n;
```

The bytecode representation contains:

```text
load %1
load %0
mul.i64
store %1
```

The corresponding IR contains:

```text
v5:i64 = load %1
v6:i64 = load %0
v7:i64 = mul.i64 v5, v6
store %1, v7
```

And the native compiler emits actual x86-64 machine code:

```text
00D0: 00 49 89 85 28 00 00 00 48 8B 83 00 00 00 00 49
00E0: 89 85 30 00 00 00 49 8B 85 28 00 00 00 4D 8B 85
00F0: 30 00 00 00 49 0F AF C0 49 89 85 38 00 00 00 49
0100: 8B 85 38 00 00 00 48 89 83 08 00 00 00
```

So the same operation can be followed through:

```text
Ember source
     ↓
verified bytecode
     ↓
typed CFG IR
     ↓
optimized IR
     ↓
x86-64 machine code
```

No debugger-only build or private developer mode is required.

### Architectural contracts

The most important module boundaries can be summarized as follows:

| Boundary | Contract |
| --- | --- |
| Source → lexer | UTF-8 source text is tokenized without assigning semantic meaning |
| Lexer → parser | The parser consumes tokens and constructs a syntax-only AST |
| Parser → semantic analysis | AST is resolved into typed declarations, expressions, symbols, and functions |
| Semantic → bytecode | Bytecode compilation consumes a successfully analyzed typed program |
| Bytecode → VM | Only verifier-produced bytecode becomes executable |
| Bytecode → IR | IR lowering consumes verified bytecode |
| IR optimization | Every pass accepts and returns verifier-checked IR |
| IR → JIT | Native compilation consumes verified IR |
| JIT → runtime | Machine code remains private until executable publication succeeds |
| Native failure → runtime | Failure preserves the valid VM representation |
| VM ↔ native | Both execution tiers preserve the same observable v0.1 semantics |

This design keeps invalid or partially constructed representations from silently crossing execution boundaries.

---

## CLI

```text
usage: ember <command> [options] <file>

commands:
  run                 execute a program
  benchmark           measure VM, cold JIT, and warmed JIT execution
  dump-tokens         print lexer tokens
  dump-ast            print the syntax tree
  dump-typed-ast      print the typed syntax tree
  dump-bytecode       print verified bytecode
  dump-ir             print optimized IR
  dump-asm            print baseline native-code listings

global options:
  --help, help        show help
  --version           show the version
```

| Command | Purpose |
| --- | --- |
| `run` | Execute an Ember program |
| `benchmark` | Measure VM, cold-JIT, and warmed-JIT execution |
| `dump-tokens` | Inspect lexer output |
| `dump-ast` | Inspect the parsed syntax tree |
| `dump-typed-ast` | Inspect resolved semantic output |
| `dump-bytecode` | Inspect verified stack bytecode |
| `dump-ir` | Inspect optimized typed IR |
| `dump-asm` | Inspect emitted native code |

Command-specific help is available:

```console
$ ember run --help
$ ember benchmark --help
$ ember dump-ir --help
```

---

## Benchmarking

EmberJIT contains two different forms of performance measurement.

### CLI execution benchmark

The public CLI can compare complete program execution across three modes:

```console
$ ember benchmark --iterations=100 examples/vm_hot_loop.ember
```

It reports:

```text
VM
cold JIT (includes native compilation)
warmed JIT
```

The three measurements represent different costs.

**VM** runs the verified bytecode program without native compilation.

**Cold JIT** includes the cost of creating runtime state and compiling native code.

**Warmed JIT** measures repeated execution after the native tier has already been prepared.

Program stdout is suppressed while measuring so repeated host calls do not dominate the terminal output.

Results are intentionally machine-local reference measurements. They depend on the workload, CPU, compiler build, operating system, and runtime configuration and should not be treated as portable performance guarantees.

### Runtime microbenchmark

The repository also contains a focused runtime benchmark for measuring dispatch and native execution behavior without conflating those costs with the full CLI workflow.

Build it with:

```console
$ cmake -S . -B build-bench -DEMBER_BUILD_BENCHMARKS=ON
$ cmake --build build-bench --config Release --parallel
```

The microbenchmark is intended for runtime engineering and regression tracking rather than as a universal benchmark of the Ember language.

---

## Verification and quality gates

EmberJIT treats verification and regression coverage as part of the runtime architecture rather than optional development tooling.

The CI pipeline currently covers:

| Check | Configuration |
| --- | --- |
| MSVC | Debug + Release |
| clang-cl | Debug + Release |
| Linux Clang | Release |
| Sanitizers | Linux Clang / ASan + UBSan |
| Static analysis | MSVC `/analyze` |
| Formatting | clang-format 18 |
| Warnings | warnings-as-errors |

The test suite covers:

- lexer and parser behavior
- semantic analysis
- scope and type rules
- bytecode compilation
- bytecode verification
- VM execution
- IR lowering
- IR verification
- optimization passes
- x86-64 emitter behavior
- exact emitter boundary arithmetic
- native ABI behavior
- recursion
- mixed VM/native calls
- VM/JIT semantic equivalence
- executable-memory ownership
- native publication lifecycle
- staged compilation failures
- deterministic diagnostics
- deterministic compiler dumps
- large generated source inputs
- malformed inputs
- hostile paths
- CLI exit codes and command behavior

Native lifecycle regression tests deliberately inject failures after major compilation/publication stages to verify that the runtime never exposes partial executable state or leaks temporary executable allocations.

---

## Codebase tour

The repository layout mirrors these compiler/runtime layers.

| Area | Responsibility | Useful starting points |
| --- | --- | --- |
| `support` | Source text, source locations, diagnostics, shared semantics | `source.*`, `diagnostic.hpp` |
| `frontend` | Lexer, parser, AST, syntax inspection | `lexer.*`, `parser.*`, `ast.hpp` |
| `semantic` | Scopes, types, symbols, functions, typed AST | `analyzer.*`, `typed_ast.hpp` |
| `bytecode` | Typed program → stack bytecode and bytecode verification | `bytecode.*` |
| `ir` | CFG IR, bytecode lowering, verification, optimization, dumps | `bytecode_lowerer.*`, `verifier.*`, `optimization.*` |
| `jit` | Baseline compiler, x86-64 emitter, executable memory | `baseline_compiler.*`, `emitter.*`, `code_buffer.*` |
| `runtime` | VM, profiling, tier dispatch, native state and lifetime | `vm.*`, `runtime_function.*`, `native_code.*` |
| CLI | User-facing execution, dumps, help, benchmark | `src/main.cpp` |
| `tests` | Unit, regression, golden, invariant, and hostile-input tests | subsystem-specific suites |
| `benchmarks` | Focused runtime measurements | `runtime_dispatch_benchmark.cpp` |

The physical tree is:

```text
EmberJIT/
├── benchmarks/
│   └── runtime_dispatch_benchmark.cpp
│
├── examples/
│   └── *.ember
│
├── include/ember/
│   ├── bytecode/
│   ├── frontend/
│   ├── ir/
│   ├── jit/
│   ├── runtime/
│   ├── semantic/
│   └── support/
│
├── src/
│   ├── bytecode/
│   ├── frontend/
│   ├── ir/
│   ├── jit/
│   ├── runtime/
│   ├── semantic/
│   └── support/
│
├── tests/
│   ├── bytecode/
│   ├── fixtures/
│   ├── frontend/
│   ├── golden/
│   ├── ir/
│   ├── jit/
│   ├── runtime/
│   ├── semantic/
│   └── support/
│
├── .github/
│   └── workflows/
│
├── CMakeLists.txt
├── LICENSE
└── README.md
```

The CMake target structure follows the same direction:

```text
ember_support
      ↓
ember_frontend
      ↓
ember_semantic
      ↓
ember_bytecode
      ↓
ember_ir
      ↓
ember_jit
      ↓
ember_runtime
      ↓
ember
```

`ember_runtime` is the integration point where verified bytecode execution and native tiering meet.

The JIT itself remains separated into IR-aware compilation, instruction encoding, and executable-memory ownership instead of placing all native behavior in a single compiler class.

---

### Reading the implementation

The implementation intentionally avoids comments that only translate C++ syntax into English.

For example, code like:

```cpp
++invocationCount;
```

does not need:

```cpp
// Increment the invocation count.
```

Comments are instead concentrated around places where the **reason** for an implementation choice is not obvious from the local code.

Examples include:

- why the IR uses a single-execution preheader for function parameters;
- why bytecode operand stacks must be empty at CFG boundaries in the non-SSA IR;
- why optimization passes are verifier-to-verifier transformations;
- why a canonical IR preheader can collapse to a native branch;
- Windows x64 ABI register and stack assumptions;
- the native entry-point conversion boundary;
- executable-memory ownership and publication;
- failure paths where VM fallback must remain intact.

For example, the bytecode-to-IR lowerer documents a subtle control-flow invariant around parameter initialization:

```cpp
// The preheader runs exactly once. Bytecode pc 0 deliberately starts in
// b1, so a loop backedge to pc 0 cannot reinitialize parameter locals.
```

The optimization layer documents its trust boundary:

```cpp
// Every pass accepts only verifier-checked IR and returns verifier-checked IR.
```

And the native invocation boundary documents the ABI assumption behind the executable entry-point conversion.

This is intentional.

The goal is for types, APIs, and control flow to explain the normal mechanics of the implementation, while comments preserve context around invariants, ABI requirements, security-sensitive behavior, and decisions that would otherwise require reconstructing several compiler stages mentally.

---

## Building from source

### Requirements

- CMake 3.25+
- C++23-capable compiler
- Windows x64 for native JIT execution

### Windows / MSVC

```console
$ cmake -S . -B build -G "Visual Studio 17 2022" -A x64
$ cmake --build build --config Release --parallel
$ ctest --test-dir build -C Release --output-on-failure
```

### Windows / clang-cl

```console
$ cmake -S . -B build-clangcl -G "Visual Studio 17 2022" -A x64 -T ClangCL
$ cmake --build build-clangcl --config Release --parallel
$ ctest --test-dir build-clangcl -C Release --output-on-failure
```

### Linux / Clang

```console
$ CC=clang CXX=clang++ cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
$ cmake --build build --parallel
$ ctest --test-dir build --output-on-failure
```

The frontend, semantic pipeline, bytecode VM, IR, and associated tooling are built and tested outside Windows as well.

Native JIT execution in v0.1 is currently specific to Windows x64.

---

## v0.1 language scope

EmberJIT v0.1 deliberately keeps the language small enough for the complete execution pipeline to remain understandable and thoroughly testable.

### Types

```text
i64
f64
bool
void
```

### Control flow and functions

Supported:

- local variables
- assignment
- functions
- typed parameters
- return values
- function calls
- forward calls
- recursion
- mutual recursion
- `if`
- `if / else`
- `while`
- host functions

### Expressions

The v0.1 pipeline supports the corresponding typed arithmetic, comparison, boolean, and function-call expressions required by these language features.

---

## v0.1 runtime scope

Supported:

- verified bytecode execution
- function frames
- runtime errors
- recursion
- runtime invocation profiling
- configurable hot threshold
- tier dispatch
- bytecode → IR lowering
- baseline IR optimizations
- x86-64 baseline compilation
- mixed VM/native calls
- native execution of `i64`, `f64`, `bool`, and `void`
- deterministic inspection tooling
- VM fallback after unavailable or failed JIT compilation

---

## Known limitations

The following are intentionally outside the v0.1 scope:

- strings
- arrays
- structs
- other aggregate user-defined types
- garbage collection
- managed heap objects
- exceptions
- multithreading
- ARM64 native code generation
- `f32`
- SSA IR
- register allocation
- spilling infrastructure
- common-subexpression elimination
- inlining
- loop optimization
- `break`
- `continue`

The baseline native compiler is intentionally simple.

The first release focuses on building a complete, verified path from source text to VM execution and then to safe native execution rather than maximizing language size or optimizer sophistication.

---

## Design principles

EmberJIT is built around a small set of explicit engineering constraints.

### Verify before execution

Representations with execution consequences are validated before becoming trusted inputs to later stages.

### Fail closed

Native compilation failure must not publish partial executable state.

The verified VM execution path remains available.

### Keep tiers semantically equivalent

VM and native execution are expected to preserve the same observable v0.1 behavior.

### Make compiler state inspectable

Tokens, syntax trees, typed trees, bytecode, IR, and machine code can be inspected through deterministic CLI commands.

### Make unsafe boundaries narrow

Executable memory, native entry-point conversion, ABI assumptions, and generated-code interaction with the runtime are kept behind small dedicated interfaces.

### Prefer explicit invariants over implicit assumptions

Important stage transitions are represented through types, verifiers, ownership boundaries, and regression tests rather than depending on callers to remember undocumented preconditions.

### Prefer a complete baseline over premature complexity

v0.1 prioritizes a coherent end-to-end compiler/runtime architecture before features such as SSA, register allocation, aggressive optimization, or a larger object model.

---

## Why EmberJIT exists

EmberJIT is primarily an exploration of how the pieces of a small production-style language runtime fit together.

The interesting part is not any one individual component.

A lexer, VM, IR, assembler, or executable-memory wrapper can each be implemented independently.

The project is about making all of them form one consistent system:

```text
language semantics
       ↓
verified bytecode
       ↓
portable execution
       ↓
runtime profiling
       ↓
verified IR
       ↓
optimization
       ↓
machine-code emission
       ↓
ABI integration
       ↓
safe executable publication
       ↓
tiered native execution
```

The emphasis is therefore on contracts between layers, predictable failure behavior, deterministic tooling, and keeping the native execution boundary understandable enough to test directly.

---

## License

EmberJIT is available under the MIT License.

See [LICENSE](LICENSE) for the full license text.
