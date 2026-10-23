#ifndef TETR_FRONTEND_FILE_DIALOG_H
#define TETR_FRONTEND_FILE_DIALOG_H
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>

namespace Frontend {
struct FileDialogResult {std::string path,error;};
// The native callback can outlive the application session. It owns this value
// request, never an application or window pointer, and copies the UTF-8 result.
class FileDialog {
public:
    const std::string directory;
    explicit FileDialog(std::string location):directory(std::move(location)) {}
    void complete(FileDialogResult result={}) {
        std::lock_guard<std::mutex> lock(mutex_);
        if(!result_)result_=std::move(result);
    }
    std::optional<FileDialogResult> result() const {
        std::lock_guard<std::mutex> lock(mutex_);return result_;
    }
private:
    mutable std::mutex mutex_;
    std::optional<FileDialogResult> result_;
};
class FileDialogService {
public:
    virtual ~FileDialogService()=default;
    virtual std::shared_ptr<FileDialog> open_file(std::string directory)=0;
};
}
#endif
