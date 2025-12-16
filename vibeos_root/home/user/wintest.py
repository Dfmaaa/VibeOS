# wintest.py - Simple window test
# Just opens a white window

import vibe

print("Creating window...")
wid = vibe.window_create(100, 100, 200, 150, "Test")

if wid < 0:
    print("Failed to create window")
else:
    print("Window created, id=" + str(wid))

    # Fill with white
    vibe.window_fill_rect(wid, 0, 0, 200, 150, 0x00FFFFFF)

    # Draw some text
    vibe.window_draw_string(wid, 10, 10, "Hello!", 0x00000000, 0x00FFFFFF)
    vibe.window_draw_string(wid, 10, 30, "Press Q to quit", 0x00000000, 0x00FFFFFF)

    vibe.window_invalidate(wid)

    print("Window drawn, entering event loop")

    running = True
    while running:
        ev = vibe.window_poll(wid)
        if ev:
            t, d1, d2, d3 = ev
            if t == vibe.WIN_EVENT_CLOSE:
                running = False
            if t == vibe.WIN_EVENT_KEY:
                if d1 == ord('q') or d1 == 27:
                    running = False
        vibe.sched_yield()

    vibe.window_destroy(wid)
    print("Done!")
