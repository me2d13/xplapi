#pragma once
#include <string>
#include <vector>

class StaticFileServer {
public:
    // Initialize the static file server path and scan www directory
    void setPluginDir(const std::string& currentDir);

    // Determines if the request path maps to an existing static file.
    // If it does, reads the file and returns true (filling outBody and content type).
    bool serveStaticFile(const std::string& reqPath, std::string& outBody, std::string& outContentType, int& outStatusCode);

    bool hasIndexHtml() const { return m_hasIndexHtml; }
    const std::vector<std::string>& getHtmlFiles() const { return m_htmlFiles; }

private:
    std::string m_wwwPath;
    bool        m_hasIndexHtml { false };
    std::vector<std::string> m_htmlFiles;

    void        scanWwwDir();
    std::string getMimeType(const std::string& ext);
    bool        readFile(const std::string& path, std::string& content);
    static bool isHtmlExt(const std::string& name);
};
