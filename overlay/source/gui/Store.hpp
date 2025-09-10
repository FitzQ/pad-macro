#include <tesla.hpp>
#include <switch.h>
#include <log.h>
#include <curl/curl.h>
#include "../i18n.hpp"
#include <json.hpp>
#include <iostream>
#include <vector>
#include <queue>
#include <mutex>

using json = nlohmann::json;
using namespace std;

// 定义一个结构体来表示 JSON 中的对象
enum FileType {
    FILETYPE_FILE,
    FILETYPE_DIR
};
struct FileInfo {
    string name;
    FileType type;
    string mtime;
    long size = -1; // 使用 -1 表示 size 字段不存在或未设置
};

static bool g_curlInitialized = false;
static CURL *g_curl = nullptr;
static std::string baseUrl = "http://121.43.66.100:9090/pad-macro/";
static std::string downloadDir = "/config/pad-macro/macros/";

size_t write_callback(char* ptr, size_t size, size_t nmemb, void* userdata) {
    size_t real_size = size * nmemb;
    std::vector<FileInfo>* file_list = (std::vector<FileInfo>*)userdata;

    json jsonData = json::parse(std::string(ptr, real_size));
    for (const auto& item : jsonData) {
        FileInfo fileInfo;
        fileInfo.name = item.value("name", "");
        fileInfo.type = item.value("type", "") == "file" ? FILETYPE_FILE : FILETYPE_DIR;
        fileInfo.mtime = item.value("mtime", "");
        fileInfo.size = item.value("size", -1);
        file_list->push_back(fileInfo);
    }
    return real_size;
}

size_t write_file_callback(char* ptr, size_t size, size_t nmemb, void* userdata) {
    size_t real_size = size * nmemb;
    FILE* fp = (FILE*)userdata;
    return fwrite(ptr, 1, real_size, fp);
}

// ---------------- 下载线程安全实现 ----------------

struct DownloadResult {
    tsl::elm::ListItem *listItem;
    bool success;
};

static Thread downloadThread;
static Event downloadReqEvent;
static bool downloadThreadRunning = false;
static bool downloadThreadExit = false;

static std::mutex resultMutex;
static std::queue<DownloadResult> resultQueue;

struct DownloadFileParam {
    std::string url;
    std::string outputPath;
    std::string filename;
    bool downloading = false;
    tsl::elm::ListItem *listItem;
} downloadFileParam;

// 后台线程：只下载文件，把结果塞进队列
void downloadFilePoll(void *args) {
    while (!downloadThreadExit) {
        eventWait(&downloadReqEvent, UINT64_MAX);
        eventClear(&downloadReqEvent);

        if (downloadThreadExit) break;

        downloadFileParam.downloading = true;
        log_info("Downloading file: %s", downloadFileParam.filename.c_str());

        CURL* curl = curl_easy_init();
        FILE *file = fopen(downloadFileParam.outputPath.c_str(), "wb");
        curl_easy_setopt(curl, CURLOPT_URL, downloadFileParam.url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_file_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, file);

        CURLcode res = curl_easy_perform(curl);
        fclose(file);
        curl_easy_cleanup(curl);

        bool ok = (res == CURLE_OK);
        log_info("Download %s: %s", downloadFileParam.filename.c_str(), ok ? "OK" : "FAIL");

        {
            std::lock_guard<std::mutex> lock(resultMutex);
            resultQueue.push({downloadFileParam.listItem, ok});
        }

        downloadFileParam.downloading = false;
    }
    threadExit();
}

// 启动下载线程
static void initDownloadThread() {
    if (!downloadThreadRunning) {
        eventCreate(&downloadReqEvent, false);
        threadCreate(&downloadThread, downloadFilePoll, nullptr, nullptr, 0x40000, 0x2c, -2);
        threadStart(&downloadThread);
        downloadThreadRunning = true;
    }
}

// 请求下载
static void downloadFile(const std::string& url, const std::string& output, const std::string& filename, tsl::elm::ListItem *listItem) {
    if (downloadFileParam.downloading) return; // 忙碌时不处理

    initDownloadThread();
    downloadFileParam.url = url;
    downloadFileParam.outputPath = output;
    downloadFileParam.filename = filename;
    downloadFileParam.listItem = listItem;
    eventFire(&downloadReqEvent);
}

// -------------------------------------------------

vector<FileInfo> openFolder(const char* url) {
    log_info("Opening folder: %s", url);
    vector<FileInfo> file_list;
    curl_easy_setopt(g_curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(g_curl, CURLOPT_WRITEDATA, &file_list);
    curl_easy_setopt(g_curl, CURLOPT_URL, url);
    CURLcode res = curl_easy_perform(g_curl);
    if (res != CURLE_OK) {
        log_error("cURL request failed: %s", curl_easy_strerror(res));
    }
    return file_list;
}

class Store : public tsl::Gui
{
private:
    tsl::elm::OverlayFrame *frame;
    tsl::elm::List *list;
    string url;
public:
    Store(string url = baseUrl) : url(url) {
        log_info("Store new");
        if (!g_curlInitialized) {
            g_curlInitialized = true;
            if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK)
                log_error("Failed to initialize cURL");
            g_curl = curl_easy_init();
        }
        log_info("Store created");
    }

    virtual tsl::elm::Element *createUI() override {
        frame = new tsl::elm::OverlayFrame(i18n_getString("A00R"),
            i18n_getString("A00S")+"\n"+i18n_getString("A00T")+"\n"+i18n_getString("A00U"),
            "\uE0E1  " + i18n_getString("A00H") + "     \uE0E0  " + i18n_getString("A00V"));
        list = new tsl::elm::List();

        vector<FileInfo> file_list = openFolder(url.c_str());
        for (const auto& file : file_list) {
            bool isFile = file.type == FILETYPE_FILE;
            auto *listItem = new tsl::elm::ListItem(file.name, isFile ? sizeToHumanReadable(file.size) : "/");

            listItem->setClickListener([this, listItem, file, isFile](u64 keys) {
                if (keys & HidNpadButton_A) {
                    if (isFile) {
                        log_info("Clicked on file: %s", file.name.c_str());
                        if (!downloadFileParam.downloading) {
                            std::string escapedUrl = this->url + curl_easy_escape(g_curl, file.name.c_str(), 0);
                            std::string outPath = downloadDir + file.name;
                            listItem->setValue(i18n_getString("A00X")); // 下载中
                            downloadFile(escapedUrl, outPath, file.name, listItem);
                        }
                    } else {
                        log_info("Clicked on folder: %s", file.name.c_str());
                        string escapedUrl = this->url + curl_easy_escape(g_curl, file.name.c_str(), 0) + "/";
                        tsl::changeTo<Store>(escapedUrl);
                    }
                    return true;
                }
                return false;
            });
            list->addItem(listItem);
            log_info("File: name=%s, type=%d, mtime=%s, size=%d", file.name.c_str(), file.type, file.mtime.c_str(), file.size);
        }
        frame->setContent(list);
        return frame;
    }

    // 每帧轮询更新下载结果
    virtual void update() override {
        std::lock_guard<std::mutex> lock(resultMutex);
        while (!resultQueue.empty()) {
            auto result = resultQueue.front();
            resultQueue.pop();
            result.listItem->setValue(result.success ? i18n_getString("A00Y") : "Failed");
        }
    }

    ~Store() {
        if (downloadThreadRunning) {
            downloadThreadExit = true;
            eventFire(&downloadReqEvent);
            threadWaitForExit(&downloadThread);
            threadClose(&downloadThread);
            eventClose(&downloadReqEvent);
            downloadThreadRunning = false;
        }
    }

    string sizeToHumanReadable(long size) {
        const char* units[] = {"B", "KB", "MB", "GB", "TB"};
        size_t i = 0;
        double humanSize = static_cast<double>(size);
        while (humanSize >= 1024 && i < sizeof(units) / sizeof(units[0]) - 1) {
            humanSize /= 1024;
            i++;
        }
        char buffer[64];
        snprintf(buffer, sizeof(buffer), "%.2f %s", humanSize, units[i]);
        return string(buffer);
    }
};
