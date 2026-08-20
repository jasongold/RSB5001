// Serves simulator/ over HTTP so the bench simulator runs as a live page.
//
// Opening index.html straight off disk works, but the preview pane loads a
// file:// URL as a hidden static snapshot, and requestAnimationFrame does not
// run in a hidden document — so the state machine never ticks. Over HTTP it is
// a real page and the loop runs.

const http = require("http");
const fs = require("fs");
const path = require("path");

const ROOT = path.join(__dirname, "..", "simulator");
const PORT = 5001;

const TYPES = {
  ".html": "text/html; charset=utf-8",
  ".js": "text/javascript; charset=utf-8",
  ".css": "text/css; charset=utf-8",
  ".png": "image/png",
  ".svg": "image/svg+xml",
};

http
  .createServer((req, res) => {
    const rel = decodeURIComponent(new URL(req.url, "http://localhost").pathname);
    const file = path.join(ROOT, rel === "/" ? "index.html" : rel);

    // Refuse anything that escapes the simulator directory.
    if (!file.startsWith(ROOT)) {
      res.writeHead(403).end("forbidden");
      return;
    }

    fs.readFile(file, (err, body) => {
      if (err) {
        res.writeHead(404).end("not found");
        return;
      }
      res.writeHead(200, {
        "Content-Type": TYPES[path.extname(file).toLowerCase()] || "application/octet-stream",
        "Cache-Control": "no-store",
      });
      res.end(body);
    });
  })
  .listen(PORT, () => console.log(`RSB5001 simulator on http://localhost:${PORT}`));
