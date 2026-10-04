// kn5baker: offline content-pipeline tool. Converts a .kn5 (Assetto Corsa
// car/track model) into the .nmsh format ks::sim::NativeRenderer reads at
// runtime, so SimulatorApp never has to parse KN5 or link Qt to load content.
// See KN5Baker.h for the full rationale and known limitations.
//
// Usage: kn5baker <input.kn5> <output_directory>

#include "plugins/simulators/kunos/assettocorsa/acFiles/KN5Baker.h"

#include <QCoreApplication>
#include <QString>
#include <cstdio>

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);

    if (argc < 3) {
        fprintf(stderr, "usage: kn5baker <input.kn5> <output_directory>\n");
        return 1;
    }

    QString kn5Path = QString::fromLocal8Bit(argv[1]);
    std::string outputDir = argv[2];

    ks::tools::BakeResult result = ks::tools::bakeKN5ToNativeMeshes(kn5Path, outputDir);
    if (!result.success) {
        fprintf(stderr, "kn5baker: FAILED - %s\n", result.error.toLocal8Bit().constData());
        return 1;
    }

    printf("kn5baker: wrote %d mesh(es) to %s (skipped %d skinned/empty)\n",
          result.meshesWritten, outputDir.c_str(), result.meshesSkipped);
    return 0;
}
