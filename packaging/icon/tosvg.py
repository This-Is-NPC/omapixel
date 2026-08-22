import json, sys

UNIT = 8          # 16 px sprite * 8 = the family's 128 viewBox
BADGE = "#101010" # what omacalc/omawrite sit on
RX = 24

def icon_svg(path):
    d = json.load(open(path))
    w, h = d["size"]["w"], d["size"]["h"]
    colour = {p["slot"]: p["colour"] for p in d["palette"]}
    rows = d["clips"][0]["frames"][0]

    out = [f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {w*UNIT} {h*UNIT}">',
           f'  <rect width="{w*UNIT}" height="{h*UNIT}" rx="{RX}" fill="{BADGE}"/>']
    for y, row in enumerate(rows):
        x = 0
        while x < w:
            slot = row[x]
            if slot == ".":
                x += 1
                continue
            # Merge the run: pixel art is mostly flat, and one rect per pixel
            # makes a file three times the size for the same picture.
            run = x
            while run < w and row[run] == slot:
                run += 1
            out.append(f'  <rect x="{x*UNIT}" y="{y*UNIT}" '
                       f'width="{(run-x)*UNIT}" height="{UNIT}" fill="{colour[slot]}"/>')
            x = run
    out.append('</svg>')
    return "\n".join(out) + "\n"

for name in sys.argv[1:]:
    svg = icon_svg(f"{name}.json")
    open(f"{name}.svg", "w").write(svg)
    print(f"{name}.svg  {len(svg)} bytes, {svg.count('<rect')} rects")
