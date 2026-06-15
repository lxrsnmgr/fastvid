#include "Logger.h"
#include <chrono>
#include <iomanip>
#include <iostream>
#include <limits.h>
#include <linux/limits.h>
#include <mutex>
#include <fstream>
#include <sstream>
#include <ctime>
#include <unistd.h>

namespace sserver{
namespace common{
namespace log{

namespace {
std::mutex &LogMutex(){
    static std::mutex mutex;
    return mutex;
}

//true 打印完整调试日志
//false 精简输出
bool &VerboseFlag(){
    static bool verbose = true;
    return verbose;
}

std::ofstream &LogFileStream(){
    static std::ofstream stream;
    return stream;
}

std::string &LogFilePath(){
    static std::string path;
    return path;
}

//false 初始化未完成
//true 初始化完成
bool &LogFileInitialized(){
    static bool initialized = false;
    return initialized;
}

const char *ToString(LogLevel level){
    switch(level){
        case LogLevel::kDebug:
            return "DEBUG";
        case LogLevel::kError:
            return "ERROR";
        case LogLevel::kInfo:
            return "INFO";
        case LogLevel::kWarn:
            return "WARN";
        default:
            return "UNKNOWN";
    }
}

std::string FormatPrefix(LogLevel level){
    const auto now = std::chrono::system_clock::now();
    const std::time_t now_time = std::chrono::system_clock::to_time_t(now);

    std::tm time_info{};
    localtime_r(&now_time, &time_info);

    std::ostringstream stream;
    stream << std::put_time(&time_info, "%F %T");
    stream << "[" << ToString(level) << "]";
    return stream.str();
}

std::ostream &OutputStreamForLevel(LogLevel level){
    return level == LogLevel::kError ? std::cerr : std::cout;
}

std::string ExecutableBaseName(){
    char buffer[PATH_MAX] = {0};
    const ssize_t size = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
    if(size <= 0){
        return "stream_server";
    }

    buffer[size] = '\0';
    const std::string full_path(buffer);
    const std::size_t separator = full_path.find_last_of("/");
    if(separator == std::string::npos || separator + 1 >= full_path.size()){
        return full_path;
    }
    return full_path.substr(separator + 1);
}

std::string ExecutablePath(){
    char buffer[PATH_MAX] = {0};
    const ssize_t size = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
    if(size < 0){
        return std::string();
    }

    buffer[PATH_MAX - 1] = '\0';
    return std::string(buffer);
}
}

}//namespace log
}//namespace common
}//namespace sserver
