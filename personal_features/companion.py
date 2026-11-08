"""Serve or stage cached text pages and a synthetic sleep image for CrossPoint."""

import argparse
import hashlib
import ipaddress
import json
import re
import xml.etree.ElementTree as ET
from tempfile import TemporaryDirectory
from datetime import date
from http.server import BaseHTTPRequestHandler, HTTPServer
from pathlib import Path
from urllib.parse import urlsplit

from browser import MAX_RESPONSE, PAGE_ID, document, extract, https_url
from sleep_preview import daily_preview, folder_image, today_preview, word_from_bank
from ticktick import PANEL_SIZES, bmp_bytes, render, select_tasks

FIXTURES = Path(__file__).parent / "fixtures"
MAX_PAGES = 24


def read_pages(directory):
    pages = {}
    for path in sorted(directory.glob("*.json")):
        if not PAGE_ID.fullmatch(path.stem):
            continue
        if path.is_symlink() or path.stat().st_size > 128 * 1024:
            raise ValueError(f"Invalid cached page: {path.name}")
        page = json.loads(path.read_text(encoding="utf-8"))
        payload = document(page).encode()
        if len(payload) > 24 * 1024:
            raise ValueError(f"Document exceeds reader limit: {path.name}")
        pages[path.stem] = (page["title"], payload)
        if len(pages) == MAX_PAGES:
            break
    return pages


def make_handler(pages, sample_bmp):
    entries = [
        {"id": page_id, "title": title, "author": "Cached web page", "url": f"/pages/{page_id}.txt"}
        for page_id, (title, _) in pages.items()
    ]
    catalog = json.dumps({"items": entries}, ensure_ascii=False).encode()

    class Handler(BaseHTTPRequestHandler):
        def do_GET(self):
            path = urlsplit(self.path).path
            if path == "/catalog.json":
                self.send_payload("application/json; charset=utf-8", catalog)
                return
            if path == "/fixture/ticktick.bmp":
                self.send_payload("image/bmp", sample_bmp)
                return
            match = re.fullmatch(r"/pages/([a-f0-9]{16})\.txt", path)
            if match and match[1] in pages:
                self.send_payload("text/plain; charset=utf-8", pages[match[1]][1])
                return
            self.send_error(404)

        def send_payload(self, mime, body):
            self.send_response(200)
            self.send_header("Content-Type", mime)
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(body)

    return Handler


def reader_origin(url):
    parsed = urlsplit(url)
    if (
        parsed.scheme != "http"
        or not parsed.hostname
        or parsed.username is not None
        or parsed.password is not None
        or parsed.path not in ("", "/")
        or parsed.query
        or parsed.fragment
    ):
        raise ValueError("Use a private-LAN HTTP origin such as http://192.168.1.10:8787")
    address = ipaddress.IPv4Address(parsed.hostname)
    private = any(address in ipaddress.ip_network(network) for network in ("10.0.0.0/8", "172.16.0.0/12", "192.168.0.0/16"))
    if not private and not address.is_loopback:
        raise ValueError("The fixture companion requires a private or loopback IPv4 address")
    return f"http://{address}:{parsed.port or 80}"


def stage(directory, origin):
    origin = reader_origin(origin)
    plugins = {
        "personal-text-web": {
            "title": "Text Web",
            "description": "Browse text pages saved by your companion.",
            "config": {"file": "config.json"},
            "browse": {
                "url": "{cfg.origin}/catalog.json",
                "items": "items",
                "fields": {"title": "title", "author": "author", "id": "id", "url": "url"},
                "page_size": 8,
            },
            "download": {"url": "{cfg.origin}{url}", "dest_dir": "/WebPages", "filename": "{id}.txt"},
        },
        "personal-ticktick-demo": {
            "title": "TickTick Sleep Demo",
            "description": "Fetch a synthetic task image before sleep.",
            "config": {"file": "config.json"},
            "events": {
                "sleep.enter": {
                    "download": {"url": "{cfg.origin}/fixture/ticktick.bmp", "dest": "/sleep.bmp"},
                },
            },
        },
    }
    for name, device in plugins.items():
        path = directory / name
        path.mkdir(parents=True, exist_ok=False)
        manifest = {
            "name": name,
            "title": device["title"],
            "version": "0.1.0",
            "files": ["manifest.json", "device.json", "config.json", "README.md"],
        }
        for filename, data in (("manifest.json", manifest), ("device.json", device), ("config.json", {"origin": origin})):
            (path / filename).write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")
        instructions = (
            "Use the catalog to download public text pages to WebPages. Open the saved file from Browse Files."
            if name == "personal-text-web"
            else "Sample tasks only. This plugin writes sleep.bmp. Use it as the sole sleep-image writer."
        )
        (path / "README.md").write_text(instructions + "\n", encoding="utf-8")


def sample_tasks(day):
    fixture = json.loads((FIXTURES / "ticktick.json").read_text(encoding="utf-8"))
    return select_tasks(fixture, day, 7)


def add_feed(pages, path):
    if len(pages) >= MAX_PAGES:
        return
    with path.open("rb") as source:
        data = source.read(MAX_RESPONSE + 1)
    if len(data) > MAX_RESPONSE:
        raise ValueError(f"Feed exceeds the byte limit: {path.name}")
    xml = data.decode("utf-8-sig")
    if re.search(r"<!\s*(?:DOCTYPE|ENTITY)\b", xml, re.IGNORECASE):
        raise ValueError(f"Feed must not declare entities: {path.name}")
    root = ET.fromstring(xml)
    atom = "{http://www.w3.org/2005/Atom}"
    if root.tag == "rss":
        entries = root.iterfind("./channel/item")
    elif root.tag == atom + "feed":
        entries = root.iterfind(atom + "entry")
    else:
        raise ValueError(f"Expected an RSS or Atom feed: {path.name}")
    for entry in entries:
        if root.tag == "rss":
            link = entry.findtext("link", "")
            title = entry.findtext("title", "")
            body = entry.findtext("description", "")
        else:
            link = next((node.get("href", "") for node in entry.iterfind(atom + "link")
                         if node.get("rel", "alternate") == "alternate"), "")
            title = entry.findtext(atom + "title", "")
            body = entry.findtext(atom + "content") or entry.findtext(atom + "summary", "")
        try:
            link = https_url(link.strip())
            page = extract(body or title, link)
        except ValueError:
            continue
        page["title"] = " ".join(title.split())[:100] or page["title"]
        page["links"] = [link]
        page_id = hashlib.sha256(link.encode()).hexdigest()[:16]
        pages.setdefault(page_id, (page["title"], document(page).encode()))
        if len(pages) >= MAX_PAGES:
            break


def stage_offline(directory, pages, snapshots):
    if not pages:
        raise ValueError("Cache contains no saved Text Web pages")
    if directory.exists():
        raise FileExistsError(f"Choose a new staging directory: {directory}")
    directory.parent.mkdir(parents=True, exist_ok=True)
    with TemporaryDirectory(prefix="x4-offline-", dir=directory.parent) as temporary:
        card = Path(temporary) / "card"
        web = card / "WebPages"
        sleep = card / ".crosspoint" / "personal-sleep"
        web.mkdir(parents=True)
        sleep.mkdir(parents=True)
        for page_id, (title, payload) in pages.items():
            safe_title = "".join(c if c.isalnum() or c in " -_" else "-" for c in title[:64]).strip(" -_")
            filename = f"{safe_title or 'Saved page'} - {page_id}.txt"
            (web / filename).write_bytes(payload)
        for filename, data in snapshots.items():
            (sleep / filename).write_bytes(data)
        card.rename(directory)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    serve = commands.add_parser("serve")
    serve.add_argument("--cache", type=Path, required=True)
    serve.add_argument("--bind", default="127.0.0.1")
    serve.add_argument("--port", default=8787, type=int)
    staging = commands.add_parser("stage")
    staging.add_argument("--output", type=Path, required=True)
    staging.add_argument("--origin", required=True)
    offline = commands.add_parser("stage-offline", help="Package cached text and sample tasks for offline reading")
    offline.add_argument("--cache", type=Path, required=True)
    offline.add_argument("--output", type=Path, required=True)
    offline.add_argument("--today", type=date.fromisoformat, default=date(2026, 11, 6))
    v1 = commands.add_parser("stage-v1", help="Package the v1 offline queue and sleep snapshots")
    v1.add_argument("--cache", type=Path, required=True)
    v1.add_argument("--output", type=Path, required=True)
    v1.add_argument("--today", type=date.fromisoformat, required=True)
    v1.add_argument("--rss", type=Path, action="append", default=[])
    v1.add_argument("--image", type=Path)
    v1.add_argument("--device", choices=PANEL_SIZES, required=True)
    v1.add_argument("--today-data", type=Path)
    v1.add_argument("--word-bank", type=Path)
    args = parser.parse_args()
    if args.command == "stage":
        stage(args.output, args.origin)
        return
    if args.command in ("stage-offline", "stage-v1"):
        pages = read_pages(args.cache)
        tasks = sample_tasks(args.today)
        size = PANEL_SIZES[args.device] if args.command == "stage-v1" else PANEL_SIZES["x4pro"]
        snapshots = {"ticktick.bmp": bmp_bytes(render(tasks, args.today, 8, fixture=True, size=size))}
        if args.command == "stage-v1":
            for feed in args.rss:
                add_feed(pages, feed)
            brief = None
            if args.today_data:
                with args.today_data.open("rb") as source:
                    data = source.read(4097)
                if len(data) > 4096:
                    raise ValueError("Today data exceeds the 4 KiB limit")
                brief = json.loads(data)
                if not isinstance(brief, dict):
                    raise ValueError("Today data must be a JSON object")
            snapshots["today.bmp"] = bmp_bytes(today_preview(args.today, tasks, size=size, brief=brief))
            word = word_from_bank(args.word_bank, args.today) if args.word_bank else None
            for name in ("affirmation", "word"):
                snapshots[name + ".bmp"] = bmp_bytes(daily_preview(name, args.today, size=size, word=word))
            if args.image:
                snapshots["folder.bmp"] = bmp_bytes(folder_image(args.image.parent, args.image.name, "fit", size=size))
        stage_offline(args.output, pages, snapshots)
        print(f"Staged {len(pages)} saved page(s) and {len(snapshots)} sleep snapshot(s).")
        return
    reader_origin(f"http://{args.bind}:{args.port}")
    sample_day = date(2026, 11, 6)
    sample = bmp_bytes(render(sample_tasks(sample_day), sample_day, 8, fixture=True))
    handler = make_handler(read_pages(args.cache), sample)
    with HTTPServer((args.bind, args.port), handler) as server:
        print(f"Fixture companion listening on {args.bind}:{args.port}", flush=True)
        server.serve_forever()


if __name__ == "__main__":
    main()
