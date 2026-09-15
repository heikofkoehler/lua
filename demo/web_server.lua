-- ==============================================================================
-- Lua C++ VM Web Server & Application Showcase
-- Features:
--   * HTTP/1.1 Request Parser (Methods, Paths, Query String, Headers, Body)
--   * Express/Sinatra-style Router with URL parameters (:id)
--   * REST API with JSON serialization (rxi/json.lua)
--   * Dynamic Markdown compilation to HTML (Niklas Frykholm's markdown.lua)
--   * In-Memory Note / Document Store (CRUD)
--   * VM Diagnostics & Live Metrics (Memory, GC, Uptime, Version)
--   * Modern Self-Contained Web Dashboard (SPA with CSS & Vanilla JS)
-- ==============================================================================

package.path = package.path .. ";./demo/?.lua;./?.lua"

local json = require("json")
local md = require("markdown")

local PORT = 8080
local HOST = "127.0.0.1"

-- In-memory data store
local notes = {
    {
        id = 1,
        title = "Welcome to Lua C++ VM",
        content = "# Lua VM Web Server\n\nThis application is running entirely on a **custom Lua VM implemented in C++**!\n\n### Features:\n* Fast bytecode compiler and register/stack VM\n* Full Lua 5.3/5.4 language capabilities\n* Built-in TCP socket networking\n* Pure-Lua JSON encoding/decoding (`rxi/json.lua`)\n* Pure-Lua Markdown to HTML rendering (`speedata/luamarkdown`)\n\nTry editing or creating new notes!",
        created_at = "2026-09-14 20:00:00"
    },
    {
        id = 2,
        title = "REST API Quickstart",
        content = "## Available REST Endpoints\n\n* `GET /api/info` - Server & VM memory diagnostics\n* `GET /api/notes` - List all notes\n* `POST /api/notes` - Create note with JSON `{title, content}`\n* `GET /api/notes/:id` - Fetch note and rendered HTML\n* `DELETE /api/notes/:id` - Remove note\n* `POST /api/render` - Render arbitrary Markdown to HTML\n* `GET /api/echo` - Echo back request parameters",
        created_at = "2026-09-14 20:05:00"
    }
}
local next_note_id = 3
local server_start_time = os.clock()
local total_requests = 0

-- ------------------------------------------------------------------------------
-- Helper: URL decode
-- ------------------------------------------------------------------------------
local function url_decode(str)
    if not str then return "" end
    str = string.gsub(str, "+", " ")
    str = string.gsub(str, "%%(%x%x)", function(h)
        return string.char(tonumber(h, 16))
    end)
    return str
end

-- ------------------------------------------------------------------------------
-- HTTP Request Parser
-- ------------------------------------------------------------------------------
local function parse_request(raw)
    if not raw or string.len(raw) == 0 then return nil end

    local header_end = string.find(raw, "\r\n\r\n") or string.find(raw, "\n\n")
    local header_part, body_part
    if header_end then
        local sep_len = (string.sub(raw, header_end, header_end + 3) == "\r\n\r\n") and 4 or 2
        header_part = string.sub(raw, 1, header_end - 1)
        body_part = string.sub(raw, header_end + sep_len)
    else
        header_part = raw
        body_part = ""
    end

    local lines = {}
    for line in string.gmatch(header_part, "[^\r\n]+") do
        table.insert(lines, line)
    end

    if #lines == 0 then return nil end

    -- Request line: METHOD URI HTTP/X.X
    local method, raw_uri, protocol = string.match(lines[1], "^(%a+)%s+(%S+)%s*(%S*)$")
    if not method or not raw_uri then return nil end

    -- Split path and query string
    local path, query_string = string.match(raw_uri, "^([^?]*)(.-)$")
    if query_string and string.sub(query_string, 1, 1) == "?" then
        query_string = string.sub(query_string, 2)
    else
        query_string = ""
    end

    -- Parse query parameters
    local query = {}
    for pair in string.gmatch(query_string, "([^&]+)") do
        local k, v = string.match(pair, "^([^=]+)=(.*)$")
        if k then
            query[url_decode(k)] = url_decode(v or "")
        else
            query[url_decode(pair)] = true
        end
    end

    -- Parse headers
    local headers = {}
    for i = 2, #lines do
        local hk, hv = string.match(lines[i], "^([^:]+):%s*(.*)$")
        if hk and hv then
            headers[string.lower(hk)] = hv
        end
    end

    local req = {
        raw = raw,
        method = string.upper(method),
        uri = raw_uri,
        path = path,
        query_string = query_string,
        query = query,
        headers = headers,
        body = body_part,
        params = {}
    }

    function req:json()
        if self.body and string.len(self.body) > 0 then
            local ok, parsed = pcall(json.decode, self.body)
            if ok then return parsed end
        end
        return nil
    end

    return req
end

-- ------------------------------------------------------------------------------
-- HTTP Response Builder
-- ------------------------------------------------------------------------------
local status_texts = {
    [200] = "OK",
    [201] = "Created",
    [204] = "No Content",
    [400] = "Bad Request",
    [404] = "Not Found",
    [405] = "Method Allowed",
    [500] = "Internal Server Error"
}

local function create_response()
    local res = {
        status_code = 200,
        headers = {
            ["Content-Type"] = "text/plain; charset=utf-8",
            ["Server"] = "Lua-Cpp-VM/1.0",
            ["Access-Control-Allow-Origin"] = "*",
            ["Access-Control-Allow-Methods"] = "GET, POST, PUT, DELETE, OPTIONS",
            ["Access-Control-Allow-Headers"] = "Content-Type, Authorization",
            ["Connection"] = "close"
        },
        body = ""
    }

    function res:status(code)
        self.status_code = code
        return self
    end

    function res:header(k, v)
        self.headers[k] = v
        return self
    end

    function res:send(body, content_type)
        self.body = body or ""
        if content_type then
            self.headers["Content-Type"] = content_type
        end
        return self
    end

    function res:html(html_body, code)
        if code then self.status_code = code end
        return self:send(html_body, "text/html; charset=utf-8")
    end

    function res:json(data, code)
        if code then self.status_code = code end
        local ok, encoded = pcall(json.encode, data)
        if not ok then
            print("[ERROR] JSON encode failed: " .. tostring(encoded))
            encoded = '{"error": "Failed to serialize JSON", "detail": "' .. tostring(encoded) .. '"}'
            self.status_code = 500
        end
        return self:send(encoded, "application/json; charset=utf-8")
    end

    function res:build()
        local st_text = status_texts[self.status_code] or "OK"
        local head = "HTTP/1.1 " .. self.status_code .. " " .. st_text .. "\r\n"
        self.headers["Content-Length"] = tostring(string.len(self.body))
        for k, v in pairs(self.headers) do
            head = head .. k .. ": " .. v .. "\r\n"
        end
        head = head .. "\r\n"
        return head .. self.body
    end

    return res
end

-- ------------------------------------------------------------------------------
-- Router System
-- ------------------------------------------------------------------------------
local App = {}
App.__index = App

function App.new()
    local self = setmetatable({}, App)
    self.routes = {}
    return self
end

function App:add_route(method, pattern, handler)
    -- Transform :param into Lua pattern capture
    local regex = "^" .. pattern .. "$"
    local param_names = {}
    for param in string.gmatch(pattern, ":([%a_][%w_]*)") do
        table.insert(param_names, param)
    end
    regex = string.gsub(regex, ":[%a_][%w_]*", "([^/]+)")

    table.insert(self.routes, {
        method = string.upper(method),
        pattern = pattern,
        regex = regex,
        param_names = param_names,
        handler = handler
    })
end

function App:get(pattern, handler) self:add_route("GET", pattern, handler) end
function App:post(pattern, handler) self:add_route("POST", pattern, handler) end
function App:delete(pattern, handler) self:add_route("DELETE", pattern, handler) end
function App:options(pattern, handler) self:add_route("OPTIONS", pattern, handler) end

function App:dispatch(req, res)
    -- Handle CORS preflight
    if req.method == "OPTIONS" then
        return res:status(204):send("")
    end

    for _, route in ipairs(self.routes) do
        if route.method == req.method then
            local matches = { string.match(req.path, route.regex) }
            if #matches > 0 or req.path == route.pattern then
                req.params = {}
                for idx, name in ipairs(route.param_names) do
                    req.params[name] = matches[idx]
                end
                local ok, err = pcall(route.handler, req, res)
                if not ok then
                    print("Handler error: " .. tostring(err))
                    return res:status(500):json({
                        error = "Internal Server Error",
                        detail = tostring(err)
                    })
                end
                return res
            end
        end
    end

    -- 404 Fallback
    if req.headers["accept"] and string.find(req.headers["accept"], "application/json") then
        return res:status(404):json({ error = "Endpoint not found", path = req.path })
    else
        return res:status(404):html([[
<!DOCTYPE html>
<html>
<head><title>404 Not Found</title>
<style>
body { font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif; background: #0f172a; color: #f8fafc; display: flex; align-items: center; justify-content: center; height: 100vh; margin: 0; }
.card { background: #1e293b; padding: 2.5rem; border-radius: 12px; border: 1px solid #334155; text-align: center; max-width: 440px; }
h1 { font-size: 3rem; margin: 0 0 0.5rem 0; color: #ef4444; }
p { color: #94a3b8; line-height: 1.5; }
a { display: inline-block; margin-top: 1.5rem; padding: 0.6rem 1.2rem; background: #3b82f6; color: white; text-decoration: none; border-radius: 6px; font-weight: 500; }
a:hover { background: #2563eb; }
</style>
</head>
<body>
<div class="card">
  <h1>404</h1>
  <p>The requested route <code>]] .. req.path .. [[</code> was not found on this server.</p>
  <a href="/">Return to Dashboard</a>
</div>
</body>
</html>
]])
    end
end

-- ------------------------------------------------------------------------------
-- Instantiate Web App & Define Routes
-- ------------------------------------------------------------------------------
local app = App.new()

-- 1. Web Dashboard (Single-Page Application)
app:get("/", function(req, res)
    local html = [[
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Lua VM Web Dashboard</title>
<style>
  :root {
    --bg: #0b0f19;
    --card-bg: #151d30;
    --border: #24304f;
    --primary: #38bdf8;
    --primary-hover: #0284c7;
    --success: #34d399;
    --danger: #f87171;
    --text: #f1f5f9;
    --text-muted: #94a3b8;
    --code-bg: #0f172a;
  }
  * { box-sizing: border-box; margin: 0; padding: 0; }
  body {
    font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, "Helvetica Neue", Arial, sans-serif;
    background-color: var(--bg);
    color: var(--text);
    padding: 2rem 1rem;
    display: flex;
    justify-content: center;
  }
  .container {
    width: 100%;
    max-width: 1100px;
    display: flex;
    flex-direction: column;
    gap: 1.75rem;
  }
  header {
    display: flex;
    justify-content: space-between;
    align-items: center;
    border-bottom: 1px solid var(--border);
    padding-bottom: 1.25rem;
  }
  .brand { display: flex; align-items: center; gap: 0.75rem; }
  .logo-badge {
    background: linear-gradient(135deg, #0284c7, #38bdf8);
    color: #fff;
    font-weight: 800;
    font-size: 1.2rem;
    padding: 0.4rem 0.8rem;
    border-radius: 8px;
    letter-spacing: 0.5px;
  }
  .brand-text h1 { font-size: 1.5rem; font-weight: 700; color: #fff; }
  .brand-text p { font-size: 0.85rem; color: var(--text-muted); }
  .status-pill {
    display: flex;
    align-items: center;
    gap: 0.5rem;
    background: rgba(52, 211, 153, 0.1);
    color: var(--success);
    border: 1px solid rgba(52, 211, 153, 0.3);
    padding: 0.35rem 0.85rem;
    border-radius: 9999px;
    font-size: 0.85rem;
    font-weight: 600;
  }
  .status-dot { width: 8px; height: 8px; background: var(--success); border-radius: 50%; box-shadow: 0 0 8px var(--success); }

  /* Metrics Grid */
  .metrics-grid {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(220px, 1fr));
    gap: 1rem;
  }
  .metric-card {
    background: var(--card-bg);
    border: 1px solid var(--border);
    border-radius: 10px;
    padding: 1.25rem;
    display: flex;
    flex-direction: column;
    gap: 0.4rem;
  }
  .metric-title { font-size: 0.8rem; text-transform: uppercase; letter-spacing: 0.05em; color: var(--text-muted); }
  .metric-val { font-size: 1.6rem; font-weight: 700; color: #fff; }

  /* Main Split View */
  .main-grid {
    display: grid;
    grid-template-columns: 1fr 1fr;
    gap: 1.5rem;
  }
  @media (max-width: 850px) {
    .main-grid { grid-template-columns: 1fr; }
  }

  .panel {
    background: var(--card-bg);
    border: 1px solid var(--border);
    border-radius: 12px;
    padding: 1.5rem;
    display: flex;
    flex-direction: column;
    gap: 1rem;
  }
  .panel h2 { font-size: 1.15rem; font-weight: 600; border-bottom: 1px solid var(--border); padding-bottom: 0.75rem; }

  /* Form */
  .form-group { display: flex; flex-direction: column; gap: 0.4rem; }
  label { font-size: 0.85rem; color: var(--text-muted); }
  input, textarea {
    background: var(--code-bg);
    border: 1px solid var(--border);
    border-radius: 6px;
    color: #fff;
    padding: 0.65rem 0.85rem;
    font-size: 0.9rem;
    font-family: inherit;
  }
  textarea { font-family: ui-monospace, SFMono-Regular, Menlo, Monaco, Consolas, monospace; min-height: 140px; resize: vertical; }
  input:focus, textarea:focus { outline: none; border-color: var(--primary); }

  .btn-row { display: flex; gap: 0.75rem; justify-content: flex-end; }
  button {
    cursor: pointer;
    border: none;
    border-radius: 6px;
    padding: 0.6rem 1.1rem;
    font-size: 0.875rem;
    font-weight: 600;
    transition: all 0.15s ease;
  }
  .btn-primary { background: var(--primary); color: #0b0f19; }
  .btn-primary:hover { background: var(--primary-hover); color: #fff; }
  .btn-secondary { background: var(--border); color: var(--text); }
  .btn-secondary:hover { background: #334155; }
  .btn-danger { background: rgba(248, 113, 113, 0.15); color: var(--danger); border: 1px solid rgba(248, 113, 113, 0.3); }
  .btn-danger:hover { background: var(--danger); color: #fff; }

  /* Notes List */
  .notes-list { display: flex; flex-direction: column; gap: 0.75rem; max-height: 480px; overflow-y: auto; }
  .note-item {
    background: var(--code-bg);
    border: 1px solid var(--border);
    border-radius: 8px;
    padding: 1rem;
    display: flex;
    flex-direction: column;
    gap: 0.5rem;
  }
  .note-header { display: flex; justify-content: space-between; align-items: baseline; }
  .note-title { font-size: 1rem; font-weight: 600; color: #fff; }
  .note-date { font-size: 0.75rem; color: var(--text-muted); }
  .note-body {
    background: #090d16;
    border-radius: 6px;
    padding: 0.85rem;
    font-size: 0.85rem;
    line-height: 1.5;
    border: 1px solid rgba(255,255,255,0.05);
  }
  .note-body h1, .note-body h2, .note-body h3 { margin-top: 0.4rem; margin-bottom: 0.4rem; color: var(--primary); }
  .note-body p { margin-bottom: 0.5rem; }
  .note-body ul, .note-body ol { margin-left: 1.25rem; margin-bottom: 0.5rem; }
  .note-body pre, .note-body code { background: #151d30; padding: 0.2rem 0.4rem; border-radius: 4px; font-family: monospace; font-size: 0.85em; }

  /* API Reference */
  .api-card {
    background: var(--card-bg);
    border: 1px solid var(--border);
    border-radius: 12px;
    padding: 1.25rem;
  }
  .api-grid {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(280px, 1fr));
    gap: 0.75rem;
    margin-top: 0.75rem;
  }
  .api-item {
    background: var(--code-bg);
    padding: 0.65rem 0.85rem;
    border-radius: 6px;
    border: 1px solid var(--border);
    font-family: monospace;
    font-size: 0.8rem;
    display: flex;
    justify-content: space-between;
  }
  .api-method { font-weight: bold; }
  .method-get { color: #38bdf8; }
  .method-post { color: #34d399; }
  .method-delete { color: #f87171; }
</style>
</head>
<body>
<div class="container">
  <header>
    <div class="brand">
      <div class="logo-badge">LUA</div>
      <div class="brand-text">
        <h1>Lua C++ VM Web Server</h1>
        <p>Real-World Application & REST API running on custom C++ Lua engine</p>
      </div>
    </div>
    <div class="status-pill">
      <div class="status-dot"></div>
      <span>VM Online</span>
    </div>
  </header>

  <!-- Metrics Cards -->
  <div class="metrics-grid">
    <div class="metric-card">
      <div class="metric-title">Lua Version</div>
      <div class="metric-val" id="val-version">Lua 5.4</div>
    </div>
    <div class="metric-card">
      <div class="metric-title">Memory Allocation</div>
      <div class="metric-val" id="val-mem">-- KB</div>
    </div>
    <div class="metric-card">
      <div class="metric-title">Server Uptime</div>
      <div class="metric-val" id="val-uptime">-- s</div>
    </div>
    <div class="metric-card">
      <div class="metric-title">Requests Served</div>
      <div class="metric-val" id="val-reqs">--</div>
    </div>
  </div>

  <!-- Main Section: Note Editor & Notes List -->
  <div class="main-grid">
    <!-- Editor / Render Panel -->
    <div class="panel">
      <h2>Create Markdown Document</h2>
      <div class="form-group">
        <label for="note-title">Title</label>
        <input type="text" id="note-title" placeholder="Document title...">
      </div>
      <div class="form-group">
        <label for="note-content">Markdown Body</label>
        <textarea id="note-content" placeholder="Type Markdown here... e.g. # Title, **bold**, - list item"></textarea>
      </div>
      <div class="btn-row">
        <button class="btn-secondary" onclick="previewMarkdown()">Preview HTML</button>
        <button class="btn-primary" onclick="saveNote()">Save Note</button>
      </div>
      <div id="preview-box" style="display:none; margin-top: 0.5rem;" class="note-body"></div>
    </div>

    <!-- Live Notes Panel -->
    <div class="panel">
      <div style="display: flex; justify-content: space-between; align-items: center; border-bottom: 1px solid var(--border); padding-bottom: 0.75rem;">
        <h2 style="border-bottom: none; padding-bottom: 0;">Stored Documents (<span id="notes-count">0</span>)</h2>
        <button class="btn-secondary" style="padding: 0.35rem 0.75rem; font-size: 0.8rem;" onclick="loadNotes()">Refresh</button>
      </div>
      <div class="notes-list" id="notes-container">
        <!-- Rendered dynamically -->
      </div>
    </div>
  </div>

  <!-- REST API Documentation -->
  <div class="api-card">
    <h3 style="font-size: 0.95rem; text-transform: uppercase; color: var(--text-muted);">Interactive REST API Endpoints</h3>
    <div class="api-grid">
      <div class="api-item"><span class="api-method method-get">GET</span> <span>/api/info</span></div>
      <div class="api-item"><span class="api-method method-get">GET</span> <span>/api/notes</span></div>
      <div class="api-item"><span class="api-method method-post">POST</span> <span>/api/notes</span></div>
      <div class="api-item"><span class="api-method method-get">GET</span> <span>/api/notes/:id</span></div>
      <div class="api-item"><span class="api-method method-delete">DELETE</span> <span>/api/notes/:id</span></div>
      <div class="api-item"><span class="api-method method-post">POST</span> <span>/api/render</span></div>
      <div class="api-item"><span class="api-method method-get">GET</span> <span>/api/echo?param=1</span></div>
    </div>
  </div>
</div>

<script>
async function updateMetrics() {
  try {
    const res = await fetch('/api/info');
    const data = await res.json();
    document.getElementById('val-version').textContent = data.version;
    document.getElementById('val-mem').textContent = Math.round(data.memory_kb) + " KB";
    document.getElementById('val-uptime').textContent = Math.round(data.uptime_seconds) + " s";
    document.getElementById('val-reqs').textContent = data.requests_served;
  } catch(e) {
    console.error("Metrics fetch error", e);
  }
}

async function loadNotes() {
  try {
    const res = await fetch('/api/notes');
    const notes = await res.json();
    document.getElementById('notes-count').textContent = notes.length;
    const container = document.getElementById('notes-container');
    container.innerHTML = '';
    notes.forEach(note => {
      const el = document.createElement('div');
      el.className = 'note-item';
      el.innerHTML = `
        <div class="note-header">
          <span class="note-title">${note.title}</span>
          <span class="note-date">#${note.id} &bull; ${note.created_at}</span>
        </div>
        <div class="note-body">${note.html}</div>
        <div style="display: flex; justify-content: flex-end; margin-top: 0.25rem;">
          <button class="btn-danger" style="padding: 0.3rem 0.6rem; font-size: 0.75rem;" onclick="deleteNote(${note.id})">Delete</button>
        </div>
      `;
      container.appendChild(el);
    });
  } catch(e) {
    console.error("Notes fetch error", e);
  }
}

async function previewMarkdown() {
  const content = document.getElementById('note-content').value;
  if (!content) return;
  try {
    const res = await fetch('/api/render', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ markdown: content })
    });
    const data = await res.json();
    const pbox = document.getElementById('preview-box');
    pbox.innerHTML = data.html;
    pbox.style.display = 'block';
  } catch(e) {
    alert("Render error: " + e);
  }
}

async function saveNote() {
  const title = document.getElementById('note-title').value.trim();
  const content = document.getElementById('note-content').value.trim();
  if (!title || !content) {
    alert("Please provide both title and markdown content.");
    return;
  }
  try {
    const res = await fetch('/api/notes', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ title, content })
    });
    if (res.ok) {
      document.getElementById('note-title').value = '';
      document.getElementById('note-content').value = '';
      document.getElementById('preview-box').style.display = 'none';
      await loadNotes();
      await updateMetrics();
    } else {
      alert("Failed to save note");
    }
  } catch(e) {
    alert("Save error: " + e);
  }
}

async function deleteNote(id) {
  if (!confirm("Delete this document?")) return;
  try {
    const res = await fetch('/api/notes/' + id, { method: 'DELETE' });
    if (res.ok) {
      await loadNotes();
      await updateMetrics();
    }
  } catch(e) {
    alert("Delete error: " + e);
  }
}

updateMetrics();
loadNotes();
setInterval(updateMetrics, 5000);
</script>
</body>
</html>
]]
    return res:html(html)
end)

-- 2. System & VM Diagnostics Endpoint
app:get("/api/info", function(req, res)
    local mem_kb = collectgarbage("count")
    local uptime = os.clock() - server_start_time
    return res:json({
        version = _VERSION,
        platform = "C++ Lua VM",
        memory_kb = mem_kb,
        uptime_seconds = uptime,
        requests_served = total_requests,
        notes_count = #notes
    })
end)

-- Alias: /api/system
app:get("/api/system", function(req, res)
    return app.routes[2].handler(req, res)
end)

-- 3. List all notes with compiled HTML
app:get("/api/notes", function(req, res)
    local response_notes = {}
    for _, n in ipairs(notes) do
        table.insert(response_notes, {
            id = n.id,
            title = n.title,
            content = n.content,
            html = md.markdown(n.content),
            created_at = n.created_at
        })
    end
    return res:json(response_notes)
end)

-- 4. Create new note
app:post("/api/notes", function(req, res)
    local data = req:json()
    if not data or not data.title or not data.content then
        return res:status(400):json({ error = "Missing required fields: title and content" })
    end

    local new_note = {
        id = next_note_id,
        title = tostring(data.title),
        content = tostring(data.content),
        created_at = os.date("%Y-%m-%d %H:%M:%S")
    }
    next_note_id = next_note_id + 1
    table.insert(notes, new_note)

    local response_note = {
        id = new_note.id,
        title = new_note.title,
        content = new_note.content,
        html = md.markdown(new_note.content),
        created_at = new_note.created_at
    }
    return res:status(201):json(response_note)
end)

-- 5. Get note by ID
app:get("/api/notes/:id", function(req, res)
    local target_id = tonumber(req.params.id)
    if not target_id then
        return res:status(400):json({ error = "Invalid note ID" })
    end

    for _, n in ipairs(notes) do
        if n.id == target_id then
            return res:json({
                id = n.id,
                title = n.title,
                content = n.content,
                html = md.markdown(n.content),
                created_at = n.created_at
            })
        end
    end

    return res:status(404):json({ error = "Note not found", id = target_id })
end)

-- 6. Delete note by ID
app:delete("/api/notes/:id", function(req, res)
    local target_id = tonumber(req.params.id)
    if not target_id then
        return res:status(400):json({ error = "Invalid note ID" })
    end

    local found_idx = nil
    for idx, n in ipairs(notes) do
        if n.id == target_id then
            found_idx = idx
            break
        end
    end

    if found_idx then
        table.remove(notes, found_idx)
        return res:json({ success = true, deleted_id = target_id })
    else
        return res:status(404):json({ error = "Note not found", id = target_id })
    end
end)

-- 7. Standalone Markdown Rendering API
app:post("/api/render", function(req, res)
    local raw_md = ""
    local data = req:json()
    if data and data.markdown then
        raw_md = data.markdown
    elseif req.body and string.len(req.body) > 0 then
        raw_md = req.body
    end

    local rendered = md.markdown(raw_md)
    return res:json({
        html = rendered,
        input_length = string.len(raw_md)
    })
end)

-- 8. Echo Endpoint
app:get("/api/echo", function(req, res)
    return res:json({
        method = req.method,
        path = req.path,
        query = req.query,
        headers = req.headers,
        body = req.body
    })
end)

-- ------------------------------------------------------------------------------
-- Main Server Loop
-- ------------------------------------------------------------------------------
print("=====================================================")
print(" Lua C++ VM Real-World Web Application")
print(" Server starting on http://" .. HOST .. ":" .. PORT)
print("=====================================================")

local server = socket.create()
if not server then
    print("[ERROR] Failed to create socket")
    return
end

if not socket.bind(server, HOST, PORT) then
    print("[ERROR] Failed to bind socket to " .. HOST .. ":" .. PORT)
    socket.close(server)
    return
end

if not socket.listen(server, 20) then
    print("[ERROR] Failed to listen on socket")
    socket.close(server)
    return
end

print("[INFO] Server is actively listening for incoming connections.")
print("[INFO] Press Ctrl+C or stop process to shutdown.")

while true do
    local client = socket.accept(server)
    if client then
        local raw_request = socket.receive(client, 8192)
        if raw_request and string.len(raw_request) > 0 then
            total_requests = total_requests + 1
            local t0 = os.clock()

            local req = parse_request(raw_request)
            local res = create_response()

            if req then
                app:dispatch(req, res)
                local duration = (os.clock() - t0) * 1000
                print(string.format("[%s] %s %s -> %d (%d bytes, %.2f ms)",
                    os.date("%H:%M:%S"), req.method, req.path, res.status_code, string.len(res.body), duration))
            else
                res:status(400):send("Bad Request")
            end

            local payload = res:build()
            socket.send(client, payload)
        end
        socket.close(client)
    end
    -- Prevent CPU spinning when idle
    sleep(0.005)
end
