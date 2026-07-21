---
name: screenshot-macos
description: Screenshot or visually inspect a running app's window on macOS (Darwin). ALWAYS use this whenever you need to see a native GUI on macOS 〜 verify a rendered frame, confirm a visual change, capture any on-screen app. Runs .claude/tools/screenshot-macos (screencapture + CGWindowList). NEVER osascript / AppleScript / System Events.
---

## Permissions gate

Capture depends on Screen Recording being granted to the terminal. If a shot fails on permissions 〜 a black or empty PNG, or screencapture erroring 〜 STOP. Do not switch mechanisms, retry in a loop, or try osascript. Tell the user to grant Screen Recording to their terminal in System Settings → Privacy & Security → Screen Recording, then WAIT for them to confirm before trying again.

## The tool

`.claude/tools/screenshot-macos <app-owner-substring> <outfile.png>`

- Matches by process OWNER name, case-insensitive substring. Largest window for that owner wins. Match by the process name, which may differ from the window title.
- Exit 2 = no matching on-screen window yet. The app has no window up. Wait and retry, or ask the user.
- Compiles a small CGWindowList helper on first use (gitignored). Needs Xcode command-line tools.

## Flow

1. Ensure the target app is running with a visible window.
2. Run the tool from a plain shell, giving the owner substring and an output path. Name the file contextually 〜 `/tmp/screenshot_{contextualname}.png` 〜 so successive shots don't clobber each other.
3. Read the PNG.

```bash
for i in $(seq 1 20); do
  .claude/tools/screenshot-macos <owner> /tmp/screenshot_<contextualname>.png && break
  sleep 1
done
```

Then Read the PNG.

## Whole-screen fallback

If you don't need the window cropped, no ID lookup is required:

```bash
screencapture -x /tmp/screenshot_<contextualname>.png
```

## Hard rules

- NEVER osascript / System Events.
- Run the tool from a plain shell. If the app needs a sandboxed environment (e.g. `nix develop`) to launch, launch it there but run the screenshot tool from the outer shell.
- No matching window means the app has no window yet 〜 wait or ask the user. Never switch capture mechanisms to compensate.
- Permission failure means stop and ask, per the gate above.
