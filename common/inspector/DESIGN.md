# ScummVM Script Inspector — a common CDP debug interface for script VMs

Status: **design sketch + core implementation** (this branch).

## Goal

Give every ScummVM engine that has a bytecode/script interpreter a *single*
debugger back-end with a *single* network protocol, chosen so that stock
tooling — VS Code's built-in js-debug (`"type": "node", "request": "attach"`),
Chrome DevTools (`chrome://inspect`), and any Chrome-DevTools-Protocol client —
can attach, set breakpoints, single-step (into/over/out), inspect call stacks,
variables and VM registers, and watch variable reads/writes.

The model deliberately copies how V8 exposes **WebAssembly** to CDP: the
engine's script bytecode is the "generated code", presented to the client as a
disassembly listing (one instruction per line), and an optional **Source Map
v3** (ECMA-426) maps that listing back to the "original" higher-level source —
for SCI/AGI that is a LISP/Smalltalk-flavoured script listing produced by the
engine's own decompiler/disassembler, not JavaScript. The CDP server never
interprets source maps; it only advertises `sourceMapURL` on
`Debugger.scriptParsed` and reports all locations in generated (listing)
coordinates. The client does the rest, including rendering `sourcesContent`
for sources that exist nowhere on disk (this is exactly how Deno debugs
TypeScript and how emscripten's `-gsource-map` debugs C in the browser).

## Why CDP (and how we stay honest about it)

Prior art is clear that non-JS runtimes usually pick DAP (Godot, Unity, Bun,
Duktape, QuickJS) because the *frontend contract* of CDP is undocumented and
moves. We still pick CDP as the wire protocol because:

1. The user-facing integrations we care about (VS Code js-debug attach, Chrome
   DevTools, `chrome-remote-interface` scripts) all speak it natively — zero
   client code to ship.
2. The subset needed for attach/breakpoint/step is small and well understood:
   Hermes ships exactly `Debugger`/`Runtime` (+`Profiler`/`HeapProfiler`
   stubs), ~36 commands, 9 events.
3. A DAP adapter can be layered on later *outside* the engine (the
   Godot/Bun pattern); CDP-in-engine does not preclude it.

We mitigate the known risks by implementing the specific quirks documented by
other non-Chrome CDP implementors (see "Corner-case ledger" below) and by
keeping the protocol layer isolated behind a small engine-facing C++
interface, so the wire protocol could be swapped without touching engines.

## Architecture

```
                        engine thread                      timer/socket thread
┌──────────────────┐   ┌─────────────────────────────┐    ┌──────────────────────┐
│ engine VM loop   │──▶│ Inspector::Agent (adapter,  │    │ InspectorServer      │
│ (per-instruction │   │  one subclass per engine)   │    │ (backends/, SDL_net) │
│  hook, 1 branch) │   └────────────┬────────────────┘    │  - TCP listen :9229  │
└──────────────────┘                │                     │  - HTTP /json/*      │
                                    ▼                     │  - RFC6455 framing   │
                       ┌─────────────────────────────┐    └─────────┬────────────┘
                       │ Inspector::Session (common/)│              │
                       │  - CDP dispatch (Debugger,  │   in/out msg │ queues
                       │    Runtime domains)         │◀─────────────┘
                       │  - ScriptRegistry           │   (mutex-guarded strings)
                       │  - BreakpointStore          │
                       │  - StepController           │
                       │  - RemoteObject table       │
                       └─────────────────────────────┘
```

**Everything protocol-shaped lives in `common/inspector/` and is pure,
synchronous, OSystem-free code** operating on strings/streams — unit-testable
under `test/common/inspector/` with the existing cxxtest harness (`make
test`). Only the socket pump (`backends/networking/sdl_net/`) touches SDL_net,
mirrors `LocalWebserver`'s 20 Hz timer-thread polling, and is compile-gated
behind `USE_SDL_NET`.

### Threading and the pause pump

All protocol *state* is owned by the engine thread. The socket thread only:
decodes WebSocket frames → pushes complete JSON strings onto an inbound queue;
pops outbound strings → encodes frames. The engine thread drains the inbound
queue at hook points. When the VM pauses (breakpoint/step), the engine thread
blocks inside the hook in a **nested message pump** (the V8
`runMessageLoopOnPause` pattern) so `Debugger.evaluateOnCallFrame`,
`Runtime.getProperties` etc. keep being serviced while paused — a server that
stops reading the socket while paused deadlocks every client (Deno hit a
literal re-entrancy crash here, denoland/deno#5822). In unit tests the pump is
driven synchronously; no timers or sockets are needed.

### The engine-facing interface (`common/inspector/agent.h`)

Each engine implements `Inspector::Agent`:

- **Script registration (push).** When the engine loads/uncovers a script
  resource it calls `Session::registerScript()` with a `ScriptListing`:
  `{url, lines[] = {byteOffset, text, isStatement}, optional original source +
  line map}`. The registry derives: the disassembly text served by
  `Debugger.getScriptSource`, offset↔line translation, statement snapping for
  breakpoints and `getPossibleBreakpoints`, and (when an original source is
  supplied) a Source Map v3 data: URL.
- **Per-instruction hook (hot path).** The VM dispatch loop calls
  `agent->onInstruction(threadRef, scriptHandle, byteOffset, frameToken)`
  guarded by a single null/flag check so release-mode cost is one predictable
  branch. `frameToken` is an opaque, stable-per-activation identity (NOT a
  depth counter — see stepping).
- **Pull callbacks (only while paused / on demand):** `buildCallFrames()`,
  `enumerateScope(frame, scope, visitor)`, `evaluate(frame, expr)` (optional;
  default resolves bare variable names via scopes), `writeVariable()`.
- **Watchpoints (optional):** adapters route their engine's variable
  read/write choke points (`ScummEngine::writeVar`, `AgiEngine::setVar`, SCI's
  existing `bp_write`, …) into `Session::variableAccessHook()`.

### Script identity: the WASM model, adapted

Every script resource gets a synthetic, stable URL:

    scummvm-dbg://<engine>/<container>/<script-name-or-number>[.<ext>]

e.g. `scummvm-dbg://scumm/room11/local-2001`, `scummvm-dbg://sci/script-994/`,
`scummvm-dbg://kyra/_STARTUP.EMC`. Rules learned from js-debug URL-matching
bugs: the URL must be byte-for-byte stable across re-parses, canonical (no
extra slashes: vscode-js-debug#519), and lowercase-stable (js-debug regexes
are case-insensitive anyway). `Debugger.scriptParsed` carries
`executionContextId: 1` — the context announced by
`Runtime.executionContextCreated` — because Hermes shipped mismatched ids and
broke clients (facebook/react-native#34639), and js-debug's child-attach
probe hardcodes context id 1.

Locations use the **disassembly-listing convention**: `lineNumber` = listing
line (one instruction per line), `columnNumber` = 0, and the raw byte offset
is recoverable server-side. This is the "textual disassembly" variant of the
wasm model (wasm proper uses `line 0, column = byte offset`); it renders and
steps correctly in every CDP client with zero wasm-specific support, and the
sourcemap channel still maps lines back to the original-language listing.

## Stepping semantics (the part naive ports get wrong)

Distilled from gdb's contract, Hermes' implementation, and mobdebug's
documented failure of call/return counting:

1. **Statement boundaries, not raw instructions.** Listings flag statement
   starts (`isStatement`); steps land only on statement starts, so multi-
   instruction statements don't cause multiple stops (gdb: "only stops at the
   first instruction of a source line").
2. **Frame identity, not depth counters.** ScummVM engines re-enter their
   interpreters from native opcode handlers — exactly the `pcall`-from-C shape
   mobdebug's source declares "not reliable" for counters. On arming a step,
   the controller captures the current frame token *chain*; step-over
   completes when execution reaches a *different statement* whose frame token
   is the armed frame or any of its captured callers. A recursive call
   creates a fresh token (never in the chain) so recursion is skipped;
   returning collapses to the caller's token so **step-over at a return
   degrades to step-out** (Hermes does the same).
3. **Same statement re-entered via a loop must stop** — completion requires
   "different statement OR same statement re-entered", Hermes'
   `sameStatementDifferentInstruction` rule.
4. **Stepping is scoped to the initiating script thread.** Engines multiplex
   many runnable scripts (closer to Lua coroutines than a JS context); a step
   in script A must not stop in script B (mobdebug's `coroutine.running()`
   guard), but breakpoints still hit in any thread. Pause is *hard*: the whole
   VM blocks (games need consistency), so a "step" may execute other threads'
   opcodes before the stepped thread runs again; the per-thread predicate
   handles that.

## Protocol surface (v1)

Methods answered (everything else → error `-32601`, which clients
feature-detect on — no version negotiation exists in practice):

- `Runtime.enable` (emits `executionContextCreated`, id 1), `Runtime.disable`,
  `Runtime.runIfWaitingForDebugger` (releases wait-for-debugger startup mode —
  the `--inspect-brk` equivalent; without it fast games finish before attach,
  denoland/deno#9886), `Runtime.evaluate`, `Runtime.getProperties`,
  `Runtime.releaseObject`, `Runtime.releaseObjectGroup`, `Runtime.discardConsoleEntries`.
- `Debugger.enable` (returns `debuggerId`; replays `scriptParsed` for every
  known script), `Debugger.disable`, `Debugger.setBreakpointsActive`,
  `Debugger.setSkipAllPauses`, `Debugger.setBreakpointByUrl` (**both `url` and
  `urlRegex`** — js-debug converts file paths to case-insensitive regexes, so
  url-only servers never bind its breakpoints), `Debugger.setBreakpoint`,
  `Debugger.removeBreakpoint`, `Debugger.getPossibleBreakpoints`,
  `Debugger.getScriptSource`, `Debugger.pause`, `Debugger.resume`,
  `Debugger.stepOver`, `Debugger.stepInto`, `Debugger.stepOut`,
  `Debugger.evaluateOnCallFrame`, `Debugger.setPauseOnExceptions`,
  `Debugger.setAsyncCallStackDepth` (no-op), `Debugger.setBlackboxPatterns`
  (no-op).
- `Profiler.enable`/`Profiler.disable` (stub `{}` — js-debug always sends it).
- Vendor domain `GameScript.*` for what CDP cannot express:
  `GameScript.setWatchpoint {variable, scope, accessType: read|write|all}`,
  `GameScript.removeWatchpoint`, `GameScript.listThreads`. Watchpoint hits
  pause with `Debugger.paused reason:"other"` and
  `data: {gameScriptReason:"watchpoint", ...}`.

Events: `Runtime.executionContextCreated`, `Runtime.consoleAPICalled`,
`Runtime.exceptionThrown`, `Debugger.scriptParsed`, `Debugger.paused`,
`Debugger.resumed`, `Debugger.breakpointResolved` (still required by shipping
clients despite tip-of-tree deprecation).

Discovery (HTTP, same port): `/json/version` → `{"Browser": "ScummVM/<ver>",
"Protocol-Version": "1.1"}` (node-shaped: no `webSocketDebuggerUrl` here —
js-debug fetches both endpoints precisely because node omits it), `/json` and
`/json/list` → one target `{id, type:"node", title, url, description,
webSocketDebuggerUrl, devtoolsFrontendUrl}`. Query strings on these paths are
ignored (Chrome sends `?for_tab`; workerd broke on it, cloudflare/workerd#1388).
WebSocket upgrade served only at the advertised `/<uuid>` path; other paths →
404 before upgrade.

## Corner-case ledger (each encoded as a unit test)

| # | Corner case | Source | Where tested |
|---|---|---|---|
| 1 | Commands pipelined before `enable` ack → must apply synchronously, answer in order | vscode-js-debug#1109 | test/common/inspector/session.h |
| 2 | Breakpoint set before script parsed: accept, `locations: []`, bind later, emit `breakpointResolved` | vscode-js-debug#568, #883 | breakpoints.h |
| 3 | Reply exactly once to every command, even unknown → `-32601`; swallowed replies hang clients / eval loops | vscode-js-debug#981 | protocol.h |
| 4 | `executionContextId` in `scriptParsed` must match `executionContextCreated` (id 1) | facebook/react-native#34639 | session.h |
| 5 | `scriptId` is a string; `executionContextId` an integer; 0-based lines/columns (DAP's 1-based is the client's problem) | CDP spec; classic off-by-one | protocol.h, session.h |
| 6 | `urlRegex` with case-insensitive classes `[fF]`, `(?:…)` alternation, `($\|\?)` suffix must match | js-debug urlUtils.ts | regex.h, breakpoints.h |
| 7 | Duplicate `setBreakpointByUrl` at same location → error (V8 behaviour) | V8 | breakpoints.h |
| 8 | One breakpoint resolving into multiple scripts → all locations, id in `hitBreakpoints` on any hit | CDP spec | breakpoints.h |
| 9 | Step-over across recursion must not stop in the deeper activation (frame identity, not depth) | mobdebug source; Hermes | stepping.h |
| 10 | Step-over at return degrades to step-out | Hermes Debugger.cpp | stepping.h |
| 11 | Same statement re-hit via loop back-edge must stop | Hermes `sameStatementDifferentInstruction` | stepping.h |
| 12 | Step scoped to initiating thread; other threads' instructions don't complete it, breakpoints still do | mobdebug coroutine guard | stepping.h |
| 13 | Evaluate-while-paused re-entrancy (nested pump; never stop reading socket while paused) | denoland/deno#5822, v8 inspector API | session.h |
| 14 | js-debug's `typeof process === 'undefined'` probe → answer `"process not defined"` string, never error-loop | js-debug nodeAttacherBase.ts | session.h |
| 15 | Client→server frames MUST be masked (else close 1002); server→client MUST NOT be masked | RFC 6455 §5.1 | websocket.h |
| 16 | 125/126/65535/65536-byte payload length encodings (7/16/64-bit) both directions | RFC 6455 §5.2 | websocket.h |
| 17 | Fragmented client messages with interleaved control frames (ping mid-message) | RFC 6455 §5.4 | websocket.h |
| 18 | Ping → Pong echoing payload; close echo; no idle timeout (paused sessions are silent for hours) | RFC 6455 | websocket.h |
| 19 | Invalid UTF-8 in text frame → close 1007; lone surrogates must be escaped, never raw (our JSON emitter is pure-ASCII, sidestepping this on send) | ws#2252, RFC 6455 | websocket.h |
| 20 | Byte-at-a-time / arbitrarily-chunked TCP delivery reassembles correctly | any TCP server | websocket.h |
| 21 | `Sec-WebSocket-Accept` = b64(SHA1(key ++ GUID)) — RFC test vector | RFC 6455 §4.2 | sha1.h, discovery.h |
| 22 | VLQ: sign bit via logical shift (`>>>` not `>>`), INT32_MIN round-trip, values ≥ 2^31 rejected — huge columns are *by design* in bytecode maps | nodejs/node#31490, tc39/source-map#80 | sourcemap.h |
| 23 | Sourcemap `mappings`: generated column resets per line; source/line/col/name deltas do NOT | ECMA-426 | sourcemap.h |
| 24 | `/json/list?for_tab` and friends: ignore query strings in discovery paths | cloudflare/workerd#1388→#1390 | discovery.h |
| 25 | Host header tolerance: `localhost`, `127.0.0.1`, `[::1]` all accepted; non-local Host rejected (DNS rebinding, node model) | nodejs docs; js-debug endpoints.ts | discovery.h |
| 26 | Response-then-event ordering: ack `stepInto`/`resume` before `Debugger.resumed`/`paused` | client per-command state machines | session.h |
| 27 | Second concurrent WS client: refuse cleanly (single-session server), don't corrupt the first | Hermes single-connection pain | server (manual) |
| 28 | Integer ids echoed verbatim; `params` optional; error codes -32700/-32600/-32601/-32602 | JSON-RPC-ish CDP framing | protocol.h |

## Engine adapters in this branch (7+)

Hook sites verified against the current tree; each adapter is
`engines/<eng>/inspector-agent.{h,cpp}` plus a guarded one-line hook:

| Engine | Hook site | Frame identity source | Scripts/URLs |
|---|---|---|---|
| SCUMM | `script.cpp` `executeScript()` fetch/dispatch (and `executeOpcode` for recursive `o5_expression` dispatch) | `vm.nest[]` + slot + invocation serial | `scumm/<room>/<global\|local\|object>-N`; opcode-name trace tables |
| SCI | `vm.cpp` `run_vm()` pre-instruction block (existing debug hook block) | `s->_executionStack` entries | `sci/script-N`; full in-tree disassembler (`scriptdebug.cpp`) |
| AGI | `op_cmd.cpp` `runLogic()` post-fetch (+ `op_test.cpp` test loop) | `_game.execStack` | `agi/logic-N`; opcode name/param tables |
| KYRA (EMC) | `script/script.cpp` `EMCInterpreter::run()` — universal choke point | bp-chain in `EMCState::stack` | `kyra/<filename.EMC>`; 19 opcodes |
| AGS | existing `ccSetDebugHook` line hook + `cc_instance.cpp` pre-dispatch | `callStackSize` + `ccInstance*` | `ags/<section-name>`; `DumpInstruction` disassembler |
| Wintermute | existing virtual `preInstHook`/`postInstHook` (`DebuggableScript`) | `_callStack->_sP` + script ptr | `wintermute/<filename.script>`; source-level lines exist |
| Director | existing `g_debugger->stepHook()` + push/popContextHook | `LingoState::callstack` `CFrame*` | `director/<castlib>/<member>`; `decodeInstruction` + LingoDec |

Stretch (hook sites documented, adapters follow the same pattern): SLUDGE
(`continueFunction`, `calledBy` chain), Tinsel (`Interpret()` post-fetch,
bp/dynamic-link chain — its existing script-workaround matcher already keys on
`(hCode, ip)`, the natural breakpoint identity).

## Configuration

- `--enable-inspector` at configure time is not needed: the common/ core is
  always built (it is plain code, ~no size cost when unused); the socket
  server rides the existing `USE_SDL_NET` component.
- Runtime: config keys `inspector_port` (default 9229), `inspector_enable`
  (bool), `inspector_wait` (bool — hold the VM before the first instruction
  until `Runtime.runIfWaitingForDebugger`, the `--inspect-brk` equivalent).

## VS Code usage (once the server lands)

```jsonc
{ "type": "node", "request": "attach", "port": 9229 }
```

js-debug polls `/json/version` + `/json/list` every 200 ms, follows
`webSocketDebuggerUrl`, and needs nothing else HTTP-wise. Sources appear under
the `scummvm-dbg://` tree; breakpoints, stepping, call stacks, scopes and the
debug console (variable names, `Vars[13] = 5` style assignments via
`evaluate`) work through the standard UI. Chrome DevTools users:
`chrome://inspect` → the target advertises `type:"node"`.
