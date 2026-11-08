"""Prepare a sleep screen for the X3 or X4 Pro from a selected source."""

import argparse
import json
from datetime import date
from io import BytesIO
from pathlib import Path
from textwrap import wrap
from urllib.request import Request, build_opener

from PIL import Image, ImageDraw, ImageFont, ImageOps

from browser import HttpsRedirect, https_url
from ticktick import HEIGHT, PANEL_SIZES, WIDTH, bmp_bytes, fit, render, select_tasks, write_snapshot

FIXTURE = Path(__file__).parent / "fixtures" / "ticktick.json"
MAX_FILE_BYTES = 8 * 1024 * 1024
MAX_PIXELS = 12_000_000
ONLINE_FORMATS = {"image/jpeg": "JPEG", "image/png": "PNG"}
AFFIRMATIONS = (
    "Small steps still move me forward.",
    "I can begin again today.",
    "I can make space for what matters.",
)
WORDS = (
    ("steadfast", "Firm in purpose or loyalty."),
    ("lucid", "Clear and easy to understand."),
    ("respite", "A short period of rest or relief."),
)


def daily_card(heading, title, detail, day, *, size=(WIDTH, HEIGHT)):
    width, height = size
    image = Image.new("1", size, 1)
    draw = ImageDraw.Draw(image)
    label = ImageFont.load_default(size=19)
    body = ImageFont.load_default(size=32)
    draw.text((32, 42), heading, fill=0, font=label)
    title_lines = "\n".join(wrap(title, 22))
    draw.multiline_text((32, 320), title_lines, fill=0, font=body, spacing=12)
    if detail:
        detail_y = max(384, draw.multiline_textbbox((32, 320), title_lines, font=body, spacing=12)[3] + 18)
        draw.multiline_text((32, detail_y), "\n".join(wrap(detail, 32)), fill=0, font=label)
    draw.line((32, height - 80, width - 32, height - 80), fill=0)
    draw.text((32, height - 62), day.strftime("%A, %b %d"), fill=0, font=label)
    return image


def word_from_bank(directory, day):
    entries = sorted(path for path in directory.glob("*.txt") if path.is_file() and not path.is_symlink())
    if not entries:
        return None
    selected = entries[day.toordinal() % len(entries)]
    with selected.open("rb") as source:
        data = source.read(4097)
    if len(data) > 4096:
        raise ValueError("Word Bank entry exceeds the text size limit")
    text = data.decode("utf-8").replace("\r\n", "\n").replace("\r", "\n")
    parts = text.strip().split("\n\n", 2)
    term = " ".join(parts[0].split())[:44]
    meaning = " ".join(next((part for part in parts[1:] if not part.startswith("Book: ")), "").split())[:160]
    return (term, meaning) if term else None


def daily_preview(source, day, *, size=(WIDTH, HEIGHT), word=None):
    if source == "affirmation":
        message = AFFIRMATIONS[day.toordinal() % len(AFFIRMATIONS)]
        return daily_card("DAILY AFFIRMATION", message, "", day, size=size)
    term, meaning = word or WORDS[day.toordinal() % len(WORDS)]
    return daily_card("WORD OF THE DAY", term, meaning, day, size=size)


def today_preview(day, tasks, *, size=(WIDTH, HEIGHT), brief=None):
    width, height = size
    image = Image.new("1", size, 1)
    draw = ImageDraw.Draw(image)
    heading = ImageFont.load_default(size=52)
    label = ImageFont.load_default(size=19)
    body = ImageFont.load_default(size=25)
    draw.text((32, 32), "Today", fill=0, font=heading)
    draw.text((32, 98), day.strftime("%A, %b %d"), fill=0, font=body)
    if brief is None:
        brief = {"weather": "Example town · 62°F · Partly cloudy", "calendar": "10:30 AM · Library visit"}
    sections = [(name, (" ".join(brief[key].split()),))
                for key, name in (("weather", "WEATHER"), ("calendar", "NEXT ON THE CALENDAR"))
                if isinstance(brief.get(key), str) and brief[key].strip()]
    sections.append(("TICKTICK", tuple(title for _, _, title in tasks[:3]) or ("No sample tasks due",)))
    for section, (name, lines) in enumerate(sections):
        y = 153 + section * 118
        draw.line((32, y, width - 32, y), fill=0)
        draw.text((32, y + 12), name, fill=0, font=label)
        for index, line in enumerate(lines):
            draw.text((32, y + 45 + index * 42), fit(draw, line[:256], body, width - 64), fill=0, font=body)
    draw.line((32, height - 80, width - 32, height - 80), fill=0)
    draw.text((32, height - 62), "Offline snapshot  |  " + day.isoformat(), fill=0, font=label)
    return image


def folder_image(directory, filename, framing, *, size=(WIDTH, HEIGHT)):
    directory = directory.resolve(strict=True)
    path = (directory / filename).resolve(strict=True)
    if path.parent != directory or not path.is_file():
        raise ValueError("Choose an image directly inside the selected folder")
    if path.stat().st_size > MAX_FILE_BYTES:
        raise ValueError("Image exceeds the 8 MiB preview limit")
    with Image.open(path, formats=("BMP", "JPEG", "PNG")) as source:
        return frame_image(source, framing, size)


def online_image(url, framing, *, size=(WIDTH, HEIGHT)):
    request = Request(https_url(url), headers={"Accept": ", ".join(ONLINE_FORMATS), "Accept-Encoding": "identity"})
    with build_opener(HttpsRedirect()).open(request, timeout=10) as response:
        image_format = ONLINE_FORMATS.get(response.headers.get_content_type())
        if image_format is None:
            raise ValueError("Use a direct link to a JPEG or PNG image")
        data = response.read(MAX_FILE_BYTES + 1)
    if len(data) > MAX_FILE_BYTES:
        raise ValueError("Image exceeds the 8 MiB preview limit")
    with Image.open(BytesIO(data), formats=(image_format,)) as source:
        return frame_image(source, framing, size)


def frame_image(source, framing, size):
    if source.width * source.height > MAX_PIXELS:
        raise ValueError("Image exceeds the 12 megapixel preview limit")
    oriented = ImageOps.exif_transpose(source).convert("RGBA")
    paper = Image.new("RGBA", oriented.size, "white")
    paper.alpha_composite(oriented)
    grayscale = paper.convert("L")
    resize = ImageOps.pad if framing == "fit" else ImageOps.fit
    options = {"color": 255} if framing == "fit" else {}
    framed = resize(grayscale, size, Image.Resampling.LANCZOS, **options)
    return framed.convert("1", dither=Image.Dither.FLOYDSTEINBERG)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--today", type=date.fromisoformat, default=date.today())
    parser.add_argument("--output", type=Path, required=True, help="Local BMP preview file")
    parser.add_argument("--device", choices=PANEL_SIZES, default="x4pro")
    sources = parser.add_subparsers(dest="source", required=True)
    for source in ("affirmation", "word", "ticktick", "today"):
        sources.add_parser(source)
    folder = sources.add_parser("folder")
    folder.add_argument("directory", type=Path)
    folder.add_argument("filename")
    folder.add_argument("--framing", choices=("fit", "fill"), default="fit")
    online = sources.add_parser("online")
    online.add_argument("url")
    online.add_argument("--framing", choices=("fit", "fill"), default="fit")
    args = parser.parse_args()
    size = PANEL_SIZES[args.device]

    try:
        if args.source in ("affirmation", "word"):
            image = daily_preview(args.source, args.today, size=size)
        elif args.source in ("ticktick", "today"):
            projects = json.loads(FIXTURE.read_text(encoding="utf-8"))
            tasks = select_tasks(projects, args.today, 7)
            image = today_preview(args.today, tasks, size=size) if args.source == "today" else render(
                tasks, args.today, 8, fixture=True, size=size
            )
        elif args.source == "folder":
            if args.output.resolve() == (args.directory / args.filename).resolve():
                raise ValueError("Choose a preview output separate from the source image")
            image = folder_image(args.directory, args.filename, args.framing, size=size)
        else:
            image = online_image(args.url, args.framing, size=size)
        write_snapshot(args.output, bmp_bytes(image))
    except (OSError, ValueError, Image.DecompressionBombError) as error:
        parser.exit(1, f"Sleep preview: {error}\n")


if __name__ == "__main__":
    main()
