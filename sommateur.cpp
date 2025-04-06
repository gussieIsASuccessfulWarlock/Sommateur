#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <cstdint>
#include <filesystem>
#include <thread>
#include <mutex>
#include <queue>
#include <condition_variable>
#include <unordered_map>
#include <string>
#include <atomic>
#include <chrono>
#include <future>
#include <curl/curl.h>

#if defined(_WIN32)
#include <windows.h>
#elif defined(__linux__)
#include <unistd.h>
#include <limits.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#include <limits.h>
#endif

std::string getExecutableName() {
    char path[PATH_MAX];
#if defined(_WIN32)
    GetModuleFileNameA(NULL, path, PATH_MAX);
#elif defined(__linux__)
    ssize_t count = readlink("/proc/self/exe", path, PATH_MAX);
    if (count == -1) return "";
    path[count] = '\0';
#elif defined(__APPLE__)
    uint32_t size = sizeof(path);
    if (_NSGetExecutablePath(path, &size) != 0) return "";
#endif
    std::string fullPath(path);
    size_t pos = fullPath.find_last_of("/\\");
    return (pos != std::string::npos) ? fullPath.substr(pos + 1) : fullPath;
}

namespace fs = std::filesystem;

uint32_t crc32_table[256];

void generate_crc32_table() {
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t crc = i;
        for (uint32_t j = 0; j < 8; j++) {
            if (crc & 1)
                crc = (crc >> 1) ^ 0xEDB88320;
            else
                crc >>= 1;
        }
        crc32_table[i] = crc;
    }
}

uint32_t crc32_checksum_from_file(const std::string& filename) {
    std::ifstream file(filename, std::ios::binary);
    if (!file)
        return 0xFFFFFFFF;

    uint32_t crc = 0xFFFFFFFF;
    char buffer[4096];

    while (file.read(buffer, sizeof(buffer)) || file.gcount() > 0) {
        std::streamsize bytesRead = file.gcount();
        for (std::streamsize i = 0; i < bytesRead; ++i) {
            uint8_t byte = static_cast<uint8_t>(buffer[i]);
            crc = (crc >> 8) ^ crc32_table[(crc ^ byte) & 0xFF];
        }
    }
    if (file.bad())
        return 0xFFFFFFFF;
    return ~crc;
}

std::queue<fs::path> fileQueue;
std::mutex queueMutex;
std::condition_variable queueCV;
bool doneEnumerating = false;
std::mutex outputMutex;

struct ChecksumRecord {
    std::string filePath;
    uint32_t checksum;
};

std::vector<ChecksumRecord> results;
std::mutex resultsMutex;

enum class Mode { PRINT, OUTPUT, CHECKS };
Mode mode = Mode::PRINT;

std::atomic<size_t> totalFiles{0};
std::atomic<size_t> processedFiles{0};
bool suppressProgress = false;
std::chrono::milliseconds fileTimeout = std::chrono::milliseconds::zero();

std::vector<std::string> skippedFiles;
std::mutex skippedMutex;

void showProgress() {
    const int barWidth = 50;
    while (!doneEnumerating || processedFiles < totalFiles) {
        size_t progress = processedFiles;
        size_t total = totalFiles;
        float ratio = total > 0 ? static_cast<float>(progress) / total : 0;

        int pos = static_cast<int>(barWidth * ratio);
        std::cout << "\r[";
        for (int i = 0; i < barWidth; ++i) {
            if (i < pos) std::cout << "=";
            else if (i == pos) std::cout << ">";
            else std::cout << " ";
        }
        std::cout << "] " << static_cast<int>(ratio * 100.0) << "% (" 
                  << progress << "/" << total << ")   ";
        std::cout.flush();
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    std::cout << "\r[==================================================] 100% (" 
              << totalFiles << "/" << totalFiles << ")   " << std::endl;
}

void producer(const fs::path& path) {
    try {
        for (const auto& entry : fs::recursive_directory_iterator(path, fs::directory_options::skip_permission_denied)) {
            try {
                if (entry.is_regular_file()) {
                    std::string fullPath = entry.path().string();
                    if (fullPath.compare(0, 5, "/proc") == 0 ||
                        fullPath.compare(0, 4, "/sys") == 0 ||
                        fullPath.compare(0, 4, "/dev") == 0)
                        continue;

                    {
                        std::lock_guard<std::mutex> lock(queueMutex);
                        fileQueue.push(entry.path());
                    }
                    queueCV.notify_one();
                }
            } catch (const fs::filesystem_error&) {
            }
        }
    } catch (const fs::filesystem_error&) {
    }

    {
        std::lock_guard<std::mutex> lock(queueMutex);
        totalFiles = fileQueue.size();
        doneEnumerating = true;
    }
    queueCV.notify_all();
}

bool compute_with_timeout(const std::string& path, uint32_t& outChecksum) {
    std::packaged_task<uint32_t()> task([=] { return crc32_checksum_from_file(path); });
    std::future<uint32_t> future = task.get_future();
    std::thread t(std::move(task));
    if (fileTimeout == std::chrono::milliseconds::zero()) {
        t.join();
        outChecksum = future.get();
        return true;
    }
    if (future.wait_for(fileTimeout) == std::future_status::ready) {
        t.join();
        outChecksum = future.get();
        return true;
    } else {
        t.detach();
        {
            std::lock_guard<std::mutex> lock(skippedMutex);
            skippedFiles.push_back(path);
        }
        return false;
    }
}

void worker() {
    while (true) {
        fs::path filePath;
        {
            std::unique_lock<std::mutex> lock(queueMutex);
            queueCV.wait(lock, [] { return !fileQueue.empty() || doneEnumerating; });
            if (fileQueue.empty() && doneEnumerating)
                break;
            filePath = fileQueue.front();
            fileQueue.pop();
        }

        std::string fullPath = filePath.string();
        uint32_t checksum = 0;
        bool success = compute_with_timeout(fullPath, checksum);

        if (success && checksum != 0xFFFFFFFF) {
            if (mode == Mode::PRINT) {
                std::lock_guard<std::mutex> lock(outputMutex);
                std::cout << " - [0x" << std::hex << checksum << std::dec << "] " << fullPath << std::endl;
            } else {
                std::lock_guard<std::mutex> lock(resultsMutex);
                results.push_back({ fullPath, checksum });
            }
        }
        processedFiles++;
    }
}

static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t totalSize = size * nmemb;
    std::vector<char>* buffer = static_cast<std::vector<char>*>(userp);
    buffer->insert(buffer->end(), (char*)contents, (char*)contents + totalSize);
    return totalSize;
}

bool download_file_from_url(const std::string& url, std::vector<char>& outBuffer) {
    CURL* curl = curl_easy_init();
    if (!curl)
         return false;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &outBuffer);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    return (res == CURLE_OK);
}

int main(int argc, char* argv[]) {
#if defined(_WIN32)
    std::string directory = "C:\\";
#else
    std::string directory = "/";
#endif
    std::string checkFile;
    bool isOutputMode = false;
    bool isChecksMode = false;
    bool isVerboseMode = false;

    if (argc >= 2) {
        std::string arg1 = argv[1];
        if (arg1 == "-h" || arg1 == "--help") {
            std::string exeName = getExecutableName();
            std::cout << "Usage:\n";
            std::cout << "  Show Checksums:            ./" << exeName << " [directory]\n";
            std::cout << "  Save Checksums to File:    ./" << exeName << " [directory] -output [file]\n";
            std::cout << "  Compare Checksums:         ./" << exeName << " [directory] -checks [file or url]\n";
            std::cout << "  Optional flags:\n";
            std::cout << "    -v / -verbose       Show unchanged files too\n";
            std::cout << "    -np / -no-progress  Suppress the progress bar\n";
            std::cout << "    -t / -timeout [ms]  Skip files that take longer than [ms] milliseconds\n";
#if defined(_WIN32)
            std::cout << "Note: If no directory is specified, 'C:\\' is used by default.\n";
#else
            std::cout << "Note: If no directory is specified, '/' is used by default.\n";
#endif
            return 0;
        }
    }

    if (argc >= 2 && argv[1][0] != '-') {
        directory = argv[1];
    }

    for (int i = 1; i < argc - 1; ++i) {
        std::string arg = argv[i];
        if (arg == "-o" || arg == "-output") {
            checkFile = argv[i + 1];
            isOutputMode = true;
        } else if (arg == "-c" || arg == "-checks") {
            checkFile = argv[i + 1];
            isChecksMode = true;
        } else if (arg == "-t" || arg == "-timeout") {
            try {
                int ms = std::stoi(argv[i + 1]);
                fileTimeout = std::chrono::milliseconds(ms);
            } catch (...) {
                std::cerr << "Invalid timeout value.\n";
                return 1;
            }
        }
    }

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-v" || arg == "-verbose") {
            isVerboseMode = true;
        }
        if (arg == "-np" || arg == "-no-progress") {
            suppressProgress = true;
        }
    }

    mode = isOutputMode ? Mode::OUTPUT :
           isChecksMode ? Mode::CHECKS :
                          Mode::PRINT;
    
    if (mode == Mode::PRINT) {
        suppressProgress = true;
    }

    generate_crc32_table();

    std::thread producerThread(producer, fs::path(directory));
    producerThread.join();

    std::thread progressThread;
    if (!suppressProgress) {
        progressThread = std::thread(showProgress);
    }

    const unsigned int numThreads = std::thread::hardware_concurrency();
    std::vector<std::thread> workers;
    for (unsigned int i = 0; i < numThreads; ++i) {
        workers.emplace_back(worker);
    }
    for (auto& t : workers) {
        t.join();
    }
    if (!suppressProgress) {
        progressThread.join();
    }

    if (mode == Mode::OUTPUT) {
        std::ofstream out(checkFile, std::ios::binary);
        if (!out) return 1;

        uint32_t count = results.size();
        out.write(reinterpret_cast<const char*>(&count), sizeof(count));
        for (const auto& rec : results) {
            uint32_t pathLength = rec.filePath.size();
            out.write(reinterpret_cast<const char*>(&pathLength), sizeof(pathLength));
            out.write(rec.filePath.data(), pathLength);
            out.write(reinterpret_cast<const char*>(&rec.checksum), sizeof(rec.checksum));
        }
        std::cout << "Checksums written to " << checkFile << std::endl;
    } else if (mode == Mode::CHECKS) {
        std::unordered_map<std::string, uint32_t> oldChecks;
        std::unique_ptr<std::istream> inStream;
        std::vector<char> buffer;
        if (checkFile.find("http://") == 0 || checkFile.find("https://") == 0) {
            if (!download_file_from_url(checkFile, buffer)) {
                std::cerr << "Failed to download check file from URL: " << checkFile << std::endl;
                return 1;
            }
            inStream = std::make_unique<std::istringstream>(std::string(buffer.data(), buffer.size()), std::ios::binary);
        } else {
            auto fileStream = std::make_unique<std::ifstream>(checkFile, std::ios::binary);
            if (!fileStream->is_open()) {
                std::cerr << "Failed to open file: " << checkFile << std::endl;
                return 1;
            }
            inStream = std::move(fileStream);
        }
        
        uint32_t count = 0;
        inStream->read(reinterpret_cast<char*>(&count), sizeof(count));
        for (uint32_t i = 0; i < count; ++i) {
            uint32_t pathLength = 0;
            inStream->read(reinterpret_cast<char*>(&pathLength), sizeof(pathLength));
            std::string path(pathLength, '\0');
            inStream->read(&path[0], pathLength);
            uint32_t storedChecksum = 0;
            inStream->read(reinterpret_cast<char*>(&storedChecksum), sizeof(storedChecksum));
            oldChecks[path] = storedChecksum;
        }

        for (const auto& rec : results) {
            auto it = oldChecks.find(rec.filePath);
            if (it != oldChecks.end() && it->second != rec.checksum) {
                std::cout << " - [‼️ File Changed] " << rec.filePath << std::endl;
            } else if (isVerboseMode) {
                std::cout << " - [✅ No Changes] " << rec.filePath << std::endl;
            }
        }
    }

    if (!skippedFiles.empty()) {
        if (isVerboseMode) {
            std::cout << "\n⚠️  Skipped files due to timeout (" << skippedFiles.size() << "):\n";
            for (const auto& path : skippedFiles) {
                std::cout << " - " << path << std::endl;
            }
        } else {
            std::cout << "\n⚠️  Skipped files due to timeout (" << skippedFiles.size() << ")\n";
        }
    }

    return 0;
}
