#pragma once
#include "../EngineModule.h"
#include <cstdint>
#include <string>
#include <vector>

namespace ks {
namespace engine {
namespace archive {

/* ContentUnpacking - Archive Extraction Support
 * Status: STUB - Currently reads in-place from folders/zips
 */

class ContentUnpacking : public EngineModule {
public:
	static ContentUnpacking& instance() {
		static ContentUnpacking s;
		return s;
	}

	std::string moduleName() const override { return "ContentUnpacking"; }
	std::string moduleId() const override { return "ks.archive.unpacking"; }
	bool initialize() override;
	void shutdown() override;

	// Archive operations
	bool unpackArchive(const std::string& archivePath, const std::string& outputDir);
	bool extractToMemory(const std::string& filePath, std::vector<uint8_t>& data);

	// In-place reading (current behavior)
	bool readZipInPlace(const std::string& zipPath, const std::string& internalPath, std::vector<uint8_t>& data);
	bool listArchiveContents(const std::string& archivePath, std::vector<std::string>& contents);

private:
	ContentUnpacking() = default;
};

} // namespace archive
} // namespace engine
} // namespace ks
