---
id: product-photography
title: Product Photography (manual)
features: product_photography
min_version: 26.4.0
---
# Product Photography

Open **Me > 6|7 Studio > Product Photography...**

1. Select the object you want to photograph in-world.
2. Choose angles (seven presets) and export sizes (six presets).
3. Set a file-name pattern, for example `{project}_{angle}_{index:3}`, and an
   output folder. The window lists every file before you start.
4. Click **Start batch**. Your camera moves around the object and takes each
   shot. **Stop** keeps what was written. Your camera always goes back where
   it was.

Originals are saved as PNG in `originals/` and never overwritten. Each size
goes to `exports/<size>/`, and a manifest records how each file was made.
If an RLV object restricts your camera, the batch is refused and the
restriction is named.

PLANNED: watermarks, backdrop and lighting switching, and AI product
photography.

Status: AVAILABLE in development builds; not yet tested in a released build.
