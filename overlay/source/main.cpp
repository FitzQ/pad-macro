// ...existing code...
#define TESLA_INIT_IMPL
#ifndef __DEBUG__
#define __DEBUG__ 0
#endif

#include <tesla.hpp>
#include <switch.h>
#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <cstring>
#include <cstdio>
#include <log.h>
#include "gui/MacroFileView.hpp"
#include "gui/Store.hpp"
#include "i18n.hpp"


#define PROGRAM_NAME "pad-macro"
#define PROGRAM_ID 0x0100000000C0FFEE
#define CMD_UPDATE_CONF 1
#define CMD_EXIT 999


// 配置文件路径
// path relative to the opened SdCard FsFileSystem (do NOT include leading '/').
static const char *CONFIG_FILE = "/config/pad-macro/config.ini";
static const char *MACROS_DIR = "/switch/pad-macro/macros";

// 配置数据结构
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

static ConfigData g_config;
static Service g_service;
static bool g_serviceConnected = false;
static Mutex programSwitchMutex = 0;

bool isProgramRunning()
{
    u64 pid = 0;
    if (R_FAILED(pmdmntGetProcessId(&pid, PROGRAM_ID)))
        return false;

    return pid > 0;
}

void startProgram()
{
    mutexLock(&programSwitchMutex);
    if (isProgramRunning()) {
        mutexUnlock(&programSwitchMutex);
        log_info("program already running");
        return;
    }
    const NcmProgramLocation programLocation{
        .program_id = PROGRAM_ID,
        .storageID = NcmStorageId_None,
    };
    u64 pid = 0;
    Result rc = pmshellLaunchProgram(0, &programLocation, &pid);
    if (R_FAILED(rc)) {
        mutexUnlock(&programSwitchMutex);
        log_error("pmshellLaunchProgram failed: 0x%x", rc);
        return;
    }
    mutexUnlock(&programSwitchMutex);
    log_info("launched program, pid=%llu", pid);
}

void terminateProgram()
{
    mutexLock(&programSwitchMutex);
    if (!isProgramRunning()) {
        mutexUnlock(&programSwitchMutex);
        log_info("program not running");
        return;
    }
    // Send exit command to the service
    Result rc = serviceDispatch(&g_service, CMD_EXIT);
    if (R_FAILED(rc)) {
        mutexUnlock(&programSwitchMutex);
        log_error("serviceDispatch failed: 0x%x", rc);
        return;
    }
    // Kill the program
    // Result rc = pmshellTerminateProgram(PROGRAM_ID);
    // if (R_FAILED(rc)) {
    //     mutexUnlock(&programSwitchMutex);
    //     log_error("pmshellTerminateProgram failed: 0x%x", rc);
    //     return;
    // }
    mutexUnlock(&programSwitchMutex);
    log_info("terminated program");
}

// 按键名映射辅助
std::string maskToComboString(u64 mask)
{
    return tsl::hlp::keysToComboString(mask);
}

u64 comboStringToMask(const std::string &combo)
{
    return tsl::hlp::comboStringToKeys(combo);
}

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

static const char* stringToGlyph(const std::string &value) {
    for (const tsl::impl::KeyInfo &keyInfo : tsl::impl::KEYS_INFO) {
        if (strcasecmp(value.c_str(), keyInfo.name) == 0)
            return keyInfo.glyph;
    }
    return nullptr;
}
static std::string comboStringToGlyph(const std::string &value) {
    std::string glyphCombo;
    for (std::string key : tsl::hlp::split(value, '+')) {
        if (!glyphCombo.empty())
            glyphCombo += "\u002B"; // \u002B
        glyphCombo += stringToGlyph(key);
    }
    return glyphCombo;
}

bool isUsedCombo(u64 mask)
{
    // treat a combo as "used" when it equals, contains, or is contained by any existing combo
    // This prevents choosing a combo that would shadow or be shadowed by an existing mapping.
    auto check = [&](u64 existing) -> bool {
        if (existing == 0) return false;
        if (existing == mask) return true; // exact match
        if ((mask & existing) == existing) return true; // candidate contains existing (candidate is superset)
        if ((mask & existing) == mask) return true; // candidate is subset of existing
        return false;
    };

    if (check(g_config.pad.recorder_btn)) return true;
    if (check(g_config.pad.play_latest_btn)) return true;

    for (const auto &m : g_config.macros)
    {
        if (check(m.key_mask)) return true;
    }
    return false;
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

static Result delFile(const char *path) {
    /* Open Sd card filesystem. */
    FsFileSystem fsSdmc;
    if (R_FAILED(fsOpenSdCardFileSystem(&fsSdmc)))
        return -1;
    tsl::hlp::ScopeGuard fsGuard([&]
                                 { fsFsClose(&fsSdmc); });
    Result rc = fsFsDeleteFile(&fsSdmc, path);
    if (R_FAILED(rc)) {
        log_error("fsFsDeleteFile failed: 0x%x", rc);
        return rc;
    }
    log_info("Configuration file deleted");
    return 0;
}

const std::array<std::string, 42> KEY_COMBO_LIST = {
    "ZL+ZR", "ZL+ZR+DLEFT", "ZL+ZR+DUP", "ZL+ZR+DRIGHT", "ZL+ZR+DDOWN",
    "L+R", "L+R+DLEFT", "L+R+DUP", "L+R+DRIGHT", "L+R+DDOWN",
    "L+A", "L+B", "L+X", "L+Y", "L+DLEFT", "L+DUP", "L+DRIGHT", "L+DDOWN",
    "ZL+A", "ZL+B", "ZL+X", "ZL+Y", "ZL+DLEFT", "ZL+DUP", "ZL+DRIGHT", "ZL+DDOWN",
    "DLEFT+A", "DLEFT+B", "DLEFT+X", "DLEFT+Y", "DUP+A", "DUP+B", "DUP+X", "DUP+Y",
    "DRIGHT+A", "DRIGHT+B", "DRIGHT+X", "DRIGHT+Y", "DDOWN+A", "DDOWN+B", "DDOWN+X", "DDOWN+Y"
};


class GuiMacroFileList : public tsl::Gui
{
private:
    // pointer to the currently-displayed ListItem for the selected combo
    tsl::elm::ListItem *item;
    std::string *path;

public:
    GuiMacroFileList(tsl::elm::ListItem *item, std::string *path) : item(item), path(path) {
        log_info("GuiMacroFileList created, item=%p:%s path=%p:%s", (void*)item, item ? item->getText().c_str() : "(null)", (void*)path, path ? path->c_str() : "(null)");
    }

    virtual tsl::elm::Element *createUI() override
    {

        auto *rootFrame = new tsl::elm::OverlayFrame(i18n_getString("A001"),
            i18n_getString("A002")
            + "\n" + i18n_getString("A00W")
            + "\n\uE0E0 " + i18n_getString("A00J") + " | \uE0E2 " + i18n_getString("A00K") + " | \uE0E3 " + i18n_getString("A00L"),
            std::string("\uE0E1  ")+i18n_getString("A00H")+std::string("     \uE0E0  ")+i18n_getString("A00J")+std::string("     \uE0E5  ")+i18n_getString("A00R"));
        /* Open Sd card filesystem. */
        FsFileSystem fsSdmc;
        if (R_FAILED(fsOpenSdCardFileSystem(&fsSdmc)))
            return rootFrame;
        tsl::hlp::ScopeGuard fsGuard([&]
                                    { fsFsClose(&fsSdmc); });

        // 假设 fs 已经用 fsOpenSdCardFileSystem(&fs) 打开
        FsDir dir;
        if (R_FAILED(fsFsOpenDirectory(&fsSdmc, MACROS_DIR, FsDirOpenMode_ReadDirs | FsDirOpenMode_ReadFiles, &dir)))
            return rootFrame;
        const int BUF_SIZE = 50;
        FsDirectoryEntry entries[BUF_SIZE];
        s64 entries_read = 0;
        if (R_FAILED(fsDirRead(&dir, &entries_read, BUF_SIZE, entries)))
            return rootFrame;
        tsl::hlp::ScopeGuard fileGuard([&]
                                    { fsDirClose(&dir); });
        auto list = new tsl::elm::List();

    for (s64 i = 0; i < entries_read; ++i)
        {
            if (std::string(entries[i].name) == "latest.bin")
                continue;

            // show combo name and a select label on the right (two-arg constructor)
            std::string absPath = std::string(MACROS_DIR) + "/" + entries[i].name;
            auto *listItem = new tsl::elm::ListItem(entries[i].name, (absPath == *path) ? std::string("\u25CF") : std::string(""));

            listItem->setClickListener([this, list, listItem, absPath](u64 keys) {
                // A to select
                if (keys & HidNpadButton_A) {
                    *this->path = absPath;
                    saveConfig();
                    std::string fileName = tsl::hlp::split(*this->path, '/').back();
                    this->item->setText(fileName);
                    tsl::goBack();
                    return true;
                // Y to rename
                } else if (keys & HidNpadButton_Y) {
                    // todo
                    tsl::changeTo<GuiMacroFileView>(listItem, absPath);
                    return true;
                // X to delete
                } else if (keys & HidNpadButton_X) {
                    log_info("deleting %s", absPath.c_str());
                    Result rc = delFile(absPath.c_str());
                    log_info("fsFsDeleteFile returned 0x%x", rc);
                    listItem->setText("deleted");
                    listItem->setClickListener([](u64){return false;});
                    return true;
                }
                return false;
            });
            list->addItem(listItem);
        }
        rootFrame->setContent(list);

        rootFrame->setClickListener([this](u64 keys){
            if (keys & HidNpadButton_R) {
                // R button pressed
                log_info("R button pressed");
                tsl::changeTo<Store>();
                return true;
            }
            return false;
        });
        return rootFrame;
    }
};


class GuiKeyComboList : public tsl::Gui
{
private:
    // pointer to the currently-displayed ListItem for the selected combo
    tsl::elm::ListItem *item;
    u64 *mask;

public:
    GuiKeyComboList(tsl::elm::ListItem *item, u64 *mask) : item(item), mask(mask) {
        log_info("GuiKeyComboList created, item=%p:%s mask=%p:%llx", (void*)item, item ? item->getText().c_str() : "(null)", (void*)mask, mask ? *mask : 0);
    }

    virtual tsl::elm::Element *createUI() override
    {
        log_info("GuiKeyComboList createUI");
        auto *rootFrame = new tsl::elm::OverlayFrame(i18n_getString("A001"),
            i18n_getString("A003")+"\n"+i18n_getString("A00M")+" | \uE0E0 "+i18n_getString("A00J"),
            "\uE0E1  "+i18n_getString("A00H")+"     \uE0E0  "+i18n_getString("A00J"));
        auto list = new tsl::elm::List();
        for (size_t i = 0; i < KEY_COMBO_LIST.size(); i++)
        {
            log_info("list %zu/%zu", i, KEY_COMBO_LIST.size());
            // compute candidate mask once
            u64 candidate = comboStringToMask(KEY_COMBO_LIST[i]);
            // skip combos that are already used elsewhere, but allow the currently selected combo
            if (candidate != *mask && isUsedCombo(candidate))
                continue;

            // show combo name and a select label on the right (two-arg constructor)
            auto *listItem = new tsl::elm::ListItem(comboStringToGlyph(KEY_COMBO_LIST[i]), (candidate == *mask) ? std::string("\u25CF") : std::string(""));

            // capture listItem and candidate so the lambda can update the UI element
            listItem->setClickListener([this, i, listItem, candidate](u64 keys) {
                if (keys & HidNpadButton_A) {
                    *this->mask = candidate;
                    saveConfig();
                    this->item->setValue(comboStringToGlyph(maskToComboString(*this->mask)));
                    tsl::goBack();
                    return true;
                }
                return false;
            });
            list->addItem(listItem);
        }
        rootFrame->setContent(list);

        return rootFrame;
    }
};

class GuiTest : public tsl::Gui
{
private:
    tsl::elm::OverlayFrame *frame;
    tsl::elm::List *list;
public:
    GuiTest() {}

    virtual tsl::elm::Element *createUI() override
    {
        log_debug("GuiTest createUI, g_serviceConnected=%d", g_serviceConnected);
        frame = new tsl::elm::OverlayFrame(i18n_getString("A001"), "v2.0.0",
            "\uE0E1  "+i18n_getString("A00H")+"     \uE0E0  "+i18n_getString("A00J"));
        list = new tsl::elm::List();
        initContent();
        frame->setContent(list);
        log_debug("GuiTest createUI done");
        return frame;
    }

    virtual void update() override {
        if (!g_serviceConnected && isProgramRunning()) {
            log_debug("program started, try to get service");
            Result rc = smGetService(&g_service, "padmacro");
            log_debug("smGetService returned %d", rc);
            g_serviceConnected = R_SUCCEEDED(rc);
            if (g_serviceConnected) {
                initContent();
            }
        } else if (g_serviceConnected && !isProgramRunning())
        {
            log_debug("program stopped, releasing service");
            g_serviceConnected = false;
            g_service = {0};
            initContent();
        }
    }

    virtual bool handleInput(u64 keysDown, u64 keysHeld, const HidTouchState &touchPos, HidAnalogStickState joyStickPosLeft, HidAnalogStickState joyStickPosRight) override
    {

        return false;
    }

    virtual void initContent() {
        log_debug("Initializing content...");
        tsl::Gui::removeFocus(nullptr);
        list->clear();
        auto pmTLI = new tsl::elm::ToggleListItem(i18n_getString("A004"), isProgramRunning(), i18n_getString("A005"), i18n_getString("A006"));
        pmTLI->setStateChangedListener([this, pmTLI](bool state) {
            pmTLI->setState(state); // keep them in sync
            if (state) {
                startProgram();
            } else {
                terminateProgram();
            }
        });
        list->addItem(pmTLI);
        if (!g_serviceConnected) {
            log_debug("service not available");
            return;
        }

        // pad section
        list->addItem(new tsl::elm::CategoryHeader(i18n_getString("A007")));
        // recorder_enable toggle
        tsl::elm::ToggleListItem *recorderEnableToggleListItem = new tsl::elm::ToggleListItem(i18n_getString("A008"), g_config.pad.recorder_enable, i18n_getString("A005"), i18n_getString("A006"));
        recorderEnableToggleListItem->setStateChangedListener([](bool state){
            g_config.pad.recorder_enable = state;
            saveConfig();
        });
        list->addItem(recorderEnableToggleListItem);
        // player_enable toggle
        tsl::elm::ToggleListItem *playerEnableToggleListItem = new tsl::elm::ToggleListItem(i18n_getString("A009"), g_config.pad.player_enable, i18n_getString("A005"), i18n_getString("A006"));
        playerEnableToggleListItem->setStateChangedListener([](bool state){
            g_config.pad.player_enable = state;
            saveConfig();
        });
        list->addItem(playerEnableToggleListItem);
        // recorder_btn 按键组合
        tsl::elm::ListItem *recorderBtnListItem = new tsl::elm::ListItem(i18n_getString("A00A"), comboStringToGlyph(maskToComboString(g_config.pad.recorder_btn)));
        recorderBtnListItem->setClickListener([recorderBtnListItem](u64 keys) {
            if (keys & HidNpadButton_A) {
                // open recording gui (pass address so Gui can modify the value)
                tsl::changeTo<ComboSet>(recorderBtnListItem, nullptr, &g_config.pad.recorder_btn);
                log_info("when this code excuted?");
                return true;
            }
            return false; });
        list->addItem(recorderBtnListItem);
        // play_latest_btn 按键组合
        tsl::elm::ListItem *playLatestBtnListItem = new tsl::elm::ListItem(i18n_getString("A00B"), comboStringToGlyph(maskToComboString(g_config.pad.play_latest_btn)));
        playLatestBtnListItem->setClickListener([playLatestBtnListItem](u64 keys) {
            if (keys & HidNpadButton_A) {
                // open recording gui (pass address so Gui can modify the value)
                tsl::changeTo<ComboSet>(playLatestBtnListItem, nullptr, &g_config.pad.play_latest_btn);
                log_info("when this code excuted?");
                return true;
            }
            return false; });
        list->addItem(playLatestBtnListItem);
        log_debug("list pad section initialized");
        // recorder_fps
        std::initializer_list<std::string> fpsOptions = { FPSOpt2string(FPS_30), FPSOpt2string(FPS_60), FPSOpt2string(FPS_120), FPSOpt2string(FPS_240) };
        std::vector<std::string> fpsOptionsVec(fpsOptions.begin(), fpsOptions.end());
        tsl::elm::NamedStepTrackBar *recorderFpsListItem = new tsl::elm::NamedStepTrackBar("\uE0F5", fpsOptions);
        recorderFpsListItem->setProgress(std::distance(fpsOptionsVec.begin(), std::find(fpsOptionsVec.begin(), fpsOptionsVec.end(), FPSOpt2string(g_config.pad.recorder_fps))));
        recorderFpsListItem->setValueChangedListener([fpsOptionsVec](u8 value) {
            log_debug("recorderFpsListItem value changed to %d", fpsOptionsVec[value]);
            FPSOpt opt = string2FPSOpt(fpsOptionsVec[value].c_str());
            g_config.pad.recorder_fps = opt;
            saveConfig();
        });
        list->addItem(recorderFpsListItem);
        // player_fps
        tsl::elm::NamedStepTrackBar *playerFpsListItem = new tsl::elm::NamedStepTrackBar("\uE076", fpsOptions);
        playerFpsListItem->setProgress(std::distance(fpsOptionsVec.begin(), std::find(fpsOptionsVec.begin(), fpsOptionsVec.end(), FPSOpt2string(g_config.pad.player_fps))));
        playerFpsListItem->setValueChangedListener([fpsOptionsVec](u8 value) {
            log_debug("playerFpsListItem value changed to %d", fpsOptionsVec[value]);
            FPSOpt opt = string2FPSOpt(fpsOptionsVec[value].c_str());
            g_config.pad.player_fps = opt;
            saveConfig();
        });
        list->addItem(playerFpsListItem);

        // macros section
        list->addItem(new tsl::elm::CategoryHeader(i18n_getString("A00C")+" | \uE0E0 "+i18n_getString("A00F") + " | \uE0E3 " + i18n_getString("A00E") + " | \uE0E2 " + i18n_getString("A00K"), true));
        // 显示所有宏映射
        for (size_t i = 0; i < g_config.macros.size(); ++i)
        {
            std::string fileName = tsl::hlp::split(g_config.macros[i].file_path, '/').back();
            tsl::elm::ListItem *macroListItem = new tsl::elm::ListItem(fileName, comboStringToGlyph(maskToComboString(g_config.macros[i].key_mask)));
            macroListItem->setClickListener([this, macroListItem, i](u64 keys)
                                             {
                // edit macro file
                if (keys & HidNpadButton_A) {
                    // pass pointer to the actual vector element's file_path (non-const)
                    tsl::changeTo<GuiKeyComboList>(macroListItem, &g_config.macros[i].key_mask);
                    return true;
                }
                // edit btn mask
                if (keys & HidNpadButton_Y) {
                    tsl::changeTo<GuiMacroFileList>(macroListItem, &g_config.macros[i].file_path);
                    return true;
                }
                // Delete
                if (keys & HidNpadButton_X) {
                    g_config.macros.erase(g_config.macros.begin() + i);
                    saveConfig();
                    this->list->removeItem(macroListItem);
                    return true;
                }
                return false; });
            list->addItem(macroListItem);
        }
        // 新增宏映射
        tsl::elm::ListItem *addMacroItem = new tsl::elm::ListItem(i18n_getString("A00D"), "+");
        addMacroItem->setClickListener([this, addMacroItem](u64 keys) mutable {
            if (keys & HidNpadButton_A) {
                g_config.macros.push_back(MacroItem{0x0, ""});
                size_t idx = g_config.macros.size() - 1;
                tsl::elm::ListItem *newMacroListItem = new tsl::elm::ListItem("\uE0A3 "+i18n_getString("A00E"), "\uE0A0 "+i18n_getString("A00F"));
                newMacroListItem->setClickListener([this, newMacroListItem, idx](u64 keys) {
                        // edit macro file
                        if (keys & HidNpadButton_A) {
                            tsl::changeTo<GuiKeyComboList>(newMacroListItem, &g_config.macros[idx].key_mask);
                            return true;
                        }
                        // edit btn mask
                        if (keys & HidNpadButton_Y) {
                            tsl::changeTo<GuiMacroFileList>(newMacroListItem, &g_config.macros[idx].file_path);
                            return true;
                        }
                        // Delete
                        if (keys & HidNpadButton_X) {
                            g_config.macros.erase(g_config.macros.begin() + idx);
                            saveConfig();
                            newMacroListItem->setText(i18n_getString("A00G"));
                            newMacroListItem->setClickListener([](u64){ return false; });
                            return true;
                        }
                        return false; });
                this->list->addItem(newMacroListItem, 0, this->list->getIndexInList(addMacroItem));
                tsl::Overlay::get()->getCurrentGui()->requestFocus(newMacroListItem, tsl::FocusDirection::None);
                return true;
            }
            return false; });
        list->addItem(addMacroItem);
        log_debug("list macros section initialized");
        log_debug("Content initialized");
    }
};

class OverlayTest : public tsl::Overlay
{
public:
    // libtesla already initialized fs, hid, pl, pmdmnt, hid:sys and set:sys

    // Called at the end to clean up all services previously initialized
    virtual void exitServices() override {
        log_info("OverlayTest exitServices");
        timeExit();
        socketExit();
        if(g_serviceConnected){
            serviceClose(&g_service);
        }
        smExit();
        setExit();
        fsdevUnmountAll();
        fsExit();
        pmshellExit();
    }

    virtual void initServices() override
    {
        Result rc = 0;

        static const SocketInitConfig socketInitConfig = {
            .tcp_tx_buf_size = 0x800,
            .tcp_rx_buf_size = 0x800,
            .tcp_tx_buf_max_size = 0x25000,
            .tcp_rx_buf_max_size = 0x25000,

            // Enable UDP for net logging
            .udp_tx_buf_size = 0x2400,
            .udp_rx_buf_size = 0x2400,

            .sb_efficiency = 1,
        };
        rc = socketInitialize(&socketInitConfig);

        if (R_FAILED(rc)) {
            log_error("socketInitialize failed: %08X", rc);
            return;
        }
        log_info("socketInitializeDefault success");
        rc = timeInitialize();
        if (R_FAILED(rc)) {
            log_error("timeInitialize failed: %08X", rc);
            return;
        }
        log_info("timeInitialize success");
        fsInitialize();
        log_info("fsInitialize success");
        fsdevMountSdmc();
        log_info("fsdevMountSdmc success");
        setInitialize();
        pmshellInitialize();
        smInitialize();
        if(isProgramRunning()) {
            g_serviceConnected = R_SUCCEEDED(smGetService(&g_service, "padmacro"));
        }
        loadConfig();
        log_info("loadConfig success");
    }

    virtual void onShow() override {} // Called before overlay wants to change from invisible to visible state

    // Called before overlay wants to change from visible to invisible state
    virtual void onHide() override {
        saveConfig();
    }
    virtual std::unique_ptr<tsl::Gui> loadInitialGui() override
    {
        return initially<GuiTest>(); // Initial Gui to load. It's possible to pass arguments to it's constructor like this
    }
};

int main(int argc, char **argv)
{
    return tsl::loop<OverlayTest>(argc, argv);
}
