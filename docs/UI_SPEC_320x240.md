# UI specification for T-Deck Plus, 320 x 240

## Display target

- Resolution: 320 x 240
- Aspect ratio: 4:3
- Orientation: landscape
- UI style: retro rugged terminal
- Touch mode: finger-friendly, not stylus-only

## Layout rules

Use one main task per screen. Do not use dense dashboards unless they are diagnostic-only.

Recommended structure:

```text
320 x 240

0,0      320 x 24    top bar
0,24     320 x 164   content area
0,188    320 x 52    action/nav bar
```

## Sizes

| Element | Recommended size |
|---|---:|
| Top bar | 24 px |
| Bottom nav/action bar | 48 to 52 px |
| Main button height | 44 to 52 px |
| List row height | 34 to 42 px |
| Icon size | 20 to 28 px |
| Body font | 16 px |
| Small font | 12 to 14 px |
| Header font | 18 to 20 px |
| Border | 1 to 2 px |
| Scrollbar | 8 to 10 px |

## Typography

Use a custom bundled bitmap-like font or an open font. Do not use Microsoft fonts or copied system assets.

Suggested sizes:

- Header: 18 px
- Body: 16 px
- Secondary: 13 px
- Status text: 12 px

## Color direction

Allowed style direction:

- deep blue title bars
- warm gray panels
- black text
- green radio/battery accents
- yellow warning accents
- red destructive actions
- blue selected rows

Avoid exact Windows CE color values and icons.

## Navigation model

Every screen should support:

- touch tap
- keyboard shortcuts
- trackball focus
- enter/select
- back key

## Bottom actions

Most screens should expose no more than four large actions.

Examples:

```text
Messages: New, Reply, Delete, More
Nodes: Refresh, Ping, Details, Map
Channels: Join, Leave, Details, New
Map: Center, Layers, Zoom In, Zoom Out
Settings: Back, Select, Apply
```
