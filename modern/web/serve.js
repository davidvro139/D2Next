#!/usr/bin/env node
// Serve the wasm page and the optional developer MPQ/Save folders on one origin (any OS with Node.js).
// On Windows, Play-Web.cmd does the same with nothing installed.
//   node modern/web/serve.js
//   node modern/web/serve.js --port 8060
const http = require("http");
const fs = require("fs");
const path = require("path");
const { pipeline } = require("stream");

const ROOT = path.resolve(__dirname, "../..");
// The built page: out/web-dist (Build-Web.cmd, build-web.sh), or an older out/web.
const WEB = fs.existsSync(path.join(ROOT, "out", "web-dist", "D2Next.html")) ? path.join(ROOT, "out", "web-dist") : path.join(ROOT, "out", "web");
const MPQ = path.join(ROOT, "MPQ");
const SAVE = fs.existsSync(path.join(ROOT, "save")) ? path.join(ROOT, "save") : path.join(ROOT, "Save");
const PORT = (() => {
  const i = process.argv.indexOf("--port");
  return i >= 0 ? Number(process.argv[i + 1]) : 8060;
})();

const TYPES = {
  ".html": "text/html; charset=utf-8",
  ".js": "text/javascript; charset=utf-8",
  ".wasm": "application/wasm",
  ".mpq": "application/octet-stream",
  ".d2s": "application/octet-stream",
  ".txt": "text/plain; charset=utf-8",
};

function resolveUrl(urlPath) {
  const clean = decodeURIComponent(urlPath.split("?")[0].split("#")[0]);
  const rel = clean.replace(/^\/+/, "");
  const lower = rel.toLowerCase();
  if (lower === "mpq" || lower.startsWith("mpq/")) {
    return path.join(MPQ, rel.slice(rel.indexOf("/") >= 0 ? rel.indexOf("/") + 1 : rel.length));
  }
  if (lower === "save" || lower.startsWith("save/")) {
    return path.join(SAVE, rel.slice(rel.indexOf("/") >= 0 ? rel.indexOf("/") + 1 : rel.length));
  }
  return path.join(WEB, rel || "D2Next.html");
}

function under(root, file) {
  const resolved = path.resolve(file);
  const base = path.resolve(root) + path.sep;
  return resolved === path.resolve(root) || resolved.startsWith(base);
}

const server = http.createServer((req, res) => {
  if ((req.url || "").split("?")[0] === "/favicon.ico") {
    res.writeHead(204);
    res.end();
    return;
  }
  const file = resolveUrl(req.url || "/");
  const allowed = under(WEB, file) || under(MPQ, file) || under(SAVE, file);
  if (!allowed) {
    res.writeHead(403);
    res.end("forbidden");
    return;
  }
  fs.stat(file, (err, st) => {
    if (err || !st.isFile()) {
      res.writeHead(404);
      res.end("not found");
      return;
    }
    const type = TYPES[path.extname(file).toLowerCase()] || "application/octet-stream";
    if (req.method === "HEAD") {
      res.writeHead(200, { "Content-Type": type, "Content-Length": st.size, "Cache-Control": "no-cache", "Cross-Origin-Opener-Policy": "same-origin", "Cross-Origin-Embedder-Policy": "require-corp" });
      res.end();
      return;
    }
    res.writeHead(200, { "Content-Type": type, "Content-Length": st.size, "Cache-Control": "no-cache", "Cross-Origin-Opener-Policy": "same-origin", "Cross-Origin-Embedder-Policy": "require-corp" });
    pipeline(fs.createReadStream(file), res, () => {});
  });
});

if (!fs.existsSync(path.join(WEB, "D2Next.html"))) {
  console.error("Missing out/web-dist/D2Next.html. Build first: Build-Web.cmd on Windows, bash modern/web/build-web.sh elsewhere.");
  process.exit(1);
}

server.listen(PORT, "127.0.0.1", () => {
  console.log("D2Next web: http://127.0.0.1:" + PORT + "/D2Next.html");
  console.log("  engine: " + WEB);
  console.log("  MPQs:   " + MPQ);
  console.log("  saves:  " + SAVE);
});
