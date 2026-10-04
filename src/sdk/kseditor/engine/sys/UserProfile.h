#pragma once

#include <QString>
#include <QStringList>
#include <QMap>
#include <QObject>
#include <QJsonObject>
#include <QSettings>
#include <QDir>
#include <QImage>

namespace ks {

class CreatorProfile : public QObject {
    Q_OBJECT

public:
    static CreatorProfile* instance();

    explicit CreatorProfile(QObject* parent = nullptr);
    ~CreatorProfile();

    QString profileName() const { return m_profileName; }
    void setProfileName(const QString& name);

    QString name() const { return m_name; }
    void setName(const QString& value);

    QString keyFigure() const { return m_keyFigure; }
    void setKeyFigure(const QString& value);

    QString author() const { return m_author; }
    void setAuthor(const QString& value);

    QString email() const { return m_email; }
    void setEmail(const QString& value);

    QString website() const { return m_website; }
    void setWebsite(const QString& value);

    QString discord() const { return m_discord; }
    void setDiscord(const QString& value);

    QString threeDArtist() const { return m_threeDArtist; }
    void setThreeDArtist(const QString& value);

    QString textureArtist() const { return m_textureArtist; }
    void setTextureArtist(const QString& value);

    QString animations() const { return m_animations; }
    void setAnimations(const QString& value);

    QString physics() const { return m_physics; }
    void setPhysics(const QString& value);

    QString soundArtist() const { return m_soundArtist; }
    void setSoundArtist(const QString& value);

    QString coding() const { return m_coding; }
    void setCoding(const QString& value);

    QString moddingLogo() const { return m_moddingLogo; }
    void setModdingLogo(const QString& value);

    QString simInstallPath() const { return m_simInstallPath; }
    void setSimInstallPath(const QString& path);

    QStringList recentProjects() const { return m_recentProjects; }
    void addRecentProject(const QString& projectPath);
    void clearRecentProjects();

    QString preferredContentType() const { return m_contentType; }
    void setPreferredContentType(const QString& type);

    QStringList favoriteCars() const { return m_favoriteCars; }
    void addFavoriteCar(const QString& car);
    void removeFavoriteCar(const QString& car);

    QStringList favoriteTracks() const { return m_favoriteTracks; }
    void addFavoriteTrack(const QString& track);
    void removeFavoriteTrack(const QString& track);

    QString licenseType() const { return m_licenseType; }
    void setLicenseType(const QString& value);

    QJsonObject toJson() const;
    void fromJson(const QJsonObject& json);

    bool save();
    bool load();
    bool loadOrCreateDefault();

    QString profileDirectory() const;
    static QString defaultProfileDirectory();

    QString getCreditString() const;

signals:
    void profileLoaded();
    void profileSaved();
    void simPathChanged(const QString& path);
    void error(const QString& message);

private:
    QString m_profileName;
    QString m_name;
    QString m_keyFigure;
    QString m_author;
    QString m_email;
    QString m_website;
    QString m_discord;
    QString m_threeDArtist;
    QString m_textureArtist;
    QString m_animations;
    QString m_physics;
    QString m_soundArtist;
    QString m_coding;
    QString m_moddingLogo;
    QString m_simInstallPath;
    QStringList m_recentProjects;
    QString m_contentType;
    QStringList m_favoriteCars;
    QStringList m_favoriteTracks;
    QString m_licenseType;
    bool m_loaded = false;
};

class ContentDatabase : public QObject {
    Q_OBJECT

public:
    static ContentDatabase* instance();

    explicit ContentDatabase(QObject* parent = nullptr);
    ~ContentDatabase();

    void scanSimInstallation(const QString& simPath);
    void clear();

    QStringList carList() const { return m_cars; }
    QStringList trackList() const { return m_tracks; }

    QString findCarPath(const QString& carName) const;
    QString findTrackPath(const QString& trackName) const;

    QString getCarDataAcd(const QString& carPath) const;
    QString getCarUiDir(const QString& carPath) const;
    QString getCarSfxDir(const QString& carPath) const;
    QString getCarSkinsDir(const QString& carPath) const;

    bool isCarValid(const QString& carPath) const;
    bool isTrackValid(const QString& trackPath) const;

    struct CarInfo {
        QString name;
        QString author;
        QString brand;
        QString class_;
        int power;
        float mass;
        QString dataAcd;
        bool hasSkins;
    };

    CarInfo getCarInfo(const QString& carPath) const;

signals:
    void scanStarted();
    void scanProgress(int current, int total);
    void scanFinished();

private:
    void scanCars(const QString& carsDir);
    void scanTracks(const QString& tracksDir);

    QString m_simPath;
    QStringList m_cars;
    QStringList m_tracks;
    QMap<QString, QString> m_carPaths;
    QMap<QString, QString> m_trackPaths;
    QMap<QString, CarInfo> m_carInfo;
};

class CategoryManager : public QObject {
    Q_OBJECT

public:
    static CategoryManager* instance();

    explicit CategoryManager(QObject* parent = nullptr);
    ~CategoryManager();

    void loadCategories();
    void saveCategories();

    struct Category {
        QString name;
        QString description;
        QString filter;
        QString icon;
        int order = 0;
        bool hidden = false;
    };

    QList<Category> carCategories() const { return m_carCategories; }
    QList<Category> trackCategories() const { return m_trackCategories; }

    QStringList getCategoryNames() const;
    QString findCategoryIcon(const QString& categoryName) const;

    void addCarCategory(const Category& cat);
    void addTrackCategory(const Category& cat);
    void removeCarCategory(const QString& name);
    void removeTrackCategory(const QString& name);

signals:
    void categoriesLoaded();

private:
    QList<Category> m_carCategories;
    QList<Category> m_trackCategories;
    QString m_categoriesPath;
};

class PresetManager : public QObject {
    Q_OBJECT

public:
    static PresetManager* instance();

    explicit PresetManager(QObject* parent = nullptr);
    ~PresetManager();

    void loadPresets();
    void savePresets();

    struct Preset {
        QString name;
        QString type;
        QString filePath;
        QString description;
    };

    QList<Preset> getPresets(const QString& type) const;
    QStringList getPresetTypes() const;
    QString findPreset(const QString& type, const QString& name) const;

    void addPreset(const Preset& preset);
    void removePreset(const QString& type, const QString& name);

    QString findPresetFile(const QString& type, const QString& name);

signals:
    void presetsLoaded();

private:
    QMap<QString, QList<Preset>> m_presets;
    QString m_presetsPath;
};

class LauncherSettings : public QObject {
    Q_OBJECT

public:
    explicit LauncherSettings(QObject* parent = nullptr);
    ~LauncherSettings();

    void load();
    void save();

    bool autoDetectAc() const { return m_autoDetectAc; }
    void setAutoDetectAc(bool value);

    bool checkForUpdates() const { return m_checkUpdates; }
    void setCheckForUpdates(bool value);

    QString simExeArgs() const { return m_simExeArgs; }
    void setSimExeArgs(const QString& args);

    int maxRecentProjects() const { return m_maxRecent; }
    void setMaxRecentProjects(int value);

signals:
    void loaded();
    void modified();

private:
    bool m_autoDetectAc = true;
    bool m_checkUpdates = true;
    QString m_simExeArgs;
    int m_maxRecent = 10;
};

}