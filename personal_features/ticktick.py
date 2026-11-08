"""Render a read-only TickTick snapshot as an X4 Pro sleep image."""

import argparse
import io
import json
import os
import re
import tempfile
from datetime import date, datetime, timedelta
from pathlib import Path
from urllib.request import HTTPRedirectHandler, Request, build_opener

from PIL import Image, ImageDraw, ImageFont

API = "https://api.ticktick.com/open/v1"
MAX_PROJECT_RESPONSE = 1024 * 1024
MAX_PROJECTS = 8
WIDTH, HEIGHT = 480, 800
MAX_ROWS = 8


class NoRedirect(HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        return None


class TickTickProvider:
    """Use an externally acquired tasks:read OAuth access token on the host."""

    def __init__(self, token):
        if not token or any(ord(char) <= 32 or ord(char) >= 127 for char in token):
            raise ValueError("Set a valid TICKTICK_ACCESS_TOKEN on the host")
        self.token = token
        self.opener = build_opener(NoRedirect())

    def load_projects(self, project_ids):
        project_ids = list(dict.fromkeys(project_ids))
        if not 1 <= len(project_ids) <= MAX_PROJECTS:
            raise ValueError("Specify one to eight TickTick project IDs")
        if any(not re.fullmatch(r"[A-Za-z0-9_-]{1,128}", project) for project in project_ids):
            raise ValueError("Invalid TickTick project ID")
        for project in project_ids:
            request = Request(
                f"{API}/project/{project}/data",
                headers={"Authorization": f"Bearer {self.token}", "Accept": "application/json"},
            )
            with self.opener.open(request, timeout=10) as response:
                payload = response.read(MAX_PROJECT_RESPONSE + 1)
            if len(payload) > MAX_PROJECT_RESPONSE:
                raise ValueError("TickTick project exceeds the snapshot limit")
            yield json.loads(payload)


def due_day(value, all_day):
    if not value:
        return None
    if all_day or len(value) == 10:
        return date.fromisoformat(value[:10])
    iso = re.sub(r"([+-]\d{2})(\d{2})$", r"\1:\2", value)
    parsed = datetime.fromisoformat(iso.replace("Z", "+00:00"))
    return parsed.astimezone().date()


def select_tasks(projects, today, days, *, include_undated=False):
    if not 0 <= days <= 30:
        raise ValueError("Upcoming window must be between 0 and 30 days")
    cutoff = today + timedelta(days=days)
    tasks = []
    for project in projects:
        for task in project["tasks"]:
            if task.get("status", 0) != 0:
                continue
            title = " ".join(task["title"].split())[:200]
            if not title:
                continue
            due = due_day(task.get("dueDate"), task.get("isAllDay", False))
            if (due is None and not include_undated) or (due is not None and due > cutoff):
                continue
            tasks.append((due, int(task.get("priority", 0)), title))
    return sorted(tasks, key=lambda item: (item[0] or date.max, -item[1], item[2].casefold()))


def fit(draw, text, font, width):
    if draw.textlength(text, font=font) <= width:
        return text
    while text and draw.textlength(text + "...", font=font) > width:
        text = text[:-1]
    return text + "..."


PANEL_SIZES = {"x4pro": (WIDTH, HEIGHT), "x3": (528, 792)}


def render(tasks, today, rows, *, fixture=False, size=(WIDTH, HEIGHT)):
    if not 1 <= rows <= MAX_ROWS:
        raise ValueError("Visible rows must be between 1 and 8")
    width, height = size
    image = Image.new("1", size, 1)
    draw = ImageDraw.Draw(image)
    heading = ImageFont.load_default(size=38)
    body = ImageFont.load_default(size=25)
    small = ImageFont.load_default(size=17)
    draw.text((28, 27), "TickTick", fill=0, font=heading)
    draw.text((28, 77), today.strftime("%A, %b %d"), fill=0, font=small)
    draw.line((28, 113, width - 28, 113), fill=0, width=2)
    for i, (due, _, title) in enumerate(tasks[:rows]):
        y = 133 + 69 * i
        draw.rectangle((29, y + 3, 46, y + 20), outline=0, width=2)
        draw.text((60, y - 2), fit(draw, title, body, width - 88), fill=0, font=body)
        if due is None:
            label = "No due date"
        elif due < today:
            label = "Overdue  " + due.strftime("%b %d")
        elif due == today:
            label = "Today"
        else:
            label = due.strftime("%a, %b %d")
        draw.text((60, y + 28), label, fill=0, font=small)
    if not tasks:
        draw.text((28, 145), "No tasks in this view", fill=0, font=body)
    extra = f"  |  +{len(tasks) - rows} more" if len(tasks) > rows else ""
    stamp = "Sample tasks" if fixture else "Snapshot"
    draw.line((28, height - 73, width - 28, height - 73), fill=0)
    draw.text((28, height - 56), stamp + "  |  " + today.isoformat() + extra, fill=0, font=small)
    return image


def bmp_bytes(image):
    stream = io.BytesIO()
    image.save(stream, format="BMP")
    return stream.getvalue()


def write_snapshot(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(dir=path.parent, delete=False) as stream:
        temporary = Path(stream.name)
        try:
            stream.write(data)
        except OSError:
            stream.close()
            temporary.unlink(missing_ok=True)
            raise
    try:
        temporary.replace(path)
    finally:
        temporary.unlink(missing_ok=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument("--fixture", type=Path)
    source.add_argument("--project", action="append")
    parser.add_argument("--today", type=date.fromisoformat, default=date.today())
    parser.add_argument("--days", type=int, default=0)
    parser.add_argument("--rows", type=int, default=8)
    parser.add_argument("--device", choices=PANEL_SIZES, default="x4pro")
    parser.add_argument("--include-undated", action="store_true")
    args = parser.parse_args()
    if args.fixture:
        projects = json.loads(args.fixture.read_text(encoding="utf-8"))
    else:
        projects = TickTickProvider(os.environ.get("TICKTICK_ACCESS_TOKEN")).load_projects(args.project)
    tasks = select_tasks(projects, args.today, args.days, include_undated=args.include_undated)
    image = render(tasks, args.today, args.rows, fixture=bool(args.fixture), size=PANEL_SIZES[args.device])
    write_snapshot(args.output, bmp_bytes(image))


if __name__ == "__main__":
    main()
