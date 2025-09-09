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
#include "config.hpp"

#define PROGRAM_NAME "pad-macro"
#define PROGRAM_ID 0x0100000000C0FFEE
#define CMD_EXIT 999

// 配置文件路径
// path relative to the opened SdCard FsFileSystem (do NOT include leading '/').
static const char *MACROS_DIR = "/config/pad-macro/macros";

ConfigData g_config;
Service g_service;
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
                    tsl::changeTo<ComboSet>(macroListItem, nullptr, &g_config.macros[i].key_mask);
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
                            tsl::changeTo<ComboSet>(newMacroListItem, nullptr, &g_config.macros[idx].key_mask);
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
    virtual void onHide() override {}
    virtual std::unique_ptr<tsl::Gui> loadInitialGui() override
    {
        return initially<GuiTest>(); // Initial Gui to load. It's possible to pass arguments to it's constructor like this
    }
};

int main(int argc, char **argv)
{
    return tsl::loop<OverlayTest>(argc, argv);
}
