
#pragma once
#include <switch.h>

#include <string>
#include <vector>
#include <map>
#include <log.h>
#define LOG_FILE_DIR "sdmc:/switch/.overlays/lang/pad-macro/"

using namespace std;

static map<string, string> g_localizedStrings;
static bool g_i18nInitialized = false;
Mutex g_i18nMutex = {0};

static map<SetLanguage, string> g_languageMap = {
    { SetLanguage_ZHHANS, "zh-CN.properties" },
    { SetLanguage_ZHCN, "zh-CN.properties" }
};

static void loadLang(string langFile) {
    log_info("Loading language file: %s", langFile.c_str());
    string filePath = string(LOG_FILE_DIR) + langFile;
    FILE* file = fopen(filePath.c_str(), "r");
    if (file) {
        char line[256];
        while (fgets(line, sizeof(line), file)) {
            string key = strtok(line, "=");
            if (key.empty()) continue;
            string value = strtok(NULL, "\n");
            g_localizedStrings[key] = value;
        }
        fclose(file);
    }
}

void i18n_init() {
    g_localizedStrings.clear();
    u64 languageCode;
    if (R_FAILED(setGetSystemLanguage(&languageCode))) {
        loadLang("un-KW.properties");
        return;
    }
    SetLanguage setLanguage;
    if (R_FAILED(setMakeLanguage(languageCode, &setLanguage))) {
        loadLang("un-KW.properties");
        return;
    }
    log_info("Detected system language: %d", setLanguage);
    auto it = g_languageMap.find(setLanguage);
    if (it != g_languageMap.end()) {
        log_info("Loading language file: %s", it->second.c_str());
        loadLang(it->second);
    } else {
        loadLang("un-KW.properties");
    }
}

const string i18n_getString(const string key) {
    if (!g_i18nInitialized) {
        mutexLock(&g_i18nMutex);
        if (!g_i18nInitialized) {
            i18n_init();
            g_i18nInitialized = true;
        }
        mutexUnlock(&g_i18nMutex);
    }
    log_info("Getting localized string for key: %s", key);
    auto it = g_localizedStrings.find(key);
    if (it != g_localizedStrings.end()) {
        return it->second;
    }
    log_info("Localized string not found for key: %s", key);
    return key;  // Return the key itself if not found
}
