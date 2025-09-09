#pragma once
#include <switch.h>
#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <cstring>
#include <cstdio>
#include <log.h>

#define CMD_UPDATE_CONF 1
static const char *CONFIG_FILE = "/config/pad-macro/config.ini";

typedef enum {
    FPS_30,
    FPS_60,
    FPS_120,
    FPS_240
} FPSOpt;

static FPSOpt string2FPSOpt(const std::string &s) {
    if (s.empty()) return FPS_60;
    if (!strcasecmp(s.c_str(), "FPS_30")) return FPS_30;
    if (!strcasecmp(s.c_str(), "FPS_60")) return FPS_60;
    if (!strcasecmp(s.c_str(), "FPS_120")) return FPS_120;
    if (!strcasecmp(s.c_str(), "FPS_240")) return FPS_240;
    return FPS_60;
}
static std::string FPSOpt2string(FPSOpt opt) {
    switch (opt) {
        case FPS_30: return "FPS_30";
        case FPS_60: return "FPS_60";
        case FPS_120: return "FPS_120";
        case FPS_240: return "FPS_240";
        default: return "FPS_60";
    }
}
struct PadConfig
{
    bool recorder_enable = false;
    bool player_enable = false;
    u64 recorder_btn = 0x0;
    u64 play_latest_btn = 0x0;
    FPSOpt recorder_fps = FPS_60;
    FPSOpt player_fps = FPS_60;
};

struct MacroItem
{
    u64 key_mask = 0;
    std::string file_path;
};

struct ConfigData
{
    PadConfig pad;
    std::vector<MacroItem> macros;
};

extern ConfigData g_config;
extern Service g_service;

u64 hexStringTo64(const std::string &hexStr)
{
    if (hexStr.empty())
        return 0;
    char *end = NULL;
    u64 value = strtoull(hexStr.c_str(), &end, 0);
    if (end == hexStr.c_str())
        return 0;
    while (*end == ' ' || *end == '\t')
        ++end;
    if (*end != 0)
        return 0;
    return value;
}
std::string u64ToHexString(u64 value)
{
    char buf[32];
    snprintf(buf, sizeof(buf), "0x%llx", (unsigned long long)value);
    return std::string(buf);
}
// 返回新字符串（两端去空白）
static inline std::string trim(const std::string &s)
{
    auto is_ws = [](unsigned char ch)
    { return std::isspace(ch); };
    auto it = std::find_if_not(s.begin(), s.end(), is_ws);
    auto rit = std::find_if_not(s.rbegin(), s.rend(), is_ws).base();
    if (it >= rit)
        return std::string();
    return std::string(it, rit);
}

static void loadConfig()
{
    // reset previous macros before loading
    g_config.macros.clear();
    /* Open Sd card filesystem. */
    FsFileSystem fsSdmc;
    if (R_FAILED(fsOpenSdCardFileSystem(&fsSdmc)))
        return;
    tsl::hlp::ScopeGuard fsGuard([&]
                                 { fsFsClose(&fsSdmc); });

    /* Open config file. */
    FsFile fileConfig;
    if (R_FAILED(fsFsOpenFile(&fsSdmc, CONFIG_FILE, FsOpenMode_Read, &fileConfig)))
        return;
    tsl::hlp::ScopeGuard fileGuard([&]
                                   { fsFileClose(&fileConfig); });

    /* Get config file size. */
    s64 configFileSize;
    if (R_FAILED(fsFileGetSize(&fileConfig, &configFileSize)))
        return;

    /* Read and parse config file. */
    std::string configFileData(configFileSize, '\0');
    u64 readSize;
    Result rc = fsFileRead(&fileConfig, 0, configFileData.data(), configFileSize, FsReadOption_None, &readSize);
    if (R_FAILED(rc) || readSize != static_cast<u64>(configFileSize))
        return;

    std::string section, line;
    // parse lines with a single istringstream (don't recreate it each iteration)
    std::istringstream iss(configFileData);
    while (std::getline(iss, line))
    {
        if (line.empty() || line[0] == ';' || line[0] == '#')
            continue;
        if (line[0] == '[')
        {
            auto end = line.find(']');
            if (end != std::string::npos && end > 1)
            {
                section = trim(line.substr(1, end - 1));
            }
            else
            {
                section.clear();
            }
            continue;
        }
        auto pos = line.find('=');
        if (pos == std::string::npos)
            continue;
        std::string key = trim(line.substr(0, pos));
        std::string value = trim(line.substr(pos + 1));
        if (section == "pad")
        {
            if (key == "recorder_enable")
                g_config.pad.recorder_enable = (value == "1" || value == "true");
            else if (key == "player_enable")
                g_config.pad.player_enable = (value == "1" || value == "true");
            else if (key == "recorder_btn")
                g_config.pad.recorder_btn = hexStringTo64(value);
            else if (key == "play_latest_btn")
                g_config.pad.play_latest_btn = hexStringTo64(value);
            else if (key == "recorder_fps")
                g_config.pad.recorder_fps = string2FPSOpt(value);
            else if (key == "player_fps")
                g_config.pad.player_fps = string2FPSOpt(value);
        }
        else if (section == "macros")
        {
            g_config.macros.push_back(MacroItem{hexStringTo64(key), value});
        }
    }
}

static void saveConfig()
{
    /* Open Sd card filesystem. */
    FsFileSystem fsSdmc;
    if (R_FAILED(fsOpenSdCardFileSystem(&fsSdmc)))
        return;
    tsl::hlp::ScopeGuard fsGuard([&]
                                 { fsFsClose(&fsSdmc); });
{
    /* Open (create/truncate) config file. */
    FsFile fileConfig;
    // Ensure parent directory exists (e.g. "config/pad-macro")
    (void)fsFsCreateDirectory(&fsSdmc, "/config/pad-macro");

    // Try opening file for write; if it doesn't exist create it first
    if (R_FAILED(fsFsOpenFile(&fsSdmc, CONFIG_FILE, FsOpenMode_Write | FsOpenMode_Append, &fileConfig)))
    {
        // Try creating file if it doesn't exist (size 0, flags 0)
        if (R_FAILED(fsFsCreateFile(&fsSdmc, CONFIG_FILE, 0, 0)))
            return;
        if (R_FAILED(fsFsOpenFile(&fsSdmc, CONFIG_FILE, FsOpenMode_Write | FsOpenMode_Append, &fileConfig)))
            return;
    }
    tsl::hlp::ScopeGuard fileGuard([&]
                                   { fsFileClose(&fileConfig); });

    std::string iniString = "[pad]\n";
    iniString += std::string("recorder_enable=") + (g_config.pad.recorder_enable ? "true" : "false") + "\n";
    iniString += std::string("player_enable=") + (g_config.pad.player_enable ? "true" : "false") + "\n";
    // recorder_btn saved as human-readable combo string
    iniString += std::string("recorder_btn=") + u64ToHexString(g_config.pad.recorder_btn) + "\n";
    // play_latest_btn saved as hex (so hexStringTo64 can accept 0x... or decimal)
    iniString += std::string("play_latest_btn=") + u64ToHexString(g_config.pad.play_latest_btn) + "\n";
    iniString += std::string("recorder_fps=") + FPSOpt2string(g_config.pad.recorder_fps) + "\n";
    iniString += std::string("player_fps=") + FPSOpt2string(g_config.pad.player_fps) + "\n";
    iniString += "[macros]\n";

    // write macros
    for (const auto &m : g_config.macros)
    {
        if (m.key_mask == 0 || m.file_path.empty())
            continue;
        log_info("%s=%s", u64ToHexString(m.key_mask).c_str(), m.file_path.c_str());
        iniString += (u64ToHexString(m.key_mask) + "=" + m.file_path + "\n");
    }

    Result rc = fsFileWrite(&fileConfig, 0, iniString.c_str(), iniString.length(), FsWriteOption_Flush);
    if (R_FAILED(rc)) {
        // 写失败：记录/处理
        log_error("fsFileWrite failed: 0x%x", rc);
        return;
    }
    // 确保文件大小和内容一致（防止旧尾部残留）
    rc = fsFileSetSize(&fileConfig, (s64)iniString.length());
    if (R_FAILED(rc)) {
        log_error("fsFileSetSize failed: 0x%x", rc);
        return;
    }
}
    // 通知 sysmodule 配置已更新
    Result rc = serviceDispatch(&g_service, CMD_UPDATE_CONF);
    if (R_FAILED(rc)) {
        log_error("serviceDispatch failed: 0x%x", rc);
        return;
    }
    log_info("Configuration saved");
}