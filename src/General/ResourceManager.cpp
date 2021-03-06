
// -----------------------------------------------------------------------------
// SLADE - It's a Doom Editor
// Copyright(C) 2008 - 2017 Simon Judd
//
// Email:       sirjuddington@gmail.com
// Web:         http://slade.mancubus.net
// Filename:    ResourceManager.cpp
// Description: The ResourceManager class. Manages all editing resources
//              (patches, gfx, etc) in all open archives and the base resource
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
#include "ResourceManager.h"
#include "Archive/ArchiveManager.h"
#include "General/Console/Console.h"
#include "Graphics/CTexture/CTexture.h"
#include "Graphics/CTexture/TextureXList.h"


// -----------------------------------------------------------------------------
//
// Variables
//
// -----------------------------------------------------------------------------
string ResourceManager::doom64_hash_table_[65536];


// -----------------------------------------------------------------------------
//
// Functions
//
// ----------------------------------------------------------------------------
namespace
{
// ----------------------------------------------------------------------------
// Removes all entries in resource [map] that are within [archive]
// ----------------------------------------------------------------------------
void removeArchiveFromMap(EntryResourceMap& map, Archive* archive)
{
	for (auto& i : map)
		i.second.removeArchive(archive);
}

// ----------------------------------------------------------------------------
// Removes [entry] from resource [map].
// If [full_check] is true, all resources in the map are checked for the entry,
// otherwise only the resource [name] is checked
// ----------------------------------------------------------------------------
void removeEntryFromMap(EntryResourceMap& map, const string& name, ArchiveEntry::SPtr& entry, bool full_check)
{
	if (full_check)
		for (auto& i : map)
			i.second.remove(entry);
	else
		map[name].remove(entry);
}
} // namespace


// ----------------------------------------------------------------------------
//
// EntryResource Class Functions
//
// -----------------------------------------------------------------------------


// -----------------------------------------------------------------------------
// Adds matching [entry] to the resource
// -----------------------------------------------------------------------------
void EntryResource::add(ArchiveEntry::SPtr& entry)
{
	if (entry->parent())
		entries_.push_back(entry);
}

// -----------------------------------------------------------------------------
// Removes matching [entry] from the resource
// -----------------------------------------------------------------------------
void EntryResource::remove(ArchiveEntry::SPtr& entry)
{
	unsigned a = 0;
	while (a < entries_.size())
	{
		if (entries_[a].lock() == entry)
			entries_.erase(entries_.begin() + a);
		else
			++a;
	}
}

// ----------------------------------------------------------------------------
// Removes any entries in the resource that are in [archive]
// ----------------------------------------------------------------------------
void EntryResource::removeArchive(Archive* archive)
{
	unsigned a = 0;
	while (a < entries_.size())
	{
		if (entries_[a].expired())
			entries_.erase(entries_.begin() + a);
		else if (entries_[a].lock()->parent() == archive)
			entries_.erase(entries_.begin() + a);
		else
			++a;
	}
}

// ----------------------------------------------------------------------------
// Gets the most relevant entry for this resource, depending on [priority] and
// [nspace]. If [priority] is set, this will prioritize entries from the
// priority archive. If [nspace] is not empty, this will prioritize entries
// within that namespace, or if [ns_required] is true, ignore anything not in
// [nspace]
// -----------------------------------------------------------------------------
ArchiveEntry* EntryResource::getEntry(Archive* priority, const string& nspace, bool ns_required)
{
	// Check resoure has any entries
	if (entries_.empty())
		return nullptr;

	ArchiveEntry::SPtr best;
	auto               i = entries_.end();
	while (i != entries_.begin())
	{
		--i;
		// Check if expired
		if (i->expired())
		{
			i = entries_.erase(i);
			continue;
		}

		auto entry = i->lock();

		if (!best)
			best = entry;

		// Check namespace if required
		if (ns_required && !nspace.IsEmpty())
			if (!entry->isInNamespace(nspace))
				continue;

		// Check if in priority archive (or its parent)
		if (priority && (entry->parent() == priority || entry->parent()->parentArchive() == priority))
		{
			best = entry;
			break;
		}

		// Check namespace
		if (!ns_required && !nspace.IsEmpty() && !best->isInNamespace(nspace) && entry->isInNamespace(nspace))
		{
			best = entry;
			continue;
		}

		// Otherwise, if it's in a 'later' archive than the current resource entry, set it
		if (App::archiveManager().archiveIndex(best->parent()) <= App::archiveManager().archiveIndex(entry->parent()))
			best = entry;
	}

	return best.get();
}


// -----------------------------------------------------------------------------
//
// TextureResource Class Functions
//
// -----------------------------------------------------------------------------


// -----------------------------------------------------------------------------
// Adds a texture to this resource
// -----------------------------------------------------------------------------
void TextureResource::add(CTexture* tex, Archive* parent)
{
	// Check args
	if (!tex || !parent)
		return;

	textures_.push_back(std::make_unique<Texture>(*tex, parent));
}

// -----------------------------------------------------------------------------
// Removes any textures in this resource that are part of [parent] archive
// -----------------------------------------------------------------------------
void TextureResource::remove(Archive* parent)
{
	// Remove any textures with matching parent
	auto i = textures_.begin();
	while (i != textures_.end())
	{
		if (i->get()->parent == parent)
			i = textures_.erase(i);
		else
			++i;
	}
}


// -----------------------------------------------------------------------------
//
// ResourceManager Class Functions
//
// -----------------------------------------------------------------------------


// -----------------------------------------------------------------------------
// Adds an archive to be managed
// -----------------------------------------------------------------------------
void ResourceManager::addArchive(Archive* archive)
{
	// Check archive was given
	if (!archive)
		return;

	// Go through entries
	vector<ArchiveEntry::SPtr> entries;
	archive->putEntryTreeAsList(entries);
	for (auto& entry : entries)
		addEntry(entry);

	// Listen to the archive
	listenTo(archive);

	// Announce resource update
	announce("resources_updated");
}

// -----------------------------------------------------------------------------
// Removes a managed archive
// -----------------------------------------------------------------------------
void ResourceManager::removeArchive(Archive* archive)
{
	// Check archive was given
	if (!archive)
		return;

	// Remove from palettes
	removeArchiveFromMap(palettes_, archive);

	// Remove from patches
	removeArchiveFromMap(patches_, archive);
	removeArchiveFromMap(patches_fp_, archive);
	removeArchiveFromMap(patches_fp_only_, archive);

	// Remove from flats
	removeArchiveFromMap(flats_, archive);
	removeArchiveFromMap(flats_fp_, archive);
	removeArchiveFromMap(flats_fp_only_, archive);

	// Remove from stand-alone textures
	removeArchiveFromMap(satextures_, archive);
	removeArchiveFromMap(satextures_fp_, archive);

	// Remove any textures in the archive
	for (auto& i : textures_)
		i.second.remove(archive);

	// Announce resource update
	announce("resources_updated");
}

// -----------------------------------------------------------------------------
// Returns the Doom64 hash of a given texture name, computed using the same
// hash algorithm as Doom64 EX itself
// -----------------------------------------------------------------------------
uint16_t ResourceManager::getTextureHash(const string& name) const
{
	char str[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
	for (size_t c = 0; c < name.length() && c < 8; c++)
		str[c] = name[c];

	uint32_t hash = 1315423911;
	for (uint32_t i = 0; i < 8 && str[i] != '\0'; ++i)
		hash ^= ((hash << 5) + toupper((int)str[i]) + (hash >> 2));
	hash %= 65536;
	return (uint16_t)hash;
}

// -----------------------------------------------------------------------------
// Adds an entry to be managed
// -----------------------------------------------------------------------------
void ResourceManager::addEntry(ArchiveEntry::SPtr& entry, bool log)
{
	if (!entry.get())
		return;

	// Detect type if unknown
	if (entry->type() == EntryType::unknownType())
		EntryType::detectEntryType(entry.get());

	// Get entry type
	auto type = entry->type();

	// Get resource name (extension cut, uppercase)
	string lname = entry->upperNameNoExt();
	string name  = entry->upperNameNoExt().Truncate(8);
	// Talon1024 - Get resource path (uppercase, without leading slash)
	string path = entry->path(true).Upper().Mid(1);

	if (log)
		Log::debug(S_FMT("Adding entry %s to resource manager", path));

	// Check for palette entry
	if (type->id() == "palette")
		palettes_[name].add(entry);

	// Check for various image entries, so only accept images
	if (type->editor() == "gfx")
	{
		// Reject graphics that are not in a valid namespace:
		// Patches in wads can be in the global namespace as well, and
		// ZDoom textures can use sprites and graphics as patches
		if (!entry->isInNamespace("global") && !entry->isInNamespace("patches") && !entry->isInNamespace("sprites")
			&& !entry->isInNamespace("graphics") &&
			// Stand-alone textures can also be found in the hires namespace
			!entry->isInNamespace("hires") && !entry->isInNamespace("textures") &&
			// Flats are kinda boring in comparison
			!entry->isInNamespace("flats"))
			return;

		bool addToFpOnly = true;

		// Check for patch entry
		if (type->extraProps().propertyExists("patch") || entry->isInNamespace("patches")
			|| entry->isInNamespace("sprites"))
		{
			if (patches_[name].length() == 0)
			{
				addToFpOnly = false;
			}
			patches_[name].add(entry);
			if (!entry->parent()->isTreeless())
			{
				patches_fp_[path].add(entry);
				if ((lname.Len() > 8 || patches_[name].length() > 0) && addToFpOnly)
				{
					patches_fp_only_[path].add(entry);
				}
			}
		}

		addToFpOnly = true;

		// Check for flat entry
		if (type->id() == "gfx_flat" || entry->isInNamespace("flats"))
		{
			if (flats_[name].length() == 0)
			{
				addToFpOnly = false;
			}
			flats_[name].add(entry);
			if (!entry->parent()->isTreeless())
			{
				flats_fp_[path].add(entry);
				if ((lname.Len() > 8 || flats_[name].length() > 0) && addToFpOnly)
				{
					flats_fp_only_[path].add(entry);
				}
			}
		}

		// Check for stand-alone texture entry
		if (entry->isInNamespace("textures") || entry->isInNamespace("hires"))
		{
			satextures_[name].add(entry);
			if (!entry->parent()->isTreeless())
			{
				satextures_fp_[path].add(entry);
			}

			// Add name to hash table
			doom64_hash_table_[getTextureHash(name)] = name;
		}
	}

	// Check for TEXTUREx entry
	int txentry = 0;
	if (type->id() == "texturex")
		txentry = 1;
	else if (type->id() == "zdtextures")
		txentry = 2;
	if (txentry > 0)
	{
		// Load patch table if needed
		PatchTable ptable;
		if (txentry == 1)
		{
			Archive::SearchOptions opt;
			opt.match_type = EntryType::fromId("pnames");
			auto pnames    = entry->parent()->findLast(opt);
			ptable.loadPNAMES(pnames, entry->parent());
		}

		// Read texture list
		TextureXList tx;
		if (txentry == 1)
			tx.readTEXTUREXData(entry.get(), ptable);
		else
			tx.readTEXTURESData(entry.get());

		// Add all textures to resources
		CTexture* tex;
		for (unsigned a = 0; a < tx.size(); a++)
		{
			tex = tx.texture(a);
			textures_[tex->name()].add(tex, entry->parent());
		}
	}
}

// ----------------------------------------------------------------------------
// Removes a managed entry
// -----------------------------------------------------------------------------
void ResourceManager::removeEntry(ArchiveEntry::SPtr& entry, bool log, bool full_check)
{
	if (!entry.get())
		return;

	// Get resource name (extension cut, uppercase)
	string name = entry->upperNameNoExt().Truncate(8);
	string path = entry->path(true).Upper().Mid(1);

	if (log)
		Log::debug(S_FMT("Removing entry %s from resource manager", path));

	// Remove from palettes
	removeEntryFromMap(palettes_, name, entry, full_check);

	// Remove from patches
	removeEntryFromMap(patches_, name, entry, full_check);
	removeEntryFromMap(patches_fp_, path, entry, full_check);
	removeEntryFromMap(patches_fp_only_, path, entry, full_check);

	// Remove from flats
	removeEntryFromMap(flats_, name, entry, full_check);
	removeEntryFromMap(flats_fp_, path, entry, full_check);
	removeEntryFromMap(flats_fp_only_, path, entry, full_check);

	// Remove from stand-alone textures
	removeEntryFromMap(satextures_, name, entry, full_check);
	removeEntryFromMap(satextures_fp_, path, entry, full_check);

	// Check for TEXTUREx entry
	int txentry = 0;
	if (entry->type()->id() == "texturex")
		txentry = 1;
	else if (entry->type()->id() == "zdtextures")
		txentry = 2;
	if (txentry > 0)
	{
		// Read texture list
		TextureXList tx;
		PatchTable   ptable;
		if (txentry == 1)
			tx.readTEXTUREXData(entry.get(), ptable);
		else
			tx.readTEXTURESData(entry.get());

		// Remove all texture resources
		for (unsigned a = 0; a < tx.size(); a++)
			textures_[tx.texture(a)->name()].remove(entry->parent());
	}
}

// -----------------------------------------------------------------------------
// Dumps all patch names and the number of matching entries for each
// -----------------------------------------------------------------------------
void ResourceManager::listAllPatches()
{
	for (auto& i : patches_)
	{
		if (i.second.length() == 0)
			continue;

		Log::info(S_FMT("%s (%d)", i.first, i.second.length()));
	}
}

// -----------------------------------------------------------------------------
// Adds all current patch entries to [list]
// -----------------------------------------------------------------------------
void ResourceManager::putAllPatchEntries(vector<ArchiveEntry*>& list, Archive* priority, bool fullPath)
{
	for (auto& i : patches_)
	{
		auto entry = i.second.getEntry(priority);
		if (entry)
			list.push_back(entry);
	}

	if (!fullPath)
		return;

	for (auto& i : patches_fp_only_)
	{
		auto entry = i.second.getEntry(priority);
		if (entry)
			list.push_back(entry);
	}
}

// -----------------------------------------------------------------------------
// Adds all current textures to [list]
// -----------------------------------------------------------------------------
void ResourceManager::putAllTextures(vector<TextureResource::Texture*>& list, Archive* priority, Archive* ignore)
{
	// Add all primary textures to the list
	for (auto& i : textures_)
	{
		// Skip if no entries
		if (i.second.length() == 0)
			continue;

		// Go through resource textures
		auto res = i.second.textures_[0].get();
		for (int a = 0; a < i.second.length(); a++)
		{
			res = i.second.textures_[a].get();

			// Skip if it's in the 'ignore' archive
			if (res->parent == ignore)
				continue;

			// If it's in the 'priority' archive, exit loop
			if (priority && i.second.textures_[a]->parent == priority)
				break;

			// Otherwise, if it's in a 'later' archive than the current resource, set it
			if (App::archiveManager().archiveIndex(res->parent)
				<= App::archiveManager().archiveIndex(i.second.textures_[a]->parent))
				res = i.second.textures_[a].get();
		}

		// Add texture resource to the list
		if (res->parent != ignore)
			list.push_back(res);
	}
}

// -----------------------------------------------------------------------------
// Adds all current texture names to [list]
// -----------------------------------------------------------------------------
void ResourceManager::putAllTextureNames(vector<string>& list)
{
	// Add all primary textures to the list
	for (auto& i : textures_)
		if (i.second.length() > 0) // Ignore if no entries
			list.push_back(i.first);
}

// -----------------------------------------------------------------------------
// Adds all current flat entries to [list]
// -----------------------------------------------------------------------------
void ResourceManager::putAllFlatEntries(vector<ArchiveEntry*>& list, Archive* priority, bool fullPath)
{
	for (auto& i : flats_)
	{
		auto entry = i.second.getEntry(priority);
		if (entry)
			list.push_back(entry);
	}

	if (!fullPath)
		return;

	for (auto& i : flats_fp_only_)
	{
		auto entry = i.second.getEntry(priority);
		if (entry)
			list.push_back(entry);
	}
}

// -----------------------------------------------------------------------------
// Adds all current flat names to [list]
// -----------------------------------------------------------------------------
void ResourceManager::putAllFlatNames(vector<string>& list)
{
	// Add all primary flats to the list
	for (auto& i : flats_)
		if (i.second.length() > 0) // Ignore if no entries
			list.push_back(i.first);
}

// -----------------------------------------------------------------------------
// Returns the most appropriate managed resource entry for [palette], or
// nullptr if no match found
// -----------------------------------------------------------------------------
ArchiveEntry* ResourceManager::getPaletteEntry(const string& palette, Archive* priority)
{
	return palettes_[palette.Upper()].getEntry(priority);
}

// -----------------------------------------------------------------------------
// Returns the most appropriate managed resource entry for [patch],
// or nullptr if no match found
// -----------------------------------------------------------------------------
ArchiveEntry* ResourceManager::getPatchEntry(const string& patch, const string& nspace, Archive* priority)
{
	// Are we wanting to use a flat as a patch?
	if (!nspace.CmpNoCase("flats"))
		return getFlatEntry(patch, priority);

	// Are we wanting to use a stand-alone texture as a patch?
	if (!nspace.CmpNoCase("textures"))
		return getTextureEntry(patch, "textures", priority);

	auto entry = patches_[patch.Upper()].getEntry(priority, nspace, true);
	if (entry)
		return entry;

	entry = patches_fp_[patch.Upper()].getEntry(priority, nspace, true);
	if (entry)
		return entry;

	return nullptr;
}

// -----------------------------------------------------------------------------
// Returns the most appropriate managed resource entry for [flat], or nullptr
// if no match found
// -----------------------------------------------------------------------------
ArchiveEntry* ResourceManager::getFlatEntry(const string& flat, Archive* priority)
{
	// Check resource with matching name exists
	auto& res = flats_[flat.Upper()];

	// Return most relevant entry
	auto entry = res.getEntry(priority);
	if (entry)
		return entry;

	entry = flats_fp_[flat.Upper()].getEntry(priority, "flats", true);
	if (entry)
		return entry;

	return nullptr;
}

// -----------------------------------------------------------------------------
// Returns the most appropriate managed resource entry for [texture], or
// nullptr if no match found
// -----------------------------------------------------------------------------
ArchiveEntry* ResourceManager::getTextureEntry(const string& texture, const string& nspace, Archive* priority)
{
	auto entry = satextures_[texture.Upper()].getEntry(priority, nspace, true);
	if (entry)
		return entry;

	entry = satextures_fp_[texture.Upper()].getEntry(priority, nspace, true);
	if (entry)
		return entry;

	return nullptr;
}

// -----------------------------------------------------------------------------
// Returns the most appropriate managed texture for [texture], or nullptr if no
// match found
// -----------------------------------------------------------------------------
CTexture* ResourceManager::getTexture(const string& texture, Archive* priority, Archive* ignore)
{
	// Check texture resource with matching name exists
	auto& res = textures_[texture.Upper()];
	if (res.textures_.empty())
		return nullptr;

	// Go through resource textures
	auto tex    = &res.textures_[0]->tex;
	auto parent = res.textures_[0]->parent;
	for (auto& res_tex : res.textures_)
	{
		// Skip if it's in the 'ignore' archive
		if (res_tex->parent == ignore)
			continue;

		// If it's in the 'priority' archive, return it
		if (priority && res_tex->parent == priority)
			return &res_tex->tex;

		// Otherwise, if it's in a 'later' archive than the current resource entry, set it
		if (App::archiveManager().archiveIndex(parent) <= App::archiveManager().archiveIndex(res_tex->parent))
		{
			tex    = &res_tex->tex;
			parent = res_tex->parent;
		}
	}

	// Return the most relevant texture
	if (parent != ignore)
		return tex;
	else
		return nullptr;
}

// -----------------------------------------------------------------------------
// Called when an announcement is recieved from any managed archive
// -----------------------------------------------------------------------------
void ResourceManager::onAnnouncement(Announcer* announcer, const string& event_name, MemChunk& event_data)
{
	event_data.seek(0, SEEK_SET);

	// An entry is modified
	if (event_name == "entry_state_changed")
	{
		wxUIntPtr ptr;
		event_data.read(&ptr, sizeof(wxUIntPtr), 4);
		auto entry = (ArchiveEntry*)wxUIntToPtr(ptr);
		auto esp   = entry->parent()->entryAtPathShared(entry->path(true));
		removeEntry(esp, true);
		addEntry(esp, true);
		announce("resources_updated");
	}

	// An entry is removed or renamed
	if (event_name == "entry_removing" || event_name == "entry_renaming")
	{
		wxUIntPtr ptr;
		event_data.read(&ptr, sizeof(wxUIntPtr), sizeof(int));
		auto entry = (ArchiveEntry*)wxUIntToPtr(ptr);
		auto esp   = entry->parent()->entryAtPathShared(entry->path(true));
		removeEntry(esp, true);
		announce("resources_updated");
	}

	// An entry is added
	if (event_name == "entry_added")
	{
		wxUIntPtr ptr;
		event_data.read(&ptr, sizeof(wxUIntPtr), 4);
		auto entry = (ArchiveEntry*)wxUIntToPtr(ptr);
		auto esp   = entry->parent()->entryAtPathShared(entry->path(true));
		addEntry(esp, true);
		announce("resources_updated");
	}
}


// -----------------------------------------------------------------------------
//
// Console Commands
//
// -----------------------------------------------------------------------------


CONSOLE_COMMAND(list_res_patches, 0, false)
{
	App::resources().listAllPatches();
}

#include "App.h"
CONSOLE_COMMAND(test_res_speed, 0, false)
{
	vector<ArchiveEntry*> list;

	Log::console("Testing...");

	long times[5];

	for (long& time : times)
	{
		auto start = App::runTimer();
		for (unsigned a = 0; a < 100; a++)
		{
			App::resources().putAllPatchEntries(list, nullptr);
			list.clear();
		}
		for (unsigned a = 0; a < 100; a++)
		{
			App::resources().putAllFlatEntries(list, nullptr);
			list.clear();
		}
		auto end = App::runTimer();
		time     = end - start;
	}

	float avg = float(times[0] + times[1] + times[2] + times[3] + times[4]) / 5.0f;
	Log::console(S_FMT("Test took %dms avg", (int)avg));
}
