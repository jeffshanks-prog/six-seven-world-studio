#!/usr/bin/env python3
"""Generate 6|7 World Studio's placeholder brand icons.

Writes the in-tree branding layout the build reads
(indra/newview/sw67branding/) and the 128 px skin logos the About floater
shows. The art is our own, drawn here from shapes and text, so it carries no
third-party trademark. Replace these files with final artwork when it
exists; keep the file names, which the build references.

Needs Pillow and an OpenType font (Inter by default; pass --font).
"""
import argparse
import os
from PIL import Image, ImageDraw, ImageFont

# Accent per channel, so test builds are recognisable at a glance.
CHANNELS = {
    "release": (0, 199, 190),
    "beta": (255, 159, 28),
    "project": (176, 140, 255),
    "test": (240, 228, 66),
}
SIZES = [16, 32, 64, 128, 256, 512, 1024]


def draw_logo(size, accent, font_path):
    scale = 4  # supersample for smooth edges
    s = size * scale
    img = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    radius = int(s * 0.22)
    d.rounded_rectangle([0, 0, s - 1, s - 1], radius=radius, fill=(22, 24, 29, 255))
    # Accent bar along the bottom.
    bar_h = max(scale, int(s * 0.09))
    d.rounded_rectangle([int(s * 0.18), s - int(s * 0.17), s - int(s * 0.18), s - int(s * 0.17) + bar_h],
                        radius=bar_h // 2, fill=accent + (255,))
    text = "6|7" if size >= 32 else "67"
    font = ImageFont.truetype(font_path, int(s * (0.46 if size >= 32 else 0.52)))
    bbox = d.textbbox((0, 0), text, font=font)
    w, h = bbox[2] - bbox[0], bbox[3] - bbox[1]
    d.text(((s - w) / 2 - bbox[0], (s * 0.44) - h / 2 - bbox[1]), text, font=font, fill=(236, 239, 244, 255))
    return img.resize((size, size), Image.LANCZOS)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--font", default="/usr/share/fonts/opentype/inter/Inter-ExtraBold.otf")
    ap.add_argument("--root", default=os.path.join(os.path.dirname(__file__), "..", ".."))
    a = ap.parse_args()
    root = os.path.abspath(a.root)
    brand = os.path.join(root, "indra", "newview", "sw67branding")
    skin = os.path.join(root, "indra", "newview", "skins", "default", "textures", "alchemy")

    for channel, accent in CHANNELS.items():
        out = os.path.join(brand, "viewer", "icons", channel)
        os.makedirs(out, exist_ok=True)
        imgs = {sz: draw_logo(sz, accent, a.font) for sz in SIZES}
        for sz, im in imgs.items():
            im.save(os.path.join(out, f"alchemy_{sz}.png"))
        imgs[256].convert("RGB").save(os.path.join(out, "alchemy_256.BMP"))
        imgs[256].save(os.path.join(out, "alchemy.ico"), sizes=[(16, 16), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])
        imgs[1024].save(os.path.join(out, "alchemy.icns"))
        # Installer splash (NSIS shows a GIF).
        sp = os.path.join(brand, "installer", "splash", channel)
        os.makedirs(sp, exist_ok=True)
        splash = Image.new("RGB", (400, 250), (22, 24, 29))
        splash.paste(imgs[128], (136, 40), imgs[128])
        sd = ImageDraw.Draw(splash)
        f = ImageFont.truetype(a.font, 26)
        label = "6|7 World Studio" + ("" if channel == "release" else f"  {channel.capitalize()}")
        bb = sd.textbbox((0, 0), label, font=f)
        sd.text(((400 - (bb[2] - bb[0])) / 2, 190), label, font=f, fill=(236, 239, 244))
        splash.save(os.path.join(sp, "install_splash.gif"))
        # Skin logo for the About floater: alchemy{,beta,project,test}_128.png.
        suffix = "" if channel == "release" else channel
        imgs[128].save(os.path.join(skin, f"alchemy{suffix}_128.png"))

    icons = os.path.join(brand, "installer", "icons")
    os.makedirs(icons, exist_ok=True)
    base = draw_logo(256, CHANNELS["release"], a.font)
    for name in ("install_icon", "uninstall_icon"):
        base.save(os.path.join(icons, f"{name}.ico"), sizes=[(16, 16), (32, 32), (48, 48), (256, 256)])
        base.save(os.path.join(icons, f"{name}.BMP"))
    write_login_logos(root, a.font)


def draw_wordmark(w, h, font_path):
    """Logo plus "World Studio" wordmark, replacing the Second Life
    Eye-in-Hand on the login screen (TPV policy section 5)."""
    img = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    mark = draw_logo(h - 8, CHANNELS["release"], font_path)
    img.paste(mark, (0, 4), mark)
    d = ImageDraw.Draw(img)
    x = h + 2
    for line, frac, y in (("World", 0.36, 0.10), ("Studio", 0.36, 0.52)):
        f = ImageFont.truetype(font_path, int(h * frac))
        d.text((x, int(h * y)), line, font=f, fill=(236, 239, 244, 255))
    return img


def write_login_logos(root, font_path):
    win = os.path.join(root, "indra", "newview", "skins", "default", "textures", "windows")
    for name, size in (("login_sl_logo_horizontal", (160, 77)), ("login_sl_logo", (225, 94)),
                       ("login_sl_logo_small", (165, 73))):
        draw_wordmark(size[0], size[1], font_path).save(os.path.join(win, name + ".png"))
        alt = os.path.join(root, "indra", "newview", "skins", "alchemy", "textures", "windows", name + ".png")
        if os.path.exists(alt):
            draw_wordmark(size[0], size[1], font_path).save(alt)


if __name__ == "__main__":
    main()
