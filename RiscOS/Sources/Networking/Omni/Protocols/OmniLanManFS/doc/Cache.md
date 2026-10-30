# LanManFS Cache

OmniLanManFS keeps a per-mount metadata and content cache so that ordinary RISC OS filing-system traffic does not have
to cross the network for every operation.  The cache sits between FileSwitch (which calls into the FSEntry handlers in
`c/CoreFn`) and the SMB2 wire layer (which talks to the server).  Most of the saving is in requests not sent: the bulk
of a typical workload is repeat reads of the same metadata (compiler include searches, directory walks for the desktop
Filer, and so on), and the cache turns each of those into a single in-memory lookup.

**The cache is a build-time option, and is on by default.**  It is compiled in when `LM_CACHE` is defined, which the
Makefile does unless `CACHE=FALSE` (each `!Mk` script passes `CACHE=TRUE`; change that to `FALSE` to build it out).
Without it, `c/Cache` compiles to a block of functions at the top of the file that do nothing, and every mount runs
uncached, which takes the cache out of the picture when debugging the rest of the filing system.  In a build with the
cache, `*LMCache Off` has every mount made afterwards run without one, as if its allocation had failed, so one build can
be timed both ways; `*LMCache On` restores it.  A mount already made keeps what it has.  Those functions
implement the NULL-cache contract: `Cache_MountCreate` returns NULL, so callers behave exactly as they do for a real
mount whose substrate allocation failed.  Lookups miss, the `Cache_Notify*` hooks do nothing, and `Func_ReadDir1` lists
directories with `SMB2_EnumerateDirectory`.  That function decodes entries with the same `smb2_qd_iter` the cache's
wire callbacks use and hands them to the same `dir_visitor`, so a cache-less listing gets identical symlink resolution,
hidden-file filtering and timestamps; it keeps no entries.  CHANGE_NOTIFY still runs on a cache-less mount, because it
drives the Filer UpCalls as well as the cache.

With nothing cached, every path FileSwitch names has to be resolved on the server, and every catalogue read sent to
it.  Four measures in the wire and translation layers cut the round trips this takes.  None of them keeps an
object's details from one filing-system call to the next: a listing left open is read only by the call that continues
it, and the remembered translation holds a path's spelling, not its details.  A mount with a cache uses the first three
only for a directory the cache cannot list, and the fourth always:

- **Narrowed lookups.**  `c/Xlate` resolves each leafname with `SMB2_FindLeaf`, which hands the server a search
  pattern so that only candidate entries come back, and applies `xlt_leafnames_equivalent` to those.  The pattern
  (`xlt_search_pattern`) is the leading run of characters that can match nothing but themselves, followed by `*`, so
  it admits every spelling the comparison could accept.  Until a filtered search on a connection has once found its
  entry (`SMB2_SRV_PATTERN_OK`), each filtered miss is confirmed by reading the whole directory; a server caught
  filtering wrongly is marked `SMB2_SRV_PATTERN_BAD` and is not sent a pattern again.
- **The lookup answers the request that follows it.**  The entry found, or the fact that there was none, is kept in
  `smb2_lookup` until the next exchange with the server or the next filing-system entry, whichever comes first.
  An open of a name found absent fails without being sent, and `SMB2_GetAttribs` of a directory found is answered
  from the listing.  A file is answered from it only in a call that has declared it needs no length
  (`SMB2_LengthNotNeeded`: the no-length catalogue read FileSwitch makes while resolving a path, and Rename's look
  at its source).  The length a listing reports for a file can lag while another client is writing the file, and
  FileSwitch sizes a load by the length it is given, so a length is only ever reported from an open of the file.
  The listing is not trusted at all for a reparse point or for a file this client holds open.  Absence is not
  asserted when a hidden entry of that name exists, when the leaf is one the server could resolve some other way (a
  short-name alias, a stream, a trailing dot or space, a character outside ASCII that it may fold differently), or
  when it was the directory itself that was missing (`SMB2_FindLeaf` reports that as `EPATHNOTFOUND`).
- **Resumable listings.**  When the caller's buffer fills, `SMB2_EnumerateDirectory` leaves the directory open on the
  server with its unread entries still in `smb2_dirbuf`.  A following `Func_ReadDir1` for the same path, at the
  offset just reported, carries on through `SMB2_ResumeListing`.  FileSwitch makes a catalogue read of the directory
  ahead of each such call, so the two handlers a continuing read passes through enter with `FSEnterKeepingListing`,
  and `fsentry_file_handler` leaves the listing open for catalogue reads.  Any other filing-system call, any new
  directory scan (the scans share `smb2_dirbuf`), the share going away, or no continuing read arriving within
  `SMB2_LISTING_IDLE_CS` closes it; a call that does not follow on rescans.  The directory is opened sharing delete
  access, so that the open listing never stops another client renaming or removing the directory.
- **One remembered translation.**  On an SMB2 mount, `c/Xlate` remembers the last RISC OS path it resolved and the
  server spelling it found (`xlt_remembered`): a single slot, holding only a path whose leaf was found.  `c/CoreFn`
  discards it on the way into and out of any call that can change a name, and a read-only call that is told "not
  found" through a remembered translation resolves the path afresh and tries once more
  (`cf_refresh_remembered_path`), so a spelling changed from another machine corrects itself.  A cached mount
  remembers too (a leaf found in a cached listing counts as found), which matters because resolving a path can mean
  scanning its parent, and a scan closes any listing left open between pages.

A cached mount uses the narrowed lookup and the resumable listing for a directory it cannot list (see
["EXCEEDS_CACHE flag"](#exceeds_cache-flag) and ["CASE_TWINS flag"](#case_twins-flag)): names in it are looked up on the
server, and `Func_ReadDir1` reads only its first page through the cache, paging through the rest on the server.

This puts two obligations on cache development.  Every caller must keep tolerating a NULL cache, since that is the
uncached build's normal path rather than an allocation-failure edge case.  And every public `Cache_*` function that
code outside `c/Cache` calls needs a matching version in that block returning its NULL-cache answer, or the uncached
build fails to link.  A test build of the cache ([section 4](#4-testing-on-linux)) must define `LM_CACHE`, or it tests only that block.

## 1. Overview

The cache holds three kinds of fact about each path it knows about:

* **It is a directory**, with metadata and an optional list of its immediate children.
* **It is a file**, with metadata (load / exec / length / attributes), and optionally a copy of the file's contents.
    The copy has its own lifetime (the content lifetime), checked whenever the bytes are about to be served, and is
    dropped as soon as the server reports a different length or datestamp; an edit that keeps a file's size still
    moves its datestamp.  When the server instead reports the same details (an open, a catalogue read or a load
    asked of the server, not answered by the cache), the copy has just been checked and its lifetime starts again,
    so a header a build keeps opening is not fetched afresh each time the lifetime runs out.
* **It is not there**: a *negative* entry, recording that the server was asked about this path and replied
    "no such object".  Negatives let the cache answer repeat lookups for paths that do not exist on the share (the
    toolchain searching for an include file across many directories along a path is the classic case).

Each of these is stored in a *cache node* (cnode for short).  All cnodes for a single mount live in a fixed-size array
called the *slab*; a free list of unused slots threads through that array.  Cnodes are reached by hash from their full
DOS path, so any lookup is one hash plus a short bucket walk.

Beside the slab there are two heaps:

* The **string heap** holds cnode names.  Directories store the full DOS path, files and negatives store only the
  leaf; the full path is reconstructed on demand by walking the parent chain.
* The **content heap** holds two kinds of variable-sized blob: cached file bytes, and a per-directory packed list of
  leaf names (the "leafnames blob", or listing) used to answer "is this child present?".

There is also a **hash table**: a separate fixed-size array of bucket head pointers, sized to the next power of two at
or above the slab capacity.  Each bucket is a singly-linked chain of the cnodes that hash there, threaded through one
of the cnode's union fields; lookups take the low bits of the hash to pick a bucket and walk the chain.

The slab, the two heaps, the hash table, and a small per-mount header all live inside one RISC OS Dynamic Area per
mount, named `LanManFS: <label>`.  This keeps cache memory off the module's own zero-init area and means an unmount
hands the whole budget back to the OS in one operation.

```
   +--------------------------- per-mount Dynamic Area ----------------------+
   |                                                                         |
   |  LMCache header   (sentinels, TTLs, quirks, wire vtable, ...)           |
   |                                                                         |
   |  Slab (LMCacheNode_t * slab_capacity)                                   |
   |   slot 0 = permanent share-root directory cnode                         |
   |   slot 1.. = files / dirs / negatives, allocated from the free list     |
   |                                                                         |
   |  Hash table  (next-power-of-two buckets; case-folded FNV-1a of path)    |
   |                                                                         |
   |  String heap  (cnode names; SRHeap allocator)                           |
   |                                                                         |
   |  Content heap  (file content blocks and leafnames blobs; SRHeap)        |
   |                                                                         |
   +-------------------------------------------------------------------------+
```

A mount's relationship to the rest of the module is:

```
   Application ---> OS_File / OS_Find / OS_Args / OS_GBPB ----> FileSwitch
                                                                   |
                                                                   v
                                          OmniLanManFS FSEntry handler (c/CoreFn)
                                                   /                \
                                              cache hit       cache miss / mutation
                                                  |                  |
                                                  |                  v
                                                  |   SMB2 wire (c/SMB2) <----> remote share
                                                  |                  |
                                                  |                  v
                                                  |        update cache (c/Cache)
                                                  |                  |
                                                  v                  v
                                          serve result to caller via FileSwitch
```

An SMB1 mount has a cache too.  It learns what the FSEntry handlers report, but cannot list directories into the cache,
because the wire vtable is bound only for SMB2 (`SMB2_InstallCacheWire`), so its directory reads go to the server.

A few design decisions show up everywhere in the code:

* **Memory comes from the OS, not from the module heap.**  Each mount's substrate comes through the allocator that
    `c/Workspace` installs (`Cache_SetSubstrateAllocator`), which creates the mount's Dynamic Area.  Its size follows
    the tier `Workspace_PickMemoryProfile` chooses from the machine's memory (small, medium or large, or the default
    sizing when the memory cannot be read); a small machine caches no file content.  The cache itself only sees an
    opaque pointer, so a test harness on Linux can pass plain malloc'd memory instead.
* **Everything is evictable, files before directories.**  Files and negatives are linked onto recency-ordered file
    lists, directories onto a recency list of their own.  When the cache is full the least recently used file or
    negative is evicted to make room; once those have run out, the least recently used directory with no
    sub-directory in the cache goes, with its files.  A directory is never evicted from under a sub-directory, whose
    parent pointer would dangle, nor from above the path being installed.
* **Freshness depends on the server class.**  Windows servers deliver CHANGE_NOTIFY reliably, so the cache stays in
    step through server-driven invalidation and the per-entry lifetimes (`LMCACHE_DEFAULT_METADATA_TTL`,
    `LMCACHE_DEFAULT_CONTENT_TTL`) are a safety net.  macOS and Samba servers deliver CHANGE_NOTIFY unreliably or not
    at all, an SMB1 mount receives none, and changes made on the server host itself (a shell, a NAS web interface)
    may never be reported.  Such a mount runs with the `LMCACHE_QUIRK_AGGRESSIVE_REFRESH` quirk, which clamps the
    lifetimes to `LMCACHE_QUIRK_METADATA_TTL`, `LMCACHE_QUIRK_NEGATIVE_TTL` and `LMCACHE_QUIRK_CONTENT_TTL`, so the
    cache revalidates soon after each access.  Names cached as absent expire sooner than other details, since they hide
    a file another client has just created, and captured content soonest.  The quirk is set at mount time for an SMB1
    mount and for a server that SESSION_SETUP classified as non-Windows, and later for every mount of a server whose
    share root lists a `.DS_Store`, which marks a macOS server.
* **A three-step design keeps asynchronous server activity safe.**  See [section 3.3](#33-the-three-steps).

## 2. Terminology

The rest of this document, and the source, use these words in the following senses.  Some are local to the cache; a few
belong to the wider RISC OS / SMB2 context but are needed to follow how the cache fits in.

### Mount

A successful connection to a single share, identified internally by an upper-case drive letter (A-Z) and labelled with
the mount name the user chose when connecting.  Each disk mount carries its own cache; mounts do not share state,
except the module-global handle table.

### Substrate

The single Dynamic Area that backs one mount's cache.  The cache module never calls OS_DynamicArea directly: it goes
through a substrate-allocator function pointer that production code (`c/Workspace`) sets to an OS_DynamicArea-backed
implementation, and that a test harness can leave as malloc.  This is the boundary that lets the same cache code build
and test on Linux.

### Cnode

A single cache entry.  It holds the object type (file / directory / negative), the path or leaf name, the
case-folded FNV-1a hash of the path, the parent pointer, recency-list links, the expiry timestamp, the end of its echo
window, RISC OS-style metadata (load / exec / length / attributes), flags, and a union for the type-specific fields
(handle and cached content for files; leafnames blob and directory recency links for directories).

Each directory also keeps a list of its children (`first_child`, threaded through the children's `sibling_prev` /
`sibling_next`), so anything that concerns one directory's children (the clean-up after an enumeration, building a
listing for a new directory, evicting a subtree) costs the size of that directory rather than a sweep of the slab.
The parent pointer and the lists cannot disagree because every change of parent goes through `child_set_parent` or
`child_detach`: a cnode is on a directory's list exactly when its parent pointer names that directory.  The links sit
outside the type union, so they survive a cnode changing type.

### Slab

The fixed-size cnode array near the start of the substrate.  Slot 0 is permanently reserved for the share-root
directory cnode, which is created when the mount is set up and survives until unmount.  All other slots come and go
through a singly-linked free list threaded through one of the cnode's union fields.  The slab capacity is chosen at
mount time from the tier.

### Hash table

A power-of-two array of bucket head pointers, at least as many as the slab has slots, so that the average bucket depth
on a full slab is at most one.  Each cnode lives in exactly one bucket while it is occupied; hash collisions chain
through the same union field that the free list uses (the slot is never on both at once).  Lookups hash the full DOS
path with FNV-1a, folding ASCII letter case, take the low bits of the hash to pick the bucket, then walk the bucket
comparing the full hash and then the path, ignoring letter case.

### String heap and content heap

Two separate SRHeap regions inside the substrate.  The string heap holds NUL-terminated cnode names; the content heap
holds the larger variable-sized blobs (cached file bytes and packed leafnames lists).  They are sized independently by
the tier, so that a mount with many small files gets enough room for names without starving content, and the other
way round.

### Hot file list and cold file list

Two doubly-linked lists threaded through the cnode `u.file.prev` / `u.file.next` fields.  Both have one *MRU end*
(most recently used) and one *LRU end* (least recently used).  Files and negatives are inserted at the cold list's MRU
end; each later hit on the same cnode moves it one position toward the MRU end of whichever list it is on.  When a
node at the cold MRU end is hit again, it crosses to the hot list's LRU end, pushing the hot list's own LRU tail back
down to cold if the hot list has reached its cap (`LMCACHE_DEFAULT_HOT_MAX`).  Eviction under pressure looks at the
cold list from its LRU end first, and at the hot list only when the cold list has nothing to give.

The two-list arrangement keeps a repeatedly used working set out of the path of one-shot scans (a `*Help` directory
listing should not evict the compiler's hot include files).  It also bounds the work of each promotion to a single
neighbour swap.

### Directory recency list

Directories are not on either file list.  They have one list of their own, threaded through `u.dir.older` /
`u.dir.newer` and ended by the mount's `dir_oldest` / `dir_newest`, which holds every directory but the root.  A new
directory goes on at the newest end, and so does one that is looked up or refreshed, or whose file or negative is
looked up, refreshed or added.  So a directory being browsed stays young even if its own cnode is not touched.

A directory cnode owns its children: each child carries a `parent` pointer back at it, which must stay valid for the
child's lifetime.  Pressure therefore only evicts a directory with no sub-directory left in the cache, taking its files
and negatives with it (see [section 3.4](#34-eviction-and-recovery-from-claim-failure)).  Anything explicit (a delete or a
removal notification, a directory rename, a type change, or a full mount tear-down) removes a directory along with its entire subtree.

### Root node

The share root.  Created with the mount, it lives in slot 0 and is never evicted.  `Cache_Lookup` returns it whatever
its expiry, so it never times out, and a catalogue read of `$` is answered from it.  `SMB_CreateShare` reads the
root's date from the server just before making the cache, so the root carries the date it had at mount time.  A server
that gives none, or a root that is hidden while hidden files are not shown (as a Windows drive's root is), leaves the
root dated with the time the mount was made.

### Leafnames blob

A directory cnode optionally owns a packed string of all its child leaf names ("name1\0name2\0...nameN\0\0") in the
content heap, called its listing.  The listing has its own lifetime, set from the metadata lifetime when it is built and
separate from the directory's own details.  While the directory's `LMCACHE_FLAG_DEFINITIVE` flag is set and that
lifetime has not run out, the listing is the complete and authoritative list of children: `Cache_DirHasLeaf` can then
answer PRESENT or ABSENT for any candidate leaf without going to the wire, and `Cache_LookupListing` hands the
directory out for callers that read the names themselves.  This is the *fast-miss* path: the toolchain's repeated
"is `swis.h` in `C:`?" probes resolve in RAM.

Names are found through an index kept beside the blob in the content heap, never by walking the blob, so a probe costs
the same in a directory of ten names or two thousand, and building a listing a name at a time (each append first checks
the name is not already listed) grows with the size of the directory rather than its square.  The index is an
open-addressed hash table of the names' body offsets, filed under a *leaf fold*: by default a hash of the name with
letter case folded, or the function installed with `Cache_SetLeafFold`.  The installed fold must give the same hash to
any two names a caller treats as the same.  `c/LanMan` installs `Xlt_LeafFold`, which also folds together the character
pairs the path translation treats as equivalent and leaves out everything from the first comma, so that `Cache_ListingFind` can answer the
path translation's looser matching.

A listing also answers for paths further down.  A compiler searching its include path asks for `Sources.Lib.h.socket` where
`Sources.Lib` has no `h` at all; with no listing of `Sources.Lib.h` the probe would otherwise go to the server, which takes a failed
open and a failed listing of the parent before it reports "path not found".  `Cache_PathRuledOut` climbs from the parent
towards the root and stops at the first directory with a cached absence or a current listing: a cached absence rules the path out, as does a
current listing that leaves none of the server's entries out and lacks the next name down.  A listing that holds the
name, a listing with entries left out, or no listing anywhere on the way leaves the server to decide.  Only current
listings count, so a directory made by another client is never ruled out once the listing that missed it expires, and
sooner if a change notification adds it.  Path translation and the CoreFn fast-miss check (`cache_fast_miss_check`)
both consult it before going to the wire.

When this client creates or renames something into a directory, or reads its details from the server, anything cached
above it that says otherwise goes first (`cache_admit_ancestors`): a negative on the way is evicted, and a current
listing that lacks the next name down is dropped, never extended, so a definitive listing always names every child it
has.  `Cache_NotifyDirExists` does the same for a directory the server reports as already existing.

Removing a name marks it gone where it lies (its first byte becomes 0x01, which no real name starts with) instead of
closing up the blob, so no other name's offset changes.  An append that finds the blob short of room squeezes the
removed names out first and re-indexes; a blob is never grown, so an append that still does not fit drops the listing.
When the index is three quarters full it is rebuilt at twice the number of names still listed (removed-name marks count
towards the three quarters, so a directory that churns is re-indexed before the marks fill it), and a listing whose
index cannot be claimed is dropped rather than kept, because without its index it would report every name absent.

### UNLISTED_ENTRIES flag

A directory flag (`LMCACHE_FLAG_UNLISTED_ENTRIES`) set when the scan that built a definitive listing passed over some of
the server's entries: hidden or system entries while hidden files are not shown, names that cannot be translated or are
too long to hold, and symlinks whose path is too long to resolve.  The listing still serves the names it holds, but a
name missing from it may exist, so `Cache_DirHasLeaf` answers UNKNOWN rather than ABSENT for it and `Cache_PathRuledOut`
leaves such a directory to the server.  While hidden files are not shown, a Windows share at a drive's root and every Samba directory
holding a dot-file have such entries, so lookups of missing names there go to the server.  The flag is cleared with the
listing.

### Negative cache entry

A cnode whose object type is "nothing" (`LMCACHE_OBJECT_NOTHING`): the server reported no such object at this
path.  Negatives sit on the file lists like files, expire on their own lifetime (the metadata lifetime, or the shorter
`LMCACHE_QUIRK_NEGATIVE_TTL` under the aggressive-refresh quirk), and answer later lookups of the same path.  When
this client later creates the path, or reads its details from the server, the negative is evicted and a file or
directory cnode takes its place; when the server reports the path added, the negative is expired, so the next lookup
asks the server.

### EXCEEDS_CACHE flag

A directory cnode flag (`LMCACHE_FLAG_EXCEEDS_CACHE`) set by an enumeration whose listing will not fit: the leafnames
blob runs out of room, the directory has more entries than its share of the slab (`LMCACHE_LISTING_SLAB_SHARE`), or
the substrate cannot take another of its children even after recovery.  The share rule keeps one huge directory from
evicting the rest of the working set each time it is read; nothing stops recovery evicting the directory's own earlier
children while its listing is built, since the listing keeps their names either way.  From that point the enumeration
stops caching the directory: the rest of its entries are passed to the visitor but not inserted, the part-built blob is
dropped, the stale-child sweep is skipped, and once the visitor has halted delivery (or there is no visitor) the scan
is abandoned rather than read to the end.  Reading the whole of a large directory for every page, and caching it each
time, would evict the rest of the cache for nothing.

The flag outlives the listing: dropping a blob for any other reason leaves it set, and only an enumeration whose
listing fits clears it.  The cache-read step never serves an EXCEEDS_CACHE directory.  A single failed insert outside an
enumeration, or a notified addition that overflows a finished blob's spare room, only drops the listing, since neither
says anything about the directory's size.

### CASE_TWINS flag

Paths match ignoring letter case, as Windows does, so one cnode answers for `Makefile` and `makefile`.  A case-sensitive
server (Samba, macOS) can hold both as different objects.  In a directory holding such a pair, the cache behaves as an
uncached mount does: every entry of a listing is passed on with its own details, and a name typed in either spelling is
looked up on the server.

An enumeration that meets a second spelling of a name it has already listed in this scan sets
`LMCACHE_FLAG_CASE_TWINS` on the directory, evicts the cnode the first twin was cached as (a directory with its
subtree), and from then on treats the directory as it would one too big to cache: the rest of the listing is passed on
uncached and no blob is kept.  `Cache_DirNotCacheable` reports either flag, so callers look names up on the server.
Only a cached child refreshed by this scan counts as the first twin; one spelt differently but not refreshed may only
have been renamed to a new case on the server, and the listing then gives its cnode the server's new spelling.

Beneath a flagged directory, `find_path_any` accepts a match only when each component under it is spelt exactly as
cached, so the two twins can hold cnodes of their own and a third spelling finds neither.  The stale-child sweep
likewise matches the listing exactly there.  Every rescan looks for twins again; a listing without them clears the
flag and keeps its blob as usual, and the sweep drops whichever spelling has gone.

A server notification that adds a name its listing (finished or still being built) already holds in another case drops
that listing and sets the flag.  A local create in another case does not: on a case-insensitive server it is the same
object overwritten, and the cache cannot tell the two apart.  The notification check matters mainly for a Windows
folder made case-sensitive; Samba and macOS servers deliver notifications unreliably or not at all and run with the
aggressive-refresh quirk's short lifetimes, which bound how long such a create can be conflated.

### Lifetimes and expired entries

Each cnode carries an `expires_at` centisecond timestamp, and a file's cached content and a directory's listing each
carry one of their own.  Cache timestamps are 64-bit (`LMCacheTime_t`): the cache counts the wraps of the 32-bit RISC
OS clock (`cache_now`, kept current through idle spells by `Cache_KeepTime` from the periodic background callback,
`LM_CALLEVERY_TICKS`), so every deadline test is a plain comparison and 0 always reads as already expired.

A `Cache_Lookup` that finds an expired cnode reports a miss to the caller (so it revalidates from the wire), but leaves
the cnode in place.  This avoids the orphan-children problem that evicting on expiry would create for directories, and
lets the next report of the path's details from the server refresh them in place.  Stale entries are
reclaimed by the pressure-driven evictor.

A listing served from a directory's blob hands out each child's cnode as it stands.  So a listing is served only while
every child it names has unexpired details; otherwise the enumeration goes back to the server.  A CHANGE_NOTIFY that
expires a file, or a scan that reports a file size the server may not have caught up with (clamped to the grace lifetime,
`LMCACHE_DEFAULT_OVERFLOW_GRACE_TTL`), clamps only that file's lifetime: the names in the listing still stand, so
fast-miss probes and path translation go on using it.

### mutation_gen

A per-mount counter that moves each time a cnode is freed or its hash key changes, and on every CHANGE_NOTIFY
overflow, connection loss or flush.  Code that holds a cnode pointer across an operation that may evict (in particular,
anything that crosses the wire) records `mutation_gen` before the call and re-resolves the pointer if it differs
afterwards.  It is `volatile`, because a notification handled inline during a wire call moves it.  See [section 3.3](#33-the-three-steps)
for why.

### Mutation hooks

The public `Cache_Notify*` functions, through which the rest of the module records changes in the cache.  `c/CoreFn`
calls them around a wire operation that changes the server-side state (a rename is shown before the request goes out;
see ["Handle bindings survive a rename via in-place re-key"](#handle-bindings-survive-a-rename-via-in-place-re-key)),
after one that reads an object's details, and to forget what a failed save or write may have left behind.  `c/SMB2`
calls them for each CHANGE_NOTIFY record, and `c/SMB` calls `Cache_NotifyConnectionLost` when a server's connection
dies.

- `Cache_NotifyCreate` records a file or directory this client created, inserting or refreshing its cnode and adding
  the name to the parent's listing; `Cache_NotifyMkdir` records a new directory with an empty listing.
- `Cache_NotifyLearned` records details just read from the server; `Cache_NotifyLearnedWithContent` also keeps a copy
  of a file read whole.
- `Cache_NotifyDelete` drops a path this client deleted (and its subtree if a directory); `Cache_NotifyRemoved` does
  the same for a removal the server reports.
- `Cache_NotifyAdded` adds a name the server reports to its parent's listing, leaving the new object's details for the
  next lookup to fetch.
- `Cache_NotifyMutate` and `Cache_NotifyMutateNode` update the details after this client changed them, by path or
  through the cnode bound to an open handle; a change to a file's bytes drops its cached content.
- `Cache_NotifyChange` expires an object whose details changed.  A file's cached content goes with them, but a
  directory's listing stays, since its names have not changed.
- `Cache_NotifyDirExists` admits a directory the server reports as already there (see the leafnames blob above).
- `Cache_NotifyRename` re-keys a cnode after a rename, and expires a symbolic link's details, which are its target's
  and change when a relative link moves to another directory.
- `Cache_NotifyOverflow` handles a CHANGE_NOTIFY overflow, after which the server has stopped reporting individual
  changes, and `Cache_NotifyConnectionLost` a lost connection, during which changes went unreported
  ([section 3.7](#37-change-notification-and-echoes)).

### CHANGE_NOTIFY (the SMB2 mechanism)

A long-lived asynchronous SMB2 request that LanManFS issues against the share root when a disk share is connected,
watching the whole tree for every change but last access.  The server completes it whenever something underneath the
root changes, and LanManFS re-arms it.  Each completion carries a list of records describing what changed;
`c/SMB2` routes those to the appropriate `Cache_Notify*` hook and raises the Filer UpCalls.  When the server cannot
keep up with the change rate it completes the request with STATUS_NOTIFY_ENUM_DIR ("overflow"); see
`Cache_NotifyOverflow`.

### Echo window

A deadline on a cnode (`suppress_until`), opened by a change this client makes, before which a CHANGE_NOTIFY event
matching that change is taken for the server reporting it back (its echo) and skipped.  Each window lasts
`LMCACHE_DEFAULT_NOTIFY_BACKOFF`.  See [section 3.7](#37-change-notification-and-echoes).

### Wire vtable

A small set of function pointers (`first`, `next`, `iter`, `discard_state`, with a `ctx` passed back to each) that the
cache calls into when it needs to list a directory on the server.  `SMB2_InstallCacheWire` binds them to `c/SMB2`'s
`smb2_qd_first`, `smb2_qd_next` and `smb2_qd_iter`; a test harness can install a scripted implementation instead.  This
is the boundary that keeps the cache code unit testable without a server.

## 3. How it works

### 3.1 Read side: Cache_Lookup

This is the simplest path: `Cache_Lookup` skips any leading separator, hashes the path with case-folded FNV-1a, takes the
low bits of the hash to pick a bucket, walks the bucket comparing first the full hash (cheap) and then the path (ignoring
ASCII letter case), and returns the matching cnode.  An expired cnode reports a miss (it stays in place, as above).  A hit is
promoted: a file or negative moves one position toward the MRU end of its file list and its parent moves to the newest
end of the directory recency list; a directory moves to the newest end itself.  A hit's cached content whose own
lifetime has run out is dropped first, so whatever content a hit carries can be served as it stands.

`Cache_DirHasLeaf` is the same idea for "is this leaf in this directory?": look the leaf up in the directory's listing
index if the listing is definitive and current, and return PRESENT, ABSENT, or UNKNOWN.  UNKNOWN means there is no
listing to trust, or the listing has entries left out and lacks the name; the caller then falls through to the wire.

### 3.2 Write side: cache_insert_or_refresh

Every file and directory insertion goes through this one function (a negative has its own, `cache_insert_nothing`,
which never replaces an existing cnode).  It looks up the path: if no cnode exists, it claims a fresh slab
slot and name; if one exists with the same object type, it refreshes its metadata and lifetime in place (dropping a
file's content if the length or datestamp changed); if one exists with a different object type (a file becoming a
directory, or a negative becoming either) it evicts the old cnode with its subtree and falls through to the fresh
insert.  A new file or negative is linked onto the cold file list at its MRU end; a new directory joins the directory
recency list at its newest end.

Making room for a new cnode can evict other cnodes entirely.  Callers that hold any other cnode pointer across the call
re-resolve it via `mutation_gen`, except a pointer to the parent or a directory above it, which recovery never evicts.

### 3.3 The three steps

Each FSEntry call that consults the cache is preceded by a drain of pending notifications;
`Cache_EnumerateDirectory` then works in three steps.

* **The pre-handler drain.**  Before any cache lookup runs, `FSEnter()` (and `FSEnterKeepingListing()`) calls
    `SMB2_PollNotifications()`, which checks each connected server's socket for data without waiting and drains any
    server-initiated traffic already in the TCP buffer.  CHANGE_NOTIFY completions are dispatched through the
    `Cache_Notify*` hooks before the handler can read a listing they make stale.  Once `SMB2X_DataReady` confirms
    bytes are queued, the follow-on receive has a short deadline, because a frame that has started must be read to its
    end; when no data is waiting, which is the common case, the check costs only the system call.  The periodic
    background callback (`LM_CALLEVERY_TICKS`) runs the same drain on an idle mount.

* **The cache-read step.**  The directory cnode is looked up.  If its listing is definitive and current, it is not marked
    EXCEEDS_CACHE, and every child it names has current details, the caller's visitor is dispatched over the listing and
    the call returns, with no wire I/O at all.  A current negative at the path reports "not found".  Otherwise the step
    falls through, after evicting anything at the path that is not a directory (an expired negative, or a file the server
    may since have replaced), so that the scan builds a proper directory entry.

* **The wire-read step.**  The wire vtable is driven (`first`, `next`, `iter`) through one or more SMB2 QUERY_DIRECTORY
    round trips, inserting or refreshing each entry's cnode and appending its name to a fresh listing as it arrives.
    This step is the *only* point at which the cache may be changed by something other than this function:
    `SMB2_SendRecv` processes any pending CHANGE_NOTIFY responses inline before returning, and that processing can free
    or replace cnodes through the `Cache_Notify*` hooks.  So `mutation_gen` is checked after each wire call, and the
    directory cnode found again by path when it has moved.

* **The cache-update step.**  The directory's own lifetime is restarted, the cached children the scan did not list are
    evicted (unless the directory proved too big to cache), the listing is finished, which makes the directory
    definitive, and the directory is marked UNLISTED_ENTRIES if the scan passed any entries over.  Finally the lifetimes
    of files listed with a size that may lag their data are clamped.  A directory the server reports missing is instead
    evicted with its subtree and cached as absent under its parent.

The drain and the wire-read step are the two points at which something other than the current call can change the
cache.  The drain applies the changes the server has *already* reported at a fixed point before the handler reads the
cache; the wire-read step handles any that arrive while a wire operation is in flight.

The split exists for one reason: a cnode pointer that was valid at the start of the cache-read step is *not* guaranteed
to be valid after a wire call, because a notification handled during that call may have freed it.  Code that respects
the step boundaries, re-resolving after each wire call and never holding a cnode pointer across one, is correct by
construction.

The same rule applies to any other path that crosses the wire while holding a cnode (CoreFn's Open, GetBytes, Close and
the rest look their cnode up afresh after the wire call): the `mutation_gen` check, or a fresh lookup, is the
way of noticing that something underneath moved.

### 3.4 Eviction and recovery from claim failure

Eviction has two trigger paths.

* **Targeted (`Cache_NotifyDelete`, `Cache_NotifyRemoved`, `cache_evict_subtree`).**  A path has been reported
    gone, or a rename or a type change needs a directory torn down before it is made again.  The walk goes down the
    child lists to a cnode with no children left, evicts it, steps back up and repeats, so each child is freed before
    its parent (the parent invariant has to hold throughout) and nothing outside the subtree is looked at.

* **Pressure-driven (`pick_file_victim`, `pick_dir_victim`).**  A claim for a new entry fails.  `pick_file_victim`
    picks the least recently used closed file or negative, trying the cold list before the hot one.  An open file is
    moved to its list's MRU end as it is passed over, so the next choice does not walk over it again and a run of
    evictions costs the same however full the cache is; an open file is evicted only when nothing closed is left, and
    then carries on uncached.  `pick_dir_victim` picks the least recently used directory that has no sub-directory in
    the cache and is not the path being installed or one of its ancestors (the install is about to hang its entry
    beneath those, and its caller may hold pointers to them).  A directory passed over moves to the newest end for the
    same reason an open file does.

A single function (`evict_node`) does the per-cnode bookkeeping in either case: release the name, release any content
or leafnames blob, unbind every handle-table slot naming the cnode, unlink it from its hash bucket, its parent's
children and (for a directory) the directory recency list, mark the slot vacant, push it onto the free list and move
`mutation_gen`.  Callers take a file or negative off its file list before calling `evict_node`.

**Recovery from claim failure.**  `cache_claim_with_recovery` makes two kinds of claim: a fresh slab slot and a
string-heap block for a new cnode and its name, and a content-heap block for a listing being built.  Either can fail,
because the region is full or because no single contiguous run of free bytes is long enough.  On failure the function
frees one thing at a time and retries the claims after each, up to `LMCACHE_RECOVERY_ROUNDS` times, because a freed
name or block only helps once it joins the free space either side of it into a run long enough.  It never touches the
path being installed or a directory above it.

* When a slot or a name is short, each round evicts a file or negative from `pick_file_victim` if there is one,
  otherwise a directory from `pick_dir_victim`, with its files and negatives.  A directory install that is short of
  string-heap space looks for a directory first, since its full-path name needs a longer run than one evicted leafname
  usually frees.
* When content-heap space is the only shortfall, only something holding content is worth freeing: first a file's
  cached content (`pick_content_victim`: content past its own lifetime first, since it can no longer be served,
  otherwise the least recently used holder), dropping only the bytes so the file's details still answer lookups; then
  the least recently used directory's listing (`release_oldest_listing`; the directory itself stays).  When neither is
  left, the claim fails: evicting a directory would free no content space by then.  Most files hold no content, so
  the content holder has to be searched for rather than taken from the old end of the recency lists.  A small,
  fragmented content heap can still run out of rounds before a listing's build block fits; that directory is then read
  from the server instead.
* When recovery fails, it rolls back its partial claims.  A failed insert while enumerating a directory marks that
  directory oversized (see ["EXCEEDS_CACHE flag"](#exceeds_cache-flag)), and a listing whose build block cannot be
  claimed leaves the directory without one; a failed insert outside an enumeration drops the parent's listing, which
  could otherwise leave out a name the server holds.

File content is claimed outside this function.  A capture through an open handle (`Cache_StreamClaim`) makes room by
dropping other files' content, a block at a time for up to `LMCACHE_RECOVERY_ROUNDS` rounds, but never a listing.  A
file read whole makes room the same way.

The cost of each eviction is the walk to a victim, which the moves to the newest end keep short, plus the victim's own
children; nothing sweeps the slab.  A recursive scan of a tree bigger than the cache therefore churns steadily through
its oldest directories rather than filling the cache and stalling.

Eviction leaves the parent's listing alone.  The evicted object is still on the server, so its name in the listing is
still true: fast-miss probes and path translation go on answering from it.  A cache-read serve, which hands out each
child's details, first checks that every listed name has a current cnode, and goes to the server if one is missing.  So
under pressure the cache sheds details before names, and a directory's listing survives its children being evicted.

### 3.5 Cached file content

A file cnode optionally has a content block in the content heap.  Content is kept only for a mount whose tier caches
content (a small machine's does not), for a non-empty file no larger than the mount's per-file cap
(`max_cacheable_file_bytes`: the content heap's size divided by `LMCACHE_CONTENT_HEAP_SHARE`, up to
`LMCACHE_MAX_CONTENT_CAP`), and for a filetype on the cacheable list (`Cache_ShouldCacheContent`: the types a build
loads again and again, such as Text, Data and Obey), so that large files such as sprites and archives do not churn the
heap.  Content is kept only on a file's second read (`Cache_ReadAgain`): the first load or read-only open sets
`LMCACHE_FLAG_READ_BEFORE` on the cnode instead.  A copy reads each file once, so it neither spends a memory copy per
file nor pushes out content a build reads again; a file read twice costs one extra read from the server before it is
kept.  The mark lasts as long as the cnode, so a file evicted between its reads starts again.  Content then arrives by
one of two routes:

* A LoadFile the cache cannot serve reads the whole file from the server and hands it to
  `Cache_NotifyLearnedWithContent`.
* A read-only open asks for a capture sized to the file.  The block is claimed at the first read
  (`streaming_claim_on_first_read`), so a file opened and closed unread costs nothing, and each GetBytes that reads
  from the server copies its bytes in.  At Close the capture is attached (`Cache_StreamAttach`) only if the whole file
  was read in order, the file's details are current and unchanged since the claim, and no other handle has the file
  open; otherwise it is abandoned and the reason traced as `STREAM_ABANDON`.

LoadFile then serves the file from its content when it is no longer than the length FileSwitch was last told, and
GetBytes serves reads through a handle bound to the file from the same bytes.  Nothing this client writes or saves is
captured.

The content has its own lifetime (`content_ttl`, from `LMCACHE_DEFAULT_CONTENT_TTL`).  It is dropped, and any capture of
the file under way is abandoned, when this client writes, extends, saves over or creates the file, when a write through
a handle fails part-way (`cf_forget_failed_write`), and when a notification reports the file changed.  It is also
dropped when the server reports a different length or datestamp, and a capture whose file's details have moved since
it began is refused when it would be attached.

### 3.6 File handles

The module-global `lm_handle_table` maps RISC OS file handles (1 to `LMCACHE_HANDLE_TABLE_SIZE`) to the cnode each one
is bound to, set by `Cache_HandleAssign` at the open.  A file open on several handles has a binding for each, so its
cached details follow writes through any of them.  This is what makes cnode reuse across renames safe: a file rename
re-keys the cnode (new path, new hash bucket) without claiming a new slab slot, so an open handle goes on referring to
the same cnode after the rename.  Every binding naming a cnode is cleared when the cnode is freed; `Cache_HandleLookup`
then returns NULL and the open file carries on uncached.  `Cache_HandleLookup` also drops a bound file's content once
its lifetime has run out, since reads through the handle are served from it.

A slot also records a capture in progress through its handle, with the file's details when the capture began.  The
record lives here rather than in `c/CoreFn` because the content heap can be emptied (`Cache_Flush`) or destroyed
(dismount) while files are open.  Either withdraws the record, `Cache_StreamBlock` then returns NULL, and the open file
carries on uncaptured instead of writing into, or later freeing, memory the heap has taken back.

### 3.7 Change notification and echoes

Each CHANGE_NOTIFY record is checked with `Cache_IsEcho` before it is applied.  A change this client makes is reported
back by the server, but the local hook has already updated the cache; applying the echo would undo state the hook has
just installed and raise a second Filer refresh.

A local change opens echo windows (`Cache_BumpSuppress`): the changed object's own window, for the server reporting it
added or modified, and its parent directory's, for the server reporting it removed.  A write through an open handle
opens the bound file's window (`Cache_BumpSuppressNode`) and expires the file's details, because the server dates the
write by its own clock and its report of that is skipped as an echo.  `Cache_IsEcho` then skips an event only when the
cache already shows its outcome:

- A removal is an echo when the parent's window is open, the cache holds nothing live at the path spelt exactly, and
  the parent's listing does not name it spelt exactly.  So the old name of this client's own change of letter case is
  an echo too: the cache holds that name only in the new case.
- Any other event is an echo when it names a cached file whose own window is open.

So a change another client makes beside this client's in the same directory disagrees with the cache and is still
applied, with its Filer UpCall.  The one change taken for an echo wrongly is another client's change to the very file
this client changed, inside its window; that file's details are then left until they expire.  Directories are never
echoes except for a removal, because one window per cnode cannot tell a directory's own change from the changes its
children report, so creating or renaming a directory costs one extra Filer refresh.

`STATUS_NOTIFY_ENUM_DIR` ("overflow") means the server has stopped tracking individual changes.  The most common cause
is a build burst in which this client made a flurry of changes and the server's buffer overflowed reporting them back,
so wiping the cache on every overflow would throw away knowledge the cache holds.  Every hook that records a change
this client made stamps `last_local_mutation_at`, and `Cache_NotifyOverflow` then takes one of two paths:

- An overflow within `LMCACHE_OVERFLOW_BUSY_THRESHOLD` of the last local change is most likely the echo of this
  client's own changes.  Every listing is dropped, since another client's additions may have been lost with them, but
  the details keep their lifetimes.  Negatives and cached content, which a lost notification could make wrong, are
  clamped to the grace lifetime (`LMCACHE_DEFAULT_OVERFLOW_GRACE_TTL`).
- Any other overflow, or any overflow on a mount with the aggressive-refresh quirk, clamps every entry's lifetime to the
  grace lifetime, drops every listing and all cached content.

A notification record whose path is too long to hold names nothing the cache can find, so it is taken as an overflow
too.  Either way every capture under way is abandoned and `mutation_gen` moves.  A lost connection is treated more
strictly: `Cache_NotifyConnectionLost` expires every entry outright, whatever the time since the last local change,
because the outage may already have lasted longer than any grace period.

## 4. Testing on Linux

The cache code compiles on Linux as a self-contained translation unit (no RISC OS headers, no SMB2 layer, no
FileSwitch) through four seams the code provides:

* the single `#ifdef RISCOS` in `c/Cache`: with `RISCOS` undefined, the RISC OS headers and the `debug` macro are not
  included, and a harness supplies them in a header of its own (`test.h`);
* the substrate allocator (`Cache_SetSubstrateAllocator`), which a harness can leave as malloc;
* the wire vtable, which a harness can replace with a scripted one that replays directory contents a test describes,
  so the cache can be exercised with no network;
* the time source passed to `Cache_Init`, so a harness can drive the clock itself.

A harness can build the cache as a unity build, including `c/Cache` into its own test source, so that every `static`
function and struct field is visible to the tests without the cache exporting anything.  It must define `LM_CACHE`,
because without it `c/Cache` is only the functions that do nothing and there is no cache to test.

RISC OS has no AddressSanitizer or UndefinedBehaviorSanitizer, so a Linux build is the place to run invariant-heavy
changes under `-fsanitize=address,undefined`.  With every static visible, a harness can also walk the slab, the hash
table, the recency lists, the heaps and the handle table after each test, looking for orphaned cnodes, broken list
links and listings that disagree with the slab.  LeakSanitizer is of no use there: the cache lives in one opaque
substrate, so per-entry leak tracking means nothing.

## 5. Design notes

### Negative caching

For every "is `kernel.h` here?" the toolchain asks, the answer is "no" the great majority of times.  In a trace from the
verbose build (`!MkVerbose`), a compiler's include-path search shows up as a flood of CREATE requests that fail because
the path does not exist: the cost is a round trip for every probe, not slow I/O, so caching "this does not exist"
matters as much as caching what does.  A
`LMCACHE_OBJECT_NOTHING` cnode is the same shape as a file or directory cnode and pays the same lifetime; what it buys is a
wire round trip avoided per probe.  The fast-miss path (`Cache_DirHasLeaf` against a definitive listing) is the
strongest form of this: an in-RAM index probe answers "does this leaf exist in this directory?" without even claiming
a negative cnode, and `Cache_PathRuledOut` extends the answer to every path beneath a missing directory.

### Re-resolving cnodes after a wire call

Never carry a cnode pointer across a wire call without re-resolving via `mutation_gen` afterwards.

A wire call is the only point inside a handler at which the cache can be changed by something other than the
caller's own code: `SMB2_SendRecv` processes any pending CHANGE_NOTIFY responses inline before returning, and that
processing runs the `Cache_Notify*` hooks.  A cnode pointer that was valid before the wire call may have been freed,
and its slot reused, by the time the call returns.  The `mutation_gen` check at each step boundary, together with the
rule of never holding a cnode pointer across a wire call without it, is what keeps every holder of a cnode pointer safe.

The pre-handler drain plays the complementary role: it applies any server invalidation that has already arrived at a
fixed point, so the handler's cache-read step sees the freshest state.  Without it the cache would serve stale data
in the gap between a CHANGE_NOTIFY message landing in the TCP buffer and the next time some wire operation happened to
drain it.

### Re-entry guards

A filing-system module called through `OS_File`, `OS_Find` and the rest is nested inside FileSwitch's SWI.  Transient
callbacks scheduled with `OS_AddCallBack` normally fire when control returns through the *outermost* SWI exit, not
through any inner SWI the module issues.  But a TaskWindow can suspend a LanManFS call part-way (it yields to the Wimp
while a socket call waits, or when its output buffer fills) and let other tasks, and their callbacks, run.  So the
background callback can arrive while an FSEntry handler is in progress.  The `lanmanfs_in_use` flag (set by
`FSEnter`) and `smb2_sendrecv_active` (`SMB2_IsBusy`) stop it, or any other re-entrant caller, from touching the cache
or the sockets: the callback leaves the network alone while either is set, and a re-entrant filing-system call is
refused with "LanManFS in use".

### Directories need a recency list of their own

Every file or negative cnode carries a `parent` pointer back at its directory cnode, so a directory cannot be dropped
under pressure while it has children.  If directories could only leave through an explicit delete, rename or
tear-down, a recursive scan of a tree bigger than the cache (a music library of thousands of directories, say) would
fill the slab and string heap with directories, and recovery would have nothing left to evict.  With per-directory
child lists, a directory with no sub-directory can be evicted with its files at the cost of its own size, so
directories age on their own recency list like everything else and such a scan churns through its oldest directories.

### One key per path

The same logical path can reach the cache in several forms: with or without a leading `\`, in another letter case,
or with the `X:` drive prefix the path translation adds.  Two spellings with two hashes would give two cnodes for one
file.  So every public entry point skips a single leading separator (`skip_leading_sep`), `c/CoreFn` strips the drive
prefix before calling in (`cache_key_from_dos`), and the hash and path comparisons fold ASCII letter case, so the
stored key is the share-relative path without a leading separator.

### A rename within one directory keeps its parent's listing

`cc` and `objasm` write `tmp.o` and rename it to `final.o` within the same directory.  Dropping the parent's listing on
every such rename would throw away a good listing and force a wire round trip.  A rename within one directory is
instead a splice of the listing (remove the old leaf, append the new), so the directory stays definitive and the next
fast-miss probe still resolves in RAM.  A move between directories drops both parents' listings, because both
memberships change.

### No leases or oplocks

The client advertises no leasing capability at NEGOTIATE and asks for no oplock or lease on any CREATE; an
`SMB2_OPLOCK_BREAK` frame that arrives anyway is consumed and ignored.  Freshness comes from CHANGE_NOTIFY and the
lifetimes instead.  A lease or oplock lasts only while the handle that took it stays open.  For a directory that would
mean a server handle held open on every cached directory, and a held handle without delete sharing stops other clients
deleting or renaming the directory.  For a file it would protect nothing between calls, because a load opens, reads and
closes the file within one FSEntry call.

### Every translation unit agrees on struct layout

Norcroft's `bool` is one byte under `<stdbool.h>` and four bytes under a legacy `enum { false, true }` typedef.  A
translation unit that saw the other definition would lay out every cache struct differently after its first `bool`
field, and the two views of a cnode would disagree silently.  `h/stdtypes` includes `<stdbool.h>`, and nothing may
define `bool` any other way.

### Initialising allocations by field

Heap and Dynamic Area allocations come back uninitialised.  Targeted field initialisation is faster and clearer than
a `memset` to zero followed by overwriting particular fields; the explicit set-up paths in `cache_insert_or_refresh`
and `cache_insert_nothing` are examples.  On hot paths `strncpy` is a trap too, because it zero-pads up to `n`, doing
work for nothing; copy a known length with `memcpy` and add the terminator explicitly.

### Handle bindings survive a rename via in-place re-key

A RISC OS file handle is bound to a cnode, not to a slab-slot index.  A file rename re-keys the existing cnode (new
path, new hash bucket, same slot) rather than claiming a fresh one, so an open handle goes on referring to the same
backing file when the file's path changes underneath it.  A directory rename instead evicts the directory with its
subtree and inserts it again, since every cnode beneath it is keyed by a path that has changed.

That only holds if the cache is re-keyed before the server's notification of the rename is handled, and the
notification can arrive while the rename request is still waiting for its answer.  Handled first, it reads as another
client removing the file and evicts the cnode, binding and all; a compiler setting an object file's type while it
still has the file open is one way to meet this.  So `cf_rename` in `c/CoreFn` renames the cnode and opens the echo
windows before sending the request, and renames it back if the server refuses.  A rename that changes only letter case
is covered too: the notification of the old name finds the renamed cnode only by ignoring case, and is taken for the
echo it is.

### Content is captured as it is read

Capturing content as GetBytes delivers it costs no extra wire round trip: the only cost is a memory copy of bytes the
caller was reading anyway.  Only reads are captured, because a build reads the same headers thousands of times and
seldom reads back what it writes.  Even of reads, only a file's second is captured, because the end of a build is
mostly copies, which read each file once.  The per-file cap derived from the content heap keeps a single large file from
starving every other cache consumer, and a claim made on a full heap gives up other files' content, least useful first
(`pick_content_victim`), but never a listing: a capture is worth less than the directories a build keeps searching.
