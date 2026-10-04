#include "ProfileDialog.h"
#include "engine/sys/UserProfile.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

namespace ks {

// ----------------------------------------------------------------------------
// Layout helpers (same visual language as PreferencesDialog)
// ----------------------------------------------------------------------------

static QString dialogSheet()
{
    return QStringLiteral(
        "QDialog { background-color: #18181b; }"
        "QLabel { color: #d4d4d8; font-size: 12px; background: transparent; }"
        "QLineEdit, QComboBox { background-color: #27272a; color: #fafafa;"
        "  border: 1px solid #3f3f46; border-radius: 4px; padding: 5px 8px;"
        "  min-height: 20px; }"
        "QLineEdit:focus, QComboBox:focus { border-color: #3b82f6; }"
        "QComboBox::drop-down { border: none; width: 22px; }"
        "QComboBox QAbstractItemView { background-color: #27272a; color: #fafafa;"
        "  selection-background-color: #3b82f6; border: 1px solid #3f3f46; }"
        "QPushButton { background-color: #27272a; color: #d4d4d8;"
        "  border: 1px solid #3f3f46; border-radius: 4px; padding: 6px 14px; }"
        "QPushButton:hover { background-color: #3f3f46; color: #fafafa; }"
        "QPushButton#primaryBtn { background-color: #3b82f6; color: white;"
        "  border: none; font-weight: 600; }"
        "QPushButton#primaryBtn:hover { background-color: #2563eb; }"
        "QScrollArea { background: transparent; border: none; }"
        "QDialogButtonBox { border-top: 1px solid #27272a; }");
}

static QLabel* sectionHeader(const QString& title)
{
    auto* label = new QLabel(title.toUpper());
    label->setStyleSheet(QStringLiteral(
        "QLabel { color: #a1a1aa; font-size: 11px; font-weight: 600;"
        " letter-spacing: 0.6px; padding-bottom: 2px; }"));
    return label;
}

// ----------------------------------------------------------------------------
// ProfileDialog
// ----------------------------------------------------------------------------

ProfileDialog::ProfileDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Creator Profile"));
    setModal(true);
    resize(640, 640);
    setMinimumSize(520, 440);
    setStyleSheet(dialogSheet());

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    outer->addWidget(scroll);

    auto* content = new QWidget;
    content->setStyleSheet(QStringLiteral("background: transparent;"));
    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(24, 20, 24, 24);
    layout->setSpacing(18);
    scroll->setWidget(content);

    layout->addWidget(createIdentityPage());
    layout->addWidget(createCreditsPage());
    layout->addWidget(createContentPage());
    layout->addStretch(1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    outer->addWidget(buttons);
    QPushButton* saveBtn = buttons->button(QDialogButtonBox::Save);
    saveBtn->setObjectName(QStringLiteral("primaryBtn"));
    saveBtn->setText(tr("Save && Close"));
    connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
        applyToProfile();
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    loadFromProfile();
}

void ProfileDialog::showProfile(QWidget* parent)
{
    static QPointer<ProfileDialog> dialog;
    if (!dialog) {
        dialog = new ProfileDialog(parent);
        dialog->setAttribute(Qt::WA_DeleteOnClose, true);
    }
    dialog->show();
    dialog->raise();
    dialog->activateWindow();
}

// ----------------------------------------------------------------------------
// Form construction
// ----------------------------------------------------------------------------

static QWidget* formShell(QFormLayout*& form)
{
    auto* shell = new QWidget;
    auto* l = new QVBoxLayout(shell);
    l->setContentsMargins(0, 0, 0, 0);
    l->setSpacing(8);
    form = new QFormLayout;
    form->setHorizontalSpacing(18);
    form->setVerticalSpacing(10);
    l->addLayout(form);
    return shell;
}

static void addRow(QFormLayout* form, const QString& name, const QString& tooltip,
                   QWidget* control)
{
    auto* label = new QLabel(name);
    label->setToolTip(tooltip);
    control->setToolTip(tooltip);
    form->addRow(label, control);
}

QWidget* ProfileDialog::createIdentityPage()
{
    QFormLayout* form = nullptr;
    auto* page = formShell(form);
    auto* v = qobject_cast<QVBoxLayout*>(page->layout());
    v->insertWidget(0, sectionHeader(tr("Identity")));

    m_profileName = new QLineEdit;
    addRow(form, tr("Profile name"),
           tr("Name of this creator profile (stored in profile.json)."),
           m_profileName);

    m_name = new QLineEdit;
    addRow(form, tr("Display name"), tr("Name shown in exported credits."), m_name);

    m_email = new QLineEdit;
    addRow(form, tr("Email"), tr("Contact email written into mod metadata."), m_email);

    m_website = new QLineEdit;
    addRow(form, tr("Website"), tr("Homepage URL written into mod metadata."), m_website);

    m_discord = new QLineEdit;
    addRow(form, tr("Discord"), tr("Discord handle for support links."), m_discord);

    return page;
}

QWidget* ProfileDialog::createCreditsPage()
{
    QFormLayout* form = nullptr;
    auto* page = formShell(form);
    auto* v = qobject_cast<QVBoxLayout*>(page->layout());
    v->insertWidget(0, sectionHeader(tr("Credits")));

    m_keyFigure = new QLineEdit;
    addRow(form, tr("Key figure"), tr("Lead person listed first in the credits."),
           m_keyFigure);

    m_threeDArtist = new QLineEdit;
    addRow(form, tr("3D artist"), tr("Author of the meshes."), m_threeDArtist);

    m_textureArtist = new QLineEdit;
    addRow(form, tr("Texture artist"), tr("Author of the liveries and textures."),
           m_textureArtist);

    m_animations = new QLineEdit;
    addRow(form, tr("Animations"), tr("Author of the animation sets."), m_animations);

    m_physics = new QLineEdit;
    addRow(form, tr("Physics"), tr("Author of the handling data."), m_physics);

    m_soundArtist = new QLineEdit;
    addRow(form, tr("Sound"), tr("Author of the engine and environment sounds."),
           m_soundArtist);

    m_coding = new QLineEdit;
    addRow(form, tr("Coding"), tr("Author of scripts and plugins."), m_coding);

    m_moddingLogo = new QLineEdit;
    addRow(form, tr("Logo"), tr("Logo file bundled with the mod."), m_moddingLogo);

    return page;
}

QWidget* ProfileDialog::createContentPage()
{
    QFormLayout* form = nullptr;
    auto* page = formShell(form);
    auto* v = qobject_cast<QVBoxLayout*>(page->layout());
    v->insertWidget(0, sectionHeader(tr("Content")));

    // Sim install path: [line edit] [Browse...]
    auto* pathEdit = new QLineEdit;
    auto* browse = new QPushButton(tr("Browse..."));
    auto* pathRow = new QWidget;
    auto* h = new QHBoxLayout(pathRow);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(6);
    h->addWidget(pathEdit, 1);
    h->addWidget(browse);
    m_simInstallPath = pathEdit;
    addRow(form, tr("Simulator install"),
           tr("Target racing sim installation used when exporting mods."), pathRow);
    connect(browse, &QPushButton::clicked, this, [this]() {
        const QString dir = QFileDialog::getExistingDirectory(
            this, tr("Select simulator install folder"), m_simInstallPath->text());
        if (!dir.isEmpty())
            m_simInstallPath->setText(dir);
    });

    m_contentType = new QComboBox;
    m_contentType->addItems({tr("car"), tr("track")});
    addRow(form, tr("Preferred content type"),
           tr("Default content type preselected when creating a new mod."),
           m_contentType);

    m_licenseType = new QComboBox;
    m_licenseType->addItems({tr("Free"), tr("Premium"), tr("Commercial"), tr("Other")});
    addRow(form, tr("License"),
           tr("License stamped into exported content metadata."), m_licenseType);

    return page;
}

// ----------------------------------------------------------------------------
// Profile binding
// ----------------------------------------------------------------------------

void ProfileDialog::loadFromProfile()
{
    CreatorProfile* p = CreatorProfile::instance();

    m_profileName->setText(p->profileName());
    m_name->setText(p->name());
    m_email->setText(p->email());
    m_website->setText(p->website());
    m_discord->setText(p->discord());

    m_keyFigure->setText(p->keyFigure());
    m_threeDArtist->setText(p->threeDArtist());
    m_textureArtist->setText(p->textureArtist());
    m_animations->setText(p->animations());
    m_physics->setText(p->physics());
    m_soundArtist->setText(p->soundArtist());
    m_coding->setText(p->coding());
    m_moddingLogo->setText(p->moddingLogo());

    m_simInstallPath->setText(p->simInstallPath());

    const QString contentType = p->preferredContentType();
    int ctIndex = m_contentType->findText(contentType);
    if (ctIndex < 0)
        ctIndex = m_contentType->findText(tr("car"));
    m_contentType->setCurrentIndex(qMax(0, ctIndex));

    const QString license = p->licenseType();
    int liIndex = m_licenseType->findText(license);
    if (liIndex < 0)
        liIndex = m_licenseType->findText(tr("Free"));
    m_licenseType->setCurrentIndex(qMax(0, liIndex));
}

void ProfileDialog::applyToProfile()
{
    CreatorProfile* p = CreatorProfile::instance();

    // Each setter persists immediately; a failure is reported via the
    // profileSaved/error signals so one save() per field is acceptable here.
    p->setProfileName(m_profileName->text().trimmed());
    p->setName(m_name->text());
    p->setEmail(m_email->text());
    p->setWebsite(m_website->text());
    p->setDiscord(m_discord->text());

    p->setKeyFigure(m_keyFigure->text());
    p->setThreeDArtist(m_threeDArtist->text());
    p->setTextureArtist(m_textureArtist->text());
    p->setAnimations(m_animations->text());
    p->setPhysics(m_physics->text());
    p->setSoundArtist(m_soundArtist->text());
    p->setCoding(m_coding->text());
    p->setModdingLogo(m_moddingLogo->text());

    p->setSimInstallPath(m_simInstallPath->text());
    p->setPreferredContentType(m_contentType->currentText());
    p->setLicenseType(m_licenseType->currentText());
}

} // namespace ks
