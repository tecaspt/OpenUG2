# OpenUG2 on iPhone and iPad

OpenUG2 is a clean-room engine reimplementation. It ships no game data. You must own the
original PC game and copy its data folder onto the device yourself.

## Install and add your game data

1. Install the build from TestFlight.
2. On a Mac or PC with the original game installed, find the game folder (the one that
   contains `TRACKS`, `GLOBAL`, `CARS` and so on). Keep the exact upper/lower-case names.
3. Copy that folder into the app's storage as `NFSU2`:
   - Files app: **On My iPad** (or **On My iPhone**) → **OpenUG2**, then paste the folder and rename it `NFSU2`.
   - Mac Finder: select the device, open the **Files** tab, drag the folder onto **OpenUG2**.
4. Launch the app. If the data is missing or misnamed it shows a message explaining what to
   fix and a **Check again** button. The app also looks in `OpenUG2` directly and in any
   one subfolder with a different name.

Switching to the Files app while copying is safe; the game pauses in the background.

## Controls

Pause with **Start** on a controller, a **two-finger tap** on the screen, or **Esc** on a keyboard.
Controllers (MFi, Xbox, DualSense, and others iOS supports) can be connected at any time.
Change bindings in **Options → Controls**; **Reset Keys To Default** restores the layout below.

Magic Keyboard has no Esc key. Use controller Start, a two-finger tap, or remap Caps Lock to
Escape in **Settings → General → Keyboard → Hardware Keyboard → Modifier Keys**.

### Default controller layout

The left stick steers. The right stick accelerates (up) and brakes (down). Start pauses.

| Action | Xbox | PlayStation |
| --- | --- | --- |
| Accelerate | A | Cross |
| Brake / reverse | X | Square |
| Look back | B | Circle |
| Change camera | Y | Triangle |
| Nitrous | Left bumper | L1 |
| Handbrake | Right bumper | R1 |
| Shift down | Left trigger | L2 |
| Shift up | Right trigger | R2 |
| Reset car | View / Back | Select / Share |
| Hydraulics pressure | Left stick click | L3 |
| Hydraulics bounce | Right stick click | R3 |
| Enter race | D-pad up | D-pad up |
| World map | D-pad down | D-pad down |
| Steer left / right | D-pad left / right | D-pad left / right |

The layout follows the PlayStation 2 release.

## Building (Codemagic)

The Xcode project is generated from `ios/project.yml` by XcodeGen; it is not committed.
`codemagic.yaml` at the repository root defines two workflows:

- `ios-testflight`: starts automatically on every push to the `ios/main` branch. It builds
  SDL2 and libpng for iOS, generates the icon and project, signs with the `ios_signing`
  variable group (`CERTIFICATE_PRIVATE_KEY`) and the team's App Store Connect integration,
  numbers the build above the latest TestFlight build, and uploads the IPA. Builds appear for
  internal testers once Apple finishes processing; nothing is submitted for external review.
- `ios-compile`: manual only. Same dependency and project steps, then an unsigned device
  build. Use it for fast compile checks.

Xcode is pinned to 26.6, not `latest`. Xcode 27 builds against the iOS 27 SDK, which requires
the UIScene lifecycle, and the SDL2 used here does not support it, so the app would not
launch. Move the pin only after SDL gains UIScene support.

Local sanity check of the packaging files (Linux or macOS, needs PyYAML): `bash ios/tools/check_ios.sh`.
