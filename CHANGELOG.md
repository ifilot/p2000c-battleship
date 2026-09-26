# Changelog

All notable changes to Zeeslag for the Philips P2000C are listed here.
The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and the project uses [semantic versioning](https://semver.org/).

## [1.0.2] - 2026-09-26

### Fixed

- A single keypress sometimes acted twice. The terminal board scans the
  keyboard with the same processor that draws the picture, so while it is
  busy the key's release is seen late and the keyboard's auto-repeat fires.
  After a key that kept the program busy for a third of a second or more
  (a shot and the computer's reply, a random fleet, a whole screen), the
  same key again is now ignored when it was already waiting or comes
  within a quarter of a second. Quick keys such as cursor steps are never
  filtered, so tapping or holding a cursor key works as before. The filter
  covers every place that waits for a key, including the help page, the
  quit confirmation and the start screen.
- The key that ends the screen saver can no longer come through a second
  time as a game key.

### Changed

- The compiler's allocator search is limited to 10000 instead of 200000
  per node, which makes the build quicker.
- The README links to the other P2000C games.
- `make test` also checks that `Z` sent twice at once deals only one
  random fleet, and that a second `Z` after a pause deals another.

## [1.0.1] - 2026-09-26

### Fixed

- The screen saver now also runs while the quit confirmation
  (`Stoppen? (J/N)`) waits for an answer, and the question is shown again
  when the picture comes back.

## [1.0.0] - 2026-09-26

### Added

- Zeeslag (Battleship) against the computer for the Philips P2000C under
  CP/M, in Dutch, in the terminal board's 512x252 high-resolution graphics
  mode, with the score panel on the text plane.
- Five ships per side on a 10x10 sea (5, 4, 3, 3 and 2 cells) that may not
  touch, not even at a corner.
- Placing the fleet by hand with the cursor keys or WASD, `R` to turn a
  ship, `U`/`BS`/`DEL` to pick up the last one, and `Z` for a random fleet;
  a ship that cannot lie where it is shown is drawn in inverse video.
- Three levels: *licht* fires at random, *normaal* finishes off a ship
  after a hit, and *zwaar* fires at the cell where the remaining ships most
  likely lie.
- Pictures of the ships from above, a sea of waves, shot animations (gun
  sight, explosion or plume of water, lasting fire or splash rings), sunk
  ships as flashing wrecks, and a mini map of your own fleet showing the
  computer's shots.
- A panel with shots, hits, hit ratio and ships afloat for both sides, plus
  the game clock; the computer's surviving ships are revealed at the end.
- Title picture, start screen, help screen, quit confirmation and a CRT
  screen saver after five minutes without a key.
- Headless-emulator tests that play whole games at every level and check
  the rules, counters and panel from memory; screenshot, benchmark and
  sprite generator tools; `make deploy` for a ZuluBlaster/SASI disk image.

[1.0.2]: https://github.com/ifilot/p2000c-battleship/compare/v1.0.1...v1.0.2
[1.0.1]: https://github.com/ifilot/p2000c-battleship/compare/v1.0.0...v1.0.1
[1.0.0]: https://github.com/ifilot/p2000c-battleship/releases/tag/v1.0.0
