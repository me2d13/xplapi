#include "WebServer.h"
#include "json.hpp"
#include "XPLMUtilities.h"
#include "XPLMDataAccess.h"
#include "plugin.h"
#include <WinSock2.h>

#include <sstream>
#include <string>
#include <vector>
#include <algorithm>
#include <cctype>
#include <cstring>

using json = nlohmann::json;

// ============================================================================
// HTTP request mini-parser
// ============================================================================
struct HttpRequest {
    std::string method;         // GET, POST, …
    std::string path;           // without query string
    std::string query;          // everything after '?'
    std::string body;           // request body (for POST)
    size_t      contentLength{ 0 };
};

static HttpRequest parseRequest(const char* raw, int len)
{
    HttpRequest req;
    std::string text(raw, len);

    // --- request line ---
    auto lineEnd = text.find("\r\n");
    if (lineEnd == std::string::npos) return req;
    std::istringstream rl(text.substr(0, lineEnd));
    std::string target;
    rl >> req.method >> target;

    auto qpos = target.find('?');
    if (qpos != std::string::npos) {
        req.path  = target.substr(0, qpos);
        req.query = target.substr(qpos + 1);
    } else {
        req.path = target;
    }

    // --- headers: extract Content-Length ---
    auto headersEnd = text.find("\r\n\r\n");
    if (headersEnd != std::string::npos) {
        std::string headers = text.substr(0, headersEnd);
        size_t clPos = headers.find("Content-Length:");
        if (clPos == std::string::npos) clPos = headers.find("content-length:");
        
        if (clPos != std::string::npos) {
            size_t valPos = headers.find_first_of("0123456789", clPos);
            if (valPos != std::string::npos) {
                req.contentLength = std::stoul(headers.substr(valPos));
            }
        }
        
        req.body = text.substr(headersEnd + 4);
    }

    return req;
}

// ============================================================================
// Helpers
// ============================================================================

// URL-decode a percent-encoded string
std::string WebServer::urlDecode(const std::string& s)
{
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size()) {
            int v = 0;
            std::istringstream ss(s.substr(i + 1, 2));
            ss >> std::hex >> v;
            out += static_cast<char>(v);
            i += 2;
        } else if (s[i] == '+') {
            out += ' ';
        } else {
            out += s[i];
        }
    }
    return out;
}

// Extract a named parameter from a query string  e.g. "name=foo&x=1" -> "foo"
std::string WebServer::queryParam(const std::string& query, const std::string& key)
{
    const std::string prefix = key + "=";
    size_t pos = 0;
    while (pos < query.size()) {
        size_t amp = query.find('&', pos);
        std::string token = query.substr(pos, amp == std::string::npos ? amp : amp - pos);
        if (token.find(prefix) == 0)
            return urlDecode(token.substr(prefix.size()));
        if (amp == std::string::npos) break;
        pos = amp + 1;
    }
    return {};
}

// Convert a DataRefEntry + name to a JSON value  {"name":..., "type":..., "value":...}
static json entryToJson(const std::string& name, const DataRefEntry& e)
{
    json j;
    j["name"] = name;

    if (!e.found) {
        j["error"] = "dataref not found";
        return j;
    }

    // Represent the XPLM type as a readable string
    if      (e.type & xplmType_FloatArray) j["type"] = "float[]";
    else if (e.type & xplmType_IntArray)   j["type"] = "int[]";
    else if (e.type & xplmType_Data)       j["type"] = "bytes";
    else if (e.type & xplmType_Double)     j["type"] = "double";
    else if (e.type & xplmType_Float)      j["type"] = "float";
    else if (e.type & xplmType_Int)        j["type"] = "int";
    else                                   j["type"] = "unknown";

    if (e.type & xplmType_FloatArray) {
        int n = e.count < 64 ? e.count : 64;
        json arr = json::array();
        for (int i = 0; i < n; i++) arr.push_back(e.value.fArrayValue[i]);
        j["value"] = arr;
    } else if (e.type & xplmType_IntArray) {
        int n = e.count < 64 ? e.count : 64;
        json arr = json::array();
        for (int i = 0; i < n; i++) arr.push_back(e.value.iArrayValue[i]);
        j["value"] = arr;
    } else if (e.type & xplmType_Data) {
        // Byte arrays: return as string if printable, else as base64-style hex
        j["value"] = std::string(e.value.cArrayValue);
    } else if (e.type & xplmType_Double) {
        j["value"] = (double)e.value.fValue;  // XPLM exposes doubles via float slot in SDK
    } else if (e.type & xplmType_Float) {
        j["value"] = e.value.fValue;
    } else if (e.type & xplmType_Int) {
        j["value"] = e.value.iValue;
    }

    return j;
}

// ============================================================================
// HTTP response writer
// ============================================================================
void WebServer::sendHttp(int sock, int status, const std::string& contentType,
                         const std::string& body)
{
    const char* reason = (status == 200) ? "OK"
                       : (status == 400) ? "Bad Request"
                       : (status == 404) ? "Not Found"
                       :                   "Error";

    std::ostringstream oss;
    oss << "HTTP/1.1 " << status << " " << reason << "\r\n"
        << "Content-Type: "   << contentType << "; charset=utf-8\r\n"
        << "Content-Length: " << body.size() << "\r\n"
        << "Cache-Control: no-cache, no-store\r\n"
        << "Access-Control-Allow-Origin: *\r\n"   // CORS — handy for browser clients
        << "Connection: close\r\n"
        << "\r\n"
        << body;

    std::string out = oss.str();
    sendToClient(sock, out.c_str(), (int)out.size());
}

void WebServer::sendRedirect(int sock, const std::string& location)
{
    std::ostringstream oss;
    oss << "HTTP/1.1 302 Found\r\n"
        << "Location: " << location << "\r\n"
        << "Content-Length: 0\r\n"
        << "Connection: close\r\n"
        << "\r\n";
    std::string out = oss.str();
    sendToClient(sock, out.c_str(), (int)out.size());
}

// ============================================================================
// Route handlers
// ============================================================================

// GET /api/dataref?name=<encoded-name>
std::string WebServer::handleGetOne(const std::string& name, int& statusCode)
{
    if (name.empty()) {
        statusCode = 400;
        return json{{"error", "missing 'name' query parameter"}}.dump(2);
    }

    // Ensure the registry knows about this name
    m_registry->ensureTracked(name);

    DataRefEntry entry;
    m_registry->readValue(name, entry);

    if (!entry.attempted) {
        // First time seeing this! Wait for up to 200ms for the flight loop to update.
        m_registry->waitForUpdate(200);
        // Try reading again after the wait
        m_registry->readValue(name, entry);
    }

    if (!entry.attempted) {
        // Still not resolved (maybe XP flight loop is paused or very slow)
        statusCode = 200;
        return json{{"name", name}, {"status", "pending"}}.dump(2);
    }

    statusCode = 200;
    return entryToJson(name, entry).dump(2);
}

// POST /api/dataref/get    body: {"name": "sim/cockpit/..."}
std::string WebServer::handleGetOneBody(const std::string& body, int& statusCode)
{
    try {
        auto j = json::parse(body);
        std::string name = j.value("name", std::string{});
        return handleGetOne(name, statusCode);
    } catch (const json::exception& e) {
        statusCode = 400;
        return json{{"error", std::string("invalid JSON: ") + e.what()}}.dump(2);
    }
}

// POST /api/dataref/getMultiple    body: ["sim/...", "sim/..."]
//   OR body: {"names": ["sim/...", "sim/..."]}
std::string WebServer::handleGetMultiple(const std::string& body, int& statusCode)
{
    try {
        auto j = json::parse(body);

        // Accept both a bare array and {"names": [...]}
        json names;
        if (j.is_array()) {
            names = j;
        } else if (j.is_object() && j.contains("names")) {
            names = j["names"];
        } else {
            statusCode = 400;
            return json{{"error", "body must be a JSON array or {\"names\":[...]}"}}.dump(2);
        }

        bool anyNew = false;
        for (const auto& nameVal : names) {
            if (!nameVal.is_string()) continue;
            std::string name = nameVal.get<std::string>();
            
            DataRefEntry entry;
            m_registry->readValue(name, entry);
            if (!entry.attempted) {
                m_registry->ensureTracked(name);
                anyNew = true;
            }
        }

        if (anyNew) {
            m_registry->waitForUpdate(200);
        }

        json result = json::array();
        for (const auto& nameVal : names) {
            if (!nameVal.is_string()) continue;
            std::string name = nameVal.get<std::string>();

            DataRefEntry entry;
            if (m_registry->readValue(name, entry)) {
                result.push_back(entryToJson(name, entry));
            } else if (!entry.attempted) {
                result.push_back(json{{"name", name}, {"status", "pending"}});
            } else {
                result.push_back(entryToJson(name, entry)); // Error object
            }
        }

        statusCode = 200;
        return result.dump(2);

    } catch (const json::exception& e) {
        statusCode = 400;
        return json{{"error", std::string("invalid JSON: ") + e.what()}}.dump(2);
    }
}

// POST /api/dataref/set    body: {"name": "sim/...", "value": 123}
std::string WebServer::handleSet(const std::string& body, int& statusCode)
{
    try {
        auto j = json::parse(body);
        if (!j.contains("name") || !j.contains("value")) {
            statusCode = 400;
            return json{{"error", "missing 'name' or 'value' field"}}.dump(2);
        }

        std::string name = j["name"];
        m_registry->queueWrite(name, j["value"]);
        m_registry->waitForUpdate(200);

        DataRefEntry entry;
        m_registry->readValue(name, entry);
        statusCode = 200;
        return entryToJson(name, entry).dump(2);

    } catch (const json::exception& e) {
        statusCode = 400;
        return json{{"error", std::string("invalid JSON: ") + e.what()}}.dump(2);
    }
}

// POST /api/dataref/setMultiple    body: [{"name":"sim/...", "value":123}, ...]
std::string WebServer::handleSetMultiple(const std::string& body, int& statusCode)
{
    try {
        auto j = json::parse(body);
        json writes;
        if (j.is_array()) writes = j;
        else if (j.is_object() && j.contains("writes")) writes = j["writes"];
        else {
            statusCode = 400;
            return json{{"error", "body must be array or {\"writes\":[...] "}}.dump(2);
        }

        std::vector<std::string> names;
        for (const auto& w : writes) {
            if (!w.is_object() || !w.contains("name") || !w.contains("value")) continue;
            std::string name = w["name"];
            m_registry->queueWrite(name, w["value"]);
            names.push_back(name);
        }

        m_registry->waitForUpdate(400); // Batch might take slightly longer to resolve all

        json result = json::array();
        for (const auto& name : names) {
            DataRefEntry entry;
            m_registry->readValue(name, entry);
            result.push_back(entryToJson(name, entry));
        }

        statusCode = 200;
        return result.dump(2);

    } catch (const json::exception& e) {
        statusCode = 400;
        return json{{"error", std::string("invalid JSON: ") + e.what()}}.dump(2);
    }
}

// POST /api/command/{once|begin|end}  body: {"name": "sim/..."}
std::string WebServer::handleCommand(const std::string& path, const std::string& body, int& statusCode)
{
    try {
        auto j = json::parse(body);
        if (!j.contains("name")) {
            statusCode = 400;
            return json{{"error", "missing 'name' field"}}.dump(2);
        }

        std::string commandName = j["name"];
        DataRefRegistry::CommandAction action = DataRefRegistry::CommandAction::Once;

        if (path.find("/once") != std::string::npos)       action = DataRefRegistry::CommandAction::Once;
        else if (path.find("/begin") != std::string::npos) action = DataRefRegistry::CommandAction::Begin;
        else if (path.find("/end") != std::string::npos)   action = DataRefRegistry::CommandAction::End;

        m_registry->queueCommand(commandName, action);
        
        statusCode = 200;
        return json{{"name", commandName}, {"status", "ok"}}.dump(2);

    } catch (const json::exception& e) {
        statusCode = 400;
        return json{{"error", std::string("invalid JSON: ") + e.what()}}.dump(2);
    }
}
std::string WebServer::handleStatusPage()
{
    auto entries = m_registry ? m_registry->snapshot() : std::vector<DataRefRegistry::SnapEntry>{};

    int resolved = 0, unresolved = 0;
    for (const auto& e : entries)
        e.found ? ++resolved : ++unresolved;

    std::ostringstream h;
    h << R"(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8"/>
<meta http-equiv="refresh" content="5"/>
<title>xplapi</title>
<style>
  *{box-sizing:border-box;margin:0;padding:0}
  body{font-family:'Consolas','Courier New',monospace;background:#0d1117;color:#c9d1d9;padding:24px;line-height:1.5}
  h1{color:#58a6ff;font-size:1.6em;margin-bottom:4px}
  .subtitle{color:#8b949e;font-size:.9em;margin-bottom:24px}
  h2{color:#79c0ff;font-size:1em;text-transform:uppercase;letter-spacing:.1em;
     border-bottom:1px solid #21262d;padding-bottom:6px;margin:24px 0 12px}
  .pills{display:flex;gap:10px;margin-bottom:20px;flex-wrap:wrap}
  .pill{background:#161b22;border:1px solid #30363d;border-radius:20px;
        padding:4px 14px;font-size:.85em;color:#8b949e}
  .pill span{color:#f0f6fc;font-weight:bold}
  .pill.ok  {border-color:#238636;color:#3fb950}
  .pill.ok span{color:#3fb950}
  a{color:#58a6ff;text-decoration:none}
  a:hover{text-decoration:underline}
  .nav{margin-bottom:24px;display:flex;gap:16px}
  .nav a{background:#161b22;border:1px solid #30363d;border-radius:6px;
         padding:5px 14px;font-size:.9em}
  table{width:100%;border-collapse:collapse;font-size:.88em}
  th{text-align:left;color:#6e7681;padding:6px 12px;font-weight:normal;
     border-bottom:1px solid #21262d}
  td{padding:6px 12px;border-bottom:1px solid #161b22;vertical-align:top}
  .dr-name{color:#e6edf3;font-family:monospace}
  .dr-type{color:#79c0ff;font-size:.82em}
  .dr-val {color:#3fb950;font-family:monospace;font-size:.88em;word-break:break-all}
  .badge-ok {display:inline-block;background:#0f2c18;color:#3fb950;
             border:1px solid #238636;border-radius:4px;padding:1px 8px;font-size:.78em}
  .badge-err{display:inline-block;background:#2d1316;color:#f85149;
             border:1px solid #6e2020;border-radius:4px;padding:1px 8px;font-size:.78em}
  .badge-pend{display:inline-block;background:#1c1c2e;color:#8b949e;
              border:1px solid #30363d;border-radius:4px;padding:1px 8px;font-size:.78em}
  .footer{margin-top:32px;color:#484f58;font-size:.78em}
  .empty{color:#484f58;font-style:italic;padding:12px}
</style>
</head>
<body>
<h1>xplapi</h1>
<p class="subtitle">X-Plane 12 REST API &mdash; datarefs over HTTP</p>
<div class="pills">
  <div class="pill ok"><span>&#x25cf;</span> running</div>
  <div class="pill">tracked <span>)" << entries.size() << R"(</span></div>
  <div class="pill">resolved <span>)" << resolved << R"(</span></div>
)";
    if (unresolved > 0)
        h << "  <div class=\"pill\">unresolved <span>" << unresolved << "</span></div>\n";

    h << R"(</div>
<div class="nav">
  <a href="/">Home</a>
  <a href="/state">Status</a>
  <a href="/api/dataref?name=sim/time/total_running_time_sec">&#128225; Try a dataref</a>
  <a href="#datarefs">&#128203; Datarefs</a>
  <a href="#commands">&#9000; Commands</a>
</div>
)";
    const auto& htmlFiles = m_staticServer.getHtmlFiles();
    if (!htmlFiles.empty()) {
        h << "<h2>Detected pages</h2>\n<div class=\"nav\">\n";
        for (const auto& f : htmlFiles) {
            std::string base = f;
            size_t dot = base.find_last_of('.');
            if (dot != std::string::npos) base = base.substr(0, dot);
            h << "  <a href=\"/" << f << "\">" << base << "</a>\n";
        }
        h << "</div>\n";
    }
    h << R"(
<h2>API endpoints</h2>
<table>
<tr><th>Method</th><th>Path</th><th>Body / query</th><th>Description</th></tr>
<tr><td>GET</td><td>/api/dataref</td><td>?name=sim/...</td><td>Read single dataref</td></tr>
<tr><td>POST</td><td>/api/dataref/get</td><td>{"name":"sim/..."}</td><td>Read single dataref</td></tr>
<tr><td>POST</td><td>/api/dataref/getMultiple</td><td>["sim/...", ...]</td><td>Read multiple datarefs</td></tr>
<tr><td>POST</td><td>/api/dataref/set</td><td>{"name":"...", "value":...}</td><td>Write single dataref</td></tr>
<tr><td>POST</td><td>/api/dataref/setMultiple</td><td>[{"name":"...", "value":...}, ...]</td><td>Write multiple datarefs</td></tr>
<tr><td>POST</td><td>/api/command/once</td><td>{"name":"sim/..."}</td><td>Trigger command once</td></tr>
<tr><td>POST</td><td>/api/command/begin</td><td>{"name":"sim/..."}</td><td>Begin held command</td></tr>
<tr><td>POST</td><td>/api/command/end</td><td>{"name":"sim/..."}</td><td>End held command</td></tr>
</table>
<h2 id="datarefs">Tracked datarefs ()" << entries.size() << R"()</h2>
)";

    if (entries.empty()) {
        h << "<p class=\"empty\">No datarefs tracked yet. Call /api/dataref?name=... to start.</p>\n";
    } else {
        h << "<table>\n<tr><th>Name</th><th>Type</th><th>Value</th><th>Status</th></tr>\n";
        for (const auto& e : entries) {
            h << "<tr><td class=\"dr-name\">" << e.name << "</td>";
            if (e.found) {
                std::string t = (e.type & xplmType_FloatArray) ? "float[" + std::to_string(e.count) + "]"
                              : (e.type & xplmType_IntArray)   ? "int[" + std::to_string(e.count) + "]"
                              : (e.type & xplmType_Data)       ? "bytes"
                              : (e.type & xplmType_Double)     ? "double"
                              : (e.type & xplmType_Float)      ? "float"
                              : (e.type & xplmType_Int)        ? "int"
                              :                                   "?";
                h << "<td class=\"dr-type\">" << t << "</td>"
                  << "<td class=\"dr-val\">" << e.valueDisplay << "</td>"
                  << "<td><span class=\"badge-ok\">resolved</span></td>";
            } else if (!e.attempted) {
                h << "<td></td><td></td><td><span class=\"badge-pend\">pending</span></td>";
            } else {
                h << "<td></td><td></td><td><span class=\"badge-err\">not found</span></td>";
            }
            h << "</tr>\n";
        }
        h << "</table>\n";
    }

    auto commands = m_registry ? m_registry->snapshotCommands() : std::vector<std::string>{};
    h << "<h2 id=\"commands\">Known Commands (" << commands.size() << ")</h2>\n";
    if (commands.empty()) {
        h << "<p class=\"empty\">No commands executed yet. Call /api/command/once to start.</p>\n";
    } else {
        h << "<table>\n<tr><th>Command Path</th></tr>\n";
        for (const auto& cmd : commands) {
            h << "<tr><td class=\"dr-name\">" << cmd << "</td></tr>\n";
        }
        h << "</table>\n";
    }

    h << "<p class=\"footer\">Auto-refreshes every 5 s &mdash; xplapi v" XPLAPI_VERSION "</p>\n"
      << "</body></html>";
    return h.str();
}

// ============================================================================
// Main dispatch — called by TcpListener on each incoming message
// ============================================================================
bool WebServer::onMessageReceived(int clientSocket, const char* msg, int length)
{
    // WebSocket: existing connection — handle frame
    if (m_wsManager.hasConnection(clientSocket)) {
        bool closed = m_wsManager.handleData(clientSocket, msg, length);
        return closed;  // true = connection was closed
    }

    // WebSocket: upgrade request
    if (length >= 20 && memcmp(msg, "GET ", 4) == 0) {
        const char* p = (const char*)memchr(msg, ' ', length);
        if (p && p - msg + 20 <= length && memcmp(p + 1, "/api/dataref/watch", 18) == 0) {
            if (strstr(msg, "Upgrade: websocket") || strstr(msg, "Upgrade: WebSocket")) {
                if (m_wsManager.tryUpgrade(clientSocket, msg, length))
                    return false;  // keep connection open
            }
        }
    }

    // HTTP
    if (!m_registry) {
        sendHttp(clientSocket, 500, "text/plain", "registry not initialised");
        closesocket(clientSocket);
        return true;
    }

    std::string firstPacket(msg, length);
    std::thread([this, clientSocket, firstPacket]() {
        this->processRequest(clientSocket, firstPacket);
        closesocket(clientSocket);
    }).detach();
    return true;
}

void WebServer::onClientDisconnected(int clientSocket)
{
    m_wsManager.removeConnection(clientSocket);
}

void WebServer::onLoopTick()
{
    for (int sock : m_wsManager.sendUpdates())
        removeClientSocket(sock);
}

void WebServer::processRequest(int clientSocket, const std::string& msgStr)
{
    HttpRequest req = parseRequest(msgStr.c_str(), (int)msgStr.length());

    // --- robust body reading for split packets (Invoke-RestMethod sends headers and body separately) ---
    // Only read body for methods that expect one — GET with spurious Content-Length would block forever
    if ((req.method == "POST" || req.method == "PUT" || req.method == "PATCH") &&
        req.contentLength > 0 && req.body.length() < req.contentLength) {
        size_t remaining = req.contentLength - req.body.length();
        char buf[2048];
        
        while (remaining > 0) {
            int received = recv(clientSocket, buf, (int)(std::min)(remaining, sizeof(buf)), 0);
            if (received <= 0) break; // socket closed or error
            req.body.append(buf, received);
            remaining -= received;
        }
    }

    // OPTIONS pre-flight for CORS
    if (req.method == "OPTIONS") {
        sendHttp(clientSocket, 200, "text/plain", "");
        return;
    }

    std::string body;
    std::string contentType = "application/json";
    int         statusCode  = 200;

    // ---- routes ----
    if (req.path == "/" || req.path.empty()) {
        if (m_staticServer.hasIndexHtml()) {
            bool ok = m_staticServer.serveStaticFile("/", body, contentType, statusCode);
            if (!ok) {
                sendRedirect(clientSocket, "/state");
                return;
            }
        } else {
            sendRedirect(clientSocket, "/state");
            return;
        }
    } else if (req.path == "/state") {
        body        = handleStatusPage();
        contentType = "text/html";

    } else if (req.method == "GET" && req.path == "/api/dataref") {
        std::string name = queryParam(req.query, "name");
        body = handleGetOne(name, statusCode);

    } else if (req.method == "POST" && req.path == "/api/dataref/get") {
        body = handleGetOneBody(req.body, statusCode);

    } else if (req.method == "POST" && req.path == "/api/dataref/getMultiple") {
        body = handleGetMultiple(req.body, statusCode);

    } else if (req.method == "POST" && req.path == "/api/dataref/set") {
        body = handleSet(req.body, statusCode);

    } else if (req.method == "POST" && req.path == "/api/dataref/setMultiple") {
        body = handleSetMultiple(req.body, statusCode);

    } else if (req.method == "POST" && req.path.find("/api/command/") == 0) {
        body = handleCommand(req.path, req.body, statusCode);

    } else {
        // Fallback to static file server
        bool isStatic = false;
        if (req.method == "GET") {
            isStatic = m_staticServer.serveStaticFile(req.path, body, contentType, statusCode);
        }

        if (!isStatic) {
            statusCode  = 404;
            contentType = "application/json";
            body        = json{{"error", "endpoint or file not found"}, {"path", req.path}}.dump(2);
        }
    }

    sendHttp(clientSocket, statusCode, contentType, body);
}
