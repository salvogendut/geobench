# Browser Work Log

This document records the paused Browser/DOX experiment as of 2026-08-27.
The work is preserved on feature branches and in Git stashes; neither feature
branch has been merged into the default branch.

## Repository State At Pause

### GEOBENCH

- Default branch: `main`
- `main` commit at pause: `6309ff3dc1414449b0234bcf7dc8a532975ade5c`
- Feature issue: [#487](https://github.com/salvogendut/geobench/issues/487)
- Feature branch: `issue-487-browser-enhancements`
- Pushed feature commit: `aa88e69d5f73f8606b0511083abeec073ad0e888`
  (`Enhance Browser with bounded DOX rendering`)
- Unfinished stash commit: `39a6c6398f567364ddae4912ce46b08e4b66b705`
- Stash description: `WIP browser cache experiment after aa88e69`

### GB-proxy

GB-proxy uses `master`, not `main`, as its default branch.

- Default branch: `master`
- `master` commit at pause: `9ae3d12bf65ad8bfc50dd4db4d2bd78001bb23e8`
- Feature issue: `#20`
- Feature branch: `issue-20-geobench-dox-profile`
- Pushed feature commit: `71d206bbbe1fb4722ff7067c9f176fbc3f52f783`
  (`Add bounded GEOBENCH DOX profile`)
- Local-files stash commit: `465c9a6df7a60fb1a22305c5a2b00d39766a27d6`
- Stash description: `WIP local files after 71d206b`

The branch commits are also present on `origin`. The stash hashes are recorded
because `stash@{N}` numbers change whenever another stash is created or
removed.

## Goal And Design History

The experiment started from the rendering model in `symapp-symzilla`. The aim
was to let GB-proxy perform expensive HTML processing on a modern host and send
GEOBENCH a deterministic, bounded DOX document rather than arbitrary HTML.

The selected architecture was:

1. Keep direct, streamed HTML for plain HTTP sites when no proxy is configured.
2. Negotiate a strict `geobench-1` DOX profile when GB-proxy is configured.
3. Decode DOX incrementally in the app-loaded `GBDOX.MOD`, with no resident
   kernel growth.
4. Preserve text, links, GET forms, tables, and image references in bounded
   Browser records.
5. Keep images in the existing GBPC/PIC format instead of adding SGX support.
6. Fetch proxy-converted images separately and retain decoded pictures in a
   bounded current-page bank cache.
7. Keep Browser work as bounded root-owned state-machine steps. Network,
   firmware, module, and drawing calls are not safe worker-task operations, so
   Browser is responsive/preemptible between those steps rather than being a
   pure preemptive worker.

The detailed feasibility analysis is on the feature branch in
`docs/BROWSER_DOX_FEASIBILITY.md`.

## Pushed GEOBENCH Baseline

Commit `aa88e69` contains the coherent implementation baseline:

- Adds app-loaded `GBDOX.MOD` and the Browser module-call glue.
- Adds strict incremental DOX validation and rendering.
- Retains the existing direct streamed-HTML path.
- Adds proxy negotiation with `X-GB-DOX: geobench-1` and GBPC mode selection.
- Supports compact paragraphs, links, GET search controls, table grids, and
  lazy external image references.
- Adds a bounded current-page image cache with a 32-entry directory and up to
  two borrowed 16 KiB image banks.
- Supports portable four-colour GBPC images and MSX Screen 7 sixteen-colour
  GBPC images.
- Reduces redraw scope for URL editing, caret updates, scrolling, and image
  completion.
- Accepts proxy values such as `192.168.68.202:5001`; the user does not need to
  type the `http://` prefix.
- Adds Browser-specific build, staging, documentation, icon, module, and QA
  artifact changes for CPC, MSX, and PCW.

GB-proxy commit `71d206b` is the matching server implementation:

- Adds the strict bounded `geobench-1` profile.
- Generates validated DOX documents from HTML.
- Emits compact lazy image references and converts requested resources to
  bounded GBPC pictures.
- Preserves bounded table layouts and GET form metadata.
- Adds GEOBENCH capability negotiation, response variation, resource-registry
  handling, documentation, and host-side tests.
- Keeps SymZilla negotiation independent, so SymZilla continues to receive its
  canonical DOX/SGX profile.

The two pushed commits must be tested together. Using only one side gives a
protocol mismatch or falls back to behavior that does not represent the
experiment.

## Tests And Observed Behavior

The main acceptance page was:

- `http://retrocheats.neocities.org`

It exercises a table containing six images. During development it progressed
from displaying only one image to displaying the table and all images, but
each image was still fetched separately and loading remained slow.

`http://frogfind.au` was used to test:

- inline image conversion;
- a one-line GET search field;
- the Search button and generated query URL;
- ordinary text and links.

The pushed baseline restored the FrogFind form and reduced several large
repaints. The last user-visible issues were:

- the search field text was clipped at the bottom;
- image-heavy pages still caused too many proxy requests;
- scrolling could fetch an image again after its cache bytes had been evicted;
- the fixed two-bank image cache was too small for some complete pages;
- loading all images serially made first-page completion slow;
- cache behavior had not yet been validated across low-memory and high-memory
  CPC, MSX, and PCW configurations.

The QA images for all three targets were rebuilt during the experiment, and
`make check` passed after the final stashed source changes. That does not count
as functional acceptance of the stashed cache design.

## Unfinished GEOBENCH Stash

Stash commit `39a6c6398f567364ddae4912ce46b08e4b66b705` is based on `aa88e69`.
Its source changes are:

- `kernel/kc/gbimg_mod.c`
  - moves full-height form text from `y + 4` to `y + 2` to address bottom
    clipping;
  - scans from the first page record through all `BUI_HIST_COUNT` records
    instead of stopping at the visible viewport;
  - extends image offsets and capacity across an optional third 16 KiB bank.
- `apps/browser/main.c`
  - resets the image scan position to record zero after HTML or DOX parsing
    completes, triggering whole-page image discovery.
- `kernel/kc/gbweb_mod.c`
  - counts free app pages;
  - allocates a third image bank only when more than two pages remain free;
  - releases the third bank during Browser cleanup.
- `lib/gb/gbbrowser.h`
  - adds `BUI_IMAGE_PAGE3` at low-RAM address `0x3BC2`.
- `kernel/lowram.tsv`
  - extends documented Browser shared state through `0x3BC2`.

In effect, this changes the policy from visible-only lazy loading to scanning
and prefetching every image referenced by the current page, with a maximum
image cache of 48 KiB when enough banks are free.

This was intentionally stashed rather than committed. It is a useful probe,
but it is not a satisfactory final design:

- it hard-codes one extra bank instead of scaling to available memory;
- it can delay first-page usability while all images are fetched serially;
- it still evicts and refetches when total decoded image data exceeds 48 KiB;
- it does not define a clear reserve for other applications, Viewer windows,
  source capture, or Save helpers;
- eager prefetch spends network time on images the user may never view;
- the complete CPC/MSX/PCW behavior was not accepted after this change.

The same stash also contains regenerated CPC/MSX/PCW binaries and disk images,
plus these untracked local test captures:

- `1983-20260826-155330.gif`
- `1983-20260826-171406.gif`
- `1983-20260826-175508.gif`
- `1983-20260827-001352.gif`
- `1983-20260827-003833.gif`

These generated files are not needed to recover the source experiment.

## GB-proxy Stash

GB-proxy stash commit `465c9a6df7a60fb1a22305c5a2b00d39766a27d6`
contains no source changes. It preserves only untracked local files:

- `1983-10102.ppm`
- `1983-11050.ppm`
- `MEMORY.md`

The matching proxy source is already fully represented by pushed commit
`71d206b`.

## Recommended Restart

Start from the pushed feature commits without applying the experimental cache
stash:

```shell
cd /var/home/salvogendut/Dev/geobench
git switch issue-487-browser-enhancements

cd /var/home/salvogendut/Dev/GB-proxy
git switch issue-20-geobench-dox-profile
```

Then proceed in small, independently testable steps:

1. Apply only the two-pixel form baseline adjustment and verify FrogFind on all
   three platforms.
2. Instrument or otherwise verify image-cache hits, misses, evictions, and
   network fetch count before changing allocation policy.
3. Replace the fixed two/three-bank fields with a bounded variable bank list.
   Retain current-page images while memory is available, but reserve enough
   pages for Browser source handling and other applications.
4. Keep visible images demand-loaded. Optional background prefetch should run
   only after the visible viewport is complete and should stop immediately on
   input, navigation, low memory, or another app launch.
5. Define eviction explicitly, preferably least-recently-used or
   viewport-distance based, and never refetch an image that is still present.
6. Test both four-colour and MSX sixteen-colour images because the latter use
   roughly twice the cache space.
7. Test low-memory fallback as well as 512 KiB/1 MiB systems before rebuilding
   committed QA artifacts.
8. Update `docs/BROWSER_DOX_FEASIBILITY.md`; parts of its phased status and form
   notes became stale as the prototype grew.

The desired policy is therefore **lazy loading with adaptive retention**, not
unconditional whole-page prefetch and not a fixed number of image banks.

## Recovering The Stashed Experiment

To restore the complete GEOBENCH stash, including generated artifacts and GIFs:

```shell
cd /var/home/salvogendut/Dev/geobench
git switch issue-487-browser-enhancements
git stash apply 39a6c6398f567364ddae4912ce46b08e4b66b705
```

Prefer applying rather than popping until the recovered tree has been checked.
To restore only the tracked source experiment:

```shell
git restore --source=39a6c6398f567364ddae4912ce46b08e4b66b705 -- \
  apps/browser/main.c \
  kernel/kc/gbimg_mod.c \
  kernel/kc/gbweb_mod.c \
  kernel/lowram.tsv \
  lib/gb/gbbrowser.h
```

The GB-proxy stash is optional because it has no source changes. To recover its
local files:

```shell
cd /var/home/salvogendut/Dev/GB-proxy
git switch issue-20-geobench-dox-profile
git stash apply 465c9a6df7a60fb1a22305c5a2b00d39766a27d6
```

