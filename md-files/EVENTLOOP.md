Implement the first real main/UI event loop for `c64m`.

Current behavior:

* Running `c64m` creates the SDL window and then exits immediately.

Desired behavior:

* Running `c64m` opens the window and keeps it open.
* The app exits cleanly when the user closes the SDL window.
* The app exits cleanly on:

  * macOS: Command+Q
  * Windows/Linux: Ctrl+Q
* F9 toggles emulator UI visibility.
* F10 toggles turbo mode state.
* F1-F8 must not be consumed by the host UI; they are C64 keys.
* F9-F12 are reserved host/emulator-control keys.
* Remove/ignore the older F2 UI-toggle assumption.

Scope:

* Do not implement real keyboard matrix input yet.
* Do not implement configurable `[Keyboard]` or `[Hotkeys]` ini sections yet.
* Do not implement Nuklear UI yet.
* Do not implement real turbo behavior yet unless the runtime command already exists cleanly.
* It is acceptable for F10 to toggle frontend state and log/record the value for now.

Architecture:

* SDL event polling and rendering stay on the main/UI thread.
* Runtime and live machine stay on the emulation thread.
* Frontend talks to runtime only through `runtime_client`.
* Do not pass SDL events to runtime directly.
* Do not let frontend call machine code.

Suggested behavior:

```text
main/UI loop:
    running = true

    while(running):
        while(SDL_PollEvent(&event)):
            if event is SDL_QUIT:
                running = false

            else if event is key down:
                if host quit shortcut:
                    running = false

                else if key == F9:
                    toggle ui_visible

                else if key == F10:
                    toggle turbo_enabled

                else if key is F1-F8:
                    leave for future emulator keyboard path

                else:
                    leave for future emulator keyboard path

        poll runtime_client events non-blocking
        clear/render placeholder frame
        present renderer
```

Host shortcut rules:

* macOS quit: Command+Q.
* Windows/Linux quit: Ctrl+Q.
* Use SDL modifier state from the key event.
* Prefer checking platform with existing compile-time platform macros if available.
* If no platform abstraction exists yet, use SDL/platform preprocessor macros locally in the SDL-facing/frontend code.

Rendering for this slice:

* Clear the renderer each frame.
* Present each frame.
* Keep vsync as currently configured.
* No texture upload required yet.
* No scaling work required unless it is already easy and isolated.

Shutdown:

* On loop exit, request runtime shutdown through `runtime_client_quit()` or existing runtime stop API.
* Drain/poll runtime events as appropriate.
* Ensure the runtime thread is joined before process exit.
* Destroy the SDL window/renderer.
* Call `platform_shutdown()`.

Acceptance checks:

* `cmake -S . -B /tmp/c64m-cmake-check -G Ninja`
* `cmake --build /tmp/c64m-cmake-check`
* `/tmp/c64m-cmake-check/c64m --noini`

  * Window remains open.
  * Closing the window exits cleanly.
* `/tmp/c64m-cmake-check/c64m --inifile /tmp/c64m-video.ini`

  * Window remains open.
  * Ctrl+Q exits on Windows/Linux.
  * Command+Q exits on macOS.
  * F9 toggles `ui_visible` state without exiting.
  * F10 toggles `turbo_enabled` state without exiting.
  * F1-F8 do not toggle UI.
