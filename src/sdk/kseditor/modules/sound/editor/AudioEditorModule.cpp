#include "AudioEditorModule.h"
#include "sdk/kseditor/engine/Audio/AudioStudioTypes.h"
#include "sdk/kseditor/engine/Audio/AudioTypes.h"
#include "sdk/kseditor/engine/FileFormat/FSPROImporter.h"
#include "sdk/kseditor/engine/FileFormat/BankWriter.h"
#include "sdk/kseditor/engine/FileFormat/KSAudioImporter.h"
#include "sdk/kseditor/engine/FileFormat/KSAudioExporter.h"
#include "sdk/kseditor/engine/FileFormat/KSAudioValidator.h"

#include <QFile>
#include <QJsonDocument>
#include <QFileDialog>
#include <QStandardPaths>
#include <QMessageBox>
#include <QInputDialog>
#include <QDir>
#include <QJsonArray>
#include <QProcess>
#include <QFileInfo>
#include <QAudioFormat>

namespace ks {

struct AudioEditorModule::Impl {
    audio::AudioStudio* studio = nullptr;
    audio::AudioProject* currentProject = nullptr;
    QString projectPath;
    bool modified = false;
    QStringList recentFiles;
};

AudioEditorModule* AudioEditorModule::s_instance = nullptr;

AudioEditorModule::AudioEditorModule(QObject* parent)
    : QObject(parent), m_impl(new Impl) {
    s_instance = this;
    m_impl->studio = new audio::AudioStudio(this);
}

AudioEditorModule::~AudioEditorModule() {
    delete m_impl;
}

bool AudioEditorModule::initialize() {
    qDebug() << "AudioEditorModule initialized";
    return true;
}

void AudioEditorModule::shutdown() {
    if (m_impl->modified) {
        m_impl->studio->saveProject(m_impl->projectPath);
    }
    m_impl->currentProject = nullptr;
    m_impl->projectPath.clear();
    m_impl->modified = false;
}

void AudioEditorModule::onNewProject() {
    if (m_impl->modified) {
        m_impl->studio->saveProject(m_impl->projectPath);
    }
    m_impl->studio->newProject();
    m_impl->currentProject = new audio::AudioProject(this);
    m_impl->projectPath.clear();
    m_impl->modified = false;
    emit soundChanged();
}

void AudioEditorModule::onOpenProject() {
    QString path = QFileDialog::getOpenFileName(nullptr,
        "Open Audio Project",
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation),
        "Audio Projects (*.ksaudio);;"
        "FMOD Studio Projects (*.fspro);;"
        "All Files (*)");

    if (path.isEmpty()) return;

    if (path.endsWith(".fspro", Qt::CaseInsensitive)) {
        fileformat::KSFSPROImporter importer;
        QString ksaudioPath = QFileInfo(path).absolutePath()
                             + "/" + QFileInfo(path).completeBaseName() + ".ksaudio";
        if (importer.convertFile(path, ksaudioPath)) {
            path = ksaudioPath;
            qInfo() << "Auto-imported .fspro -> .ksaudio:" << path;
        } else {
            QMessageBox::warning(nullptr, "Import Failed",
                "Failed to import FMOD project:\n" + importer.lastError());
            return;
        }
    }

    if (m_impl->studio->loadProject(path)) {
        m_impl->projectPath = path;
        m_impl->modified = false;
        emit soundChanged();
    }
}

void AudioEditorModule::onSaveProject() {
    if (m_impl->projectPath.isEmpty()) {
        QString path = QFileDialog::getSaveFileName(nullptr,
            "Save Audio Project",
            QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation),
            "Audio Projects (*.ksaudio)");
        if (path.isEmpty()) return;
        m_impl->projectPath = path;
    }

    if (m_impl->studio->saveProject(m_impl->projectPath)) {
        m_impl->modified = false;
    }
}

void AudioEditorModule::onImportAsset() {
    QStringList paths = QFileDialog::getOpenFileNames(nullptr,
        "Import Audio / FMOD Project",
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation),
        "All Supported (*.wav *.ogg *.mp3 *.flac *.aiff *.fspro *.bank);;"
        "Audio Files (*.wav *.ogg *.mp3 *.flac *.aiff);;"
        "FMOD Studio Projects (*.fspro);;"
        "FMOD Bank Files (*.bank);;"
        "All Files (*)");

    if (paths.isEmpty()) return;

    // Ensure we have a project to import into
    if (m_impl->projectPath.isEmpty()) {
        QString savePath = QFileDialog::getSaveFileName(nullptr,
            "Create Audio Project",
            QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation),
            "Audio Projects (*.ksaudio)");
        if (savePath.isEmpty()) return;

        fileformat::KSAudioImporter importer;
        if (!importer.createProject(savePath, QFileInfo(savePath).completeBaseName())) {
            QMessageBox::warning(nullptr, "Error",
                "Failed to create project:\n" + importer.lastError());
            return;
        }
        m_impl->projectPath = savePath;
    }

    fileformat::KSAudioImporter importer;
    fileformat::KSAudioImporter::ImportOptions opts;
    opts.copyAssets = true;
    opts.detectLoopPoints = true;

    int successCount = 0;
    for (const QString& path : paths) {
        if (path.endsWith(".fspro", Qt::CaseInsensitive)) {
            fileformat::KSFSPROImporter fsproImporter;
            QString ksaudioPath = QFileInfo(path).absolutePath()
                                 + "/" + QFileInfo(path).completeBaseName() + ".ksaudio";
            if (fsproImporter.convertFile(path, ksaudioPath)) {
                qInfo() << "Imported .fspro -> .ksaudio:" << ksaudioPath;
                successCount++;
            } else {
                QMessageBox::warning(nullptr, "Import Failed",
                    "Failed to import FMOD project:\n" + fsproImporter.lastError());
            }
        } else if (path.endsWith(".bank", Qt::CaseInsensitive)) {
            if (importer.importBank(m_impl->projectPath, path, opts)) {
                qInfo() << "Imported .bank:" << path;
                successCount++;
            } else {
                QMessageBox::warning(nullptr, "Import Failed",
                    "Failed to import bank:\n" + importer.lastError());
            }
        } else {
            if (importer.importAudioFile(m_impl->projectPath, path, opts).guid.isEmpty() == false) {
                qInfo() << "Imported audio:" << path;
                successCount++;
            } else {
                QMessageBox::warning(nullptr, "Import Failed",
                    "Failed to import audio file:\n" + importer.lastError());
            }
        }
    }

    if (successCount > 0) {
        m_impl->modified = true;
        m_impl->studio->loadProject(m_impl->projectPath);
        emit soundChanged();
    }
}

void AudioEditorModule::onImportBank() {
    QString path = QFileDialog::getOpenFileName(nullptr,
        "Import FMOD Bank File",
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation),
        "FMOD Bank Files (*.bank);;All Files (*)");

    if (path.isEmpty()) return;

    // Ensure we have a project
    if (m_impl->projectPath.isEmpty()) {
        onNewProject();
        if (m_impl->projectPath.isEmpty()) return;
    }

    fileformat::KSAudioImporter importer;
    fileformat::KSAudioImporter::ImportOptions opts;
    opts.copyAssets = true;

    if (importer.importBank(m_impl->projectPath, path, opts)) {
        int count = importer.lastImportedEvents().size();
        qInfo() << "Imported" << count << "events from bank:" << path;
        m_impl->modified = true;
        m_impl->studio->loadProject(m_impl->projectPath);
        emit soundChanged();
    } else {
        QMessageBox::warning(nullptr, "Import Failed",
            "Failed to import bank:\n" + importer.lastError());
    }
}

void AudioEditorModule::onExportAsset() {
    if (m_impl->projectPath.isEmpty()) {
        QMessageBox::information(nullptr, "No Project",
            "No project is currently open.");
        return;
    }

    QString path = QFileDialog::getSaveFileName(nullptr,
        "Export Audio Project As",
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation),
        "Audio Files (*.wav);;"
        "Ogg Vorbis (*.ogg);;"
        "MP3 (*.mp3);;"
        "FLAC (*.flac);;"
        "All Files (*)");

    if (path.isEmpty()) return;

    fileformat::KSAudioExporter exporter;
    fileformat::KSAudioExporter::ExportOptions opts;

    QString ext = QFileInfo(path).suffix().toLower();
    if (ext == "ogg") opts.format = fileformat::KSAudioExporter::ExportOGG;
    else if (ext == "mp3") opts.format = fileformat::KSAudioExporter::ExportMP3;
    else if (ext == "flac") opts.format = fileformat::KSAudioExporter::ExportFLAC;
    else opts.format = fileformat::KSAudioExporter::ExportWAV;

    // Export all events
    QString outputDir = QFileInfo(path).absolutePath() + "/" + QFileInfo(path).completeBaseName() + "_stems";
    auto files = exporter.exportAll(m_impl->projectPath, outputDir, opts);

    if (!files.isEmpty()) {
        QMessageBox::information(nullptr, "Export Complete",
            QString("Exported %1 files to:\n%2").arg(files.size()).arg(outputDir));
    } else {
        QMessageBox::warning(nullptr, "Export Failed",
            "No events were exported.\n" + exporter.lastError());
    }
}

void AudioEditorModule::onExportStems() {
    if (m_impl->projectPath.isEmpty()) {
        QMessageBox::information(nullptr, "No Project",
            "No project is currently open.");
        return;
    }

    QString outputDir = QFileDialog::getExistingDirectory(nullptr,
        "Select Export Directory",
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation));

    if (outputDir.isEmpty()) return;

    // Ask for format
    QStringList formats = {"WAV", "OGG", "MP3", "FLAC"};
    bool ok;
    QString format = QInputDialog::getItem(nullptr,
        "Export Format", "Select audio format:", formats, 0, false, &ok);

    if (!ok || format.isEmpty()) return;

    fileformat::KSAudioExporter exporter;
    fileformat::KSAudioExporter::ExportOptions opts;
    if (format == "OGG") opts.format = fileformat::KSAudioExporter::ExportOGG;
    else if (format == "MP3") opts.format = fileformat::KSAudioExporter::ExportMP3;
    else if (format == "FLAC") opts.format = fileformat::KSAudioExporter::ExportFLAC;
    else opts.format = fileformat::KSAudioExporter::ExportWAV;

    auto files = exporter.exportAll(m_impl->projectPath, outputDir, opts);

    QMessageBox::information(nullptr, "Export Complete",
        QString("Exported %1 stem files to:\n%2").arg(files.size()).arg(outputDir));
}

void AudioEditorModule::onExportSummary() {
    if (m_impl->projectPath.isEmpty()) {
        QMessageBox::information(nullptr, "No Project",
            "No project is currently open.");
        return;
    }

    QString path = QFileDialog::getSaveFileName(nullptr,
        "Export Project Summary",
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation),
        "Text Files (*.txt);;JSON Files (*.json);;All Files (*)");

    if (path.isEmpty()) return;

    fileformat::KSAudioExporter exporter;
    bool success;

    if (path.endsWith(".json", Qt::CaseInsensitive)) {
        success = exporter.exportMetadata(m_impl->projectPath, path);
    } else {
        success = exporter.exportSummary(m_impl->projectPath, path);
    }

    if (success) {
        QMessageBox::information(nullptr, "Export Complete",
            "Project summary exported to:\n" + path);
    } else {
        QMessageBox::warning(nullptr, "Export Failed",
            "Failed to export summary:\n" + exporter.lastError());
    }
}

void AudioEditorModule::onValidateProject() {
    if (m_impl->projectPath.isEmpty()) {
        QMessageBox::information(nullptr, "No Project",
            "No project is currently open.");
        return;
    }

    fileformat::KSAudioValidator validator;
    auto result = validator.validate(m_impl->projectPath);

    QString message;
    message += QString("Validation %1\n\n").arg(result.valid ? "PASSED" : "FAILED");
    message += QString("Errors: %1\n").arg(result.errorCount);
    message += QString("Warnings: %1\n").arg(result.warningCount);
    message += QString("Info: %1\n\n").arg(result.infoCount);

    if (!result.issues.isEmpty()) {
        message += "Issues:\n";
        for (const auto& issue : result.issues) {
            QString severity;
            switch (issue.severity) {
            case fileformat::KSAudioValidator::Error:   severity = "[ERROR]"; break;
            case fileformat::KSAudioValidator::Warning: severity = "[WARN]"; break;
            case fileformat::KSAudioValidator::Info:    severity = "[INFO]"; break;
            }
            message += QString("%1 %2: %3\n").arg(severity, issue.category, issue.message);
            if (!issue.suggestion.isEmpty())
                message += QString("  -> %1\n").arg(issue.suggestion);
        }
    }

    QMessageBox msgBox;
    msgBox.setWindowTitle("Project Validation");
    if (result.valid) {
        msgBox.setIcon(QMessageBox::Information);
    } else {
        msgBox.setIcon(QMessageBox::Warning);
    }
    msgBox.setText(message);
    msgBox.exec();
}

void AudioEditorModule::onBuildBanks() {
    if (m_impl->projectPath.isEmpty()) return;

    QString outputDir = QFileInfo(m_impl->projectPath).absolutePath() + "/banks";
    QDir().mkpath(outputDir);

    fileformat::KSBankWriter writer;
    QObject::connect(&writer, &fileformat::KSBankWriter::writeCompleted, this, [&](const QString& dir) {
        qInfo() << "Bank build completed:" << dir;
    });

    ks::audio::KSAudioProject project;
    if (project.load(m_impl->projectPath)) {
        if (writer.writeProjectBanks(project, outputDir)) {
            QString guidsPath = QFileInfo(m_impl->projectPath).absolutePath() + "/sfx/GUIDs.txt";
            QDir().mkpath(QFileInfo(guidsPath).absolutePath());
            writer.writeGUIDsFile(project, guidsPath);
        } else {
            qWarning() << "Bank build failed:" << writer.lastError();
        }
    }

    emit soundChanged();
}

} // namespace ks