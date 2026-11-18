# Contributing

Thanks for helping. The aim is to run LEGO Island Xtreme Stunts as if the best programmers had rebuilt it with
unlimited time, without ever changing how it plays.

## The rules

- **No game files, ever.** Nothing from the game goes into the repository or into an issue: no executables,
  data, textures, models, sounds, music, movies, extracted tables, and no translated C (`gen/` is made on each
  player's machine and is never committed). Hand-written code that does what a game function does is fine.
  Addresses, sizes and descriptions of the game's code are fine.
- **Gameplay never changes.** Timing, physics and the game's logic stay exactly as the original. Graphics and
  options may change how it looks, never how it plays.
- **The checks must pass.** A change that shouldn't alter the game must leave `tools/route1.sh`'s hashes, call
  counts and frames as they were (compare with `tools/route_cmp.py`); say so in the pull request.
- **Settings belong in the game's own menus.** A new option is a row in the game's Options, drawn in its style,
  never only an environment variable, a launcher or a file to edit. The original look stays one setting away.
- **Every controller works.** Any controller drives the player, plugged in before or after the game starts, in
  any order, with the keyboard still working.
- **Every enhancement can be turned off,** and what's on by default has to hold the full frame rate on a
  Radeon 890M handheld and the Steam Deck.
- **Write it down.** `DESIGN.md` is the project's record: add what you found and what you changed, and mark
  claims you haven't checked as [guess] and checked ones as [verified], with the evidence.

## Where a change goes

The translator and the Windows stand-in (files, threads, windows, input, sound, DirectMusic) are AntiStatic's,
in `third_party/antistatic`, shared with other games: a fix there goes to that repository, and this one moves
its pin. Everything in `runtime/` (the Direct3D 8 bridge, the films, the game's hooks) is this game's.

## How to send a change

Open an issue first for anything big, so the approach can be agreed. Keep a pull request to one subject,
describe how you tested it, and attach frame captures for anything visual. Pull requests are read in full
before anything in them is built or run.
