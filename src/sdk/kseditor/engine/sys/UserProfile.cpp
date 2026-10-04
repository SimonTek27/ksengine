#include "UserProfile.h"
#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonArray>
#include <QFile>
#include <QStandardPaths>
#include <QDebug>
#include "sdk/kseditor/engine/Config/EditorConfig.h"

namespace ks {

QString CreatorProfile::defaultProfileDirectory() {
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/ksEditor";
}

CreatorProfile::CreatorProfile(QObject* parent)
    : QObject(parent) {
    loadOrCreateDefault();
}

CreatorProfile::~CreatorProfile() {
}

CreatorProfile* CreatorProfile::instance() {
    static CreatorProfile inst;
    return &inst;
}

QString CreatorProfile::profileDirectory() const {
    return defaultProfileDirectory();
}

bool CreatorProfile::save() {
    QDir dir;
    dir.mkpath(profileDirectory());

    QString configFile = profileDirectory() + "/profile.json";
    QFile file(configFile);
    if (!file.open(QIODevice::WriteOnly)) {
        emit error("Failed to create profile file");
        return false;
    }

    QJsonObject json = toJson();
    QJsonDocument doc(json);
    file.write(doc.toJson());
    file.close();

    emit profileSaved();
    return true;
}

bool CreatorProfile::load() {
    QString configFile = profileDirectory() + "/profile.json";
    QFile file(configFile);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (doc.isNull()) {
        return false;
    }

    fromJson(doc.object());
    file.close();
    return true;
}

bool CreatorProfile::loadOrCreateDefault() {
    if (!load()) {
        m_profileName = "Default";
        m_name = EditorConfig::instance().defaultAuthorName();
        m_keyFigure = "Stefano Casillo";
        m_author = EditorConfig::instance().defaultAuthorName();
        m_email = EditorConfig::instance().defaultAuthorEmail();
        m_website = EditorConfig::instance().defaultWebsite();
        m_discord = "";
        m_threeDArtist = "Gianluca Miragoli";
        m_textureArtist = "Gianluca Miragoli";
        m_animations = "Gianluca Miragoli";
        m_physics = "Aris";
        m_soundArtist = "LucasoDano";
        m_coding = EditorConfig::instance().defaultAuthorName();
        m_moddingLogo = "";
        m_simInstallPath = EditorConfig::instance().simInstallPath();
        m_contentType = "car";
        m_licenseType = "Free";
        m_loaded = true;
        save();
    }
    emit profileLoaded();
    return m_loaded;
}

QString CreatorProfile::getCreditString() const {
    QStringList credits;
    
    if (!m_name.isEmpty()) credits << "Name: " + m_name;
    if (!m_keyFigure.isEmpty()) credits << "Key Figure: " + m_keyFigure;
    if (!m_threeDArtist.isEmpty()) credits << "3D Artist: " + m_threeDArtist;
    if (!m_textureArtist.isEmpty()) credits << "Texture: " + m_textureArtist;
    if (!m_animations.isEmpty()) credits << "Animations: " + m_animations;
    if (!m_physics.isEmpty()) credits << "Physics: " + m_physics;
    if (!m_soundArtist.isEmpty()) credits << "Sound: " + m_soundArtist;
    if (!m_coding.isEmpty()) credits << "Coding: " + m_coding;
    if (!m_moddingLogo.isEmpty()) credits << "Logo: " + m_moddingLogo;
    
    return credits.join("\n");
}

void CreatorProfile::setProfileName(const QString& name) { m_profileName = name; save(); }
void CreatorProfile::setName(const QString& value) { m_name = value; save(); }
void CreatorProfile::setKeyFigure(const QString& value) { m_keyFigure = value; save(); }
void CreatorProfile::setAuthor(const QString& value) { m_author = value; save(); }
void CreatorProfile::setEmail(const QString& value) { m_email = value; save(); }
void CreatorProfile::setWebsite(const QString& value) { m_website = value; save(); }
void CreatorProfile::setDiscord(const QString& value) { m_discord = value; save(); }
void CreatorProfile::setThreeDArtist(const QString& value) { m_threeDArtist = value; save(); }
void CreatorProfile::setTextureArtist(const QString& value) { m_textureArtist = value; save(); }
void CreatorProfile::setAnimations(const QString& value) { m_animations = value; save(); }
void CreatorProfile::setPhysics(const QString& value) { m_physics = value; save(); }
void CreatorProfile::setSoundArtist(const QString& value) { m_soundArtist = value; save(); }
void CreatorProfile::setCoding(const QString& value) { m_coding = value; save(); }
void CreatorProfile::setModdingLogo(const QString& value) { m_moddingLogo = value; save(); }

void CreatorProfile::setSimInstallPath(const QString& path) {
    m_simInstallPath = path;
    emit simPathChanged(path);
    save();
}

void CreatorProfile::addRecentProject(const QString& projectPath) {
    m_recentProjects.removeAll(projectPath);
    m_recentProjects.prepend(projectPath);
    while (m_recentProjects.size() > 20) {
        m_recentProjects.removeLast();
    }
    save();
}

void CreatorProfile::clearRecentProjects() {
    m_recentProjects.clear();
    save();
}

void CreatorProfile::setPreferredContentType(const QString& type) {
    m_contentType = type;
    save();
}

void CreatorProfile::addFavoriteCar(const QString& car) {
    if (!m_favoriteCars.contains(car)) {
        m_favoriteCars.append(car);
        save();
    }
}

void CreatorProfile::removeFavoriteCar(const QString& car) {
    m_favoriteCars.removeAll(car);
    save();
}

void CreatorProfile::addFavoriteTrack(const QString& track) {
    if (!m_favoriteTracks.contains(track)) {
        m_favoriteTracks.append(track);
        save();
    }
}

void CreatorProfile::removeFavoriteTrack(const QString& track) {
    m_favoriteTracks.removeAll(track);
    save();
}

void CreatorProfile::setLicenseType(const QString& value) {
    m_licenseType = value;
    save();
}

QJsonObject CreatorProfile::toJson() const {
    QJsonObject json;
    json["profileName"] = m_profileName;
    json["name"] = m_name;
    json["keyFigure"] = m_keyFigure;
    json["author"] = m_author;
    json["email"] = m_email;
    json["website"] = m_website;
    json["discord"] = m_discord;
    json["threeDArtist"] = m_threeDArtist;
    json["textureArtist"] = m_textureArtist;
    json["animations"] = m_animations;
    json["physics"] = m_physics;
    json["soundArtist"] = m_soundArtist;
    json["coding"] = m_coding;
    json["moddingLogo"] = m_moddingLogo;
    json["simInstallPath"] = m_simInstallPath;
    json["contentType"] = m_contentType;
    json["licenseType"] = m_licenseType;

    QJsonArray recent;
    for (const QString& p : m_recentProjects) recent.append(p);
    json["recentProjects"] = recent;

    QJsonArray cars;
    for (const QString& c : m_favoriteCars) cars.append(c);
    json["favoriteCars"] = cars;

    QJsonArray tracks;
    for (const QString& t : m_favoriteTracks) tracks.append(t);
    json["favoriteTracks"] = tracks;

    return json;
}

void CreatorProfile::fromJson(const QJsonObject& json) {
    m_profileName = json.value("profileName").toString();
    m_name = json.value("name").toString();
    m_keyFigure = json.value("keyFigure").toString();
    m_author = json.value("author").toString();
    m_email = json.value("email").toString();
    m_website = json.value("website").toString();
    m_discord = json.value("discord").toString();
    m_threeDArtist = json.value("threeDArtist").toString();
    m_textureArtist = json.value("textureArtist").toString();
    m_animations = json.value("animations").toString();
    m_physics = json.value("physics").toString();
    m_soundArtist = json.value("soundArtist").toString();
    m_coding = json.value("coding").toString();
    m_moddingLogo = json.value("moddingLogo").toString();
    m_simInstallPath = json.value("simInstallPath").toString();
    m_contentType = json.value("contentType").toString("car");
    m_licenseType = json.value("licenseType").toString("Free");

    m_recentProjects.clear();
    QJsonArray recent = json.value("recentProjects").toArray();
    for (const QJsonValue& v : recent) m_recentProjects.append(v.toString());

    m_favoriteCars.clear();
    QJsonArray cars = json.value("favoriteCars").toArray();
    for (const QJsonValue& v : cars) m_favoriteCars.append(v.toString());

    m_favoriteTracks.clear();
    QJsonArray tracks = json.value("favoriteTracks").toArray();
    for (const QJsonValue& v : tracks) m_favoriteTracks.append(v.toString());

    m_loaded = true;
    emit profileLoaded();
}

ContentDatabase::ContentDatabase(QObject* parent) : QObject(parent) {}
ContentDatabase::~ContentDatabase() {}
ContentDatabase* ContentDatabase::instance() { static ContentDatabase inst; return &inst; }

void ContentDatabase::scanSimInstallation(const QString& simPath) {
    m_simPath = simPath;

    scanCars(simPath + "/content/cars");
    scanTracks(simPath + "/content/tracks");
}

void ContentDatabase::clear() {
    m_cars.clear(); m_tracks.clear();
    m_carPaths.clear(); m_trackPaths.clear(); m_carInfo.clear();
}

void ContentDatabase::scanCars(const QString& carsDir) {
    QDir dir(carsDir);
    if (!dir.exists()) return;
    for (const QFileInfo& info : dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        QString name = info.fileName();
        m_carPaths[name.toLower()] = info.filePath();
        emit scanProgress(m_cars.size(), 0);
    }
    m_cars = m_carPaths.keys();
    m_cars.sort();
}

void ContentDatabase::scanTracks(const QString& tracksDir) {
    QDir dir(tracksDir);
    if (!dir.exists()) return;
    for (const QFileInfo& info : dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        m_trackPaths[info.fileName().toLower()] = info.filePath();
    }
    m_tracks = m_trackPaths.keys();
    m_tracks.sort();
}

QString ContentDatabase::findCarPath(const QString& carName) const {
    return m_carPaths.value(carName.toLower());
}

QString ContentDatabase::findTrackPath(const QString& trackName) const {
    return m_trackPaths.value(trackName.toLower());
}

QString ContentDatabase::getCarDataAcd(const QString& carPath) const {
    return carPath + "/data.acd";
}

QString ContentDatabase::getCarUiDir(const QString& carPath) const {
    return carPath + "/ui";
}

QString ContentDatabase::getCarSfxDir(const QString& carPath) const {
    return carPath + "/sfx";
}

QString ContentDatabase::getCarSkinsDir(const QString& carPath) const {
    return carPath + "/skins";
}

bool ContentDatabase::isCarValid(const QString& carPath) const {
    return QFile::exists(getCarDataAcd(carPath)) || QDir(getCarUiDir(carPath)).exists();
}

bool ContentDatabase::isTrackValid(const QString& trackPath) const {
    return QDir(trackPath).exists();
}

ContentDatabase::CarInfo ContentDatabase::getCarInfo(const QString& carPath) const {
    CarInfo info;
    QFileInfo fi(carPath);
    info.name = fi.fileName();
    
    QString dataAcd = getCarDataAcd(carPath);
    if (QFile::exists(dataAcd)) {
        info.dataAcd = dataAcd;
    }
    
    QDir skinsDir(getCarSkinsDir(carPath));
    info.hasSkins = skinsDir.exists() && !skinsDir.isEmpty();
    
    return info;
}

CategoryManager::CategoryManager(QObject* parent) : QObject(parent) {
    QString appData = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    m_categoriesPath = appData + "/AcTools Content Manager/Data";
    loadCategories();
}

CategoryManager::~CategoryManager() {}
CategoryManager* CategoryManager::instance() { static CategoryManager inst; return &inst; }

void CategoryManager::loadCategories() {
    m_carCategories.clear();
    m_trackCategories.clear();

    QFile carFile(m_categoriesPath + "/Car Categories/List.json");
    if (carFile.open(QIODevice::ReadOnly)) {
        QJsonDocument doc = QJsonDocument::fromJson(carFile.readAll());
        QJsonArray arr = doc.array();
        for (const QJsonValue& v : arr) {
            QJsonObject obj = v.toObject();
            Category cat;
            cat.name = obj.value("name").toString();
            cat.description = obj.value("description").toString();
            cat.filter = obj.value("filter").toString();
            cat.icon = obj.value("icon").toString();
            cat.order = obj.value("order").toInt();
            cat.hidden = obj.value("hidden").toBool();
            m_carCategories.append(cat);
        }
    }

    QFile trackFile(m_categoriesPath + "/Track Categories/List.json");
    if (trackFile.open(QIODevice::ReadOnly)) {
        QJsonDocument doc = QJsonDocument::fromJson(trackFile.readAll());
        QJsonArray arr = doc.array();
        for (const QJsonValue& v : arr) {
            QJsonObject obj = v.toObject();
            Category cat;
            cat.name = obj.value("name").toString();
            cat.description = obj.value("description").toString();
            cat.filter = obj.value("filter").toString();
            cat.icon = obj.value("icon").toString();
            cat.order = obj.value("order").toInt();
            cat.hidden = obj.value("hidden").toBool();
            m_trackCategories.append(cat);
        }
    }
    emit categoriesLoaded();
}

void CategoryManager::saveCategories()
{
    QDir().mkpath(m_categoriesPath + "/Car Categories");
    QDir().mkpath(m_categoriesPath + "/Track Categories");

    QJsonArray carArr;
    for (const auto& cat : m_carCategories) {
        QJsonObject obj;
        obj["name"] = cat.name;
        obj["description"] = cat.description;
        obj["filter"] = cat.filter;
        obj["icon"] = cat.icon;
        obj["order"] = cat.order;
        obj["hidden"] = cat.hidden;
        carArr.append(obj);
    }
    QFile carFile(m_categoriesPath + "/Car Categories/List.json");
    if (carFile.open(QIODevice::WriteOnly))
        carFile.write(QJsonDocument(carArr).toJson());

    QJsonArray trackArr;
    for (const auto& cat : m_trackCategories) {
        QJsonObject obj;
        obj["name"] = cat.name;
        obj["description"] = cat.description;
        obj["filter"] = cat.filter;
        obj["icon"] = cat.icon;
        obj["order"] = cat.order;
        obj["hidden"] = cat.hidden;
        trackArr.append(obj);
    }
    QFile trackFile(m_categoriesPath + "/Track Categories/List.json");
    if (trackFile.open(QIODevice::WriteOnly))
        trackFile.write(QJsonDocument(trackArr).toJson());
}

QStringList CategoryManager::getCategoryNames() const {
    QStringList names;
    for (const Category& c : m_carCategories) names.append(c.name);
    return names;
}

QString CategoryManager::findCategoryIcon(const QString& categoryName) const {
    for (const Category& c : m_carCategories) {
        if (c.name == categoryName) return c.icon;
    }
    return QString();
}

void CategoryManager::addCarCategory(const Category& cat) { m_carCategories.append(cat); saveCategories(); }
void CategoryManager::addTrackCategory(const Category& cat) { m_trackCategories.append(cat); saveCategories(); }
void CategoryManager::removeCarCategory(const QString& name) {
    for (int i = 0; i < m_carCategories.size(); i++) {
        if (m_carCategories[i].name == name) { m_carCategories.removeAt(i); break; }
    }
    saveCategories();
}

void CategoryManager::removeTrackCategory(const QString& name) {
    for (int i = 0; i < m_trackCategories.size(); i++) {
        if (m_trackCategories[i].name == name) { m_trackCategories.removeAt(i); break; }
    }
    saveCategories();
}

PresetManager::PresetManager(QObject* parent) : QObject(parent) {
    QString appData = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    m_presetsPath = appData + "/AcTools Content Manager/Data/Built-in Presets";
    loadPresets();
}

PresetManager::~PresetManager() {}
PresetManager* PresetManager::instance() { static PresetManager inst; return &inst; }

void PresetManager::loadPresets() {
    m_presets.clear();
    QDir dir(m_presetsPath);
    for (const QFileInfo& typeInfo : dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        QString type = typeInfo.fileName();
        QDir typeDir(typeInfo.filePath());
        for (const QFileInfo& presetInfo : typeDir.entryInfoList()) {
            if (presetInfo.suffix() == "ini") {
                Preset preset;
                preset.name = presetInfo.baseName();
                preset.type = type;
                preset.filePath = presetInfo.filePath();
                m_presets[type].append(preset);
            }
        }
    }
    emit presetsLoaded();
}

void PresetManager::savePresets()
{
    for (auto it = m_presets.begin(); it != m_presets.end(); ++it) {
        QString typeDir = m_presetsPath + "/" + it.key();
        QDir().mkpath(typeDir);
        for (const auto& preset : it.value()) {
            QString filePath = typeDir + "/" + preset.name + ".ini";
            QFile f(filePath);
            if (!f.open(QIODevice::WriteOnly)) continue;
            f.write(QString("[Preset]\nname=%1\ntype=%2\ndescription=%3\n")
                    .arg(preset.name, preset.type, preset.description).toUtf8());
        }
    }
}

QList<PresetManager::Preset> PresetManager::getPresets(const QString& type) const {
    return m_presets.value(type);
}

QStringList PresetManager::getPresetTypes() const {
    return m_presets.keys();
}

QString PresetManager::findPreset(const QString& type, const QString& name) const {
    QList<Preset> presets = m_presets.value(type);
    for (const Preset& p : presets) {
        if (p.name == name) return p.filePath;
    }
    return QString();
}

QString PresetManager::findPresetFile(const QString& type, const QString& name) {
    return findPreset(type, name);
}

void PresetManager::addPreset(const Preset& preset) {
    m_presets[preset.type].append(preset);
    savePresets();
}

void PresetManager::removePreset(const QString& type, const QString& name) {
    QList<Preset>& presets = m_presets[type];
    for (int i = 0; i < presets.size(); i++) {
        if (presets[i].name == name) { presets.removeAt(i); break; }
    }
    savePresets();
}

LauncherSettings::LauncherSettings(QObject* parent) : QObject(parent) { load(); }
LauncherSettings::~LauncherSettings() {}

void LauncherSettings::load() {
    QString appData = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QSettings settings(appData + "/ksEditor/settings.ini", QSettings::IniFormat);
    m_autoDetectAc = settings.value("autoDetectAc", true).toBool();
    m_checkUpdates = settings.value("checkUpdates", true).toBool();
    m_simExeArgs = settings.value("simExeArgs", "").toString();
    m_maxRecent = settings.value("maxRecentProjects", 10).toInt();
    emit loaded();
}

void LauncherSettings::save() {
    QString appData = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QSettings settings(appData + "/ksEditor/settings.ini", QSettings::IniFormat);
    settings.setValue("autoDetectAc", m_autoDetectAc);
    settings.setValue("checkUpdates", m_checkUpdates);
    settings.setValue("simExeArgs", m_simExeArgs);
    settings.setValue("maxRecentProjects", m_maxRecent);
    emit modified();
}

void LauncherSettings::setAutoDetectAc(bool value) { m_autoDetectAc = value; save(); }
void LauncherSettings::setCheckForUpdates(bool value) { m_checkUpdates = value; save(); }
void LauncherSettings::setSimExeArgs(const QString& args) { m_simExeArgs = args; save(); }
void LauncherSettings::setMaxRecentProjects(int value) { m_maxRecent = value; save(); }

}