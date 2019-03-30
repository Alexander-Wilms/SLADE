
// -----------------------------------------------------------------------------
// SLADE - It's a Doom Editor
// Copyright(C) 2008 - 2019 Simon Judd
//
// Email:       sirjuddington@gmail.com
// Web:         http://slade.mancubus.net
// Filename:    Archive.cpp
// Description: Functions to export Archive-related types and namespaces to lua
//              using sol3
//
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the Free
// Software Foundation; either version 2 of the License, or (at your option)
// any later version.
//
// This program is distributed in the hope that it will be useful, but WITHOUT
// ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
// FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
// more details.
//
// You should have received a copy of the GNU General Public License along with
// this program; if not, write to the Free Software Foundation, Inc.,
// 51 Franklin Street, Fifth Floor, Boston, MA  02110 - 1301, USA.
// -----------------------------------------------------------------------------


// -----------------------------------------------------------------------------
//
// Includes
//
// -----------------------------------------------------------------------------
#include "Main.h"
#include "Archive/Archive.h"
#include "Archive/ArchiveManager.h"
#include "Archive/Formats/All.h"
#include "General/Misc.h"
#include "Utility/StringUtils.h"
#include "thirdparty/sol/sol.hpp"


// -----------------------------------------------------------------------------
//
// Lua Namespace Functions
//
// -----------------------------------------------------------------------------
namespace Lua
{
// -----------------------------------------------------------------------------
// Returns a vector of all open archives.
// If [resources_only] is true, only includes archives marked as resources
// -----------------------------------------------------------------------------
vector<Archive*> allArchives(bool resources_only)
{
	vector<Archive*> list;
	for (int a = 0; a < App::archiveManager().numArchives(); a++)
	{
		auto archive = App::archiveManager().getArchive(a);

		if (resources_only && !App::archiveManager().archiveIsResource(archive))
			continue;

		list.push_back(archive);
	}
	return list;
}

// -----------------------------------------------------------------------------
// Returns the name of entry [self] with requested formatting:
// [include_path] - if true, include the path to the entry
// [include_extension] - if true, include the extension
// [name_uppercase] - if true, return the name in uppercase (except the path)
// -----------------------------------------------------------------------------
std::string formattedEntryName(ArchiveEntry& self, bool include_path, bool include_extension, bool name_uppercase)
{
	std::string name;
	if (include_path)
		name = self.path();
	if (name_uppercase)
		name.append(include_extension ? self.upperName() : self.upperNameNoExt());
	else
		name.append(include_extension ? self.name() : self.nameNoExt());
	return name;
}

// -----------------------------------------------------------------------------
// Returns a vector of all entries in the archive [self]
// -----------------------------------------------------------------------------
vector<ArchiveEntry*> archiveAllEntries(Archive& self)
{
	vector<ArchiveEntry*> list;
	self.putEntryTreeAsList(list);
	return list;
}

// -----------------------------------------------------------------------------
// Creates a new entry in archive [self] at [full_path],[position-1].
// Returns the created entry
// -----------------------------------------------------------------------------
ArchiveEntry* archiveCreateEntry(Archive& self, std::string_view full_path, int position)
{
	auto dir = self.dir(StrUtil::beforeLast(full_path, '/'));
	return self.addNewEntry(StrUtil::afterLast(full_path, '/'), position - 1, dir);
}

// -----------------------------------------------------------------------------
// Creates a new entry in archive [self] with [name] in namespace [ns].
// Returns the created entry
// -----------------------------------------------------------------------------
ArchiveEntry* archiveCreateEntryInNamespace(Archive& self, std::string_view name, std::string_view ns)
{
	return self.addNewEntry(name, ns);
}

// -----------------------------------------------------------------------------
// Returns a list of all entries in the archive dir [self]
// -----------------------------------------------------------------------------
vector<ArchiveEntry*> archiveDirEntries(ArchiveTreeNode& self)
{
	vector<ArchiveEntry*> list;
	for (const auto& e : self.entries())
		list.push_back(e.get());
	return list;
}

// -----------------------------------------------------------------------------
// Returns a list of all subdirs in the archive dir [self]
// -----------------------------------------------------------------------------
vector<ArchiveTreeNode*> archiveDirSubDirs(ArchiveTreeNode& self)
{
	vector<ArchiveTreeNode*> dirs;
	for (auto child : self.allChildren())
		dirs.push_back(dynamic_cast<ArchiveTreeNode*>(child));
	return dirs;
}

// -----------------------------------------------------------------------------
// Registers the ArchiveFormat type with lua
// -----------------------------------------------------------------------------
void registerArchiveFormat(sol::state& lua)
{
	// Create ArchiveFormat type, no constructor
	auto lua_archiveformat = lua.new_usertype<ArchiveFormat>("ArchiveFormat", "new", sol::no_constructor);

	// Properties
	// -------------------------------------------------------------------------
	lua_archiveformat["id"]            = sol::readonly(&ArchiveFormat::id);
	lua_archiveformat["name"]          = sol::readonly(&ArchiveFormat::name);
	lua_archiveformat["supportsDirs"]  = sol::readonly(&ArchiveFormat::supports_dirs);
	lua_archiveformat["hasExtensions"] = sol::readonly(&ArchiveFormat::names_extensions);
	lua_archiveformat["maxNameLength"] = sol::readonly(&ArchiveFormat::max_name_length);
	lua_archiveformat["entryFormat"]   = sol::readonly(&ArchiveFormat::entry_format);
	// TODO: extensions - need to export key_value_t or do something custom
}

// -----------------------------------------------------------------------------
// Registers the ArchiveSearchOptions type with lua
// -----------------------------------------------------------------------------
void registerArchiveSearchOptions(sol::state& lua)
{
	auto lua_search_opt = lua.new_usertype<Archive::SearchOptions>(
		"ArchiveSearchOptions", "new", sol::constructors<Archive::SearchOptions()>());

	// Properties
	// -------------------------------------------------------------------------
	lua_search_opt["matchName"]      = sol::property(&Archive::SearchOptions::match_name);
	lua_search_opt["matchType"]      = sol::property(&Archive::SearchOptions::match_type);
	lua_search_opt["matchNamespace"] = sol::property(&Archive::SearchOptions::match_namespace);
	lua_search_opt["dir"]            = sol::property(&Archive::SearchOptions::dir);
	lua_search_opt["ignoreExt"]      = sol::property(&Archive::SearchOptions::ignore_ext);
	lua_search_opt["searchSubdirs"]  = sol::property(&Archive::SearchOptions::search_subdirs);
}

// -----------------------------------------------------------------------------
// Registers the Archive type with lua
// -----------------------------------------------------------------------------
void registerArchive(sol::state& lua)
{
	// Create Archive type, no constructor
	auto lua_archive = lua.new_usertype<Archive>("Archive", "new", sol::no_constructor);

	// Properties
	// -------------------------------------------------------------------------
	lua_archive["filename"] = sol::property([](Archive& self) { return self.filename(); });
	lua_archive["entries"]  = sol::property(&archiveAllEntries);
	lua_archive["rootDir"]  = sol::property(&Archive::rootDir);
	lua_archive["format"]   = sol::property(&Archive::formatDesc);

	// Functions
	// -------------------------------------------------------------------------
	lua_archive["FilenameNoPath"]         = [](Archive& self) { return self.filename(false); };
	lua_archive["EntryAtPath"]            = &Archive::entryAtPath;
	lua_archive["DirAtPath"]              = [](Archive& self, const std::string& path) { return self.dir(path); };
	lua_archive["CreateEntry"]            = &archiveCreateEntry;
	lua_archive["CreateEntryInNamespace"] = &archiveCreateEntryInNamespace;
	lua_archive["RemoveEntry"]            = &Archive::removeEntry;
	lua_archive["RenameEntry"]            = &Archive::renameEntry;
	lua_archive["Save"]                   = sol::overload(
        [](Archive& self) { return std::make_tuple(self.save(), Global::error); },
        [](Archive& self, const std::string& filename) { return std::make_tuple(self.save(filename), Global::error); });
	lua_archive["FindFirst"] = &Archive::findFirst;
	lua_archive["FindLast"]  = &Archive::findLast;
	lua_archive["FindAll"]   = &Archive::findAll;

// Register all subclasses
// (perhaps it'd be a good idea to make Archive not abstract and handle
//  the format-specific stuff somewhere else, rather than in subclasses)
#define REGISTER_ARCHIVE(type) lua.new_usertype<type>(#type, sol::base_classes, sol::bases<Archive>())
	REGISTER_ARCHIVE(WadArchive);
	REGISTER_ARCHIVE(ZipArchive);
	REGISTER_ARCHIVE(LibArchive);
	REGISTER_ARCHIVE(DatArchive);
	REGISTER_ARCHIVE(ResArchive);
	REGISTER_ARCHIVE(PakArchive);
	REGISTER_ARCHIVE(BSPArchive);
	REGISTER_ARCHIVE(GrpArchive);
	REGISTER_ARCHIVE(RffArchive);
	REGISTER_ARCHIVE(GobArchive);
	REGISTER_ARCHIVE(LfdArchive);
	REGISTER_ARCHIVE(HogArchive);
	REGISTER_ARCHIVE(ADatArchive);
	REGISTER_ARCHIVE(Wad2Archive);
	REGISTER_ARCHIVE(WadJArchive);
	REGISTER_ARCHIVE(WolfArchive);
	REGISTER_ARCHIVE(GZipArchive);
	REGISTER_ARCHIVE(BZip2Archive);
	REGISTER_ARCHIVE(TarArchive);
	REGISTER_ARCHIVE(DiskArchive);
	REGISTER_ARCHIVE(PodArchive);
	REGISTER_ARCHIVE(ChasmBinArchive);
#undef REGISTER_ARCHIVE
}

// -----------------------------------------------------------------------------
// Returns the data of entry [self] as a string
// Lua doesn't really have a dedicated binary array type so strings are
// generally used for that kind of thing
// -----------------------------------------------------------------------------
std::string entryData(ArchiveEntry& self)
{
	return std::string((const char*)self.rawData(), self.size());
}

// -----------------------------------------------------------------------------
// Imports data from [string] into entry [self]
// -----------------------------------------------------------------------------
std::tuple<bool, std::string> entryImportString(ArchiveEntry& self, const std::string& string)
{
	return std::make_tuple(self.importMem(string.data(), string.size()), Global::error);
}

// -----------------------------------------------------------------------------
// Registers the ArchiveEntry type with lua
// -----------------------------------------------------------------------------
void registerArchiveEntry(sol::state& lua)
{
	// Create ArchiveEntry type, no constructor
	auto lua_entry = lua.new_usertype<ArchiveEntry>("ArchiveEntry", "new", sol::no_constructor);

	// Properties
	// -------------------------------------------------------------------------
	lua_entry["name"]  = sol::property([](ArchiveEntry& self) { return self.name(); });
	lua_entry["path"]  = sol::property([](ArchiveEntry& self) { return self.path(); });
	lua_entry["type"]  = sol::property(&ArchiveEntry::type);
	lua_entry["size"]  = sol::property(&ArchiveEntry::size);
	lua_entry["index"] = sol::property([](ArchiveEntry& self) { return self.parentDir()->entryIndex(&self) + 1; });
	lua_entry["crc32"] = sol::property([](ArchiveEntry& self) { return Misc::crc(self.rawData(), self.size()); });
	lua_entry["data"]  = sol::property(&entryData);

	// Functions
	// -------------------------------------------------------------------------
	lua_entry["FormattedName"] = sol::overload(
		[](ArchiveEntry& self) { return formattedEntryName(self, true, true, false); },
		[](ArchiveEntry& self, bool include_path) { return formattedEntryName(self, include_path, true, false); },
		[](ArchiveEntry& self, bool include_path, bool include_extension) {
			return formattedEntryName(self, include_path, include_extension, false);
		},
		&formattedEntryName);
	lua_entry["FormattedSize"] = &ArchiveEntry::sizeString;
	lua_entry["ImportFile"]    = [](ArchiveEntry& self, std::string_view filename) {
        return std::make_tuple(self.importFile(filename), Global::error);
	};
	lua_entry["ImportEntry"] = [](ArchiveEntry& self, ArchiveEntry* entry) {
		return std::make_tuple(self.importEntry(entry), Global::error);
	};
	lua_entry["ImportData"] = &entryImportString;
	lua_entry["ExportFile"] = [](ArchiveEntry& self, std::string_view filename) {
		return std::make_tuple(self.exportFile(filename), Global::error);
	};
}

// -----------------------------------------------------------------------------
// Registers the ArchiveDir type with lua
// -----------------------------------------------------------------------------
void registerArchiveTreeNode(sol::state& lua)
{
	// Create ArchiveDir type, no constructor
	auto lua_dir = lua.new_usertype<ArchiveTreeNode>("ArchiveDir", "new", sol::no_constructor);

	// Properties
	// -------------------------------------------------------------------------
	lua_dir["name"]    = sol::property(&ArchiveTreeNode::name);
	lua_dir["archive"] = sol::property(&ArchiveTreeNode::archive);
	lua_dir["entries"] = sol::property(&archiveDirEntries);
	lua_dir["parent"]  = sol::property(
        [](ArchiveTreeNode& self) { return dynamic_cast<ArchiveTreeNode*>(self.parent()); });
	lua_dir["path"]           = sol::property(&ArchiveTreeNode::path);
	lua_dir["subDirectories"] = sol::property(&archiveDirSubDirs);
}

// -----------------------------------------------------------------------------
// Registers the EntryType type with lua
// -----------------------------------------------------------------------------
void registerEntryType(sol::state& lua)
{
	// Create EntryType type, no constructor
	auto lua_etype = lua.new_usertype<EntryType>("EntryType", "new", sol::no_constructor);

	// Properties
	// -------------------------------------------------------------------------
	lua_etype["id"]        = sol::property(&EntryType::id);
	lua_etype["name"]      = sol::property(&EntryType::name);
	lua_etype["extension"] = sol::property(&EntryType::extension);
	lua_etype["formatId"]  = sol::property(&EntryType::formatId);
	lua_etype["editor"]    = sol::property(&EntryType::editor);
	lua_etype["category"]  = sol::property(&EntryType::category);
}

// -----------------------------------------------------------------------------
// Registers the Archives namespace with lua
// -----------------------------------------------------------------------------
void registerArchivesNamespace(sol::state& lua)
{
	auto archives = lua.create_table("Archives");

	archives["All"]    = sol::overload(&allArchives, []() { return allArchives(false); });
	archives["Create"] = [](std::string_view format) {
		return std::make_tuple(App::archiveManager().newArchive(format), Global::error);
	};
	archives["OpenFile"] = [](std::string_view filename) {
		return std::make_tuple(App::archiveManager().openArchive(filename), Global::error);
	};
	archives["Close"] = sol::overload(
		[](Archive* archive) { return App::archiveManager().closeArchive(archive); },
		[](int index) { return App::archiveManager().closeArchive(index); });
	archives["CloseAll"]             = []() { App::archiveManager().closeAll(); };
	archives["FileExtensionsString"] = []() { return App::archiveManager().getArchiveExtensionsString(); };
	archives["BaseResource"]         = []() { return App::archiveManager().baseResourceArchive(); };
	archives["BaseResourcePaths"]    = []() { return App::archiveManager().baseResourcePaths(); };
	archives["OpenBaseResource"]     = [](int index) { return App::archiveManager().openBaseResource(index - 1); };
	archives["ProgramResource"]      = []() { return App::archiveManager().programResourceArchive(); };
	archives["RecentFiles"]          = []() { return App::archiveManager().recentFiles(); };
	archives["Bookmarks"]            = []() { return App::archiveManager().bookmarks(); };
	archives["AddBookmark"]          = [](ArchiveEntry* entry) { App::archiveManager().addBookmark(entry); };
	archives["RemoveBookmark"]       = [](ArchiveEntry* entry) { App::archiveManager().deleteBookmark(entry); };
	archives["EntryType"]            = &EntryType::fromId;
}

// -----------------------------------------------------------------------------
// Registers various Archive-related types with lua
// -----------------------------------------------------------------------------
void registerArchiveTypes(sol::state& lua)
{
	registerArchiveFormat(lua);
	registerArchiveSearchOptions(lua);
	registerArchive(lua);
	registerArchiveEntry(lua);
	registerEntryType(lua);
	registerArchiveTreeNode(lua);
}

} // namespace Lua