#include "BankParserInterface.h"
#include "BankParserFactory.h"
#include "BankParserFmod1x.h"
#include "BankParserFmod2x.h"
#include "BankVersion.h"
#include <QDebug>

namespace ks { namespace fileformat {

// ============================================================================
// BankParserRegistry — Initialize and register all bank parsers
// ============================================================================

class BankParserRegistry : public QObject {
    Q_OBJECT
public:
    static BankParserRegistry& instance() {
        static BankParserRegistry registry;
        return registry;
    }

    void initialize() {
        if (m_initialized) return;

        // Register FMOD 1.x parser (AC1)
        auto* parser1x = new BankParserFmod1x(this);
        BankParserFactory::instance().registerParser(BankVersion::FMOD_1_08, parser1x);
        BankParserFactory::instance().registerParser(BankVersion::FMOD_1_10, parser1x);
        BankParserFactory::instance().registerParser(BankVersion::FMOD_1_12, parser1x);
        BankParserFactory::instance().registerParser(GameTarget::AssettoCorsa1, parser1x);

        // Register FMOD 2.x parser (ACC, ACR, ACE)
        auto* parser2x = new BankParserFmod2x(this);
        BankParserFactory::instance().registerParser(BankVersion::FMOD_2_00, parser2x);
        BankParserFactory::instance().registerParser(BankVersion::FMOD_2_01, parser2x);
        BankParserFactory::instance().registerParser(BankVersion::FMOD_2_02, parser2x);
        BankParserFactory::instance().registerParser(BankVersion::FMOD_2_03, parser2x);
        BankParserFactory::instance().registerParser(BankVersion::FMOD_2_10, parser2x);
        BankParserFactory::instance().registerParser(GameTarget::AssettoCorsaCompetizione, parser2x);

        // ACR uses same version but different game target
        auto* parserAcr = new BankParserFmod2x(this);
        parserAcr->setGameTarget(GameTarget::AssettoCorsaRally);
        BankParserFactory::instance().registerParser(GameTarget::AssettoCorsaRally, parserAcr);

        // ACE - use 2.x parser for now
        auto* parserAce = new BankParserFmod2x(this);
        parserAce->setGameTarget(GameTarget::AssettoCorsaEVO);
        BankParserFactory::instance().registerParser(GameTarget::AssettoCorsaEVO, parserAce);

        m_initialized = true;
        qDebug() << "BankParserRegistry: All parsers registered";
    }

private:
    BankParserRegistry(QObject* parent = nullptr) : QObject(parent), m_initialized(false) {}
    bool m_initialized = false;
};

// Auto-initialize on first use
struct BankParserAutoInit {
    BankParserAutoInit() {
        BankParserRegistry::instance().initialize();
    }
};
static BankParserAutoInit g_bankParserAutoInit;

}} // namespace ks::fileformat

#include "BankParserRegistry.moc"