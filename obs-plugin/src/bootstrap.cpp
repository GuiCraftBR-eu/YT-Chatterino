#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QVersionNumber>
#include <windows.h>
#include <cstring>
#include <iterator>

extern "C" __declspec(dllexport) bool obs_module_load();

namespace {
HMODULE implementation = nullptr;
void *modulePointer = nullptr;
using Load = bool (*)();
using Unload = void (*)();
using SetPointer = void (*)(void *);
template<class T> T symbol(HMODULE module, const char *name) {
    const auto raw = GetProcAddress(module, name);
    T result = nullptr;
    static_assert(sizeof(result) == sizeof(raw));
    std::memcpy(&result, &raw, sizeof(result));
    return result;
}

QString bundledDirectory() {
    HMODULE module = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(&obs_module_load), &module)) return {};
    wchar_t path[32768];
    const auto length = GetModuleFileNameW(module, path, DWORD(std::size(path)));
    return length && length < std::size(path) ? QFileInfo(QString::fromWCharArray(path, int(length))).absolutePath() : QString();
}

QJsonObject readJson(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 65536) return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}

QString cachedCore() {
    const QDir root(QDir(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation))
                        .filePath("YT-Chatterino/OBS/plugin-updates"));
    const auto marker = readJson(root.filePath("current.json"));
    const auto directory = marker.value("directory").toString(), version = marker.value("version").toString();
    if (!QRegularExpression("^[0-9]+\\.[0-9]+\\.[0-9]+-[a-f0-9-]{36}$").match(directory).hasMatch() ||
        !directory.startsWith(version + '-') ||
        QVersionNumber::compare(QVersionNumber::fromString(version), QVersionNumber::fromString(QStringLiteral(YT_OBS_VERSION))) <= 0)
        return {};
    const QDir folder(root.filePath(directory));
    const QFileInfo folderInfo(folder.absolutePath()), rootInfo(root.absolutePath());
    if (!folderInfo.isDir() || folderInfo.isSymLink() || folderInfo.isJunction() || rootInfo.canonicalFilePath().isEmpty() ||
        !folderInfo.canonicalFilePath().startsWith(rootInfo.canonicalFilePath() + '/', Qt::CaseInsensitive)) return {};
    const QString core = folder.filePath("yt-chatterino-obs-core.dll");
    const QFileInfo coreInfo(core);
    if (!coreInfo.isFile() || coreInfo.isSymLink() || coreInfo.isJunction() ||
        !QFileInfo::exists(folder.filePath("data/sidecar/YT-Chatterino.exe")) ||
        readJson(folder.filePath("data/sidecar/update-manifest.json")).value("version").toString() != version) return {};
    QFile file(core);
    if (!file.open(QIODevice::ReadOnly) || file.size() <= 0 || file.size() > 64 * 1024 * 1024) return {};
    QCryptographicHash digest(QCryptographicHash::Sha256);
    if (!digest.addData(&file) || QString::fromLatin1(digest.result().toHex()) != marker.value("core_sha256").toString()) return {};
    return core;
}

bool loadCore(const QString &path) {
    if (path.isEmpty()) return false;
    auto module = LoadLibraryW(reinterpret_cast<LPCWSTR>(path.utf16()));
    if (!module) return false;
    auto setPointer = symbol<SetPointer>(module, "obs_module_set_pointer");
    auto load = symbol<Load>(module, "obs_module_load");
    if (setPointer) setPointer(modulePointer);
    if (load && load()) { implementation = module; return true; }
    FreeLibrary(module);
    return false;
}
} // namespace

extern "C" __declspec(dllexport) void obs_module_set_pointer(void *pointer) { modulePointer = pointer; }
extern "C" __declspec(dllexport) unsigned int obs_module_ver() { return 33u << 24; }
extern "C" __declspec(dllexport) const char *obs_module_description() { return "YT-Chatterino mixed chat dock"; }
extern "C" __declspec(dllexport) bool obs_module_load() {
    return loadCore(cachedCore()) || loadCore(QDir(bundledDirectory()).filePath("yt-chatterino-obs-core.dll"));
}
extern "C" __declspec(dllexport) void obs_module_unload() {
    if (implementation) {
        auto unload = symbol<Unload>(implementation, "obs_module_unload");
        if (unload) unload();
    }
    // Keep the module loaded while OBS tears down its Qt widgets.
}
