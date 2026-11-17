---
id: performance-assistant
title: Performance Assistant
features: performance_assistant
min_version: 26.4.0
---
# Performance Assistant

Open **Me > 6|7 Studio > Performance Assistant...**

- Every 3 seconds it reads the viewer's own measurements: frame rate, frame
  time, ping, packet loss, textures still loading, avatars nearby and drawn
  in full, and worn HUDs.
- Each explanation is labelled **[Measured]** or **[Estimate]** and names
  the settings involved. It never promises a frame rate.
- Presets: **Everyday**, **Shopping**, **Building**, **Photography**,
  **Event**. Choosing one shows each setting it would change, from and to,
  and why.
- **Apply** saves a restore point first. **Restore previous** puts your
  earlier values back, even after a restart.
- Presets change only eleven of your own graphics settings.
- **Graphics preferences** opens the standard graphics settings.

Troubleshooting low frame rate: open the assistant in the place where it is
slow, read the [Measured] findings first, then try the **Event** preset for
crowded venues. Texture memory is not measured yet.

Status: AVAILABLE in development builds; not yet tested in a released build.
