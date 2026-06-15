# DISPLAY.md

# c64m Display Architecture

This document defines how video is presented to the user.

It intentionally separates:

* emulated video generation
* video presentation
* UI layout
* SDL window management

The emulator always operates as a single-window application.

---

# Goals

The user should be able to:

* run the emulator with no UI visible
* see the Commodore display at maximum size
* press F9 to toggle the emulator UI
* continue seeing the Commodore display while the UI is visible
* resize the window at any time

The Commodore display must scale automatically to fit its assigned area.

Aspect ratio must be preserved.

---

# Single Window Model

The application owns exactly one SDL window.

```text
SDL Window
    ├── Commodore display area
    └── Optional emulator UI
```

No additional SDL windows are created.

No debugger popups.

No detached tool windows.

All emulator functionality lives inside the main application window.

---

# Display Modes

The frontend supports two display modes.

## Mode 1: Display Only

Default mode.

```text
+--------------------------------------+
|                                      |
|                                      |
|          Commodore Display           |
|                                      |
|                                      |
+--------------------------------------+
```

The Commodore display area occupies the entire client area of the window.

The display is scaled to fit while preserving aspect ratio.

Letterboxing or pillarboxing is allowed.

---

## Mode 2: UI Visible

Toggled with:

```text
F9
```

Example layout:

```text
+--------------------------------------+
| Commodore Display | CPU Registers    |
|                   +------------------|
|                   | Disassembly      |
|-------------------|                  |
| Memory View       |                  |
|                   +------------------|
|                   | Misc (scroll)    |
+--------------------------------------+
```

The Commodore display becomes one frontend view.

Initially it is positioned in the upper-left corner.

The frontend may later allow layout customization.

The display continues to scale within its assigned rectangle.

The Misc area is a scrollable area where you can see SID, VIC II registers, configured disk images, etc.

---

# Commodore Display Surface

The emulator produces a fixed-size display surface.

Recommended dimensions:

```text
384 x 272
```

This represents:

```text
320 x 200 active display
plus visible border area
```

The exact VIC-II implementation may internally produce PAL or NTSC raster timings.

The frontend display surface remains fixed.

---

# PAL and NTSC

PAL and NTSC affect:

```text
timing
frame rate
raster behavior
VIC-II emulation
```

PAL and NTSC do not affect:

```text
SDL window size
frontend layout
display scaling logic
```

The user should not see the window resize when switching standards.

---

# Default Window Size

Recommended startup size:

```text
1152 x 816
```

This is:

```text
384 x 272
scaled by 3
```

Advantages:

```text
integer scaling
good visibility
reasonable desktop footprint
```

The window is resizable.

---

# Scaling Rules

The Commodore display is always scaled from:

```text
source:
    384 x 272
```

to:

```text
destination:
    assigned display rectangle
```

Rules:

```text
preserve aspect ratio
never stretch independently in X/Y
allow letterboxing
allow pillarboxing
```

The display should always be centered within its assigned rectangle.

---

# Video Ownership

Machine owns video generation.

```text
machine
    ->
pixel snapshot
```

Runtime owns publication.

```text
machine
    ->
runtime
    ->
snapshot publication
```

Frontend owns presentation.

```text
snapshot
    ->
SDL texture
    ->
screen
```

The machine never creates SDL resources.

The machine never references SDL types.

---

# Snapshot Model

Machine produces:

```text
host-side pixel buffer
```

Example:

```c
typedef struct video_frame {
    uint32_t *pixels;
    int width;
    int height;
} video_frame;
```

Recommended dimensions:

```text
width  = 384
height = 272
```

The runtime publishes completed frames.

The frontend consumes completed frames.

No live machine pointers cross threads.

---

# Texture Ownership

Frontend owns:

```text
SDL_Texture
```

The texture is updated from the latest published frame.

Example flow:

```text
machine
    ->
frame buffer
    ->
runtime publishes frame
    ->
frontend receives frame
    ->
SDL_UpdateTexture()
    ->
SDL_RenderCopy()
```

---

# Turbo Mode

Runtime may generate frames faster than the UI can display them.

The frontend should:

```text
display newest complete frame
drop stale frames
```

The frontend should not attempt to display every generated frame.

Responsiveness is preferred over perfect frame delivery.

---

# Input

Display mode toggle:

```text
F9
```

Behavior:

```text
display only
    ->
F9
    ->
UI visible

UI visible
    ->
F9
    ->
display only
```

No additional window creation occurs.

---

# Initial Implementation

Implement the following first:

```text
SDL window
SDL renderer
Resizable window

Fixed Commodore surface:
    384 x 272

Default window:
    1152 x 816

Display-only mode

F9 toggle

UI-visible mode placeholder

Aspect-preserving scaling

Letterboxing/pillarboxing
```

Do not implement:

```text
multi-window support
docking
layout persistence
multi-monitor support
advanced scaling options
```

Those can be added later.

---

# Core Rule

The Commodore display is a fixed emulator-generated image.

The frontend decides where it is displayed.

The window may change size.

The display surface does not.

```text
machine generates pixels
runtime publishes frames
frontend places and scales frames
platform owns SDL
```
