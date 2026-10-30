# Symbolic links

A symbolic link (or "symlink") is an object on an SMB server that points at another file or directory.  RISC OS has no
native concept of a symlink, so when a share holding one is mounted, LanManFS makes it look like the thing it
points at: the size, type and dates shown are the **target's**, not the link's, and reading it or listing a directory
through it works as it would on the target.  This applies to SMB2 mounts; an SMB1 mount sees whatever the server
presents.

## How LanManFS handles them

Resolution happens eagerly, whenever a directory listing read from the server meets the link.  When the listing reports
an entry as a symlink reparse point (`IO_REPARSE_TAG_SYMLINK`), LanManFS reads the link's target from the server on the
spot and replaces the entry's details with the target's.  The entry then looks the same as one for the underlying
object: the same kind (file or directory), the same length, the same dates.

If the target is itself a symlink, the chain is followed for up to `SMB2_SYMLINK_MAX_HOPS` links before giving up.

Only targets inside the mounted share can be followed:

- **Relative** targets (`..\foo\bar`) are resolved against the directory containing the link.  A target whose `..`
  steps climb above the share root is refused.
- **Absolute** targets (`\some\path`, `\??\C:\...`, `\??\UNC\otherserver\share\...` and so on) are refused, because
  they name a path on the server's own disc rather than one inside the share.

Only symlinks are followed.  Any other kind of reparse point (a junction or mount point, a deduplicated file, a cloud
placeholder) is listed as the server reports it, and the server handles any access to it.  A symlink whose target is
such an object resolves to that object.

The link's own leaf name is kept.  A symlink called `Cache_link.h` that points at `Sources\Cache.h` shows up as
`Cache_link.h` with the size of `Cache.h`; the filetype comes from the **link's** leaf name, so RISC OS callers see the
type the name suggests.

A path that passes through a symlink, such as a file inside a linked directory, is followed the same way when it is
opened or renamed: when the server stops at the link, LanManFS reads the link's target and tries again at the resolved
path, within the same limits.

## Deleting, renaming and saving over a link

Deleting or renaming a symlink acts on the link itself; the object it points at is left alone.  This holds too for a link
whose target cannot be reached, so a link left behind when its target was deleted can still be deleted or renamed.
A link whose target can be reached is removed as the kind of object its target is.  One whose target cannot be reached
is removed as the kind of link it was made as (a directory link or a file link).  So a link made as one kind but
pointing at the other (on Windows, `mklink` without `/D` aimed at a directory, or `mklink /D` aimed at a file) cannot
be deleted from RISC OS while its target is there.

Other operations go through the link.  Saving over a link writes the new contents to its target and keeps the link.
Setting a link's type renames the link to carry the new type, and writes the attributes and dates to its target.

On a mount with a cache, renaming a link drops the details cached for it, which are its target's, because a relative
link moved to another directory points at another object.  The next access reads them from the server again.

A link to a directory shows up as a directory, so deleting it from the Filer deletes the directory's contents first, as
for any directory, and then the link.  The target directory is left in place but empty.

## In the Filer

Symlinks are not drawn distinctively.  Because the details are the target's, a link to a directory shows up as a
directory (with a folder icon, which opens) and a link to a file shows up as a file (with the file's type and size).
Double-click does what it would on the real object.

## When resolution fails

A symlink whose target cannot be reached when the directory is listed is presented as a **zero-length file**
("inert").  Recursive scans see a zero-length file and do not try to descend into it.

A link is inert when:

- The target has been deleted or renamed.
- The target is absolute, or climbs out of the mounted share.
- The chain is longer than `SMB2_SYMLINK_MAX_HOPS` links, or loops.
- The server refuses to disclose the link's target.

On a mount with a cache, the cache records that the link is inert.  While that entry is current, loading the link,
opening it or reading its catalogue information first tries the resolution again (see "Self-healing on access" below);
if it still fails, the call fails with **"Symbolic link could not be followed"** (`ESYMLINKFAILED`).  Otherwise, as on
a mount without a cache, each access goes to the server, which fails it while the target is missing (most often as
"not found") and works once the target is back.  Deleting and renaming the link work throughout, because they act on
the link itself.

## Self-healing on access

If a symlink was inert when listed because its target did not exist, and then someone restores the target on the
server, the next load, open or catalogue read of the link resolves it again before giving up.  On success the cached
entry takes the target's details and a Filer refresh is raised, so any window showing the link redraws with the new
size and type.

The call that heals the link reports what happened in one of two ways:

- A catalogue read carries on and returns the healed details.
- A load or an open fails with **"Object has been modified; please try again"** (`EOBJECTCHANGED`), because the caller
  acted on the zero-length details it was given before.  Trying again works.

A link that was inert as a file and whose target turns out to be a directory is not healed in place; it stays
"Symbolic link could not be followed" until the directory holding it is listed again.

Every access tries afresh, with no waiting period between attempts; each attempt costs one resolution of the link on
the server.

## Server compatibility notes

### Windows hosts

Both file symlinks (`mklink`) and directory symlinks (`mklink /D`) are handled.  A junction (`mklink /J`) is not a
symlink: it is listed as the server reports it.

### Samba hosts

Samba normally follows symlinks itself, so LanManFS sees the target as an ordinary file or directory.  A server that
reports a link as a symlink reparse point is handled as described above.

### Minshall-French symlinks

The Linux SMB client, mounted with the `mfsymlinks` option, stores each symlink it makes as an ordinary fixed-length
1067-byte file whose contents start `XSym` followed by the target path.  Any server holds such a file as it would any
other.  LanManFS does not recognise that format and shows such links as plain files; opening one shows the XSym header
followed by the target path in readable text.

### macOS hosts

LanManFS has not been tested against symlinks on macOS's built-in SMB server (the "File Sharing" panel).  A link that
the server reports as a symlink reparse point is handled as described above.

A "Symbolic link could not be followed" error against a macOS share is worth a Reporter log and a bug report.
