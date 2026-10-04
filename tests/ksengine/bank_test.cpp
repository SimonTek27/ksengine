#include "KsTest.h"
#include "engine/Audio/BankParserBridge.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>

int main() {
    const ks::audio::ParsedBank missing =
        ks::audio::parseBankFile("definitely_missing_ks_qtfree_test.bank");
    KS_CHECK(!missing.valid);
    KS_CHECK(missing.events.empty());
    KS_CHECK(missing.sounds.empty());

    const ks::audio::ParsedBank empty = ks::audio::parseBankFile("");
    KS_CHECK(!empty.valid);
    KS_CHECK(empty.events.empty());

    const std::filesystem::path p =
        std::filesystem::temp_directory_path() / "ks_qtfree_garbage.bank";
    {
        std::ofstream out(p, std::ios::binary);
        const char junk[] = "NOTAFMODBANK0123456789";
        out.write(junk, sizeof(junk));
    }
    const ks::audio::ParsedBank garbage = ks::audio::parseBankFile(p.string());
    KS_CHECK(!garbage.valid);
    KS_CHECK(garbage.events.empty());
    KS_CHECK(garbage.sounds.empty());
    std::filesystem::remove(p);

    const ks::audio::ParsedBank directory =
        ks::audio::parseBankFile(std::filesystem::temp_directory_path().string());
    KS_CHECK(!directory.valid);

#ifdef KS_BANK_TEST_DIR
    const std::string dataDir = KS_BANK_TEST_DIR;

    const ks::audio::ParsedBank common =
        ks::audio::parseBankFile(dataDir + "/common.bank");
    KS_CHECK(common.valid);
    KS_CHECK(common.events.size() == 16);
    KS_CHECK(common.sounds.size() == 28);
    bool allSoundNames = true;
    for (const ks::audio::BankSoundMeta& s : common.sounds) {
        if (s.name.empty()) allSoundNames = false;
    }
    KS_CHECK(allSoundNames);

    bool foundSpeedParam = false;
    bool linkedAnyParams = false;
    for (const ks::audio::BankEventMeta& ev : common.events) {
        KS_CHECK(static_cast<int>(ev.parameterNames.size()) == ev.parameterCount);
        KS_CHECK(ev.parameterDefaults.size() == ev.parameterNames.size());
        if (ev.parameterCount > 0) linkedAnyParams = true;
        for (size_t i = 0; i < ev.parameterNames.size(); ++i) {
            if (ev.parameterNames[i] == "speed") {
                foundSpeedParam = true;
                KS_CHECK(ev.parameterDefaults[i] > 400.0f && ev.parameterDefaults[i] < 600.0f);
            }
        }
    }
    KS_CHECK(linkedAnyParams);
    KS_CHECK(foundSpeedParam);

    const ks::audio::ParsedBank strings =
        ks::audio::parseBankFile(dataDir + "/common.strings.bank");
    KS_CHECK(strings.valid);
    KS_CHECK(strings.events.empty());
    KS_CHECK(strings.sounds.empty());
    KS_CHECK(strings.stringTable.size() == 89u);

    // Roadmap 2.5 - event names resolved out of the companion .strings.bank
    // STDT radix tree instead of raw GUID strings.
    std::size_t readableEvents = 0;
    for (const ks::audio::BankEventMeta& ev : common.events) {
        if (ev.name.compare(0, 6, "event:") == 0) ++readableEvents;
    }
    KS_CHECK(readableEvents == common.events.size());

    // Roadmap 2.5 - FSB5 per-sample metadata: PCM16, real channel/rate/size.
    std::uint64_t spanTotal = 0;
    bool metaOk = true;
    for (const ks::audio::BankSoundMeta& s : common.sounds) {
        if (s.channels == 0 || s.sampleRate == 0) metaOk = false;
        if (s.codec != 2 || s.bytesPerSample != 2) metaOk = false;
        if (s.dataSize == 0) metaOk = false;
        if (static_cast<std::uint64_t>(s.sampleCount) * s.channels * 2 > s.dataSize)
            metaOk = false;
        if (s.dataOffset == 0) metaOk = false;
        spanTotal += s.dataSize;
    }
    KS_CHECK(metaOk);
    KS_CHECK(spanTotal == 43814944u);

    // Roadmap 2.5 - PCM16 sample extracted to a standalone RIFF/WAVE file.
    const std::filesystem::path wav =
        std::filesystem::temp_directory_path() / "ks_qtfree_fsb5_sample0.wav";
    KS_CHECK(ks::audio::extractBankSampleToWav(dataDir + "/common.bank", 0, wav.string()));
    KS_CHECK(std::filesystem::file_size(wav) == 44u + 122640u);
    KS_CHECK(!ks::audio::extractBankSampleToWav(dataDir + "/common.bank", 100000,
                                                wav.string()));
    std::filesystem::remove(wav);
#endif

    return KS_TEST_RESULT("bank_test");
}
