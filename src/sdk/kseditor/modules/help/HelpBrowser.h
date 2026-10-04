#pragma once

#include <QDialog>
#include <QString>
#include <QVector>
#include <QMap>

class QTreeWidget;
class QTreeWidgetItem;
class QTextBrowser;

namespace ks {

struct HelpTopic {
    QString id;
    QString title;
    QString category;
    QString content;
};

class HelpContentRegistry {
public:
    static void registerTopic(const HelpTopic& topic);
    static QVector<HelpTopic> allTopics();
    static HelpTopic findTopic(const QString& id);
    static QVector<HelpTopic> topicsByCategory(const QString& category);
    static QStringList categories();
    static void loadDefaults();
};

class HelpBrowser : public QDialog {
    Q_OBJECT
public:
    explicit HelpBrowser(QWidget* parent = nullptr);

    void showTopic(const QString& topicId);
    static QString categoryForContext(const QString& context);

private slots:
    void onTopicSelected(QTreeWidgetItem* current, QTreeWidgetItem* previous);

private:
    void buildTopicTree();

    QTreeWidget* m_treeWidget = nullptr;
    QTextBrowser* m_textBrowser = nullptr;
    QString htmlContent;
};

} // namespace ks