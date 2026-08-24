# Contiki-NG Ideas for GEOBENCH

Reviewed 2026-07-10 against Contiki-NG `develop` commit
[`6b91314`](https://github.com/contiki-ng/contiki-ng/tree/6b913142b667b62e392be9f3339e9f2e2ee54992).
The focus is networking and a possible small web browser for 8-bit targets.
Implementation is tracked by [issue #367](https://github.com/salvogendut/geobench/issues/367).

## Short conclusion

Do not port the complete Contiki-NG network stack. GEOBENCH already uses hardware
or firmware TCP/IP implementations in Net4CPC/W5100S, M4ROM and PerryNet. Adding
Contiki-NG's IPv6, 6LoWPAN, routing, packet and scheduler layers would duplicate
that work, consume scarce RAM, and require a raw packet interface that the current
GEOBENCH backends do not expose.

Several smaller ideas are valuable:

1. Make network operations non-blocking and report distinct events for connected,
   data ready, closed, timed out and aborted.
2. Keep all buffers caller-owned, bounded and configurable at build time.
3. Extract WGET's HTTP handling into a reusable streaming component, then add
   range requests, redirects and stricter error reporting.
4. Build a text-first `BROWSER.APP` around a byte-at-a-time HTML parser. The most
   relevant parser is from classic Contiki, not Contiki-NG.
5. Borrow small, reviewed pieces only when they beat a clean GEOBENCH-specific
   implementation in measured Z80 code and RAM size.

## Networking assessment

| Contiki design | Fit for GEOBENCH | Recommendation |
|---|---|---|
| One shared packet buffer in uIP | Good principle, wrong layer | GEOBENCH already has a shared 1 KiB transfer area at `#2200`; keep it and avoid per-app packet buffers where possible. |
| Compile-time connection counts and optional features | Excellent | Continue building one socket per backend and omit listening, IPv6, statistics and other unused features. |
| Event-driven TCP socket API | Excellent | Extend `gb_net_*` with pollable status/error events instead of adding a second TCP implementation. |
| Caller-supplied TCP input/output buffers | Good | Let each app choose small buffers. Do not reserve them in the resident kernel. |
| uIP IPv6/TCP/UDP implementation | Poor today | Do not port while all supported adapters already offload TCP/IP. Revisit only for future raw-Ethernet hardware. |
| DNS cache, retry and timeout state | Good in reduced form | Add one small cached A record, randomized query IDs, bounded retry/backoff and non-blocking completion. |
| Protothreads | Worth a measured experiment | Test a standalone HTTP transaction first. Keep explicit state machines if SDCC produces smaller or clearer Z80 code. |

Contiki-NG's [uIP implementation](https://github.com/contiki-ng/contiki-ng/blob/6b913142b667b62e392be9f3339e9f2e2ee54992/os/net/ipv6/uip6.c)
uses one packet buffer shared between the driver, stack and application. That is
the right memory philosophy for GEOBENCH. Its actual stack is not a drop-in fit:
the current project is IPv6-centred and brings neighbour discovery, ICMPv6 and
other infrastructure beyond GEOBENCH's IPv4 socket requirements. The configurable
buffer and connection limits in
[`uipopt.h`](https://github.com/contiki-ng/contiki-ng/blob/6b913142b667b62e392be9f3339e9f2e2ee54992/os/net/ipv6/uipopt.h)
are more useful as a design example than as code to import.

The strongest API idea is Contiki-NG's
[`tcp-socket`](https://github.com/contiki-ng/contiki-ng/blob/6b913142b667b62e392be9f3339e9f2e2ee54992/os/net/ipv6/tcp-socket.h):
it distinguishes connected, closed, timed-out, aborted and data-sent events, and
it lets the receiver apply backpressure by leaving bytes unconsumed. GEOBENCH's
current boolean/zero results conflate no-data-yet, EOF and errors. A small
`gb_net_status()` or `gb_net_step()` operation would improve Telnet, WGET and a
future browser without using resident kernel space beyond the existing transfer
block.

The current GEOBENCH DNS code is deliberately small but blocks while polling,
uses a fixed transaction ID and has no cache or retry. Contiki-NG's full
[`resolv.c`](https://github.com/contiki-ng/contiki-ng/blob/6b913142b667b62e392be9f3339e9f2e2ee54992/os/services/resolv/resolv.c)
is too large and includes IPv6/mDNS functionality that GEOBENCH does not need.
The useful parts to reproduce are its explicit query state, retries, expiry and
bounds checks. This should be a small GEOBENCH implementation, not a source port.

## HTTP assessment

Contiki-NG provides a streaming
[`http-socket`](https://github.com/contiki-ng/contiki-ng/tree/6b913142b667b62e392be9f3339e9f2e2ee54992/os/net/app-layer/http-socket)
layer. It has bounded URL, host, path and socket buffers; parses headers one byte
at a time; reports headers and body through callbacks; supports GET, POST and byte
range requests; and has an explicit timeout.

It should not replace WGET. GEOBENCH's WGET already handles `Content-Length`,
connection-close bodies and chunked transfer encoding. Contiki-NG's client does
not decode chunked bodies and explicitly leaves redirects unhandled. It also uses
64-bit range fields and depends on Contiki processes, timers, DNS and TCP sockets,
which would be expensive on SDCC/Z80.

The useful next step is a small shared GEOBENCH HTTP core, linked only into apps
that need it. Start by extracting the tested URL, response-header and chunked-body
state machines from WGET. Add these Contiki-inspired capabilities:

- distinct transport and HTTP error codes;
- per-phase deadlines instead of one undifferentiated idle counter;
- `Range` and `Content-Range` for resume and partial retrieval;
- a bounded redirect count with relative `Location` resolution;
- callback-style delivery so WGET can write files while Browser lays out text;
- caller-selected buffers, with no allocation and no resident-kernel cost.

A paged `GBHTTP.MOD` is possible later, but a shared app-side source module is the
safer first version. It saves kernel headroom and avoids loading another module on
every received chunk. Issue #367 now provides this first version as
`lib/gb/gbhttp.h`: bounded response-header, `Content-Range` and chunk framing
logic with app-bound buffers and compile-time body/error callbacks. Host tests
exercise malformed and successful responses. Keeping the state as app-local
scalars instead of a struct is deliberate because SDCC emits much smaller Z80
code for direct scalar accesses.

## Browser assessment

Contiki-NG does not ship a graphical web browser. It has HTTP clients, WebSockets
and small HTTP servers. The browser commonly associated with Contiki is in the
older [`contiki-os/contiki`](https://github.com/contiki-os/contiki) repository.
Its
[`htmlparser.c`](https://github.com/contiki-os/contiki/blob/master/apps/webbrowser/htmlparser.c)
is the most relevant reference: it is a streaming state machine that consumes
small pieces of a page, emits text and link callbacks, recognizes a limited tag
set, uses image `alt` text, and avoids building a DOM.

That parser's architecture fits GEOBENCH better than its browser application,
which is tightly coupled to Contiki's desktop and network APIs. A GEOBENCH browser
should initially support:

- plain HTTP pages containing text, headings, paragraphs, lists, preformatted
  text and links;
- image `alt` text, with remote image rendering deferred;
- one current page, a small URL history and back/forward by refetching;
- incremental layout into fixed-width display lines;
- bounded link records containing screen position and URL;
- one borrowed 16K page for bounded rendered-line/link storage;
- no DOM, CSS, JavaScript, TLS, cookies or general image decoding.

Issue #367 now includes a standalone prototype in `lib/gb/gbhtml.h`. It streams
text, title text, block/line breaks, links and image `alt` text through callbacks;
normalizes whitespace; preserves `pre`; decodes bounded basic/numeric entities;
and skips comments, scripts and styles. Host tests feed documents at different
chunk boundaries and cover malformed/oversized input. A minimal SDCC probe
currently measures 4,505 bytes of code and 299 bytes of default parser state,
before browser UI, history or networking. `BROWSER.APP` now composes that parser
with the shared HTTP response parser and the existing CPC/PCW transports. It
stores up to 208 CPC or 182 PCW wrapped display rows in a borrowed 16K page,
including compact GET-form and inline-image records, and pauses its TCP
stream after laying out one viewport, and resumes from retained receive-buffer
state only as the user scrolls down. It follows bounded redirects, opens
underlined link labels by click, and retains one refetched Back URL. Link targets
are stored separately so proxy transport URLs stay out of rendered text. Simple
table rows become compact two- or three-column records whose text and images
remain clickable. Visible images are fetched sequentially through one bounded
GBPC v2 slot and drawn directly; a proxy can convert ordinary image formats.
The Browser build disables numeric-entity
and attribute-entity decoding to remain within its 16K PCW bank. Reaching the
row bound is reported as truncated; no-spare-bank systems use an explicit
seven-line fallback.

This would be useful for retro-oriented sites and local services. Most modern
public sites require HTTPS, CSS and JavaScript, so a practical optional companion
would be a proxy that fetches HTTPS and emits the browser's small HTML subset.
TLS should not be brought onto the Z80 merely to claim HTTPS support.

## Suggested order of work

1. Add precise network receive status plus bounded DNS retries and caching to
   the existing backend-neutral API. **Implemented in issue #367; DNS/connect
   still run synchronously within their bounded calls.**
2. Add bounded redirects and validated Range continuation to WGET. **Implemented
   in issue #367 for CPC exact-length files; PCW restarts CP/M-record files.**
3. Extract and unit-test a bounded `gb_http` parser from the proven WGET state
   machine without changing its behaviour. **Implemented in issue #367 for
   response metadata and chunk framing; URL resolution remains in WGET.**
4. Prototype a standalone streaming HTML parser with host-side tests based on
   classic Contiki's callback model. **Implemented in issue #367 and integrated
   into Browser. Browser now renders bounded link labels with click navigation
   and keeps one previous URL for Back; richer history remains future work.**
5. Build `BROWSER.APP` for CPC and PCW first. **The initial text-first version
   was implemented in issue #367; the same app now builds for MSX through the
   TCP/IP UNAPI backend added in issue #397.**
6. Measure every addition using the produced `.RAW` size, static RAM map and real
   transfer timing. Reject abstractions that do not reduce duplication or improve
   responsiveness.

## Code reuse and licence

Contiki-NG and GEOBENCH both use the BSD 3-Clause licence. Direct reuse is
compatible, but copied files or substantial code must retain the original
copyright and licence notice. Binary distributions must reproduce the notice in
their documentation or other supplied material. If code is copied, add a small
third-party notices document naming the exact upstream file and commit.

The preferred approach is still to reuse designs and write code against the
existing `gb_net_*` and window APIs. The best candidates for direct, selective
reuse are the small protothread macros and isolated parsing logic. The complete
uIP, process scheduler, resolver, HTTP socket and classic browser applications
should not be imported wholesale.
