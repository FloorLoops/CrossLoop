"""Turn a public HTTPS page into a bounded document the reader can page through."""

import argparse
import hashlib
import json
import re
import time
from html.parser import HTMLParser
from pathlib import Path
from urllib.parse import urljoin, urlsplit
from urllib.request import HTTPRedirectHandler, Request, build_opener

MAX_RESPONSE = 256 * 1024
MAX_TEXT = 20_000
MAX_LINKS = 16
PAGE_ID = re.compile(r"[a-f0-9]{16}")


def https_url(url):
    if len(url) > 2048 or any(ord(char) <= 32 or ord(char) == 127 for char in url):
        raise ValueError("Invalid page URL")
    parts = urlsplit(url)
    if parts.scheme != "https" or not parts.hostname or parts.username or parts.password is not None:
        raise ValueError("Use a public HTTPS URL without embedded credentials")
    return parts._replace(fragment="").geturl()


class HttpsRedirect(HTTPRedirectHandler):
    max_redirections = 3
    max_repeats = 1

    def redirect_request(self, req, fp, code, msg, headers, newurl):
        return super().redirect_request(req, fp, code, msg, headers, https_url(newurl))


class ArticleParser(HTMLParser):
    hidden = {"script", "style", "svg", "iframe", "noscript", "template"}
    blocks = {"p", "li", "div", "br", "h1", "h2", "h3", "article", "section", "blockquote"}

    def __init__(self, url):
        super().__init__(convert_charrefs=True)
        self.url = url
        self.skip = None
        self.in_head = False
        self.in_title = False
        self.title = []
        self.body = []
        self.links = []

    def handle_starttag(self, tag, attrs):
        if self.skip:
            return
        if tag in self.hidden:
            self.skip = tag
        elif tag == "head":
            self.in_head = True
        elif tag == "title":
            self.in_title = True
        elif not self.in_head and tag in self.blocks:
            self.body.append("\n")
        elif not self.in_head and tag == "a" and len(self.links) < MAX_LINKS:
            href = dict(attrs).get("href")
            if not href or href.startswith("#"):
                return
            try:
                link = https_url(urljoin(self.url, href))
            except ValueError:
                return
            if link not in self.links:
                self.links.append(link)

    def handle_endtag(self, tag):
        if self.skip:
            if tag == self.skip:
                self.skip = None
            return
        if tag == "head":
            self.in_head = False
        elif tag == "title":
            self.in_title = False
        elif tag in self.blocks:
            self.body.append("\n")

    def handle_data(self, data):
        if not self.skip:
            if self.in_title:
                self.title.append(data)
            elif not self.in_head:
                self.body.append(data)


def extract(source, url, *, html=True):
    url = https_url(url)
    if len(source) > MAX_RESPONSE:
        raise ValueError("Page exceeds the document limit")
    parser = ArticleParser(url)
    if html:
        parser.feed(source)
        parser.close()
    text = "".join(parser.body) if html else source
    lines = [" ".join(line.split()) for line in text.splitlines()]
    article = "\n\n".join(line for line in lines if line)
    if not article:
        raise ValueError("The page contains no readable text")
    title = " ".join("".join(parser.title).split())[:100] or urlsplit(url).hostname
    article = article.encode()[:MAX_TEXT].decode("utf-8", errors="ignore")
    return {"title": title, "text": article, "links": parser.links}


def fetch(url):
    opener = build_opener(HttpsRedirect())
    request = Request(https_url(url), headers={"Accept": "text/html,text/plain", "User-Agent": "PersonalReader/0.1"})
    with opener.open(request, timeout=12) as response:
        kind = response.headers.get_content_type()
        if kind not in {"text/html", "text/plain", "application/xhtml+xml"}:
            raise ValueError(f"Unsupported document type: {kind}")
        body = bytearray()
        deadline = time.monotonic() + 20
        while chunk := response.read1(16 * 1024):
            body.extend(chunk)
            if len(body) > MAX_RESPONSE:
                raise ValueError("Page exceeds the network download limit")
            if time.monotonic() >= deadline:
                raise TimeoutError("Page download exceeded its time budget")
        charset = response.headers.get_content_charset() or "utf-8"
        try:
            source = body.decode(charset, errors="replace")
        except LookupError:
            source = body.decode("utf-8", errors="replace")
        return extract(source, response.geturl(), html=kind != "text/plain")


def cache_page(directory, url, page):
    directory.mkdir(parents=True, exist_ok=True)
    page_id = hashlib.sha256(https_url(url).encode()).hexdigest()[:16]
    target = directory / f"{page_id}.json"
    temporary = target.with_suffix(".tmp")
    temporary.write_text(json.dumps(page, ensure_ascii=False), encoding="utf-8")
    temporary.replace(target)
    return page_id


def document(page):
    lines = []
    for i, url in enumerate(page["links"], 1):
        encoded = url.encode()
        short = encoded[:96].decode("utf-8", errors="ignore")
        lines.append(f"{i}. {short}{'...' if len(encoded) > 96 else ''}")
    links = "\n".join(lines)
    return f'{page["title"]}\n\n{page["text"]}' + (f"\n\nLinks\n{links}\n" if links else "\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cache", type=Path, required=True)
    inputs = parser.add_mutually_exclusive_group(required=True)
    inputs.add_argument("--url")
    inputs.add_argument("--fixture", type=Path)
    inputs.add_argument("--follow", nargs=2, metavar=("PAGE_ID", "LINK_NUMBER"))
    args = parser.parse_args()
    if args.fixture:
        url = "https://example.com/fixture"
        page = extract(args.fixture.read_text(encoding="utf-8"), url)
    else:
        url = args.url
        if args.follow:
            page_id, number = args.follow
            if not PAGE_ID.fullmatch(page_id):
                parser.error("Invalid page ID")
            links = json.loads((args.cache / f"{page_id}.json").read_text(encoding="utf-8"))["links"]
            if not number.isdecimal() or not 1 <= int(number) <= len(links):
                parser.error("Link number out of range")
            url = links[int(number) - 1]
        page = fetch(url)
    print(cache_page(args.cache, url, page))


if __name__ == "__main__":
    main()
