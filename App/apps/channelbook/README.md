# ChannelBook overlay

ChannelBook is a visual memory browser for the v7 Fusion overlay platform. It
organizes the active Multiconfig bank into `ALL Channels`, useful scan lists and
`Out of list`, then selects a memory on the current VFO without writing
external Flash while the app is executing.

The UI follows the other Fusion applications while staying deliberately sober:
an inverted status capsule, a simple `>` selection marker, fixed columns and
concise bottom-key hints. There are no side markers, full-width separators or
decorative icons. Static strings, hint bars and empty-state artwork live in the
app asset sector rather than the 4 KiB executable overlay.

Three directories expose the channel organization:

- **Scan lists** shows named or populated scan lists, their live counts and the
  total channel count in the header capsule.
- **Alphanum index** groups named memories by their first character:
  `0-9`, `ABC`, `DEF`, `GHI`, `JKL`, `MNO`, `PQRS`, `TUV` or `WXYZ`. Symbols
  join `0-9`; unnamed memories are kept separately in `NO NAME`.
- **Mem num index** groups memories by number: `0001-0100`, `0101-0200`, and
  so on through `1001-1024`.

Empty scan lists, alphanumeric groups and memory-number ranges are omitted.

Opening any group shows five fixed-column memory rows with name and
frequency. The header uses capsules for the group, selected four-digit memory
number and position counter. Channels keep their memory-bank order inside every
group.

`F` cycles through scan lists, the alphanum index and mem num index. Typing a
digit from any directory or channel view starts direct memory-number entry on
the bottom line; `MENU` opens the matching memory in `ALL Channels`. The active
directory, index or content view, selected group and current memory are restored
on the next launch. The Scan List position is also retained while another
directory is active during the same session.

## Controls

- `UP` / `DOWN` or `LEFT` / `RIGHT`, according to `SetNav`: move visually
  through directories or channels; hold to repeat.
- `MENU`: open a group, confirm direct number entry or use the selected memory.
- `F`: switch directly to the next directory, from either a directory or a
  channel list.
- `*`: delete the last entered digit.
- `0`–`9`: enter a memory number directly.
- `EXIT`: delete the last entered digit, return to the directory, or quit.
