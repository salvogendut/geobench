# Browser DOX Feasibility

Issue [#487](https://github.com/salvogendut/geobench/issues/487) investigates
using the DOX document model implemented by
[`symapp-symzilla`](https://github.com/salvogendut/symapp-symzilla) and the
HTML-to-DOX converter already present in
[`GB-proxy`](https://github.com/salvogendut/GB-proxy). The goal is a smaller,
more deterministic Browser while retaining the current CPC, MSX2, and PCW
network transports and fitting GEOBENCH's preemptive execution model.

## Conclusion

The approach is feasible, and the first negotiated implementation now exists
in the issue branches for GEOBENCH and GB-proxy. GB-proxy emits and validates a
strict `geobench-1` DOX profile with lazy proxy image references. `BROWSER.APP`
retains direct streamed HTML, but requests the bounded DOX profile whenever a
proxy is configured. A separate `GBDOX.MOD` validates and incrementally
publishes paragraphs, links, and two-to-four-column table rows; visible images
are fetched and converted lazily into Browser's bounded current-page cache.

It is not a drop-in replacement for the current Browser. SymZilla loads a TEXT
area plus separate banked LINK, CTRL, and per-image SGX allocations. A default
GB-proxy response may be as large as 96 KiB and may embed 64 KiB of graphics.
GEOBENCH's Browser currently occupies almost one complete 16 KiB app page and
borrows a single 16 KiB page for rendered rows. Copying SymZilla's allocation
model would exhaust the small shared page pool and compete with Viewer and
other windows.

The implemented first stage is therefore:

1. Keep the current native HTML path operational during development.
2. Add an app-loaded `GBDOX.MOD`; do not add DOX code or state to the kernel.
3. Preserve paragraph, link, graphic, and table-row semantics while publishing
   the compact records already consumed by Browser's bounded renderer.
4. Negotiate smaller resource limits and proxy-local `.PIC` image references
   with GB-proxy, reusing GEOBENCH's existing image codec.
5. Parse and publish bounded work from the Browser frame callback.
6. Keep forms, continuation pages, offline `.DOX`, and fuller pixel-geometry
   fidelity as explicit follow-up milestones.

This should be treated as a renderer replacement project, not as another
parser compiled into the already-full Browser app.

## Existing Implementations

### Current GEOBENCH Browser

`BROWSER.APP` currently:

- parses HTTP and HTML incrementally without retaining a DOM;
- supports direct plain HTTP on CPC, MSX2, and PCW;
- uses an optional plain-HTTP GB-proxy for HTTPS sites and simplification;
- stores up to 208 rendered 49-byte rows in one borrowed 16 KiB page;
- captures up to three borrowed pages of HTML source for offline `.HTM` Save;
- retains links, one-line GET forms, simple tables, and lazy image records;
- fetches only visible images, one at a time, through `GBIMG.MOD`;
- retains converted pictures in up to two borrowed 16 KiB banks so scrolling
  redraws cached images without another network request;
- pauses an open response after filling the viewport and resumes it on scroll;
- keeps networking and rendering as bounded root-owned frame work.

Its main strength is demand streaming. A long page does not have to fit in RAM
before the first viewport is useful. Its main weakness is complexity: HTTP,
HTML state, line layout, resources, forms, tables, lazy images, source capture,
and three transports meet in one nearly full application page.

The current built artifacts are approximately 15 KiB on CPC and 16 KiB on MSX
and PCW. `GBWEB.MOD` is about 8 KiB and `GBIMG.MOD` about 7 KiB. Browser already
uses compact private controls because linking the generic widget unit exceeded
the PCW app-loader ceiling.

### SymZilla DOX

The current `symapp-symzilla` source recognizes these chunks:

| Chunk | Purpose |
|---|---|
| `INFO` | Bounded metadata strings, including title. |
| `HEAD` | Minimum/maximum layout width, background, and format version. |
| `TEXT` | Text plus layout, formatting, link, graphic, table, and control bytecode. |
| `GRPH` | Counted embedded extended-SGX graphic records. |
| `LINK` | Counted method and URL records. |
| `CTRL` | Optional bounded GET form controls and strings. |
| `ENDF` | Empty final chunk. |

SymZilla loads the TEXT chunk into its remaining application data area. LINK
and CTRL receive separate banked allocations, and every non-empty SGX graphic
receives another allocation. The renderer then interprets the TEXT bytecode by
paragraph and column while resolving references into those tables.

The format is more than a container around plain text. Its paragraph, column,
frame, alignment, spacing, inline graphic, link, and control semantics are why
SymZilla renders better than GEOBENCH's current line-oriented HTML path. The
GEOBENCH implementation should preserve those semantics for every opcode that
GB-proxy emits rather than translate the document into the old fixed-width row
format.

### GB-proxy

GB-proxy performs strict per-request negotiation. SymZilla uses:

```http
Accept: application/x-symbos-dox
X-GB-SGX: 0,4
```

GEOBENCH uses the smaller explicit request (the MIME `Accept` is optional):

```http
X-GB-DOX: geobench-1
X-GBPC: 1
```

MSX Mode 7 advertises `X-GBPC: 7,1`. The supported SGX capability values are
`0,2`, `0,4`, and `5,16`. The proxy
emits the complete chunk set, downloads images eagerly, converts them to SGX,
and validates the result before returning it. It also generates bounded error
pages as DOX.

Current default and hard constraints include:

| Resource | Current bound |
|---|---:|
| Complete document | 96 KiB |
| TEXT payload | 11,500 bytes by default; 11,764 hard maximum |
| Links | 64 by default; 254 format maximum |
| Graphics | 8 by default; 127 validated maximum |
| Aggregate graphics | 64 KiB by default |
| One graphic | Less than 16 KiB |
| Image dimensions | 160x96 default; each dimension at most 255 |
| Controls | 16 |
| CTRL working allocation | 2 KiB |
| URL | 127 bytes |
| Tables | Up to 4 columns and 64 rows by default |

These are sensible SymZilla limits, not safe defaults for GEOBENCH's smaller
shared app-page pool.

## Native HTML Versus DOX

| Area | Native streamed HTML | Proxy-produced DOX |
|---|---|---|
| Direct HTTP | Works without a proxy. | Web conversion requires GB-proxy. |
| HTTPS and modern pages | Requires GB-proxy simplification. | Naturally handled by GB-proxy upstream. |
| First paint | Begins while the body arrives. | Simple if buffered; streaming needs a chunk-aware design. |
| Long pages | Pauses and resumes the network stream on demand. | Default DOX eagerly describes the complete page. |
| Client complexity | HTTP framing plus HTML parsing and layout. | HTTP framing plus strict chunk validation and bytecode rendering. |
| Predictability | Must tolerate varied and malformed HTML. | Deterministic bounded binary input after proxy validation. |
| Images | Lazy, visible-only GBPC fetches. | Eager embedded SGX graphics by default. |
| Forms | Compact HTML-derived GET form. | Explicit bounded `CTRL` records for GET forms. |
| Offline files | Existing `.HTM` Load and Save. | Natural `.DOX` Load and Save can be added. |
| Shared ecosystem | GEOBENCH-specific renderer. | Can share documents and proxy logic with SymZilla. |
| Security surface | Parser sees hostile upstream markup, even after simplification in direct mode. | Proxy strips active content; client still must validate every offset and length. |
| Low-memory behavior | Seven-line fallback and one-page line cache exist. | Requires a GEOBENCH-specific allocation and truncation policy. |

DOX is a clear improvement when a proxy is available: it removes variable HTML
from the 8-bit client, gives links/forms/images explicit records, and moves the
expensive conversion to a modern host. Native HTML remains valuable for direct
plain-HTTP access and as a fallback when no proxy is configured.

## GEOBENCH Resource Profile

The first GEOBENCH implementation should accept the existing chunk envelope and
render the same TEXT bytecode emitted for SymZilla. A GEOBENCH capability header
should select lower resource limits, pagination, and lazy image records while
leaving the document layout semantics unchanged. For example:

```http
Accept: application/x-symbos-dox
X-GB-DOX: geobench-1
X-GBPC: 1
```

The exact header spelling is an implementation detail to agree with GB-proxy.
It must be consumed by the proxy and included in `Vary`.

The implemented `geobench-1` limits are:

| Resource | Bound |
|---|---:|
| Complete document | 16,384 bytes |
| TEXT payload | 4,096 bytes |
| Links / URL bytes | 16 / 47 |
| Graphic references / aggregate bytes | 127 format IDs / 8,192 |
| Controls / CTRL bytes | 8 / 512 |
| Table columns / rows / cells | 4 / 24 / 96 |
| Image dimensions | 160x96 |

ASCII text uses the platform font. The first renderer preserves table row and
column structure, but intentionally uses Browser's uniform compact grid rather
than reproducing every SymZilla pixel-width and frame attribute. Forms are
validated by GB-proxy but are not yet rendered by the DOX client path.

### Images

GEOBENCH does not need a new SGX decoder. GB-proxy already converts images to
GBPC v2 for the current Browser, and GEOBENCH already validates and displays
four-colour Mode-1 and sixteen-colour MSX Mode-7 `.PIC` payloads. The negotiated
profile therefore keeps the counted `GRPH` table and TEXT graphic IDs, but each
page-image record is a compact proxy-local HTTP URL. The proxy converts that
resource to a self-identifying GBPC v2 payload only when Browser scrolls it into
view. The byte-sized ID allows 127 records; this is a wire-format ceiling, not
an image allocation or page-level conversion cap.

SymZilla requests `X-GB-SGX` and continues receiving canonical SGX records.
GEOBENCH's `BROWSER.APP` requests the GEOBENCH DOX profile plus `X-GBPC` and
receives PIC records. This is selected independently on every HTTP request, not
as a global proxy mode. GB-proxy already varies image output by client
capabilities, so this is one conversion pipeline with two negotiated codecs
rather than a fork of the HTML-to-DOX layout logic.

GEOBENCH retains each reference as an ID in the bounded raw DOX slice and fetches
only visible PICs into a bounded current-page cache. Off-screen images consume
neither an app page nor DOX payload space, so image-heavy tables are not reduced
to an arbitrary small number of pictures.

Image-bearing files from this profile will not display in an unmodified
SymZilla, and canonical SGX-bearing DOX will initially display without images
in GEOBENCH. That is an acceptable compatibility boundary because proxy
negotiation always returns the codec requested by the client. Optional SGX
support can be considered later, but it is not required for the Browser work.

### Pagination And Long Pages

A single complete 96 KiB DOX response is unsuitable. There are three possible
policies:

1. Truncate the document to one page. This is simplest but regresses browsing.
2. Stream TEXT and stop at a complete paragraph when the borrowed page is full.
   This cannot reach later content unless the TCP stream remains open.
3. Let GB-proxy expose stable, short continuation URLs/tokens for page slices.

The third policy is recommended. Each DOX response should be independently
valid and fit the client profile. Scrolling near the end requests the next
slice; Back or upward scrolling uses the retained current/previous slice. This
survives connection closure, bounds RAM, and avoids keeping a server TCP socket
open while the user reads. Continuation tokens need the same expiry and bounded
registry policy already used for shortened proxy URLs.

## Proposed Client Architecture

### `BROWSER.APP`

Keep ownership of:

- URL field, menus, history, status, scroll position, and window lifecycle;
- HTTP transport and cancellation;
- deciding between direct HTML and negotiated DOX;
- the raw bounded DOX slice and its small validated chunk/resource index;
- scheduling one bounded parser or renderer operation per frame.

### `GBDOX.MOD`

The app-loaded module is responsible for:

- validating chunk order, uniqueness, lengths, counts, and references;
- indexing required chunks without retaining untrusted pointers across a bank
  switch;
- interpreting the bounded paragraph, link, graphic, and table commands emitted
  by the current GEOBENCH profile;
- resolving LINK, CTRL, and visible proxy image `GRPH` records without
  allocating one bank per resource;
- exposing `init`, one-record `parse-step`, and visible-graphic `load` operations;
- returning `done`, `more`, or a specific validation failure.

The module does not call screen drawing, networking, or window services. It
publishes a validated visible image URL to Browser, which uses the same network
and one-slot GBPC path as native HTML. Only one Browser operation may own the
shared low-RAM transfer area at a time.

### Native HTML

During development, keep the current parser for direct mode. Once DOX reaches
feature parity, measure both paths. If carrying both formats remains too large,
move native HTML parsing to a separate `GBHTML.MOD` or ship two Browser variants
rather than increasing the resident kernel or exceeding the app page.

## Preemptive Execution

Browser is not a safe pure compute worker. Its network, firmware, modules,
bank-switching, drawing, and storage calls must remain on the non-preemptible
root task. Marking the whole Browser as a `TASK=1` worker would violate the
scheduler contract.

For Browser, "preemptible" therefore means bounded root-owned jobs:

- one DNS, connect, send, receive, module, or storage operation per state step;
- a fixed receive burst per frame;
- a fixed number of DOX chunk bytes or TEXT commands interpreted per frame;
- a fixed number of display records published per frame;
- visible PIC decoding split into the existing bounded module states;
- immediate cancellation that closes the transport and releases every page;
- repaint only after complete records are published.

This matches the current File Manager, Settings, Paint, Notepad, Icon Editor,
Shell, and Browser audit model. Timer preemption cannot interrupt a kernel,
firmware, paged-module, or transport call, so long polling loops inside those
services must still be shortened independently.

The PCW PerryNet transport remains a specific risk: connect/send/receive wait
for serial acknowledgements in bounded but potentially long polling loops.
DOX does not fix that. The transport should eventually expose incremental
request/ack states so each Browser frame performs a limited poll budget.

## Benefits

- GB-proxy already contains the costly HTML, Markdown, table, form, image, and
  validation work.
- The 8-bit client receives a deterministic representation instead of parsing
  arbitrary HTML.
- Explicit records reduce accidental coupling between visible labels and proxy
  transport URLs.
- A strict profile is straightforward to fuzz and reject safely.
- Shared DOX content can serve both SymZilla and GEOBENCH.
- Page slicing can give long pages predictable memory use.
- A renderer module may eventually reclaim more Browser app-page space than it
  consumes, especially if native HTML is also modularized.
- Local `.DOX` files become a useful compact offline-document format.

## Limits And Risks

- A proxy becomes mandatory for DOX web browsing; direct mode still needs HTML.
- Exact SymZilla layout behavior is significantly more work than parsing chunk
  headers, but it is also the main reason to choose DOX.
- Image-bearing GEOBENCH-profile DOX is not directly interchangeable with
  canonical SGX-bearing SymZilla DOX.
- The current SymZilla limits are too large for GEOBENCH.
- Carrying HTML and DOX in one app page is not viable without modularization.
- Page slicing is a GB-proxy extension and needs stable continuation semantics.
- Malformed DOX can otherwise cause bank overreads, invalid resource IDs, or
  infinite bytecode loops; validation must precede rendering.
- Proxy tokens expire and do not survive server restarts, so saved online pages
  may contain stale continuation or shortened links.
- DOX deliberately does not provide JavaScript, CSS layout, POST, authentication
  sessions, arbitrary downloads, or full HTML forms.
- Network calls remain non-preemptible while inside platform drivers.

## Delivery Plan

### Phase 1: Format Probe (implemented)

- Add host-side golden fixtures generated by GB-proxy.
- Document every TEXT opcode emitted by GB-proxy and its SymZilla behavior;
  reject unknown opcodes safely.
- Build a small validator/indexer with malformed and boundary tests.
- Define the GEOBENCH capability header and low-memory limits in GB-proxy.
- Return one-slice DOX fixtures with representative paragraph, column, frame,
  table, link, control, and lazy graphic-reference commands.

Exit criterion: the same fixture validates identically on the host and in a
small CPC/MSX/PCW diagnostic app, with no kernel changes.

### Phase 2: Browser Text, Links, Tables, And PIC (prototype implemented)

- Add `GBDOX.MOD` and format negotiation.
- Render bounded paragraphs, links, compact table grids, and visible PICs.
- Keep native HTML as direct-mode fallback.
- Add cancellation and malformed/truncated response errors.
- Loading local `.DOX` files remains follow-up work.

Exit criterion: example.com, FrogFind, NeverSSL, and saved fixtures navigate and
scroll on CPC, MSX2, and PCW with responsive pointer and clock.

### Phase 3: Continuations And Forms

- Add proxy-generated page slices and stable continuation tokens.
- Add the bounded `CTRL` subset for one-line GET search forms.
- Preserve Back across slices and ordinary links.

Exit criterion: FrogFind search and a page longer than the local cache work
without truncation or an open idle TCP connection.

### Phase 4: Lazy Images (prototype implemented)

- Extend GB-proxy's DOX `GRPH` serializer and validator with compact external
  image references for the GEOBENCH profile.
- Fetch and decode only visible PIC resources from those references.
- Reuse the existing one-image-slot discipline rather than allocating one bank
  per `GRPH` record.
- Advertise CPC/PCW/Screen-6 four-colour or Screen-7 sixteen-colour GBPC
  capability.

Exit criterion: image-bearing pages use no additional bank per off-screen
image and retain the current low-memory fallback.

### Phase 5: Consolidation

- Compare app/module size, first-paint latency, page throughput, and RAM use.
- Decide whether native HTML remains built in, moves to `GBHTML.MOD`, or becomes
  a separate legacy Browser build.
- Add `.DOX` association and offline Save only after the format is stable.
- Update user documentation and proxy setup instructions.

## Test Matrix

Every phase should cover:

- CPC floppy, M4, and Albireo/Net4CPC;
- MSX2 Screen 6 and Screen 7 using TCP/IP UNAPI;
- PCW using PerryNet/PerryFi;
- direct mode, proxy mode, local file mode, cancellation, timeout, and close;
- zero-length, truncated, duplicate, reordered, oversized, and unknown chunks;
- bad TEXT opcodes and missing LINK/CTRL/GRPH references;
- cache exhaustion and no-free-page startup;
- pages with no links, maximum links, no controls, maximum controls, and long
  URLs;
- pointer, clock, and another open app remaining responsive during download and
  parsing.

The initial live acceptance target is `http://retrocheats.neocities.org`. A
host regression fixture verifies its representative two-row, three-column
image grid retains all six lazy image resources without fetching any during
page conversion. A separate fixture verifies that three two-column HTML table
rows remain three bounded DOX table rows.

## Decision

Proceed with a SymZilla-compatible layout prototype using a separate
`GBDOX.MOD` and lower resource limits in GB-proxy. Preserve the DOX semantics
that make SymZilla render well. Do not replace native HTML yet, do not load the
default 96 KiB SymZilla profile, do not implement SGX merely for web images, do
not allocate one bank per graphic, and do not add any DOX code to the
kernel.
