#pragma once
#include <string>

// ---------------------------------------------------------------------------
// Config — reads config.yaml next to the plugin .xpl file
//
// YAML format (all fields optional):
//   port: 8012
// ---------------------------------------------------------------------------
class Config {
public:
    static constexpr int DEFAULT_PORT = 8012;

    Config() : m_port(DEFAULT_PORT) {}

    // Read config.yaml from the given directory path (no trailing slash).
    // Returns true if the file was found and parsed; false falls back to defaults.
    bool load(const std::string& pluginDir);

    int  port()    const { return m_port; }

private:
    int  m_port;
};
