/**
 * kstirenorm — offline tire normalize / validate (Milliken Ch.14)
 *
 *   kstirenorm [--preset slick|street|wet|rally] [--ini path]
 *              [--fz0 4000] [--target-mu 1.3] [--normalize]
 *              [--curves out.csv] [--report out.txt]
 */

#include "TireNormalizeTool.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

static float argf(int argc, char** argv, const char* key, float def)
{
    for (int i = 1; i + 1 < argc; ++i)
        if (std::strcmp(argv[i], key) == 0)
            return static_cast<float>(std::atof(argv[i + 1]));
    return def;
}
static const char* args(int argc, char** argv, const char* key, const char* def)
{
    for (int i = 1; i + 1 < argc; ++i)
        if (std::strcmp(argv[i], key) == 0)
            return argv[i + 1];
    return def;
}
static bool has(int argc, char** argv, const char* key)
{
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], key) == 0)
            return true;
    return false;
}

int main(int argc, char** argv)
{
    if (has(argc, argv, "--help") || has(argc, argv, "-h")) {
        std::printf(
            "kstirenorm — tire data normalize/validate (Milliken Ch.14)\n"
            "  --preset NAME     slick|street|wet|rally (default slick)\n"
            "  --ini PATH        load coefficients from INI\n"
            "  --fz0 N           reference load (default nominal)\n"
            "  --target-mu M     target peak mu for --normalize\n"
            "  --normalize       scale coeffs toward target-mu\n"
            "  --curves FILE     write Fy/Fx pure-slip CSV\n"
            "  --report FILE     write text metrics report\n");
        return 0;
    }

    ks::physics::TireNormalizeTool tool;
    const char* ini = args(argc, argv, "--ini", "");
    if (ini && ini[0])
        tool.loadIni(ini);
    else
        tool.usePreset(args(argc, argv, "--preset", "slick"));

    const float fz0 = argf(argc, argv, "--fz0", -1.f);
    const float targetMu = argf(argc, argv, "--target-mu", 1.3f);

    auto m = tool.evaluate(fz0, 1.f);
    std::printf("# tire metrics  Fz0=%.0f  valid=%d\n", m.fz0, m.valid ? 1 : 0);
    std::printf("  muY=%.3f  peakFy=%.0f N  alphaPeak=%.1f deg  Ca=%.0f N/rad\n",
                m.muY, m.peakFy, m.alphaPeakDeg, m.corneringStiff);
    std::printf("  muX=%.3f  peakFx=%.0f N  kappaPeak=%.3f   Cs=%.0f N/1\n",
                m.muX, m.peakFx, m.kappaPeak, m.longStiff);
    std::printf("  loadSensRatio=%.3f  (peak@1.5Fz / 1.5*peak@Fz)\n", m.loadSensRatio);
    if (!m.message.empty())
        std::printf("  note: %s\n", m.message.c_str());

    if (has(argc, argv, "--normalize")) {
        m = tool.normalizeToMu(targetMu, fz0);
        std::printf("# after normalize → target mu=%.2f\n", targetMu);
        std::printf("  muY=%.3f  muX=%.3f  Ca=%.0f\n", m.muY, m.muX, m.corneringStiff);
        if (!m.message.empty())
            std::printf("  %s\n", m.message.c_str());
    }

    const char* curves = args(argc, argv, "--curves", "");
    if (curves && curves[0]) {
        auto lat = tool.lateralCurve(m.fz0);
        auto lng = tool.longitudinalCurve(m.fz0);
        if (ks::physics::TireNormalizeTool::writeCurveCsv(curves, lat, lng))
            std::printf("# wrote curves %s\n", curves);
        else
            std::fprintf(stderr, "failed curves %s\n", curves);
    }

    const char* report = args(argc, argv, "--report", "");
    if (report && report[0]) {
        if (ks::physics::TireNormalizeTool::writeReport(report, m, tool.coefficients()))
            std::printf("# wrote report %s\n", report);
        else
            std::fprintf(stderr, "failed report %s\n", report);
    }
    return m.valid ? 0 : 2;
}
