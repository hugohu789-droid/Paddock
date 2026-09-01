# 15. The application fetches elevation, and nothing else

Date: 2026-09-01

## Status

Accepted. Extends ADR 0012 (LINZ sources) and ADR 0013 (release packaging).

## Context

Every download of this simulator arrives without ground.

That is deliberate and it stays deliberate. `data/snapshots/` is not shipped:
the files are tens of megabytes, they go stale, and the whole project's rule for
data it did not write is that a fetch script plus a content hash reproduces the
input, where a committed copy rots and takes somebody's licence with it. All
four shipped scenarios name a LiDAR tile. None of them has one.

So the shipped behaviour is that a farm with measured ground draws flat and says
so, and the instruction for fixing it was `python scripts/nz-elevation-snapshot.py`.
That instruction asks a farm consultant to install a language runtime before they
can see a hill, and until this change it also asked them to run a file that was
not in the archive.

**The four sources do not sit at the same distance, and lumping them together is
what made this look like one decision.** From `docs/validation/verify.md` item 7:

| Source | Licence | Account | May we redistribute? |
|---|---|---|---|
| LINZ elevation (`nz-elevation` S3) | CC BY 4.0 | **none** | Yes, and we choose not to |
| LINZ Data Service (cadastre, Topo50) | CC BY 4.0 | free API key | Yes, and we choose not to |
| NIWA CliFlo | DataHub licence | registration | **No** - forbids passing data to third parties |
| Manaaki Whenua S-map | CC BY-NC-ND 3.0 NZ | LRIS account | **No** - the ND alone settles it |

The two we may not redistribute must be fetched by the person licensed for them,
under their own agreement, and no design gets around that. Of the two we may, one
needs no credential at all.

**Searching for a tile and fetching a known one are also not the same job.**
Finding which of thousands of 1 m tiles covers a farm means walking a STAC
catalogue and checking each tile's own footprint rather than its collection's
bounding box - a collection's box is the union of its tiles and can have holes,
and the Canterbury 2018-19 box contains Lincoln while not one of its 290 tiles
does. That is several hundred requests and a JSON parser. Fetching a tile whose
address and hash are already written down is one request and no parsing.

## Decision

**The application downloads elevation for a scenario that already names its
tile. It fetches nothing else.**

1. `[terrain]` gains `url` and `attribution` beside the `path` and `sha256` it
   already had. Both optional; a `url` without an `attribution` is refused at
   load, because a credit that only exists after a successful network call is
   not a credit, and pinning it means the archive carries it whether or not
   anybody downloads anything.
2. The URL must begin `http://` or `https://`, checked where the manifest is
   parsed **and** again in the downloader. It is handed to GDAL's virtual file
   system, which opens `/vsizip/`, `/vsis3/` and plain local paths as readily as
   the web; a manifest that could name any of those is a manifest that can be
   made to read a file the person running it did not choose.
3. The hash is required and is checked before the file is put where a scenario
   looks for it. The download lands on a `.partial` sibling and is renamed only
   after it matches. A mismatch is discarded rather than kept for inspection: a
   wrong file beside the right path is one rename from being loaded as the right
   one.
4. `paddock ground fetch <bundle>` on the command line; a **Fetch ground** button
   in the application, visible only when a farm has ground it has not got.
5. **Searching stays in `scripts/nz-elevation-snapshot.py`**, along with the
   cadastre, CliFlo and S-map. Those scripts now ship in the archive.

**No new dependency.** This goes through GDAL's own HTTP layer - `/vsicurl/` -
which `gis/` already links and which is what opens the tile afterwards. libcurl
was already in the desktop build transitively. A second HTTP client in a module
that had one would have been two ways of reaching the network to maintain.

## Consequences

Good:

- Measured terrain is a button. The M5 acceptance test - a stranger downloads the
  installer and watches a year of farm life - now includes the hills.
- Nothing about what may be redistributed changed. The archive still carries no
  snapshot, and the two restricted sources are still fetched only by the person
  licensed for them.
- The bundle got more reproducible, not less: it now records where its ground was
  published as well as what it must hash to, so a collaborator can reconstruct it
  from the manifest alone.

Bad, and accepted:

- **A pinned URL can go stale.** LINZ revises collections; a tile that moves
  leaves a bundle whose `url` 404s. The failure is legible - the command says it
  cannot reach the address - and the fix is to re-run the search script, which is
  the same fix as before this change existed.
- **A pinned URL can also stop matching its hash**, if a collection is
  re-processed. That is the more interesting failure, and it is caught: the
  download is discarded and the message says the published tile has been revised.
  It is the same guard the manifest already had, moved earlier.
- **The progress dialog pumps the event loop from the download callback** rather
  than running the fetch on a worker thread. The dialog is modal, so the only
  interface it reaches is its own Cancel; the alternative is a worker plus a
  queued connection per block for a bar that moves for half a minute.
- **This is a network call in `gis/`**, and `gis/` had none. It does not reach
  `core/`, which stays free of dependencies and of the network - the rule that
  matters is unchanged.

## Alternatives considered

**Ship the LiDAR.** Lawful for LINZ - CC BY 4.0, attribution, commercial use
allowed - and rejected. Two 34 MB tiles is most of an archive, they go stale, and
`data/snapshots/` is also where a NIWA or S-map file lands, so an exclusion by
directory is the only one that stays correct as sources are added. The strictest
licence in a shared directory has to govern the whole of it.

**Ship only the scripts** (which is what happens for everything else). Simplest,
no networking in the binary, and it leaves the common case requiring Python for
data that needs no account at all. Kept as the answer for the three harder
sources.

**A full catalogue search in the application**, so a user could point at any farm
in New Zealand. That is the scenario editor's problem, not the download's, and
it wants a JSON parser and several hundred requests. Deferred rather than
refused - when the editor lands it will need exactly this, and this is the layer
it will sit on.

**An API key field in the application, for the cadastre.** Deliberately not done
yet. It is the same licence tier and it would work, but it puts a secret in the
application's hands - one that must never reach a log, a scenario bundle or an
exported report - and the boundary of a shipped farm is a far less common want
than its ground.
