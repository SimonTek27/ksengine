#include "KsTest.h"
#include "engine/AI/AiFileReader.h"

#include <algorithm>
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

    {
        // lanes.csv — the merged world-space dump of the .ai splines. One file
        // holds several lanes, a radius column (not curvature), and blank
        // speeds where the source records carried sentinel values.
        const fs::path p = tmp("ks_qtfree_lanes.csv");
        std::ofstream out(p);
        out << "source_file,row_index,x,y,z,dist_m,speed,radius_m,edge_left,edge_right\n"
               "fast_lane.ai,0,0,0,0,0.000,30.0,1000.0,7.5,6.5\n"
               // blank speed -> must be interpolated from 30 and 40, not left at 0
               "fast_lane.ai,1,10,0,0,10.000,,50.0,7.5,6.5\n"
               "fast_lane.ai,2,20,0,0,20.000,40.0,50.0,7.5,6.5\n"
               "fast_lane.ai,3,30,0,0,30.000,50.0,1000.0,7.5,6.5\n"
               "pit_lane.ai,1,110,0,0,10.000,25.0,500.0,3.0,3.0\n"
               "pit_lane.ai,0,100,0,0,0.000,20.0,500.0,3.0,3.0\n";
        out.close();

        // No lane requested -> fast_lane wins over the pit lane.
        const ks::ai::AiSpline s = ks::ai::AiFileReader::readSpline(p.string());
        KS_CHECK(s.isValid());
        KS_CHECK(s.size() == 4);
        KS_CHECK_NEAR(s.points[0].position.x, 0.0f, 1e-6);
        KS_CHECK_NEAR(s.points[3].position.x, 30.0f, 1e-6);
        KS_CHECK_NEAR(s.totalDistance, 30.0f, 1e-5);

        // radius_m is a RADIUS in metres; the controller wants curvature.
        KS_CHECK_NEAR(s.points[0].curvature, 0.001f, 1e-6);
        KS_CHECK_NEAR(s.points[2].curvature, 0.02f, 1e-6);

        // Blank speed is interpolated, never 0 (0 = brake to a stop).
        KS_CHECK_NEAR(s.points[1].speed, 35.0f, 1e-6);
        KS_CHECK_NEAR(s.points[3].speed, 50.0f, 1e-6);

        // An explicit lane is honoured, including its row order.
        const ks::ai::AiSpline pit = ks::ai::AiFileReader::readLanesCsv(p.string(), "pit_lane.ai");
        KS_CHECK(pit.isValid());
        KS_CHECK(pit.size() == 2);
        KS_CHECK_NEAR(pit.points[0].position.x, 100.0f, 1e-6); // row_index 0 first
        KS_CHECK_NEAR(pit.points[1].position.x, 110.0f, 1e-6);
        KS_CHECK_NEAR(pit.totalDistance, 10.0f, 1e-5);
        KS_CHECK_NEAR(pit.points[1].speed, 25.0f, 1e-6);
        KS_CHECK_NEAR(pit.points[0].curvature, 0.002f, 1e-6);

        // Asking for a lane the file does not hold yields nothing.
        KS_CHECK(ks::ai::AiFileReader::readLanesCsv(p.string(), "nope.ai").empty());

        fs::remove(p);
    }

    {
        // A headered x,y,z CSV is NOT lanes.csv (no source_file column); it
        // must keep going through the plain text reader.
        const fs::path p = tmp("ks_qtfree_xyz.csv");
        std::ofstream out(p);
        out << "x,y,z\n0,0,0\n10,0,0\n";
        out.close();

        KS_CHECK(ks::ai::AiFileReader::readLanesCsv(p.string()).empty());
        const ks::ai::AiSpline s = ks::ai::AiFileReader::readSpline(p.string());
        KS_CHECK(s.isValid());
        KS_CHECK(s.size() == 2);
        KS_CHECK_NEAR(s.totalDistance, 10.0f, 1e-5);
        fs::remove(p);
    }

    {
        // Optional on-disk check: the real Mugello dump, when present.
        // Skipped on machines without it.
        const fs::path real =
            "E:/ksimulator/content/tracks/mugello/data/lanes.csv";
        if (fs::is_regular_file(real)) {
            const ks::ai::AiSpline s = ks::ai::AiFileReader::readSpline(real.string());
            KS_CHECK(s.isValid());
            KS_CHECK(s.size() == 3374); // fast_lane.ai rows
            KS_CHECK(s.closed);
            KS_CHECK(s.totalDistance > 5100.0f && s.totalDistance < 5300.0f);

            // No blank speeds in fast_lane, and every speed is a sane m/s.
            bool allSpeedsPresent = true;
            bool speedsInRange = true;
            for (const ks::ai::AiSplinePoint& pt : s.points) {
                if (pt.speed <= 0.0f) allSpeedsPresent = false;
                if (pt.speed > 200.0f) speedsInRange = false; // > 720 km/h
            }
            KS_CHECK(allSpeedsPresent);
            KS_CHECK(speedsInRange);

            // radius_m inversion, measured against the real file: the tightest
            // corner on the racing line is R = 46.9 m (Bucine hairpin), so the
            // largest curvature is 1/46.9 = 0.0213.
            float maxCurvature = 0.0f;
            for (const ks::ai::AiSplinePoint& pt : s.points)
                maxCurvature = std::max(maxCurvature, pt.curvature);
            KS_CHECK(maxCurvature > 0.02f && maxCurvature < 0.025f);

            // The ideal line must be selectable separately from the racing line.
            const ks::ai::AiSpline ideal =
                ks::ai::AiFileReader::readLanesCsv(real.string(), "ideal_line.ai");
            KS_CHECK(ideal.isValid());
            KS_CHECK(ideal.size() == 3350);
            // Its 5 sentinel records had blank speeds; all must be filled in.
            for (const ks::ai::AiSplinePoint& pt : ideal.points)
                KS_CHECK(pt.speed > 0.0f);
        }
    }

    return KS_TEST_RESULT("ai_line_test");
}
