# Coffee Maker Dashboard – Color & Design Guide

Nov 8, 2026 · @Phillip

> **Live document.** Update it whenever the dashboard design changes, and check every new
> UI feature against it before it ships. How the guide maps onto Gaggia Guru (real machine
> states, files, deviations, items not built yet) is in
> [Gaggia Guru implementation notes](#gaggia-guru-implementation-notes) at the end. Record
> changes in the [change log](#change-log).

## Purpose

This guide is the single source of truth for color and visual decisions on the coffee maker web dashboard, which is viewed mainly on phones. Use it to update the existing design and to check every new feature before it ships. It merges two earlier palette suggestions and fixes the places where they failed accessibility contrast checks. All ratios below were calculated against WCAG 2.x.

## How the two suggestions compare

| Topic | Suggestion A (espresso/crema) | Suggestion B (Premium Espresso / Café Latte) | Decision |
| --- | --- | --- | --- |
| Dark background | #1A1310 dark brown | #121110 onyx | **#121110** – deeper, better for OLED and early-morning glare |
| Dark accent | #E0A06A latte | #D79C2B crema gold | **#D79C2B** – 7.8:1 with dark text, reads as "heat" |
| Light background | #FAF6F1 cream | #FAF6F0 milky froth | **#FAF6F0** – effectively identical |
| Light accent | #8B4A1C caramel | #6F4E37 / #8B6A55 bean brown | **#6F4E37** – 6.91:1 on cream; #8B6A55 is borderline (4.55:1) |
| Light cards | White | #E8D8C4 sand | **White cards**, sand for chips and dividers – sand on cream is only 1.3:1 and cards disappear |
| Status colors | Separate light and dark sets | One set for both modes | **Separate sets** – B's #E65F5F (3.16:1) and #5CB87E (2.26:1) fail on cream |
| Structure | Role-based tokens | 60-30-10 rule, 48px targets | **Both** – tokens for consistency, 60-30-10 for balance |

## Color tokens

Build everything from these tokens. Never use a raw hex value in a component.

| Token | Light | Dark | Use |
| --- | --- | --- | --- |
| bg | #FAF6F0 | #121110 | Page canvas (the 60%) |
| surface | #FFFFFF | #2B1B14 | Cards, status tiles (the 30%) |
| surface-raised | #E8D8C4 | #3A2E26 | Bottom sheets, chips, selected rows, input fills |
| border | #D6C4AE | #3A2E26 | Card outlines and dividers (decorative only) |
| text | #2C2B20 | #F3E7D6 | Headings, body, readings |
| text-secondary | #6B5546 | #BFAE9F | Labels, units, timestamps |
| accent | #6F4E37 | #D79C2B | Primary buttons, slider fill, heating ring, active tab (the 10%) |
| on-accent | #FFFFFF | #121110 | Text and icons on accent |
| accent-highlight | – | #E1C78F | Large hero numbers in dark mode (temperature, timer) |
| success | #4F7942 | #5CB87E | Brew complete, ready |
| warning | #A8521F | #F0B35A | Descale soon, low water |
| error | #B23A2E | #E65F5F | Reservoir empty, hopper jammed, fault |
| chart-temp | = accent | = accent | Graph: boiler temperature line, target and axis labels *(added 2026-11-08)* |
| chart-pressure | #2F6690 | #7FB3D5 | Graph: brew pressure line, target and axis labels *(added 2026-11-08)* |

## Applying 60-30-10

- **60% background:** the page canvas. Keep it empty and calm.
- **30% surfaces:** cards and tiles that hold controls and readings.
- **10% accent:** only things you can tap or things that are actively changing (heating ring, brew progress). If more than one primary button per screen uses accent, demote the others to outline buttons.
- Status colors sit outside this budget and appear only when a state is true.

## Status colors

- Reserve success, warning and error for real machine states. Never use them for decoration or branding.
- "Brewing" or "heating" is not a status color. It uses the accent, because it is the active state.
- Always pair a status color with an icon and a text label. Red and green are hard to tell apart for colorblind users and in harsh kitchen light.
- Critical alerts (reservoir empty, hopper jammed) use a full-width banner at the top of the screen with the error color, not just a colored dot.

## Verified contrast

WCAG AA needs 4.5:1 for body text and 3:1 for large text and UI controls.

| Pair | Ratio | Result |
| --- | --- | --- |
| Light text on bg | 13.24 | Pass |
| Light text-secondary on bg | 6.47 | Pass |
| Light text-secondary on surface-raised | 5.00 | Pass |
| Light accent on bg | 6.91 | Pass |
| White on light accent | 7.44 | Pass |
| Light success / warning / error on bg | 4.70 / 5.01 / 5.51 | Pass |
| Dark text on bg / surface / raised | 15.46 / 13.56 / 10.77 | Pass |
| Dark text-secondary on bg / surface | 8.78 / 7.70 | Pass |
| Dark accent on bg / surface | 7.80 / 6.84 | Pass |
| On-accent (#121110) on dark accent | 7.80 | Pass |
| Accent-highlight on dark bg | 11.47 | Pass |
| Dark success / error on bg | 7.74 / 5.54 | Pass |
| Dark warning on surface | 8.90 | Pass |
| Light chart-pressure on surface / bg | 6.13 / 5.70 | Pass *(added 2026-11-08)* |
| Dark chart-pressure on surface / bg | 7.33 / 8.36 | Pass *(added 2026-11-08)* |
| Light success / warning / error on surface (white) | 5.07 / 5.40 / 5.94 | Pass *(added 2026-11-08)* |
| Dark success / error on surface | 6.78 / 4.86 | Pass *(added 2026-11-08)* |
| Light surface-raised vs bg | 1.30 | Too low to separate on its own – needs a border |
| Dark surface vs bg | 1.14 | Too low to separate on its own – needs a border |

Because cards and backgrounds are so close in value, every card gets a 1px border using the border token. Input fields and toggles need an outline that reaches 3:1, so use text-secondary for their borders rather than the border token.

## Component rules

- **Touch targets:** at least 48×48px, with 8px spacing between targets. Slider thumbs at least 28px visible, with a 48px hit area.
- **Primary button:** accent fill, on-accent text, full width on phones, one per screen.
- **Secondary button:** transparent fill, 1px accent border, accent text.
- **Cards:** surface fill, 1px border, 12–16px corner radius, 16px padding.
- **Sliders and gauges:** filled track and heating ring use accent; empty track uses surface-raised. Show the numeric value in text next to the control.
- **Hero readings:** temperature and brew timer use text in light mode and accent-highlight in dark mode, at 32px or larger.
- **Focus state:** 2px accent outline offset by 2px, visible in both modes.
- **Disabled:** 40% opacity on the whole control. Do not invent a separate grey.

## Typography

Use Inter for all text, with tabular figures (font-variant-numeric: tabular-nums) so readings and times don't jump as they change. There are no brand fonts.

| Role | Size | Weight |
| --- | --- | --- |
| Hero reading (status, temperature, timer) | 30–32px | 600 |
| Card value | 22px | 600 |
| Screen title | 20px | 600 |
| Primary button | 17px | 600 |
| Body and secondary button | 14–15px | 400–600 |
| Labels, units, timestamps | 13px | 400–500 |
| Tab bar labels (minimum) | 12px | 500–600 |

## Standby mode

Standby means the heater is off and the machine is waiting. The screen should look calm and dimmed: nothing glows except the one action that wakes it. [Mockups: Coffee Maker Standby Mode](https://claude.ai/artifact/Fbndu7nwTBXf337eS5oWuq)

- **When it shows:** after the idle timer turns the heater off, after the user taps the power button, or between scheduled brews.
- **Color:** no accent on status elements. The ring uses surface-raised, the moon icon and level bars use text-secondary. Accent appears only on the Wake and heat button, the Edit schedule outline button and the active tab.
- **Screen order:** header (machine name, connection, power button) → status ring with "Standby" and "Heater off" → Water and Beans cards → Next scheduled brew card → Wake and heat (primary) → Edit schedule (secondary) → tab bar.
- **Copy:** status word "Standby", subtitle "Heater off", primary action "Wake and heat". Next brew card says "Wakes automatically to heat" when a schedule exists.
- **Alerts in standby:** a warning banner sits directly under the header, with a warning-color border, icon and title, and body text in the text color. The affected card repeats the state with an icon and warning-color value. Drop the secondary button to make room.
- **Leaving standby:** on Wake and heat, the ring fills with accent as the machine heats, and the subtitle changes to the heating progress. Standby colors never mix with heating colors on the same screen.

## Theme switching

Default to the device setting with prefers-color-scheme, and offer a manual Light / Dark / System toggle in settings. Store the choice per device. Define the tokens once as CSS custom properties:

```css
:root {
  --bg: #FAF6F0; --surface: #FFFFFF; --surface-raised: #E8D8C4;
  --border: #D6C4AE; --text: #2C2B20; --text-secondary: #6B5546;
  --accent: #6F4E37; --on-accent: #FFFFFF;
  --success: #4F7942; --warning: #A8521F; --error: #B23A2E;
}
@media (prefers-color-scheme: dark) {
  :root:not([data-theme="light"]) {
    --bg: #121110; --surface: #2B1B14; --surface-raised: #3A2E26;
    --border: #3A2E26; --text: #F3E7D6; --text-secondary: #BFAE9F;
    --accent: #D79C2B; --on-accent: #121110; --accent-highlight: #E1C78F;
    --success: #5CB87E; --warning: #F0B35A; --error: #E65F5F;
  }
}
```

Repeat the dark block under :root\[data-theme="dark"\] so the manual toggle works. Set meta theme-color to the bg value so the phone's browser bar matches.

## Updating the existing design

1. Replace every hard-coded color with the tokens above.
2. Swap any sand-colored cards on the light background for white cards with a border.
3. Switch light-mode status colors to the darker light set.
4. Make sure brewing and heating states use accent, not a status color.
5. Add icons and labels to every status indicator.
6. Check all tap targets against the 48px minimum.
7. Test both themes on a real phone in a dim room and in bright light.

## Checklist for new features

- Uses only tokens, no raw hex values.
- Adds at most one accent-filled primary action per screen.
- Any new text or control pair is checked for 4.5:1 (text) or 3:1 (controls) in both themes.
- Status colors appear only for real machine states, with icon and label.
- Tested in light, dark and system modes.
- New tokens, if truly needed, are added to this guide first with their contrast ratios.

## Open questions

- Are there other machine states (for example cleaning cycle, descaling, offline) that need their own visual treatment?
- How long should the machine sit idle before it drops into standby?

---

## Gaggia Guru implementation notes

The guide above was written for a generic coffee maker. This section records how it applies
to this machine, where it is implemented, and the few places where the dashboard deviates
on purpose. Keep it current.

### Where it lives

| File | What |
| --- | --- |
| `data/style.css` | All tokens (light, dark via `prefers-color-scheme`, dark via `[data-theme="dark"]`) and every component style. Components use only `var(--token)`. |
| `data/*.html` | Each page's `<head>` has two `theme-color` metas (light / dark bg) and a one-line script that applies the stored theme before first paint. |
| `data/app.js` | Theme toggle (`applyTheme`, localStorage key `theme`: `light` / `dark`, absent = System), keeps `theme-color` equal to `--bg`, alert banner, state chip icons, graph colors from `--chart-*`. |
| `data/fonts/Inter.woff2` | Bundled Inter (see Fonts). The dashboard must work offline (setup hotspot), so no font CDN. |
| `src/web_server.cpp` (`SETUP_HTML`) | Built-in Wi-Fi setup page, served even when LittleFS is empty: inline copy of the bg / text / text-secondary / accent / on-accent tokens, system font. |

The plain `/update` fallback page in `web_server.cpp` is deliberately unstyled (emergency path).

### Machine states → visual treatment

| Firmware state / signal | Dashboard treatment |
| --- | --- |
| `IDLE` | State chip neutral (surface-raised, text-secondary). |
| `BREWING`, `STEAM`, `HOT_WATER` | State chip accent fill (active state, not a status color). |
| Boiler at temperature (coffee or steam light on) | Success check icon + "Ready · 93.0°" under the boiler reading. The number itself stays the hero color. |
| `INHIBITED` (shown as "Paused") | Warning banner under the header (icon, title "Paused", body text) + warning-outline chip with icon. |
| Pump run-time limit, Coffee switch already on | Warning banner with icon, title and instruction. |
| `FAULT` | Error banner under the header (icon, "Machine fault", fault text) + error-outline chip with icon. |
| Connection (top bar) | Dot + text label: online = success, setup hotspot = warning, offline = error. |

### Not built yet (keep the guide text; apply when the feature exists)

- Water reservoir / low water, beans / hopper, descale reminders: no sensors or firmware for these yet.
- Scheduled brews and the **standby mode** screen: the firmware has no heater-off standby state; `IDLE` keeps the boiler at temperature. Build the standby screen from the guide when that state is added.
- Tab bar: the dashboard uses tiles on the home page and a back link on sub-pages. Use the guide's tab-bar rules if one is added.
- Sliders / heating ring: none yet (setpoints are number inputs).

### Deliberate deviations and clarifications

- **Input text is 16px**, not 14–15px: smaller text makes iOS Safari zoom the page on focus.
- **Tabular figures on readings only** (gauges, key/value values, GPIO numbers, pin states, chip counts), not on all text: Inter's `tnum` also gives hyphens a fixed width, which spaces out prose like "over-temperature".
- **Hero readings:** boiler temperature gets a full-width row and pressure / shot time share the next row, so all three stay at 32px on a 360px-wide phone.
- **Card section titles** are 15px / 600 in the text color (body size, top of the weight range).
- **Destructive actions** (Reset to defaults, Forget Wi-Fi, Reboot) are secondary buttons. They use a two-tap confirm; only the armed state ("Tap again to confirm") turns the outline and text to the error color, because at that moment it is a real warning.
- **IO Status pin types** (Digital / Analog / Comms) are not color-coded: the type is written as text, so no extra category tokens are needed. Pin cautions (for example strapping pins) use a warning icon in the text color, not the warning color, since they are not machine states.
- **Secondary text on surface-raised** stays above 4.5:1 (5.00 light, 6.11 dark), so chips and the connection pill use surface-raised fills.
- **Setup page fonts:** the built-in setup page uses the system font, since it must work when the filesystem (and Inter) is missing.

### Fonts

`data/fonts/Inter.woff2` is Inter 4.1 (SIL Open Font License, `data/fonts/Inter-LICENSE.txt`),
subset to Latin + the symbols the UI uses, weight axis 400–600, optical size fixed at text
(about 25 KB). To rebuild it from `InterVariable.ttf` in the
[Inter release](https://github.com/rsms/inter/releases) (needs `pip install fonttools brotli`):

```sh
python3 -m fontTools.varLib.instancer InterVariable.ttf wght=400:600 opsz=14 -o inter_inst.ttf
python3 -m fontTools.subset inter_inst.ttf \
  --unicodes="U+0020-007E,U+00A0-00FF,U+2013-2014,U+2018-201D,U+2022,U+2026,U+2039-203A,U+2122,U+2190-2193,U+2212,U+2264-2265" \
  --layout-features='kern,liga,calt,tnum,case,ccmp,locl,mark,mkmk' \
  --flavor=woff2 --output-file=data/fonts/Inter.woff2
```

Add any new symbol the UI needs to `--unicodes`, or it falls back to the system font.

## Change log

| Date | Change |
| --- | --- |
| 2026-11-08 | Guide added to the project. Dashboard restyled to it: tokens, Light / Dark / System toggle (Settings → Appearance), Inter bundled, header on bg, alert banner under the header, icons + labels on all status indicators, 48px targets, graph tokens `chart-temp` / `chart-pressure` added. |
