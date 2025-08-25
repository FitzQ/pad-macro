// ...existing code...
#define TESLA_INIT_IMPL
#include <tesla.hpp>
#include <switch.h>
#include <vector>
#include <string>
#include <fstream>
#include <sstream>
#include <cstdio>

// 配置文件路径
// path relative to the opened SdCard FsFileSystem (do NOT include leading '/').
static const char *CONFIG_FILE = "/config/pad-macro/config.ini";
static const char *MACROS_DIR = "/switch/pad-macro/macros";

// 配置数据结构
struct PadConfig
{
    bool recorder_enable = false;
    bool player_enable = false;
    u64 recorder_btn = 0x0;
    u64 play_latest_btn = 0x0;
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

ConfigData g_config;

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

    /* Open (create/truncate) config file. */
    FsFile fileConfig;
    // Ensure parent directory exists (e.g. "config/pad-macro")
    (void)fsFsCreateDirectory(&fsSdmc, "/config/pad-macro");

    // Try opening file for write; if it doesn't exist create it first
    if (R_FAILED(fsFsOpenFile(&fsSdmc, CONFIG_FILE, FsOpenMode_Write, &fileConfig)))
    {
        // Try creating file if it doesn't exist (size 0, flags 0)
        if (R_FAILED(fsFsCreateFile(&fsSdmc, CONFIG_FILE, 0, 0)))
            return;
        if (R_FAILED(fsFsOpenFile(&fsSdmc, CONFIG_FILE, FsOpenMode_Write, &fileConfig)))
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
    iniString += std::string("play_latest_btn=") + u64ToHexString(g_config.pad.play_latest_btn) + "\n\n";
    iniString += "[macros]\n";

    // write macros
    for (const auto &m : g_config.macros)
    {
        iniString += u64ToHexString(m.key_mask) + "=" + m.file_path + "\n";
    }

    Result rc = fsFileWrite(&fileConfig, 0, iniString.c_str(), iniString.length(), FsWriteOption_Flush);
    (void)rc; // optionally handle rc/log
}

// 按键组合录入
u64 g_recording_mask = 0;
bool g_is_recording = false;
enum class RecordingTarget
{
    None,
    RecorderMask,
    Macro
};
RecordingTarget g_recording_target = RecordingTarget::None;
int g_ignore_frames = 0; // frames to ignore input immediately after starting recording

// 文件选择模拟（实际可用自定义UI或预设路径）
std::string selectFile()
{
    // TODO: 可扩展为文件浏览器，这里简单返回一个示例路径
    return "sdmc:/switch/pad-macro/example.bin";
}

const std::array<std::string, 32> KEY_COMBO_LIST = {
    "ZL+ZR", "ZL+ZR+DLEFT", "ZL+ZR+DUP", "ZL+ZR+DRIGHT", "ZL+ZR+DDOWN",
    "L+R", "L+R+DLEFT", "L+R+DUP", "L+R+DRIGHT", "L+R+DDOWN",
    "L+A", "L+B", "L+X", "L+Y", "L+DLEFT", "L+DUP", "L+DRIGHT", "L+DDOWN",
    "ZL+A", "ZL+B", "ZL+X", "ZL+Y", "ZL+DLEFT", "ZL+DUP", "ZL+DRIGHT", "ZL+DDOWN"};


class GuiMacroFileList : public tsl::Gui
{
private:
    // pointer to the currently-displayed ListItem for the selected combo
    tsl::elm::ListItem *item;
    std::string *path;

public:
    GuiMacroFileList(tsl::elm::ListItem *item, std::string *path) : item(item), path(path) {}

    virtual tsl::elm::Element *createUI() override
    {

        auto *rootFrame = new tsl::elm::OverlayFrame("Tesla Example", "v1.3.2 - Secondary Gui");
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
        const int BUF_SIZE = 16;
        FsDirectoryEntry entries[BUF_SIZE];
        s64 entries_read = 0;
        if (R_FAILED(fsDirRead(&dir, &entries_read, BUF_SIZE, entries)))
            return rootFrame;
        tsl::hlp::ScopeGuard fileGuard([&]
                                    { fsDirClose(&dir); });

        auto list = new tsl::elm::List();
        list->addItem(new tsl::elm::CategoryHeader("pad"));

    for (s64 i = 0; i < entries_read; ++i)
        {

            // show combo name and a select label on the right (two-arg constructor)
            std::string absPath = std::string(MACROS_DIR) + entries[i].name;
            auto *listItem = new tsl::elm::ListItem(entries[i].name, (absPath == *path) ? std::string("Selected") : std::string(""));

            listItem->setClickListener([this, absPath](u64 keys) {
                if (keys & HidNpadButton_A) {
                    *this->path = absPath;
                    saveConfig();
                    this->item->setText(*this->path);
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


class GuiKeyComboList : public tsl::Gui
{
private:
    // pointer to the currently-displayed ListItem for the selected combo
    tsl::elm::ListItem *item;
    u64 *mask;

public:
    GuiKeyComboList(tsl::elm::ListItem *item, u64 *mask) : item(item), mask(mask) {}

    virtual tsl::elm::Element *createUI() override
    {
        auto *rootFrame = new tsl::elm::OverlayFrame("Tesla Example", "v1.3.2 - Secondary Gui");
        auto list = new tsl::elm::List();
        list->addItem(new tsl::elm::CategoryHeader("pad"));
        for (size_t i = 0; i < KEY_COMBO_LIST.size(); i++)
        {

            // compute candidate mask once
            u64 candidate = comboStringToMask(KEY_COMBO_LIST[i]);
            // skip combos that are already used elsewhere, but allow the currently selected combo
            if (candidate != *mask && isUsedCombo(candidate))
                continue;

            // show combo name and a select label on the right (two-arg constructor)
            auto *listItem = new tsl::elm::ListItem(KEY_COMBO_LIST[i], (candidate == *mask) ? std::string("Selected") : std::string(""));

            // capture listItem and candidate so the lambda can update the UI element
            listItem->setClickListener([this, i, listItem, candidate](u64 keys) {
                if (keys & HidNpadButton_A) {
                    *this->mask = candidate;
                    saveConfig();
                    this->item->setValue(maskToComboString(*this->mask));
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
    tsl::elm::ToggleListItem *recorderEnableToggleListItem;
    tsl::elm::ToggleListItem *playerEnableToggleListItem;
    tsl::elm::ListItem *recorderBtnListItem;
    tsl::elm::ListItem *playLatestBtnListItem;
public:
    GuiTest(u8 arg1, u8 arg2, bool arg3) {}

    virtual tsl::elm::Element *createUI() override
    {
        auto frame = new tsl::elm::OverlayFrame("pad-macro", "v1.0.0");
        auto list = new tsl::elm::List();

        // pad section
        list->addItem(new tsl::elm::CategoryHeader("pad"));
        // recorder_enable toggle
        recorderEnableToggleListItem = new tsl::elm::ToggleListItem("recorder_enable", g_config.pad.recorder_enable);
        recorderEnableToggleListItem->setStateChangedListener([](bool state){
            g_config.pad.recorder_enable = state;
            saveConfig();
        });
        list->addItem(recorderEnableToggleListItem);
        // player_enable toggle
        playerEnableToggleListItem = new tsl::elm::ToggleListItem("player_enable", g_config.pad.player_enable);
        playerEnableToggleListItem->setStateChangedListener([](bool state){
            g_config.pad.player_enable = state;
            saveConfig();
        });
        list->addItem(playerEnableToggleListItem);
        // recorder_btn 按键组合
        recorderBtnListItem = new tsl::elm::ListItem("recorder_btn", maskToComboString(g_config.pad.recorder_btn));
        recorderBtnListItem->setClickListener([this](u64 keys) {
            if (keys & HidNpadButton_A) {
                // open recording gui (pass address so Gui can modify the value)
                tsl::changeTo<GuiKeyComboList>((this->recorderBtnListItem), &g_config.pad.recorder_btn);
                return true;
            }
            return false; });
        list->addItem(recorderBtnListItem);
        // play_latest_btn 按键组合
        playLatestBtnListItem = new tsl::elm::ListItem("play_latest_btn", maskToComboString(g_config.pad.play_latest_btn));
        playLatestBtnListItem->setClickListener([this](u64 keys) {
            if (keys & HidNpadButton_A) {
                // open recording gui (pass address so Gui can modify the value)
                tsl::changeTo<GuiKeyComboList>((this->playLatestBtnListItem), &g_config.pad.play_latest_btn);
                return true;
            }
            return false; });
        list->addItem(playLatestBtnListItem);

        // macros section
        list->addItem(new tsl::elm::CategoryHeader("macros"));
        // 显示所有宏映射
        for (size_t i = 0; i < g_config.macros.size(); ++i)
        {
            tsl::elm::ListItem *macroListItem = new tsl::elm::ListItem(g_config.macros[i].file_path, maskToComboString(g_config.macros[i].key_mask));
            macroListItem->setClickListener([list, macroListItem, i](u64 keys)
                                             {
                // edit macro file
                if (keys & HidNpadButton_A) {
                    // pass pointer to the actual vector element's file_path (non-const)
                    tsl::changeTo<GuiMacroFileList>(macroListItem, &g_config.macros[i].file_path);
                    return true;
                }
                // edit btn mask
                if (keys & HidNpadButton_Y) {
                    tsl::changeTo<GuiKeyComboList>(macroListItem, &g_config.macros[i].key_mask);
                    return true;
                }
                // Delete
                if (keys & HidNpadButton_X) {
                    g_config.macros.erase(g_config.macros.begin() + i);
                    saveConfig();
                    list->removeItem(macroListItem);
                    return true;
                }
                return false; });
            list->addItem(macroListItem);
        }
        // 新增宏映射
        // auto *addMacroItem = new tsl::elm::ListItem("Add Mapping", "+");
        // addMacroItem->setClickListener([this](u64 keys)
        //                                {
        //     if (keys & HidNpadButton_A) {
        //         // open recording gui
        //         tsl::changeTo<GuiKeyComboList>( );
        //         return true;
        //     }
        //     return false; });
        // list->addItem(addMacroItem);

        frame->setContent(list);
        return frame;
    }

    virtual void update() override {}

    virtual bool handleInput(u64 keysDown, u64 keysHeld, const HidTouchState &touchPos, HidAnalogStickState joyStickPosLeft, HidAnalogStickState joyStickPosRight) override
    {

        return false;
    }
};

class OverlayTest : public tsl::Overlay
{
public:
    // libtesla already initialized fs, hid, pl, pmdmnt, hid:sys and set:sys
    virtual void exitServices() override {} // Called at the end to clean up all services previously initialized

    virtual void initServices() override
    {
        loadConfig();
    }

    virtual void onShow() override {} // Called before overlay wants to change from invisible to visible state
    virtual void onHide() override {} // Called before overlay wants to change from visible to invisible state

    virtual std::unique_ptr<tsl::Gui> loadInitialGui() override
    {
        return initially<GuiTest>(1, 2, true); // Initial Gui to load. It's possible to pass arguments to it's constructor like this
    }
};

int main(int argc, char **argv)
{
    return tsl::loop<OverlayTest>(argc, argv);
}
