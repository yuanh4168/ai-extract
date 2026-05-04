#include "config.h"
#include <fstream>

bool Config::load(const std::string& path) {
    std::ifstream in(path);
    if (!in) return false;
    std::string line;
    while (std::getline(in, line)) {
        size_t pos = line.find('#');
        if (pos != std::string::npos) line.erase(pos);
        pos = line.find_first_not_of(" \t\r\n");
        if (pos == std::string::npos) continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);
        auto trimKey = [](std::string& s) {
            s.erase(0, s.find_first_not_of(" \t"));
            s.erase(s.find_last_not_of(" \t") + 1);
        };
        trimKey(key);
        trimKey(val);
        if (key == "default_mode") defaultMode = val;
        else if (key == "output_dir") outDir = val;
        else if (key == "startup_working_dir") startupDir = val;
        else if (key == "force") force = (val == "1" || val == "true");
        else if (key == "debug") debug = (val == "1" || val == "true");
        else if (key == "no_backup") noBackup = (val == "1" || val == "true");
        else if (key == "file_read_mode") fileReadMode = val;
        else if (key == "confirm_exec") confirmExec = (val == "1" || val == "true");
        else if (key == "max_clipboard_size") maxClipboardSize = std::stoul(val);
        else if (key == "max_read_size") maxReadSize = std::stoul(val);
    }
    return true;
}

void Config::save(const std::string& path) const {
    std::ofstream out(path);
    out << "# ai-extract configuration\n";
    out << "default_mode=" << defaultMode << "\n";
    out << "output_dir=" << outDir << "\n";
    out << "startup_working_dir=" << startupDir << "\n";
    out << "force=" << (force ? "true" : "false") << "\n";
    out << "debug=" << (debug ? "true" : "false") << "\n";
    out << "no_backup=" << (noBackup ? "true" : "false") << "\n";
    out << "file_read_mode=" << fileReadMode << "\n";
    out << "confirm_exec=" << (confirmExec ? "true" : "false") << "\n";
    out << "max_clipboard_size=" << maxClipboardSize << "\n";
    out << "max_read_size=" << maxReadSize << "\n";
}