
#include <tesla.hpp>
#include <switch.h>
#include <log.h>
#include <curl/curl.h>
#include "../i18n.hpp"
#include <json.hpp>
#include <iostream>
#include <vector>

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
    // ptr: 指向接收到数据的指针
    // size: 总是 1
    // nmemb: 本次回调接收到的数据大小（字节数）
    // userdata: 你传入的自定义指针，通常用来传递一个缓冲区
    
    size_t real_size = size * nmemb;
    // 例如，如果 userdata 是一个 FILE*，你可以写入文件
    // FILE* fp = (FILE*)userdata;
    // return fwrite(ptr, 1, real_size, fp);
    
    // 更常见的是，userdata 是一个 std::string* 或 自定义内存缓冲区
    std::vector<FileInfo>* file_list = (std::vector<FileInfo>*)userdata;

    // 解析 JSON 数据并填充 file_list
    json jsonData = json::parse(std::string(ptr, real_size));
    for (const auto& item : jsonData) {
        FileInfo fileInfo;
        fileInfo.name = item.value("name", "");
        fileInfo.type = item.value("type", "") == "file" ? FILETYPE_FILE : FILETYPE_DIR;
        fileInfo.mtime = item.value("mtime", "");
        fileInfo.size = item.value("size", -1);
        file_list->push_back(fileInfo);
    }

    return real_size; // 必须返回实际处理的字节数
}
size_t write_file_callback(char* ptr, size_t size, size_t nmemb, void* userdata) {
    size_t real_size = size * nmemb;
    FILE* fp = (FILE*)userdata;
    return fwrite(ptr, 1, real_size, fp);
}
// struct DownloadFileParam {
//     std::string url;
//     std::string outputPath;
//     tsl::elm::ListItem* list;
//     bool downloading = false;
//     Event reqEvent = {0};
// } downloadFileParam;
// // DownloadFileParam downloadFileParam;
// Thread downloadThread;
// void downloadFilePoll(void *args) {
//     while(1) {
//         eventWait(&downloadFileParam.reqEvent, UINT64_MAX);
//         eventClear(&downloadFileParam.reqEvent);
//         downloadFileParam.downloading = true;
//         const char *url = downloadFileParam.url.c_str();
//         const char *outputPath = downloadFileParam.outputPath.c_str();
//         log_info("Downloading file from %s to %s", url, outputPath);
//         downloadFileParam.list->setValue(i18n_getString("A00X"));
//         curl_easy_setopt(g_curl, CURLOPT_URL, url);
//         FILE *file = fopen(outputPath, "wb");
//         curl_easy_setopt(g_curl, CURLOPT_WRITEFUNCTION, write_file_callback);
//         curl_easy_setopt(g_curl, CURLOPT_WRITEDATA, file);
//         curl_easy_perform(g_curl);
//         fclose(file);
//         downloadFileParam.list->setValue(i18n_getString("A00Y"));
//         downloadFileParam.downloading = false;
//     }
// }
// static void downloadFile() {
//     if (!downloadThread.handle) {
//         eventCreate(&downloadFileParam.reqEvent, false);
//         threadCreate(&downloadThread, downloadFilePoll, nullptr, nullptr, 0x40000, 0x2c, -2);
//         threadStart(&downloadThread);
//     }
//     eventFire(&downloadFileParam.reqEvent);
// }

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

    virtual tsl::elm::Element *createUI() override
    {
        // Create the UI elements
        frame = new tsl::elm::OverlayFrame(i18n_getString("A00R"),
            i18n_getString("A00S")+"\n"+i18n_getString("A00T")+"\n"+i18n_getString("A00U"),
            "\uE0E1  " + i18n_getString("A00H") + "     \uE0E0  " + i18n_getString("A00V"));
        list = new tsl::elm::List();
        vector<FileInfo> file_list = openFolder(url.c_str());
        for (const auto& file : file_list) {
            bool isFile = file.type == FILETYPE_FILE;
            auto *listItem = new tsl::elm::ListItem(file.name, isFile ? sizeToHumanReadable(file.size) : "/");
            std::function<bool(u64 keys)> clickListener = [this, listItem, file, isFile](u64 keys) {
                if (keys & HidNpadButton_A) {
                    if (isFile) {
                        log_info("Clicked on file: %s", file.name.c_str());
                        // if (downloadFileParam.downloading) {
                        //     return true;
                        // }
                        // string escapedUrl = this->url + curl_easy_escape(g_curl, file.name.c_str(), 0);
                        // downloadFileParam.url = escapedUrl;
                        // downloadFileParam.outputPath = downloadDir + file.name;
                        // downloadFileParam.list = listItem;
                        // downloadFile();
                    } else {
                        log_info("Clicked on folder: %s", file.name.c_str());
                        string escapedUrl = this->url + curl_easy_escape(g_curl, file.name.c_str(), 0) + "/";
                        tsl::changeTo<Store>(escapedUrl);
                    }
                    return true;
                }
                return false;
            };
            listItem->setClickListener(clickListener);
            list->addItem(listItem);
            log_info("File: name=%s, type=%d, mtime=%s, size=%d", file.name.c_str(), file.type, file.mtime.c_str(), file.size);
        }
        frame->setContent(list);
        return frame;
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