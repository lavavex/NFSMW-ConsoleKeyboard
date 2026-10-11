# NFSMW Console Keyboard

Need for Speed: Most Wanted (2005) on PC asks you to *type* your alias. This
script brings back the console version's on-screen keyboard instead, so you can
name your profile with a controller, no keyboard needed.

The keyboard was never removed from the PC game: its screen (`Keyboard.fng`)
and code are still in `speed.exe`, just never opened. This mod opens it whenever
the game starts text entry, then hands the name you picked back to the game as
if you had typed it.

Handy for couch/Big Picture setups, Steam Deck, and anything else without a
keyboard.

## Requirements

- NFS Most Wanted **v1.3** `speed.exe` (6,029,312 bytes)
- An ASI loader (e.g. Ultimate ASI Loader as `dinput8.dll`, which the Widescreen
  Fix and most mod packs already include)
- A controller the game can use for menus (e.g. via
  [NFS-XtendedInput](https://github.com/xan1242/NFS-XtendedInput))

## Install

Copy `scripts/NFSMWConsoleKeyboard.asi` into the game's `scripts` folder.

## Use

When the game asks for a name, the on-screen keyboard appears. Pick letters with
the d-pad/stick and the accept button, then confirm with Start (or the keyboard's
"done" key). Back cancels, as typing Esc would.

Each run writes `scripts/NFSMWConsoleKeyboard.log`, a short record of what the
mod did; attach it if you report a problem.

## Compatibility

Tested with the Xbox 360 Stuff Pack 4.2, Widescreen Fix, XtendedInput, HUD
Adapter, HD Reflections, Front-End Shadows and HD Content Support, on Windows
through Wine and on PS5 (prospero-win).

Tested: creating an alias at first boot. Other text prompts go through the same
path but haven't been tried yet. If the mod finds unexpected code at any of the
addresses it hooks (a different exe, or another mod hooking the same place) it
hooks nothing and says so in the log.

## How it works

| Address    | What                                   | Mod's use                                              |
|------------|----------------------------------------|--------------------------------------------------------|
| `0x5865e0` | `MenuScreen::StartTextEntry`           | hooked: opens `Keyboard.fng` instead of the PC field   |
| `0x5257f0` | push FE package                        | called to open the keyboard                            |
| `0x91cf90` | FEDatabase (`+0x318..+0x32c`)          | keyboard's accept/cancel messages, max length, buffer  |
| `0x516c50` | FE message queue                       | hooked: catches the keyboard's accept/cancel           |
| `0x59b2c0` | MenuScreen notification (screen tick)  | hooked: restarts the PC field with the name, posts Enter |
| `0x599100` / `0x5991b0` | text entry commit / cancel | hooked: shows the PC text box again as the game takes over |
| `0x514c70` / `0x514cc0` | FE object invisible / visible | hides the PC text box while the keyboard is up    |

## Building

    make          # needs i686-w64-mingw32-gcc
    make dist     # NFSMWConsoleKeyboard.zip

## License

MIT
