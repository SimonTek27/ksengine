// ks_cartool: content pipeline for Assetto Corsa car folders.
//
// The flow, in order:
//   1. convert  - write the seven documents (mandatory rule: every required entry)
//   2. verify   - compare the values with reality, `apply` the accepted fixes
//   3. prune    - remove the .ini files the documents supersede
//   4. pack     - rebuild data.acd from the folder as it is now
//
//   ks_cartool convert <car-dir> [dest-dir]
//   ks_cartool split   <car-dir>
//   ks_cartool verify  <car-dir>
//   ks_cartool apply   <car-dir>
//   ks_cartool prune   <car-dir>
//   ks_cartool info    <car-dir>

#include "../../src/adapters/assetto_corsa/AcCarManifestConverter.h"

#include <cstdio>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;
using ks::assetto_corsa::AcCarManifestConverter;

static void printInfo(const fs::path& car) {
    std::printf("car:           %s\n", car.string().c_str());
    for (const std::string& name : AcCarManifestConverter::documents()) {
        std::printf("  %-12s %s\n", name.c_str(),
                    fs::exists(AcCarManifestConverter::documentPath(car, name))
                        ? "present" : "missing");
    }
    std::printf("has manifest:  %s\n",
                AcCarManifestConverter::hasManifest(car) ? "yes" : "no");
    std::printf("needs convert: %s\n",
                AcCarManifestConverter::needsConversion(car) ? "yes" : "no");
    std::printf("checklist:     %s\n",
                fs::exists(AcCarManifestConverter::verifyPath(car)) ? "present" : "missing");
}

static void printRule(const AcCarManifestConverter::Result& result) {
    if (result.missing.empty()) {
        std::printf("rule: every required entry is there\n");
        return;
    }
    std::printf("rule: %d required entries missing\n",
                static_cast<int>(result.missing.size()));
    for (std::size_t i = 0; i < result.missing.size() && i < 12; ++i)
        std::printf("  - %s\n", result.missing[i].c_str());
    if (result.missing.size() > 12)
        std::printf("  ... +%d\n", static_cast<int>(result.missing.size() - 12));
}

static void printVerification(const AcCarManifestConverter::Result& result) {
    if (result.claims == 0) {
        std::printf("verification: no claims (is the car converted?)\n");
        return;
    }
    std::printf("verification: %d claims, %d checked, %d mismatch\n",
                result.claims, result.checked, result.mismatches);
}

static int runVerify(const fs::path& car) {
    const AcCarManifestConverter::Result result = AcCarManifestConverter::verify(car);
    if (!result.verified) {
        std::printf("verify failed: %s\n", result.error.c_str());
        return 1;
    }
    std::printf("checklist: %s\n", AcCarManifestConverter::verifyPath(car).string().c_str());
    printVerification(result);
    std::printf("record each outcome in that file (status match/mismatch/unknown, source, note),\n"
                "then run this command again to see the coverage.\n");
    return 0;
}

static int runApply(const fs::path& car) {
    const AcCarManifestConverter::Result result = AcCarManifestConverter::applyProposals(car);
    if (!result.error.empty()) {
        std::printf("apply failed: %s\n", result.error.c_str());
        return 1;
    }
    std::printf("apply: %d proposals applied, %d skipped (not accepted, or a curve)\n",
                result.applied, result.skipped);
    printVerification(result);
    return 0;
}

static int runPrune(const fs::path& car) {
    const AcCarManifestConverter::Result result = AcCarManifestConverter::prune(car);
    if (!result.pruneDone) {
        std::printf("prune refused: %s\n", result.error.c_str());
        if (!result.missing.empty()) printRule(result);
        return 1;
    }
    std::printf("prune: %d .ini files removed, everything else untouched\n", result.pruned);
    if (!result.packDone) {
        std::printf("data.acd NOT rebuilt: %s\n", result.error.c_str());
        return 1;
    }
    std::printf("data.acd rebuilt from %d files\n", result.packed);
    return 0;
}

static int runPack(const fs::path& car) {
    const AcCarManifestConverter::Result result = AcCarManifestConverter::packAcd(car);
    if (!result.packDone) {
        std::printf("pack failed: %s\n", result.error.c_str());
        return 1;
    }
    std::printf("pack: data.acd rebuilt from %d files\n", result.packed);
    return 0;
}

int main(int argc, char** argv) {
    if (argc < 3) {
        std::printf("usage: ks_cartool convert <car-dir> [dest-dir]\n"
                    "       ks_cartool split   <car-dir>\n"
                    "       ks_cartool verify  <car-dir>\n"
                    "       ks_cartool apply   <car-dir>\n"
                    "       ks_cartool prune   <car-dir>\n"
                    "       ks_cartool pack    <car-dir>\n"
                    "       ks_cartool info    <car-dir>\n");
        return 2;
    }
    const std::string command = argv[1];
    const fs::path src = argv[2];

    if (command == "info") {
        printInfo(src);
        return 0;
    }
    if (command == "verify") return runVerify(src);
    if (command == "apply") return runApply(src);
    if (command == "prune") return runPrune(src);
    if (command == "pack") return runPack(src);
    if (command == "split") {
        const AcCarManifestConverter::Result result = AcCarManifestConverter::split(src);
        if (!result.converted) {
            std::printf("split failed: %s\n", result.error.c_str());
            return 1;
        }
        std::printf("split %s -> %d documents in %s\n", src.string().c_str(),
                    result.documents, (src / "data").string().c_str());
        printRule(result);
        printVerification(result);
        return 0;
    }
    if (command != "convert") {
        std::printf("unknown command: %s\n", command.c_str());
        return 2;
    }

    const fs::path dst = argc > 3 ? fs::path(argv[3]) : src;
    if (dst != src) {
        std::error_code ec;
        fs::copy(src, dst, fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
        if (ec) {
            std::printf("copy failed: %s\n", ec.message().c_str());
            return 1;
        }
        std::printf("copied %s -> %s\n", src.string().c_str(), dst.string().c_str());
    }

    const AcCarManifestConverter::Result result = AcCarManifestConverter::convert(dst);
    if (!result.converted) {
        std::printf("convert failed: %s\n", result.error.c_str());
        return 1;
    }
    std::printf("converted %d .ini files, %d sections -> %d documents in %s\n",
                result.iniFiles, result.sections, result.documents,
                (dst / "data").string().c_str());
    printRule(result);
    printVerification(result);
    return 0;
}
