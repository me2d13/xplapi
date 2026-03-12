#include "StaticFileServer.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <windows.h>

void StaticFileServer::setPluginDir(const std::string& currentDir)
{
    m_wwwPath = currentDir;
    // ensure trailing slash or backslash
    if (!m_wwwPath.empty() && m_wwwPath.back() != '/' && m_wwwPath.back() != '\\') {
        m_wwwPath += '\\';
    }
    // The plugin DLL is in 'plugins/xplapi/64/', so we go up one level to 'plugins/xplapi/www/'
    m_wwwPath += "..\\www\\";
    scanWwwDir();
}

bool StaticFileServer::isHtmlExt(const std::string& name)
{
    if (name.size() >= 5) {
        std::string ext = name.substr(name.size() - 5);
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c){ return std::tolower(c); });
        if (ext == ".html") return true;
    }
    if (name.size() >= 4) {
        std::string ext = name.substr(name.size() - 4);
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c){ return std::tolower(c); });
        if (ext == ".htm") return true;
    }
    return false;
}

void StaticFileServer::scanWwwDir()
{
    m_hasIndexHtml = false;
    m_htmlFiles.clear();

    std::string searchPath = m_wwwPath + "*";
    std::replace(searchPath.begin(), searchPath.end(), '/', '\\');

    WIN32_FIND_DATAA fd{};
    HANDLE h = FindFirstFileA(searchPath.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;

    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        std::string name = fd.cFileName;
        if (isHtmlExt(name)) {
            m_htmlFiles.push_back(name);
            std::string lower = name;
            std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c){ return std::tolower(c); });
            if (lower == "index.html" || lower == "index.htm")
                m_hasIndexHtml = true;
        }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
}

bool StaticFileServer::readFile(const std::string& path, std::string& content)
{
    std::ifstream fs(path, std::ios::in | std::ios::binary);
    if (!fs.is_open()) return false;
    
    std::ostringstream ss;
    ss << fs.rdbuf();
    content = ss.str();
    return true;
}

std::string StaticFileServer::getMimeType(const std::string& ext)
{
    if (ext == ".html" || ext == ".htm") return "text/html";
    if (ext == ".css") return "text/css";
    if (ext == ".js") return "application/javascript";
    if (ext == ".json") return "application/json";
    if (ext == ".png") return "image/png";
    if (ext == ".jpg" || ext == ".jpeg") return "image/jpeg";
    if (ext == ".gif") return "image/gif";
    if (ext == ".svg") return "image/svg+xml";
    if (ext == ".ico") return "image/x-icon";
    if (ext == ".txt") return "text/plain";
    return "application/octet-stream";
}

bool StaticFileServer::serveStaticFile(const std::string& reqPath, std::string& outBody, std::string& outContentType, int& outStatusCode)
{
    // Basic security: block directory traversal
    if (reqPath.find("..") != std::string::npos) {
        return false;
    }

    // Default to index.html if root is requested
    std::string safePath = reqPath;
    if (safePath == "/" || safePath.empty()) {
        safePath = "/index.html"; 
    }

    std::string fullPath = m_wwwPath + safePath.substr(1); // skip leading '/'

    // Fix path separators for Windows
    std::replace(fullPath.begin(), fullPath.end(), '/', '\\');

    if (!readFile(fullPath, outBody)) {
        return false; // Not found -> let WebServer handle it (might be API or 404)
    }

    // Determine extension
    std::string ext = "";
    size_t dotPos = fullPath.find_last_of('.');
    if (dotPos != std::string::npos) {
        ext = fullPath.substr(dotPos);
        // lowercase the extension
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c){ return std::tolower(c); });
    }

    outContentType = getMimeType(ext);
    outStatusCode = 200;
    
    return true;
}
