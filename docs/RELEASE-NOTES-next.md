# pokopia-native (next release): draft notes for Story co-op

A draft for the next release's notes (the version number isn't decided yet): merge the section below into that release's
`RELEASE-NOTES-<version>.md` and delete this file.

---

### Story co-op (experimental)

**Link Play visitors can now talk to the story characters in every town.** In a story town the game lets only the host talk
to characters like Professor Tangrowth: a visitor got the "Talk" prompt, and nothing happened. With **Story co-op** on,
visitors talk to them and hear the same story as the host, including the host's current requests.

- **What counts for the host:** everything visitors do in the world, as with Interact everywhere (which Story co-op turns on
  with it): building, watering, placing and picking up, the host's challenges and habitats.
- **What doesn't:** story requests still count only for the host. A visitor can hear a request, but finishing the
  conversation doesn't complete it; the host has to. A visitor's shop purchases still go to their own town, so for a
  building request the host buys the kit and drops it for the visitor.
- **Turn it on on every device:** iPhone and Mac: **Story co-op** in the launcher (under Link Play); Windows: the **Story
  co-op (experimental)** box in the window before the game starts, or `-Story` from a terminal. A device with it on only
  sees hosts that have it on too; with it off, nothing changes from 1.2.x.
- **It is experimental.** Tested with two players in Withered Wasteland (story areas at Environment Level 2 and 3). Keep a copy
  of your save before a long session, and please report what does or doesn't work.
