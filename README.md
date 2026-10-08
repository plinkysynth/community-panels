# Plinky 12 Community Panels

This repository collects community-submitted Plinky 12 custom panel designs.
Panels listed here are shown in the [Community section of the Plinky 12 web
panel library](https://plinky12.com/community.html).

Community panels are not official Plinky examples. They are reviewed for basic
metadata and build compatibility, but they are not officially supported by
Plinky.

If you find a problem, please [file an issue](https://github.com/plinkysynth/community-panels/issues).

For more information about Plinky 12, visit [plinky12.com](https://plinky12.com).

## Sharing Your Panel

Got a panel you'd like other people to try? **Submit it by opening a pull
request (PR) to this repository on GitHub.** A pull request is a way to ask us
to review your files and add them to the community collection.

1. **Get your panel ready.** Test it in the [Plinky 12 web
   IDE](https://plinky12.com/ide.html), then save your panel's `.cpp` source
   file. Add the required metadata (your author name and firmware version),
   plus a display name and short description to help people find it. The
   sections below explain the details.
2. **Make your own copy of this repository.** Sign in to GitHub and click
   **Fork** on [plinkysynth/community-panels](https://github.com/plinkysynth/community-panels).
   A fork is your own copy where you can add your files.
3. **Add your panel to your fork.** Put the source file in
   `your_name/your_panel/your_panel.cpp`, following the layout below. You can
   also add a README with instructions and a small square artwork image.
   You can upload files through GitHub's website or use Git locally; commit
   (save) the changes to your fork.
4. **Open a pull request back here.** From your fork on GitHub, choose
   **Contribute → Open pull request**, with `plinkysynth/community-panels`
   as the destination repository and `main` as the destination branch.
   Briefly explain what your panel does and how you've tested it, then submit
   the PR.
5. **Work through the review.** A maintainer will review the submission and
   may ask for changes, which you can add to the same PR. Once accepted, your
   panel can be built and published to the community library. Opening a PR
   does not publish it immediately.

To update a panel later, follow the same process with changes to its existing
files. Once it's published, use its [community permalink](#permalinks) to
share it with other Plinky users.

## Panel Layout

Use one author directory, then one directory per panel:

```text
author_slug/
  README.md          # optional author bio shown on the author gallery page
  my_panel/
    my_panel.cpp
    README.md        # optional, but recommended
    artwork.png      # optional; artwork.webp is also accepted
    manual.pdf       # optional PDF manual, up to 5 MiB
```

Each author directory can contain one optional `README.md` and one or more panel
directories. Each panel directory must contain exactly one `.cpp` file directly
inside the directory. Do not put panel `.cpp` files at the repository root or
directly inside an author directory, and do not use subdirectories inside a
panel submission.

Inside each author directory:

- `README.md`: optional author bio, shown on that author's community gallery
  page and on community panel covers in the Plinky web IDE.

Inside each panel directory, the only other accepted files are:

- `README.md`: optional panel-specific notes.
- `artwork.png` or `artwork.webp`: optional square thumbnail artwork. We recommend
  1024×1024 pixels; the publishing build downsizes larger images to that maximum,
  preserving your original in this repository. Smaller images are not enlarged.
- `manual.pdf`: optional PDF manual, up to 5 MiB. It is hosted on the Plinky
  website and linked as **PDF manual** on the panel detail page and in the IDE
  library. You can also link to it from your panel README using
  `[PDF manual](manual.pdf)`. Use a static document without scripts or attachments.

The community panel ID comes from the author and panel directory names:
`author_slug/panel_slug`. Directory slugs must be lowercase and contain only
letters, numbers, and underscores. Use the `@Name` metadata for display names
with spaces and punctuation.

Metadata for your panel goes in a block comment in the `.cpp` file. See
[Required Metadata](#required-metadata) below for the format.

The optional panel `README.md` is shown in the Plinky web IDE panel cover after
someone opens the panel from the library. The public library listing uses the
C++ metadata and artwork, not the README.

## Permalinks

Use the consumer-facing community panel page when sharing a panel with Plinky
users:

```text
https://plinky12.com/community/author_slug/panel_slug
```

The panel ID is the author slug plus panel slug, for example
`mmalex/zone_plate`. This page is intended for people who want to inspect,
download, or flash a community panel without opening the code editor.

If you want to link directly to the code editor version of a panel, use the
same community panel ID in the Plinky web IDE community library URL:

```text
https://plinky12.com/ide.html?library=community&panel=author_slug/panel_slug
```

That link opens the panel inside the Custom Panels IDE.

## Required Metadata

The first block comment in the panel `.cpp` file is used as the panel library
metadata. Every community panel must include:

```cpp
/*
@Name: My Panel
@Author: Your Name
@Firmware: latest
@Tags: sequencer, midi
@Preferred Panels: blocks, chords
@Description: A one-sentence summary shown in the library.
  Indented lines continue the previous metadata value.

Longer plain-text description can go here if you do not use @Description.
*/
```

Indented lines after a metadata field are joined onto that field with spaces.
This is useful for keeping longer `@Description` values readable in source.

Required fields:

- `@Author`: author or maintainer name.
- `@Firmware`: firmware base to build against. Use `latest` for the current
  beta firmware, `beta`, `alpha`, or `release` for a specific channel, or a
  four-character firmware build code such as `c019` to pin the panel to a
  specific firmware base. The maintainer can only bless artifacts for firmware
  entries that are listed in Plinky's `versions.json` and whose Git commit is
  available locally.

Optional fields:

- `@Name`: display name. If omitted, the panel class or filename is used.
- `@Description`: short in-page description.
- `@Documentation`: an `https://` or `http://` URL with usage notes,
  documentation, or a project page.
- `@Video`: a YouTube URL to embed on the panel detail page. Any metadata field
  whose value is a YouTube link may be embedded by the website, but `@Video` is
  the preferred key for a primary demo.
- `@Tags`: comma-separated discovery tags describing what the panel does. Use
  single words with no spaces. Tags are matched case-insensitively by the
  website, but lowercase is preferred for readability. Suggested starting tags
  are `midi`, `effect`, `sequencer`, `groovebox`, and `visuals`.
- `@Preferred Panels`: comma-separated Plinky panel layouts this design works
  especially well with. Use `blocks`, `chords`, or `toadstep` when the control
  layout or visual language fits one of those panels. Use `all` only when the
  design is broadly suitable for any Plinky 12 panel. If this field is omitted,
  the website treats the panel as not restricted to a specific panel layout when
  users filter by `blocks`, `chords`, or `toadstep`. These values are used by
  the website to show compatibility chips and help users filter the community
  gallery.
- `@Discussion`: an `https://` or `http://` URL for discussion or support.
- `@Category`: optional free-text metadata. It is shown with the panel, but it
  is not used for library grouping or filtering.
- `@Level`: intended for built-in examples, not community panels. If present,
  it must be one of `Simple`, `Intermediate`, or `Advanced`.

Do not use `@Artwork` metadata in community panel submissions. If a panel has
artwork, include exactly one file named `artwork.png` or `artwork.webp` next to
the `.cpp` file.

## Code Requirements

Each panel must be one self-contained C++ source file that defines one class or
struct derived from `panel_t`. Do not use `#include` directives or rely on
local `.h` files; the Plinky 12 web IDE does not support includes in custom
panel source.

Do not include generated firmware binaries, build directories, or large media
assets other than the optional PDF manual in a panel submission. Artwork should be reasonably small and suitable
for display as a square library thumbnail.
