#include "Config.h"
#include "XPLMUtilities.h"
#include <fstream>
#include <string>
#include <sstream>

// ---------------------------------------------------------------------------
// Minimal single-value YAML reader.
// We only need "port: <number>" so we don't pull in a full yaml library.
// Lines starting with '#' are comments. Keys are trimmed.
// ---------------------------------------------------------------------------
static std::string trim(const std::string& s)
{
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return {};
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

bool Config::load(const std::string& pluginDir)
{
    std::string path = pluginDir + "\\config.yaml";
    std::ifstream f(path);
    if (!f.is_open()) {
        XPLMDebugString(("xplapi: config.yaml not found at " + path + ", using defaults\n").c_str());
        return false;
    }

    std::string line;
    while (std::getline(f, line)) {
        // strip comment
        auto cpos = line.find('#');
        if (cpos != std::string::npos) line = line.substr(0, cpos);

        auto colon = line.find(':');
        if (colon == std::string::npos) continue;

        std::string key = trim(line.substr(0, colon));
        std::string val = trim(line.substr(colon + 1));
        if (val.empty()) continue;

        if (key == "port") {
            try { m_port = std::stoi(val); }
            catch (...) {}
        }
    }

    XPLMDebugString(("xplapi: config loaded – port=" + std::to_string(m_port) + "\n").c_str());
    return true;
}
