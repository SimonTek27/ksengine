#pragma once

#include <QDialog>
#include <QString>

class QWidget;
class QLineEdit;
class QComboBox;
class QPushButton;

namespace ks {

// Creator profile editor: identity, credits and content settings bound to
// ks::CreatorProfile. Mirrors PreferencesDialog::showSettings usage.
class ProfileDialog : public QDialog {
    Q_OBJECT
public:
    explicit ProfileDialog(QWidget* parent = nullptr);

    static void showProfile(QWidget* parent);

private:
    void loadFromProfile();
    void applyToProfile();
    QWidget* createIdentityPage();
    QWidget* createCreditsPage();
    QWidget* createContentPage();

    QLineEdit* m_profileName = nullptr;
    QLineEdit* m_name = nullptr;
    QLineEdit* m_email = nullptr;
    QLineEdit* m_website = nullptr;
    QLineEdit* m_discord = nullptr;

    QLineEdit* m_keyFigure = nullptr;
    QLineEdit* m_threeDArtist = nullptr;
    QLineEdit* m_textureArtist = nullptr;
    QLineEdit* m_animations = nullptr;
    QLineEdit* m_physics = nullptr;
    QLineEdit* m_soundArtist = nullptr;
    QLineEdit* m_coding = nullptr;
    QLineEdit* m_moddingLogo = nullptr;

    QLineEdit* m_simInstallPath = nullptr;
    QComboBox* m_contentType = nullptr;
    QComboBox* m_licenseType = nullptr;
};

} // namespace ks
