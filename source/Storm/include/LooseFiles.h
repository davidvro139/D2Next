#pragma once
// A mod's loose files: <mod folder>/data/... read before the MPQs, as the game's -direct switch reads a data folder.
// Game paths are case-insensitive ("DATA\GLOBAL\EXCEL\misc.txt"), the folder may be on a case-sensitive filesystem
// (Android), so the folder is indexed once by lower-cased relative path. Only the data folder is indexed: the mod's
// MPQs next to it are mounted as archives.
#include <cctype>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>

namespace d2loose {

// "data/Global\\EXCEL/misc.txt" -> "data\\global\\excel\\misc.txt"
inline std::string key(std::string_view name)
{
	std::string out;
	out.reserve(name.size());
	for (const char c : name)
	{
		out += c == '/' ? '\\' : char(std::tolower(static_cast<unsigned char>(c)));
	}
	const size_t start = out.find_first_not_of('\\');
	return start == std::string::npos ? std::string() : out.substr(start);
}

class Folder
{
public:
	Folder() = default;
	// modFolder: <game>/Mods/<name>. Its data folder (any letter case) is indexed; no folder, no files.
	explicit Folder(const std::filesystem::path& modFolder)
	{
		std::error_code error;
		for (const auto& entry : std::filesystem::directory_iterator(modFolder, error))
		{
			if (entry.is_directory(error) && key(entry.path().filename().string()) == "data")
			{
				for (auto it = std::filesystem::recursive_directory_iterator(entry.path(), error);
					!error && it != std::filesystem::recursive_directory_iterator(); it.increment(error))
				{
					if (it->is_regular_file(error))
					{
						files_[key(std::filesystem::relative(it->path(), modFolder, error).generic_string())] = it->path();
					}
				}
			}
		}
	}

	// The loose file for a game path, or nullptr.
	const std::filesystem::path* find(std::string_view name) const
	{
		if (files_.empty())
		{
			return nullptr;
		}
		const auto it = files_.find(key(name));
		return it == files_.end() ? nullptr : &it->second;
	}

	size_t size() const { return files_.size(); }

private:
	std::unordered_map<std::string, std::filesystem::path> files_;
};

}
