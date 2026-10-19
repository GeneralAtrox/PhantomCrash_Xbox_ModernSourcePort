"""Authoring help for data/ui/sprite_text.json's display lettering.

Entries name their sprite, text and class; the class (sprite_text.json's
"classes") holds the font, colours, shadow and erase settings. This tool
fills what depends on each sprite's art and renders a check sheet:

  measure  gives every entry with a font and no "region" the box of its
           lettering's ink (the class's colour, within the entry's or class's
           "within" box) and an "erase_region" round its ink and shadow.
  adopt    has every entry without a class that repeats all of a class's
           members (in either table) name that class instead.
  preview  writes a sheet: each sprite's atlas art beside its replacement.
  add      merges a batch file ({"classes": {...}, "replacements": [...]})
           into the table: fonts and classes by name, entries by their source; its
           "shape_classes" and "shapes" go to sprite_shapes.json alike.

    python tools/sprite_lettering.py measure [--match TEXT]
    python tools/sprite_lettering.py preview OUT.png [--match TEXT]
    python tools/sprite_lettering.py add BATCH.json
    python tools/sprite_lettering.py adopt

--match limits either to entries that contain TEXT (an image path, a text,
a class name). A class's
"measure" member tunes the measuring ("grow": the erase box's margin left,
top, right, bottom; "tolerance": how far ink may stray from the colour); the
port ignores it, as it does "within". Needs Pillow and the extraction under
artifacts/europe/assets.
"""

from __future__ import annotations

import argparse
import copy
import json
import sys
from pathlib import Path

from PIL import Image

from sprite_ink import Ink
from sprite_preview import Atlas, Preview

ROOT = Path(__file__).resolve().parents[1]
TABLE = ROOT / "data" / "ui" / "sprite_text.json"
SHAPES = ROOT / "data" / "ui" / "sprite_shapes.json"



class Table:
    """sprite_text.json, kept in its one-entry-per-line layout."""

    def __init__(self, path: Path = TABLE, items: str = "replacements") -> None:
        self.path = path
        self.items = items
        raw = path.read_bytes()
        self.newline = "\r\n" if b"\r\n" in raw else "\n"
        text = raw.decode("utf-8")
        self.doc = json.loads(text)
        # Hand-laid entries keep their text while unchanged.
        self.kept = {canonical(e): t for e, t in zip(self.doc.get(items, []), entry_texts(text, items))}

    def resolved(self, entry: dict) -> dict:
        """The entry with its classes applied, as the port reads it."""
        names = entry.get("class")
        if names is None:
            return entry
        out: dict = {}
        for name in names if isinstance(names, list) else [names]:
            merge(out, self.resolved(self.doc.get("classes", {})[name]))
        merge(out, entry)
        out.pop("class", None)
        return out

    def entries(self, match: str | None):
        for entry in self.doc["replacements"]:
            if "font" not in self.resolved(entry):
                continue
            if match and match not in json.dumps(entry):
                continue
            yield entry

    def save(self) -> None:
        # Lists one item a line, tables of objects one member a line, the
        # rest inline: the layout the file is kept in.
        def inline(value) -> str:
            return json.dumps(value, ensure_ascii=False)

        lines = ["{"]
        keys = list(self.doc)
        for k, key in enumerate(keys):
            tail = "," if k < len(keys) - 1 else ""
            value = self.doc[key]
            if key == "shapes":
                body = ",\n".join(self.kept.get(canonical(v)) or shape_lines(v) for v in value)
                lines += [f' "{key}": [', body, f" ]{tail}"]
            elif isinstance(value, list):
                body = ",\n".join(f"  {inline(v)}" for v in value)
                lines += [f' "{key}": [', body, f" ]{tail}"]
            elif isinstance(value, dict) and all(isinstance(v, dict) for v in value.values()):
                body = ",\n".join(f"  {inline(n)}: {inline(v)}" for n, v in value.items())
                lines += [f' "{key}": {{', body, f" }}{tail}"]
            else:
                lines.append(f' "{key}": {inline(value)}{tail}')
        lines.append("}")
        text = "\n".join(lines) + "\n"
        json.loads(text)
        self.path.write_bytes(text.replace("\n", self.newline).encode("utf-8"))


def canonical(value) -> str:
    return json.dumps(value, sort_keys=True)


def entry_texts(text: str, items: str) -> list[str]:
    """The text of each object in the document's `items` list, as written."""
    start = text.find(f'"{items}": [')
    if start < 0:
        return []
    out, depth, begin, quoted, i = [], 0, 0, False, text.index("[", start) + 1
    while i < len(text):
        c = text[i]
        if quoted:
            i += 2 if c == "\\" else 1
            quoted = quoted and c != '"'
            continue
        if c == '"':
            quoted = True
        elif c in "{[":
            if depth == 0:
                begin = text.rfind("\n", 0, i) + 1
            depth += 1
        elif c in "}]":
            if depth == 0:
                break
            depth -= 1
            if depth == 0:
                out.append(text[begin : i + 1].replace("\r", ""))
        i += 1
    return out


def shape_lines(entry: dict) -> str:
    """A sprite_shapes.json entry: a member a line, each outline's too."""
    rows = ["  {"]
    members = list(entry.items())
    for m, (name, value) in enumerate(members):
        comma = "," if m < len(members) - 1 else ""
        if name == "outlines":
            rows.append('   "outlines": [')
            for o, outline in enumerate(value):
                rows.append("    {")
                fields = list(outline.items())
                for f, (field, v) in enumerate(fields):
                    end = "," if f < len(fields) - 1 else ""
                    rows.append(f"     {json.dumps(field)}: {json.dumps(v, ensure_ascii=False)}{end}")
                rows.append("    }" + ("," if o < len(value) - 1 else ""))
            rows.append("   ]" + comma)
        else:
            rows.append(f"   {json.dumps(name)}: {json.dumps(value, ensure_ascii=False)}{comma}")
    rows.append("  }")
    return "\n".join(rows)


def merge(base: dict, overlay: dict) -> None:
    for key, value in overlay.items():
        if isinstance(value, dict) and isinstance(base.get(key), dict):
            merge(base[key], value)
        else:
            base[key] = copy.deepcopy(value)


def key(entry: dict) -> tuple:
    s = entry["source"]
    return s["image"], tuple(s["rect"]), s.get("id", "")


def measure(table: Table, match: str | None) -> None:
    """Regions for entries without one. Several on one sprite with one colour
    take its labels in order; a lone one takes all its lettering."""
    atlas = Atlas()
    groups: dict[tuple, list[dict]] = {}
    for entry in table.entries(match):
        style = table.resolved(entry)
        if "region" not in style:
            within = tuple(style.get("within", ()))
            groups.setdefault((*key(entry), style.get("colour", "#ffffff"), within), []).append(entry)
    done = missed = 0
    for entries in groups.values():
        style = table.resolved(entries[0])
        ink = Ink(atlas.sprite(entries[0]["source"]["image"], entries[0]["source"]["rect"]), style)
        boxes = [ink.region()[0]] if len(entries) == 1 and ink.region() else ink.labels()
        if len(boxes) != len(entries):
            texts = [e.get("text") for e in entries]
            print(f"{entries[0]['source']}: {len(boxes)} labels for {texts}: {boxes}", file=sys.stderr)
            missed += len(entries)
            continue
        for entry, box in zip(entries, boxes):
            entry["region"] = box
            erase = ink.erase_box(box)
            if table.resolved(entry).get("erase") and erase != box:
                entry["erase_region"] = erase
            done += 1
    table.save()
    print(f"measured {done}, not {missed}")
    trace(Table(SHAPES, "shapes"), atlas, match)


def trace(shapes: Table, atlas: Atlas, match: str | None) -> None:
    """Outlines for shapes with a "trace" ({colour, tolerance, min_line,
    within}) and none yet: each ruled line of that colour as a box."""
    traced = 0
    for entry in shapes.doc["shapes"]:
        hints = shapes.resolved(entry).get("trace")
        if hints is None or "outlines" in entry or (match and match not in json.dumps(entry)):
            continue
        style = {**hints, "measure": {k: hints[k] for k in ("tolerance", "min_line") if k in hints}}
        ink = Ink(atlas.sprite(entry["image"], entry["rect"]), style)
        tint = hints.get("colour", "#ffffff")
        entry["outlines"] = [
            {"box": box, "radius": 0, "stops": [[0, tint, 1], [0, tint, 0]]} for box in ink.rules
        ]
        traced += 1
    shapes.save()
    print(f"traced {traced} shapes")


def with_classes(doc: dict, items: str, classes: dict) -> dict:
    """`doc` with `classes` merged into its "classes", placed before `items`."""
    out: dict = {}
    for name, value in doc.items():
        if name == items:
            out["classes"] = {**doc.get("classes", {}), **classes}
        if name != "classes":
            out[name] = value
    if not out["classes"]:
        del out["classes"]
    return out


def merge_entries(entries: list, batch: list, key_of, same) -> tuple[int, int]:
    """Batch entries replace a `same` entry, else follow their sprite's last."""
    added = replaced = 0
    placed: list[int] = []  # ids of entries this batch put in: a batch's
    # repeated labels (several "t" on one table) each keep their own
    for entry in batch:
        k = key_of(entry)
        match = [
            i for i, e in enumerate(entries) if key_of(e) == k and same(e, entry) and id(e) not in placed
        ]
        if match:
            entries[match[0]] = entry
            replaced += 1
        else:
            after = [i for i, e in enumerate(entries) if key_of(e) == k]
            entries.insert(after[-1] + 1, entry) if after else entries.append(entry)
            added += 1
        placed.append(id(entry))
    return added, replaced


def add(table: Table, batch_path: Path) -> None:
    """Text classes and entries into sprite_text.json; "shape_classes" and
    "shapes" into sprite_shapes.json (a shape replaces its sprite's)."""
    batch = json.loads(batch_path.read_text(encoding="utf-8"))
    table.doc = with_classes(table.doc, "replacements", batch.get("classes", {}))
    table.doc["fonts"].update(batch.get("fonts", {}))
    counts = merge_entries(
        table.doc["replacements"], batch.get("replacements", []), key,
        lambda a, b: a.get("text") == b.get("text"),
    )
    table.save()
    print(f"text: added {counts[0]}, replaced {counts[1]}")
    if "shapes" in batch or "shape_classes" in batch:
        shapes = Table(SHAPES, "shapes")
        shapes.doc = with_classes(shapes.doc, "shapes", batch.get("shape_classes", {}))
        counts = merge_entries(
            shapes.doc["shapes"], batch.get("shapes", []),
            lambda e: (e["image"], tuple(e["rect"])), lambda a, b: True,
        )
        shapes.save()
        print(f"shapes: added {counts[0]}, replaced {counts[1]}")


def adopt(table: Table, items: str) -> int:
    """Entries with no class that hold every member of a class (the class
    with the most, if several) name it instead of repeating them."""
    tool_only = ("within", "measure")
    classes = {
        name: {k: v for k, v in table.resolved(value).items() if k not in tool_only}
        for name, value in table.doc.get("classes", {}).items()
    }
    changed = 0
    for i, entry in enumerate(table.doc[items]):
        if "class" in entry:
            continue
        fits = [(len(m), n) for n, m in classes.items() if m and all(entry.get(k) == v for k, v in m.items())]
        if not fits:
            continue
        name = max(fits)[1]
        head = [k for k in ("source", "image", "rect", "text") if k in entry]
        rest = {k: v for k, v in entry.items() if k not in head and k not in classes[name]}
        table.doc[items][i] = {**{k: entry[k] for k in head}, "class": name, **rest}
        changed += 1
    table.save()
    return changed


def preview(table: Table, out: Path, match: str | None) -> None:
    atlas = Atlas()
    draw = Preview(table.doc.get("fonts", {}))
    shapes = Table(SHAPES, "shapes")
    shaped = {(e["image"], tuple(e["rect"])): shapes.resolved(e) for e in shapes.doc["shapes"]}
    groups: dict[tuple, list[dict]] = {}
    for entry in table.entries(match):
        style = table.resolved(entry)
        if "region" in style:
            groups.setdefault(key(entry), []).append(style)
    rows = []
    for k, styles in groups.items():
        shape = shaped.get(k[:2])
        if shape is not None:
            shape = {**shape, "outlines": [shapes.resolved(o) for o in shape.get("outlines", [])]}
        rows.append(draw.pair(atlas.sprite(k[0], list(k[1])), styles, shape))
    if not rows:
        raise SystemExit("nothing to preview")
    width = max(r.width for r in rows)
    sheet = Image.new("RGBA", (width, sum(r.height + 4 for r in rows)), (30, 30, 30, 255))
    y = 0
    for r in rows:
        sheet.alpha_composite(r, (0, y))
        y += r.height + 4
    sheet.save(out)
    print(f"{len(rows)} sprites -> {out}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("command", choices=["measure", "preview", "add", "adopt"])
    parser.add_argument("out", nargs="?")
    parser.add_argument("--match")
    args = parser.parse_args()
    table = Table()
    if args.command == "measure":
        measure(table, args.match)
    elif args.command == "adopt":
        print("text:", adopt(table, "replacements"), "shapes:", adopt(Table(SHAPES, "shapes"), "shapes"))
    elif args.command == "add":
        if not args.out:
            parser.error("add needs a batch file")
        add(table, Path(args.out))
    else:
        if not args.out:
            parser.error("preview needs an output path")
        preview(table, Path(args.out), args.match)


if __name__ == "__main__":
    main()
