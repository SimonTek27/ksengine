#include "KsTest.h"
#include "engine/AI/AiFileReader.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

namespace {

fs::path tmp(const char* name) { return fs::temp_directory_path() / name; }

void writeFloats(std::ofstream& out, const float* v, std::size_t n) {
    out.write(reinterpret_cast<const char*>(v),
              static_cast<std::streamsize>(sizeof(float) * n));
}

} // namespace

int main() {
    {
        const fs::path p = tmp("ks_qtfree_ai_binary.ai");
        std::ofstream out(p, std::ios::binary);
        const unsigned int magic = 0x00414900u;
        const unsigned int version = 1u;
        const unsigned int count = 3u;
        out.write(reinterpret_cast<const char*>(&magic), 4);
        out.write(reinterpret_cast<const char*>(&version), 4);
        out.write(reinterpret_cast<const char*>(&count), 4);
        const float pts[] = {
            0.0f, 0.0f, 0.0f, 0.1f, 50.0f, //
            3.0f, 4.0f, 0.0f, 0.2f, 55.0f, //
            6.0f, 8.0f, 0.0f, 0.3f, 60.0f,
        };
        writeFloats(out, pts, sizeof(pts) / sizeof(float));
        out.close();

        const ks::ai::AiSpline s = ks::ai::AiFileReader::readSpline(p.string());
        KS_CHECK(s.isValid());
        KS_CHECK(s.size() == 3);
        KS_CHECK_NEAR(s.points[1].position.x, 3.0f, 1e-6);
        KS_CHECK_NEAR(s.points[1].position.y, 4.0f, 1e-6);
        KS_CHECK_NEAR(s.points[2].speed, 60.0f, 1e-6);
        KS_CHECK_NEAR(s.points[1].curvature, 0.2f, 1e-6);
        KS_CHECK_NEAR(s.points[0].distance, 0.0f, 1e-6);
        KS_CHECK_NEAR(s.points[1].distance, 5.0f, 1e-5);
        KS_CHECK_NEAR(s.points[2].distance, 10.0f, 1e-5);
        KS_CHECK_NEAR(s.totalDistance, 10.0f, 1e-5);
        KS_CHECK(!s.closed);
        KS_CHECK(s.points[1].lap == 0);
        fs::remove(p);
    }

    {
        const fs::path p = tmp("ks_qtfree_ai_closed.ai");
        std::ofstream out(p, std::ios::binary);
        const unsigned int magic = 0x00414900u;
        const unsigned int version = 1u;
        const unsigned int count = 4u;
        out.write(reinterpret_cast<const char*>(&magic), 4);
        out.write(reinterpret_cast<const char*>(&version), 4);
        out.write(reinterpret_cast<const char*>(&count), 4);
        const float pts[] = {
            0.0f, 0.0f, 0.0f, 0.0f, 50.0f,
            1.0f, 0.0f, 0.0f, 0.0f, 50.0f,
            1.0f, 1.0f, 0.0f, 0.0f, 50.0f,
            0.0f, 0.0f, 0.0f, 0.0f, 50.0f,
        };
        writeFloats(out, pts, sizeof(pts) / sizeof(float));
        out.close();

        const ks::ai::AiSpline s = ks::ai::AiFileReader::readSpline(p.string());
        KS_CHECK(s.isValid());
        KS_CHECK(s.size() == 4);
        KS_CHECK(s.closed);
        KS_CHECK_NEAR(s.totalDistance, 2.0f + std::sqrt(2.0f), 1e-5);
        fs::remove(p);
    }

    {
        const fs::path p = tmp("ks_qtfree_ai_truncated.ai");
        std::ofstream out(p, std::ios::binary);
        const unsigned int magic = 0x00414900u;
        const unsigned int version = 1u;
        const unsigned int count = 5u;
        out.write(reinterpret_cast<const char*>(&magic), 4);
        out.write(reinterpret_cast<const char*>(&version), 4);
        out.write(reinterpret_cast<const char*>(&count), 4);
        const float one[] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
        writeFloats(out, one, 5);
        out.close();

        const ks::ai::AiSpline s = ks::ai::AiFileReader::readSpline(p.string());
        KS_CHECK(!s.isValid());
        KS_CHECK(s.empty());
        fs::remove(p);
    }

    {
        const fs::path p = tmp("ks_qtfree_ai_csv.ai");
        std::ofstream out(p);
        out << "# comment line\n"
               "0,0,0,0.1,50\n"
               "\n"
               "3,4,0,0.2,55\n"
               "6 8 0 0.3 60\n";
        out.close();

        const ks::ai::AiSpline s = ks::ai::AiFileReader::readSpline(p.string());
        KS_CHECK(s.isValid());
        KS_CHECK(s.size() == 3);
        KS_CHECK_NEAR(s.points[1].position.x, 3.0f, 1e-6);
        KS_CHECK_NEAR(s.points[2].position.y, 8.0f, 1e-6);
        KS_CHECK_NEAR(s.points[2].speed, 60.0f, 1e-6);
        KS_CHECK_NEAR(s.totalDistance, 10.0f, 1e-5);
        KS_CHECK(!s.closed);
        fs::remove(p);
    }

    {
        const ks::ai::AiSpline s =
            ks::ai::AiFileReader::readSpline(tmp("ks_qtfree_missing.ai").string());
        KS_CHECK(!s.isValid());
        KS_CHECK(s.empty());
    }

    return KS_TEST_RESULT("ai_line_test");
}
