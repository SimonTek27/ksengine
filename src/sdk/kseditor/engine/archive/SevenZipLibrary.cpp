#include "SevenZipLibrary.h"

namespace ks {
namespace archive {

// Thin facade: in-process 7-Zip integration is not built (external/7zip
// sources are not part of the kslib target). Callers fall back to the
// external 7z.exe command line (see ModInstallEngine::extractZip).
class SevenZipLibrary::Impl {
public:
    bool ready = false;
};

SevenZipLibrary* SevenZipLibrary::s_instance = nullptr;

SevenZipLibrary* SevenZipLibrary::instance()
{
    if (!s_instance) s_instance = new SevenZipLibrary();
    return s_instance;
}

SevenZipLibrary::SevenZipLibrary()
    : m_impl(std::make_unique<Impl>())
{
}

SevenZipLibrary::~SevenZipLibrary() = default;

QJsonObject SevenZipLibrary::extract(const QString&, const QString&, const QString&)
{
    QJsonObject result;
    result.insert("success", false);
    result.insert("error", "In-process 7-Zip support is not built; use the 7z.exe fallback");
    return result;
}

QJsonObject SevenZipLibrary::extractFiles(const QString&, const QStringList&, const QString&, const QString&)
{
    QJsonObject result;
    result.insert("success", false);
    result.insert("error", "In-process 7-Zip support is not built");
    return result;
}

QJsonObject SevenZipLibrary::compress(const QStringList&, const QString&, const QString&, int)
{
    QJsonObject result;
    result.insert("success", false);
    result.insert("error", "In-process 7-Zip support is not built");
    return result;
}

QJsonObject SevenZipLibrary::listContents(const QString&, const QString&)
{
    return QJsonObject();
}

QJsonObject SevenZipLibrary::getArchiveInfo(const QString&)
{
    return QJsonObject();
}

QJsonObject SevenZipLibrary::testArchive(const QString&, const QString&)
{
    return QJsonObject();
}

bool SevenZipLibrary::isFormatSupported(const QString& format)
{
    static const QStringList supported = getSupportedFormats();
    return supported.contains(format.toLower());
}

QStringList SevenZipLibrary::getSupportedFormats()
{
    return { QStringLiteral("7z"), QStringLiteral("zip"), QStringLiteral("tar"),
             QStringLiteral("gz"), QStringLiteral("bz2"), QStringLiteral("xz") };
}

QStringList SevenZipLibrary::getSupportedExtensions()
{
    return { QStringLiteral("7z"), QStringLiteral("zip"), QStringLiteral("tar"),
             QStringLiteral("gz"), QStringLiteral("tgz"), QStringLiteral("bz2"),
             QStringLiteral("xz"), QStringLiteral("rar") };
}

} // namespace archive
} // namespace ks
