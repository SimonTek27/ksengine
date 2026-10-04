#include "BankWriterInterface.h"
#include "BankWriterFactory.h"
#include "BankWriterFmod1x.h"
#include "BankWriterFmod2x.h"
#include "BankVersion.h"
#include <QDebug>

namespace ks { namespace fileformat {

class BankWriterRegistry : public QObject {
    Q_OBJECT
public:
    static BankWriterRegistry& instance() {
        static BankWriterRegistry registry;
        return registry;
    }

    void initialize() {
        if (m_initialized) return;

        auto* writer1x = new BankWriterFmod1x(this);
        BankWriterFactory::instance().registerWriter(BankVersion::FMOD_1_08, writer1x);
        BankWriterFactory::instance().registerWriter(BankVersion::FMOD_1_10, writer1x);
        BankWriterFactory::instance().registerWriter(BankVersion::FMOD_1_12, writer1x);
        BankWriterFactory::instance().registerWriter(GameTarget::AssettoCorsa1, writer1x);

        auto* writer2x = new BankWriterFmod2x(this);
        BankWriterFactory::instance().registerWriter(BankVersion::FMOD_2_00, writer2x);
        BankWriterFactory::instance().registerWriter(BankVersion::FMOD_2_01, writer2x);
        BankWriterFactory::instance().registerWriter(BankVersion::FMOD_2_02, writer2x);
        BankWriterFactory::instance().registerWriter(BankVersion::FMOD_2_03, writer2x);
        BankWriterFactory::instance().registerWriter(BankVersion::FMOD_2_10, writer2x);
        BankWriterFactory::instance().registerWriter(GameTarget::AssettoCorsaCompetizione, writer2x);

        auto* writerAcr = new BankWriterFmod2x(this);
        writerAcr->setGameTarget(GameTarget::AssettoCorsaRally);
        BankWriterFactory::instance().registerWriter(GameTarget::AssettoCorsaRally, writerAcr);

        auto* writerAce = new BankWriterFmod2x(this);
        writerAce->setGameTarget(GameTarget::AssettoCorsaEVO);
        BankWriterFactory::instance().registerWriter(GameTarget::AssettoCorsaEVO, writerAce);

        m_initialized = true;
        qDebug() << "BankWriterRegistry: All writers registered";
    }

private:
    BankWriterRegistry(QObject* parent = nullptr) : QObject(parent), m_initialized(false) {}
    bool m_initialized = false;
};

struct BankWriterAutoInit {
    BankWriterAutoInit() {
        BankWriterRegistry::instance().initialize();
    }
};
static BankWriterAutoInit g_bankWriterAutoInit;

}} // namespace ks::fileformat

#include "BankWriterRegistry.moc"