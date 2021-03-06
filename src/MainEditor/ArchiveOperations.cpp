
// -----------------------------------------------------------------------------
// SLADE - It's a Doom Editor
// Copyright(C) 2008 - 2017 Simon Judd
//
// Email:       sirjuddington@gmail.com
// Web:         http://slade.mancubus.net
// Filename:    ArchiveOperations.cpp
// Description: Functions that perform specific operations on archives
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
#include "ArchiveOperations.h"
#include "Archive/ArchiveManager.h"
#include "Archive/Formats/WadArchive.h"
#include "Dialogs/ExtMessageDialog.h"
#include "General/Console/Console.h"
#include "General/ResourceManager.h"
#include "Graphics/CTexture/TextureXList.h"
#include "MainEditor/MainEditor.h"
#include "MainEditor/UI/MainWindow.h"
#include "MapEditor/SLADEMap/MapLine.h"
#include "MapEditor/SLADEMap/MapSector.h"
#include "MapEditor/SLADEMap/MapSide.h"
#include "MapEditor/SLADEMap/MapThing.h"
#include "Utility/Tokenizer.h"


// -----------------------------------------------------------------------------
//
// Variables
//
// -----------------------------------------------------------------------------
typedef std::map<string, int>                   StrIntMap;
typedef std::map<string, vector<ArchiveEntry*>> PathMap;
typedef std::map<int, vector<ArchiveEntry*>>    CRCMap;


// -----------------------------------------------------------------------------
//
// ArchiveOperations Namespace Functions
//
// -----------------------------------------------------------------------------


// -----------------------------------------------------------------------------
// Removes any patches and associated entries from [archive] that are not used
// in any texture definitions
// -----------------------------------------------------------------------------
bool ArchiveOperations::removeUnusedPatches(Archive* archive)
{
	if (!archive)
		return false;

	// Find PNAMES entry
	Archive::SearchOptions opt;
	opt.match_type = EntryType::fromId("pnames");
	auto pnames    = archive->findLast(opt);

	// Find TEXTUREx entries
	opt.match_type  = EntryType::fromId("texturex");
	auto tx_entries = archive->findAll(opt);

	// Can't do anything without PNAMES/TEXTUREx
	if (!pnames || tx_entries.empty())
		return false;

	// Open patch table
	PatchTable ptable;
	ptable.loadPNAMES(pnames, archive);

	// Open texturex entries to update patch usage
	vector<TextureXList*> tx_lists;
	for (auto& entry : tx_entries)
	{
		auto texturex = new TextureXList();
		texturex->readTEXTUREXData(entry, ptable);
		for (unsigned t = 0; t < texturex->size(); t++)
			ptable.updatePatchUsage(texturex->texture(t));
		tx_lists.push_back(texturex);
	}

	// Go through patch table
	unsigned              removed = 0;
	vector<ArchiveEntry*> to_remove;
	for (unsigned a = 0; a < ptable.nPatches(); a++)
	{
		auto& p = ptable.patch(a);

		// Check if used in any texture
		if (p.used_in.empty())
		{
			// Unused

			// If its entry is in the archive, flag it to be removed
			auto entry = App::resources().getPatchEntry(p.name, "patches", archive);
			if (entry && entry->parent() == archive)
				to_remove.push_back(entry);

			// Update texturex list patch indices
			for (auto& tx_list : tx_lists)
				tx_list->removePatch(p.name);

			// Remove the patch from the patch table
			Log::info(S_FMT("Removed patch %s", p.name));
			removed++;
			ptable.removePatch(a--);
		}
	}

	// Remove unused patch entries
	for (auto& a : to_remove)
	{
		Log::info(S_FMT("Removed entry %s", a->name()));
		archive->removeEntry(a);
	}

	// Write PNAMES changes
	ptable.writePNAMES(pnames);

	// Write TEXTUREx changes
	for (unsigned a = 0; a < tx_lists.size(); a++)
		tx_lists[a]->writeTEXTUREXData(tx_entries[a], ptable);

	// Cleanup
	for (auto& tx_list : tx_lists)
		delete tx_list;

	// Notify user
	wxMessageBox(
		S_FMT("Removed %d patches and %lu entries. See console log for details.", removed, to_remove.size()),
		"Removed Unused Patches",
		wxOK | wxICON_INFORMATION);

	return true;
}

// -----------------------------------------------------------------------------
// Checks [archive] for multiple entries of the same name, and displays a list
// of duplicate entry names if any are found
// -----------------------------------------------------------------------------
bool ArchiveOperations::checkDuplicateEntryNames(Archive* archive)
{
	StrIntMap map_namecounts;
	PathMap   map_entries;

	// Get list of all entries in archive
	vector<ArchiveEntry*> entries;
	archive->putEntryTreeAsList(entries);

	// Go through list
	for (auto& entry : entries)
	{
		// Skip directory entries
		if (entry->type() == EntryType::folderType())
			continue;

		// Increment count for entry name
		map_namecounts[entry->path(true)] += 1;

		// Enqueue entries
		map_entries[entry->name(true)].push_back(entry);
	}

	// Generate string of duplicate entry names
	string dups;
	// Treeless archives such as WADs can just include a simple list of duplicated names and how often they appear
	if (archive->isTreeless())
	{
		auto i = map_namecounts.begin();
		while (i != map_namecounts.end())
		{
			if (i->second > 1)
			{
				string name = i->first;
				name.Remove(0, 1);
				dups += S_FMT("%s appears %d times\n", name, i->second);
			}
			++i;
		}
		// Hierarchized archives, however, need to compare only the name (not the whole path) and to display the full
		// path of each entry with a duplicated name, so that they might be found more easily than by having the user
		// recurse through the entire directory tree -- such a task is something a program should do instead.
	}
	else
	{
		auto i = map_entries.begin();
		while (i != map_entries.end())
		{
			if (i->second.size() > 1)
			{
				string name;
				dups += S_FMT("\n%i entries are named %s\t", i->second.size(), i->first);
				auto j = i->second.begin();
				while (j != i->second.end())
				{
					name = (*j)->path(true);
					name.Remove(0, 1);
					dups += S_FMT("\t%s", name);
					++j;
				}
			}
			++i;
		}
	}

	// If no duplicates exist, do nothing
	if (dups.IsEmpty())
	{
		wxMessageBox("No duplicated entry names exist");
		return false;
	}

	// Display list of duplicate entry names
	ExtMessageDialog msg(theMainWindow, "Duplicate Entries");
	msg.setExt(dups);
	msg.setMessage("The following entry names are duplicated:");
	msg.ShowModal();

	return true;
}

// -----------------------------------------------------------------------------
// Compare the archive's entries with those sharing the same name and namespace
// in the base resource archive, deleting duplicates
// -----------------------------------------------------------------------------
void ArchiveOperations::removeEntriesUnchangedFromIWAD(Archive* archive)
{
	// Do nothing if there is no base resource archive,
	// or if the archive *is* the base resource archive.
	auto bra = App::archiveManager().baseResourceArchive();
	if (bra == nullptr || bra == archive || archive == nullptr)
		return;

	// Get list of all entries in archive
	vector<ArchiveEntry*> entries;
	archive->putEntryTreeAsList(entries);

	// Init search options
	Archive::SearchOptions search;
	ArchiveEntry*          other = nullptr;
	string                 dups  = "";
	size_t                 count = 0;

	// Go through list
	for (auto& entry : entries)
	{
		// Skip directory entries
		if (entry->type() == EntryType::folderType())
			continue;

		// Skip markers
		if (entry->type() == EntryType::mapMarkerType() || entry->size() == 0)
			continue;

		// Now, let's look for a counterpart in the IWAD
		search.match_namespace = archive->detectNamespace(entry);
		search.match_name      = entry->name();
		other                  = bra->findLast(search);

		// If there is one, and it is identical, remove it
		if (other != nullptr && (other->data().crc() == entry->data().crc()))
		{
			++count;
			dups += S_FMT("%s\n", search.match_name);
			archive->removeEntry(entry);
			entry = nullptr;
		}
	}


	// If no duplicates exist, do nothing
	if (count == 0)
	{
		wxMessageBox("No duplicated entries exist");
		return;
	}

	string message = S_FMT(
		"The following %d entr%s duplicated from the base resource archive and deleted:",
		count,
		(count > 1) ? "ies were" : "y was");

	// Display list of deleted duplicate entries
	ExtMessageDialog msg(theMainWindow, (count > 1) ? "Deleted Entries" : "Deleted Entry");
	msg.setExt(dups);
	msg.setMessage(message);
	msg.ShowModal();
}

// -----------------------------------------------------------------------------
// Checks [archive] for multiple entries with the same data, and displays a list
// of the duplicate entries' names if any are found
// -----------------------------------------------------------------------------
bool ArchiveOperations::checkDuplicateEntryContent(Archive* archive)
{
	CRCMap map_entries;

	// Get list of all entries in archive
	vector<ArchiveEntry*> entries;
	archive->putEntryTreeAsList(entries);
	string dups = "";

	// Go through list
	for (auto& entry : entries)
	{
		// Skip directory entries
		if (entry->type() == EntryType::folderType())
			continue;

		// Skip markers
		if (entry->type() == EntryType::mapMarkerType() || entry->size() == 0)
			continue;

		// Enqueue entries
		map_entries[entry->data().crc()].push_back(entry);
	}

	// Now iterate through the dupes to list the name of the duplicated entries
	auto i = map_entries.begin();
	while (i != map_entries.end())
	{
		if (i->second.size() > 1)
		{
			string name = i->second[0]->path(true);
			name.Remove(0, 1);
			dups += S_FMT("\n%s\t(%8x) duplicated by", name, i->first);
			auto j = i->second.begin() + 1;
			while (j != i->second.end())
			{
				name = (*j)->path(true);
				name.Remove(0, 1);
				dups += S_FMT("\t%s", name);
				++j;
			}
		}
		++i;
	}

	// If no duplicates exist, do nothing
	if (dups.IsEmpty())
	{
		wxMessageBox("No duplicated entry data exist");
		return false;
	}

	// Display list of duplicate entry names
	ExtMessageDialog msg(theMainWindow, "Duplicate Entries");
	msg.setExt(dups);
	msg.setMessage("The following entry data are duplicated:");
	msg.ShowModal();

	return true;
}



// Hardcoded doom defaults for now
int    n_tex_anim       = 13;
string tex_anim_start[] = {
	"BLODGR1",  "SLADRIP1", "BLODRIP1", "FIREWALA", "GSTFONT1", "FIRELAV3", "FIREMAG1",
	"FIREBLU1", "ROCKRED1", "BFALL1",   "SFALL1",   "WFALL1",   "DBRAIN1",
};
string tex_anim_end[] = {
	"BLODGR4",  "SLADRIP3", "BLODRIP4", "FIREWALL", "GSTFONT3", "FIRELAVA", "FIREMAG3",
	"FIREBLU2", "ROCKRED3", "BFALL4",   "SFALL4",   "WFALL4",   "DBRAIN4",
};

int    n_flat_anim       = 9;
string flat_anim_start[] = {
	"NUKAGE1", "FWATER1", "SWATER1", "LAVA1", "BLOOD1", "RROCK05", "SLIME01", "SLIME05", "SLIME09",
};
string flat_anim_end[] = {
	"NUKAGE3", "FWATER4", "SWATER4", "LAVA4", "BLOOD3", "RROCK08", "SLIME04", "SLIME08", "SLIME12",
};

struct texused_t
{
	bool used;
	texused_t() { used = false; }
};
WX_DECLARE_STRING_HASH_MAP(texused_t, TexUsedMap);
void ArchiveOperations::removeUnusedTextures(Archive* archive)
{
	// Check archive was given
	if (!archive)
		return;

	// --- Build list of used textures ---
	TexUsedMap used_textures;
	int        total_maps = 0;

	// Get all SIDEDEFS entries
	Archive::SearchOptions opt;
	opt.match_type = EntryType::fromId("map_sidedefs");
	auto sidedefs  = archive->findAll(opt);
	total_maps += sidedefs.size();

	// Go through and add used textures to list
	MapSide::DoomData sdef;
	string            tex_lower, tex_middle, tex_upper;
	for (auto& sidedef : sidedefs)
	{
		int nsides = sidedef->size() / 30;
		sidedef->seek(0, SEEK_SET);
		for (int s = 0; s < nsides; s++)
		{
			// Read side data
			sidedef->read(&sdef, 30);

			// Get textures
			tex_lower  = wxString::FromAscii(sdef.tex_lower, 8);
			tex_middle = wxString::FromAscii(sdef.tex_middle, 8);
			tex_upper  = wxString::FromAscii(sdef.tex_upper, 8);

			// Add to used textures list
			used_textures[tex_lower].used  = true;
			used_textures[tex_middle].used = true;
			used_textures[tex_upper].used  = true;
		}
	}

	// Get all TEXTMAP entries
	opt.match_name = "TEXTMAP";
	opt.match_type = EntryType::fromId("udmf_textmap");
	auto udmfmaps  = archive->findAll(opt);
	total_maps += udmfmaps.size();

	// Go through and add used textures to list
	Tokenizer tz;
	tz.setSpecialCharacters("{};=");
	for (auto& udmfmap : udmfmaps)
	{
		// Open in tokenizer
		tz.openMem(udmfmap->data(), "UDMF TEXTMAP");

		// Go through text tokens
		string token = tz.getToken();
		while (!token.IsEmpty())
		{
			// Check for sidedef definition
			if (token == "sidedef")
			{
				tz.getToken(); // Skip {

				token = tz.getToken();
				while (token != "}")
				{
					// Check for texture property
					if (token == "texturetop" || token == "texturemiddle" || token == "texturebottom")
					{
						tz.getToken(); // Skip =
						used_textures[tz.getToken()].used = true;
					}

					token = tz.getToken();
				}
			}

			// Next token
			token = tz.getToken();
		}
	}

	// Check if any maps were found
	if (total_maps == 0)
		return;

	// Find all TEXTUREx entries
	opt.match_name  = "";
	opt.match_type  = EntryType::fromId("texturex");
	auto tx_entries = archive->findAll(opt);

	// Go through texture lists
	PatchTable    ptable; // Dummy patch table, patch info not needed here
	wxArrayString unused_tex;
	for (auto& tx_entrie : tx_entries)
	{
		TextureXList txlist;
		txlist.readTEXTUREXData(tx_entrie, ptable);

		// Go through textures
		bool anim = false;
		for (unsigned t = 1; t < txlist.size(); t++)
		{
			string texname = txlist.texture(t)->name();

			// Check for animation start
			for (int b = 0; b < n_tex_anim; b++)
			{
				if (texname == tex_anim_start[b])
				{
					anim = true;
					break;
				}
			}

			// Check for animation end
			bool thisend = false;
			for (int b = 0; b < n_tex_anim; b++)
			{
				if (texname == tex_anim_end[b])
				{
					anim    = false;
					thisend = true;
					break;
				}
			}

			// Mark if unused and not part of an animation
			if (!used_textures[texname].used && !anim && !thisend)
				unused_tex.Add(txlist.texture(t)->name());
		}
	}

	// Pop up a dialog with a checkbox list of unused textures
	wxMultiChoiceDialog dialog(
		theMainWindow,
		"The following textures are not used in any map,\nselect which textures to delete",
		"Delete Unused Textures",
		unused_tex);

	// Get base resource textures (if any)
	auto                  base_resource = App::archiveManager().baseResourceArchive();
	vector<ArchiveEntry*> base_tx_entries;
	if (base_resource)
		base_tx_entries = base_resource->findAll(opt);
	PatchTable   pt_temp;
	TextureXList tx;
	for (auto& texturex : base_tx_entries)
		tx.readTEXTUREXData(texturex, pt_temp, true);
	vector<string> base_resource_textures;
	for (unsigned a = 0; a < tx.size(); a++)
		base_resource_textures.push_back(tx.texture(a)->name());

	// Determine which textures to check initially
	wxArrayInt selection;
	for (unsigned a = 0; a < unused_tex.size(); a++)
	{
		bool swtex = false;

		// Check for switch texture
		if (unused_tex[a].StartsWith("SW1"))
		{
			// Get counterpart switch name
			string swname = unused_tex[a];
			swname.Replace("SW1", "SW2", false);

			// Check if its counterpart is used
			if (used_textures[swname].used)
				swtex = true;
		}
		else if (unused_tex[a].StartsWith("SW2"))
		{
			// Get counterpart switch name
			string swname = unused_tex[a];
			swname.Replace("SW2", "SW1", false);

			// Check if its counterpart is used
			if (used_textures[swname].used)
				swtex = true;
		}

		// Check for base resource texture
		bool br_tex = false;
		for (auto& texture : base_resource_textures)
		{
			if (texture.CmpNoCase(unused_tex[a]) == 0)
			{
				Log::info(3, "Texture " + texture + " is in base resource");
				br_tex = true;
				break;
			}
		}

		if (!swtex && !br_tex)
			selection.Add(a);
	}
	dialog.SetSelections(selection);

	int n_removed = 0;
	if (dialog.ShowModal() == wxID_OK)
	{
		// Get selected textures
		selection = dialog.GetSelections();

		// Go through texture lists
		for (auto& entry : tx_entries)
		{
			TextureXList txlist;
			txlist.readTEXTUREXData(entry, ptable);

			// Go through selected textures to delete
			for (int i : selection)
			{
				// Get texture index
				int index = txlist.textureIndex(unused_tex[i]);

				// Delete it from the list (if found)
				if (index >= 0)
				{
					txlist.removeTexture(index);
					n_removed++;
				}
			}

			// Write texture list data back to entry
			txlist.writeTEXTUREXData(entry, ptable);
		}
	}

	wxMessageBox(S_FMT("Removed %d unused textures", n_removed));
}

void ArchiveOperations::removeUnusedFlats(Archive* archive)
{
	// Check archive was given
	if (!archive)
		return;

	// --- Build list of used flats ---
	TexUsedMap used_textures;
	int        total_maps = 0;

	// Get all SECTORS entries
	Archive::SearchOptions opt;
	opt.match_type = EntryType::fromId("map_sectors");
	auto sectors   = archive->findAll(opt);
	total_maps += sectors.size();

	// Go through and add used flats to list
	MapSector::DoomData sec;
	string              tex_floor, tex_ceil;
	for (auto& sector : sectors)
	{
		int nsec = sector->size() / 26;
		sector->seek(0, SEEK_SET);
		for (int s = 0; s < nsec; s++)
		{
			// Read sector data
			sector->read(&sec, 26);

			// Get textures
			tex_floor = wxString::FromAscii(sec.f_tex, 8);
			tex_ceil  = wxString::FromAscii(sec.c_tex, 8);

			// Add to used textures list
			used_textures[tex_floor].used = true;
			used_textures[tex_ceil].used  = true;
		}
	}

	// Get all TEXTMAP entries
	opt.match_name = "TEXTMAP";
	opt.match_type = EntryType::fromId("udmf_textmap");
	auto udmfmaps  = archive->findAll(opt);
	total_maps += udmfmaps.size();

	// Go through and add used flats to list
	Tokenizer tz;
	tz.setSpecialCharacters("{};=");
	for (auto& udmfmap : udmfmaps)
	{
		// Open in tokenizer
		tz.openMem(udmfmap->data(), "UDMF TEXTMAP");

		// Go through text tokens
		string token = tz.getToken();
		while (!token.IsEmpty())
		{
			// Check for sector definition
			if (token == "sector")
			{
				tz.getToken(); // Skip {

				token = tz.getToken();
				while (token != "}")
				{
					// Check for texture property
					if (token == "texturefloor" || token == "textureceiling")
					{
						tz.getToken(); // Skip =
						used_textures[tz.getToken()].used = true;
					}

					token = tz.getToken();
				}
			}

			// Next token
			token = tz.getToken();
		}
	}

	// Check if any maps were found
	if (total_maps == 0)
		return;

	// Find all flats
	opt.match_name      = "";
	opt.match_namespace = "flats";
	opt.match_type      = nullptr;
	auto flats          = archive->findAll(opt);

	// Create list of all unused flats
	wxArrayString unused_tex;
	bool          anim = false;
	for (auto& flat : flats)
	{
		// Skip markers
		if (flat->size() == 0)
			continue;

		// Check for animation start
		string flatname = flat->name(true);
		for (int b = 0; b < n_flat_anim; b++)
		{
			if (flatname == flat_anim_start[b])
			{
				anim = true;
				Log::info(S_FMT("%s anim start", flatname));
				break;
			}
		}

		// Check for animation end
		bool thisend = false;
		for (int b = 0; b < n_flat_anim; b++)
		{
			if (flatname == flat_anim_end[b])
			{
				anim    = false;
				thisend = true;
				Log::info(S_FMT("%s anim end", flatname));
				break;
			}
		}

		// Add if not animated
		if (!used_textures[flatname].used && !anim && !thisend)
			unused_tex.Add(flatname);
	}

	// Pop up a dialog with a checkbox list of unused textures
	wxMultiChoiceDialog dialog(
		theMainWindow,
		"The following textures are not used in any map,\nselect which textures to delete",
		"Delete Unused Textures",
		unused_tex);

	// Select all flats initially
	wxArrayInt selection;
	for (unsigned a = 0; a < unused_tex.size(); a++)
		selection.push_back(a);
	dialog.SetSelections(selection);

	int n_removed = 0;
	if (dialog.ShowModal() == wxID_OK)
	{
		// Go through selected flats
		selection           = dialog.GetSelections();
		opt.match_namespace = "flats";
		for (int i : selection)
		{
			opt.match_name      = unused_tex[i];
			ArchiveEntry* entry = archive->findFirst(opt);
			archive->removeEntry(entry);
			n_removed++;
		}
	}

	wxMessageBox(S_FMT("Removed %d unused flats", n_removed));
}


CONSOLE_COMMAND(test_cleantex, 0, false)
{
	auto current = MainEditor::currentArchive();
	if (current)
		ArchiveOperations::removeUnusedTextures(current);
}

CONSOLE_COMMAND(test_cleanflats, 0, false)
{
	auto current = MainEditor::currentArchive();
	if (current)
		ArchiveOperations::removeUnusedFlats(current);
}

size_t replaceThingsDoom(ArchiveEntry* entry, int oldtype, int newtype)
{
	if (entry == nullptr)
		return 0;

	size_t size      = entry->size();
	size_t numthings = size / sizeof(MapThing::DoomData);
	size_t changed   = 0;

	auto things = new MapThing::DoomData[numthings];
	memcpy(things, entry->rawData(), size);

	// Perform replacement
	for (size_t t = 0; t < numthings; ++t)
	{
		if (things[t].type == oldtype)
		{
			things[t].type = newtype;
			++changed;
		}
	}
	// Import the changes if needed
	if (changed > 0)
		entry->importMem(things, size);
	delete[] things;

	return changed;
}
size_t replaceThingsDoom64(ArchiveEntry* entry, int oldtype, int newtype)
{
	if (entry == nullptr)
		return 0;

	size_t size      = entry->size();
	size_t numthings = size / sizeof(MapThing::Doom64Data);
	size_t changed   = 0;

	auto things = new MapThing::Doom64Data[numthings];
	memcpy(things, entry->rawData(), size);

	// Perform replacement
	for (size_t t = 0; t < numthings; ++t)
	{
		if (things[t].type == oldtype)
		{
			things[t].type = newtype;
			++changed;
		}
	}
	// Import the changes if needed
	if (changed > 0)
		entry->importMem(things, size);
	delete[] things;

	return changed;
}
size_t replaceThingsHexen(ArchiveEntry* entry, int oldtype, int newtype)
{
	if (entry == nullptr)
		return 0;

	size_t size      = entry->size();
	size_t numthings = size / sizeof(MapThing::HexenData);
	size_t changed   = 0;

	auto things = new MapThing::HexenData[numthings];
	memcpy(things, entry->rawData(), size);

	// Perform replacement
	for (size_t t = 0; t < numthings; ++t)
	{
		if (things[t].type == oldtype)
		{
			things[t].type = newtype;
			++changed;
		}
	}
	// Import the changes if needed
	if (changed > 0)
		entry->importMem(things, size);
	delete[] things;

	return changed;
}
size_t replaceThingsUDMF(ArchiveEntry* entry, int oldtype, int newtype)
{
	if (entry == nullptr)
		return 0;

	size_t changed = 0;
	// TODO: parse and replace code
	// Import the changes if needed
	if (changed > 0)
	{
		// entry->importMemChunk(mc);
	}
	return changed;
}
size_t ArchiveOperations::replaceThings(Archive* archive, int oldtype, int newtype)
{
	size_t changed = 0;
	// Check archive was given
	if (!archive)
		return changed;

	// Get all maps
	auto   maps   = archive->detectMaps();
	string report = "";

	for (auto& map : maps)
	{
		size_t achanged = 0;
		// Is it an embedded wad?
		if (map.archive)
		{
			// Attempt to open entry as wad archive
			Archive* temp_archive = new WadArchive();
			if (temp_archive->open(map.head))
			{
				achanged = ArchiveOperations::replaceThings(temp_archive, oldtype, newtype);
				MemChunk mc;
				if (!(temp_archive->write(mc, true)))
				{
					achanged = 0;
				}
				else
				{
					temp_archive->close();
					if (!(map.head->importMemChunk(mc)))
					{
						achanged = 0;
					}
				}
			}

			// Cleanup
			delete temp_archive;
		}
		else
		{
			// Find the map entry to modify
			auto          mapentry = map.head;
			ArchiveEntry* things   = nullptr;
			if (map.format == MapFormat::Doom || map.format == MapFormat::Doom64 || map.format == MapFormat::Hexen)
			{
				while (mapentry && mapentry != map.end)
				{
					if (mapentry->type() == EntryType::fromId("map_things"))
					{
						things = mapentry;
						break;
					}
					mapentry = mapentry->nextEntry();
				}
			}
			else if (map.format == MapFormat::UDMF)
			{
				while (mapentry && mapentry != map.end)
				{
					if (mapentry->type() == EntryType::fromId("udmf_textmap"))
					{
						things = mapentry;
						break;
					}
					mapentry = mapentry->nextEntry();
				}
			}

			// Did we get a map entry?
			if (things)
			{
				switch (map.format)
				{
				case MapFormat::Doom: achanged = replaceThingsDoom(things, oldtype, newtype); break;
				case MapFormat::Hexen: achanged = replaceThingsHexen(things, oldtype, newtype); break;
				case MapFormat::Doom64: achanged = replaceThingsDoom64(things, oldtype, newtype); break;
				case MapFormat::UDMF: achanged = replaceThingsUDMF(things, oldtype, newtype); break;
				default: Log::warning("Unknown map format for " + map.head->name()); break;
				}
			}
		}
		report += S_FMT("%s:\t%i things changed\n", map.head->name(), achanged);
		changed += achanged;
	}
	Log::info(1, report);
	return changed;
}

CONSOLE_COMMAND(replacethings, 2, true)
{
	auto current = MainEditor::currentArchive();
	long oldtype, newtype;

	if (current && args[0].ToLong(&oldtype) && args[1].ToLong(&newtype))
	{
		ArchiveOperations::replaceThings(current, oldtype, newtype);
	}
}

CONSOLE_COMMAND(convertmapchex1to3, 0, false)
{
	Archive* current    = MainEditor::currentArchive();
	long     rep[23][2] = {
        //  #	Chex 1 actor			==>	Chex 3 actor			(unwanted replacement)
        { 25, 78 },   //  0	ChexTallFlower2			==> PropFlower1				(PropGlobeStand)
        { 28, 79 },   //  1	ChexTallFlower			==>	PropFlower2				(PropPhone)
        { 30, 74 },   //  2	ChexCavernStalagmite	==>	PropStalagmite			(PropPineTree)
        { 31, 50 },   //  3	ChexSubmergedPlant		==>	PropHydroponicPlant		(PropGreyRock)
        { 32, 73 },   //  4	ChexCavernColumn		==>	PropPillar				(PropBarrel)
        { 34, 80 },   //  5	ChexChemicalFlask		==>	PropBeaker				(PropCandlestick)
        { 35, 36 },   //  6	ChexGasTank				==>	PropOxygenTank			(PropCandelabra)
        { 43, 9061 }, //  7	ChexOrangeTree			==>	TreeOrange				(PropTorchTree)
        { 45, 70 },   //  8	ChexCivilian1			==>	PropCaptive1			(PropGreenTorch)
        { 47, 9060 }, //  9	ChexAppleTree			==>	TreeApple				(PropStalagtite)
        { 54, 9058 }, // 10	ChexBananaTree			==>	TreeBanana				(PropSpaceship -- must go before its own
                      // replacement)
        { 48,
          54 },       // 11	ChexSpaceship			==>	PropSpaceship			(PropTechPillar -- must go after banana tree
                      // replacement)
        { 55, 42 },   // 12	ChexLightColumn			==>	LabCoil					(PropShortBlueTorch)
        { 56, 26 },   // 13	ChexCivilian2			==>	PropCaptive2			(PropShortGreenTorch)
        { 57, 52 },   // 14	ChexCivilian3			==>	PropCaptive3			(PropShortRedTorch)
        { 3002, 58 }, // 15	F.CycloptisCommonus		==>	F.CycloptisCommonusV3	(FlemoidusStridicus)
        { 3003, 69 }, // 16	Flembrane				==>	FlembraneV3				(FlemoidusMaximus)
        { 33,
          53 },     // 17	ChexMineCart			==> PropBazoikCart			(none, but the sprite is modified otherwise)
        { 27, 81 }, // 18	"HeadOnAStick"			==> PropSmallBrush
        { 53, 75 }, // 19	"Meat5"					==> PropStalagtite2
        { 49, 63 }, // 20	Redundant bats
        { 51, 59 }, // 21	Redundant hanging plant #1
        { 50, 61 }, // 22	Redundant hanging plant #2
	};
	for (auto& i : rep)
	{
		ArchiveOperations::replaceThings(current, i[0], i[1]);
	}
}

CONSOLE_COMMAND(convertmapchex2to3, 0, false)
{
	auto current    = MainEditor::currentArchive();
	long rep[20][2] = {
		{ 3001, 9057 }, //  0	Quadrumpus
		{ 3002, 9050 }, //  1	Larva
		{ 27, 81 },     //  2	"HeadOnAStick"		==> PropSmallBrush
		{ 70, 49 },     //  3	"BurningBarrel"		==> PropStool
		{ 36, 9055 },   //  4	Chex Warrior
		{ 52, 9054 },   //  5	Tutanhkamen
		{ 53, 9053 },   //  6	Ramses
		{ 30, 9052 },   //  7	Thinker
		{ 31, 9051 },   //  8	David
		{ 54, 76 },     //  9	Triceratops
		{ 32, 23 },     // 10	Chef -- replaced by a dead lost soul in Chex 3
		{ 33, 9056 },   // 11	Big spoon
		{ 34, 35 },     // 12	Street light
		{ 62, 9053 },   // 13	Ramses again
		{ 56, 49 },     // 14	Barstool again
		{ 57, 77 },     // 15	T-rex
		{ 49, 63 },     // 16	Redundant bats
		{ 51, 59 },     // 17	Redundant hanging plant #1
		{ 50, 61 },     // 18	Redundant hanging plant #2
	};
	for (int i = 0; i < 19; ++i)
	{
		ArchiveOperations::replaceThings(current, rep[i][0], rep[i][1]);
	}
}

size_t replaceSpecialsDoom(ArchiveEntry* entry, int oldtype, int newtype, bool tag, int oldtag, int newtag)
{
	if (entry == nullptr)
		return 0;

	size_t size     = entry->size();
	size_t numlines = size / sizeof(MapLine::DoomData);
	size_t changed  = 0;

	auto lines = new MapLine::DoomData[numlines];
	memcpy(lines, entry->rawData(), size);

	// Perform replacement
	for (size_t l = 0; l < numlines; ++l)
	{
		if (lines[l].type == oldtype)
		{
			if (!tag || lines[l].sector_tag == oldtag)
			{
				lines[l].type = newtype;
				if (tag)
					lines[l].sector_tag = newtag;
				++changed;
			}
		}
	}
	// Import the changes if needed
	if (changed > 0)
		entry->importMem(lines, size);
	delete[] lines;

	return changed;
}
size_t replaceSpecialsDoom64(ArchiveEntry* entry, int oldtype, int newtype, bool tag, int oldtag, int newtag)
{
	return 0;
}
size_t replaceSpecialsHexen(
	ArchiveEntry* l_entry,
	ArchiveEntry* t_entry,
	int           oldtype,
	int           newtype,
	bool          arg0,
	bool          arg1,
	bool          arg2,
	bool          arg3,
	bool          arg4,
	int           oldarg0,
	int           oldarg1,
	int           oldarg2,
	int           oldarg3,
	int           oldarg4,
	int           newarg0,
	int           newarg1,
	int           newarg2,
	int           newarg3,
	int           newarg4)
{
	if (l_entry == nullptr && t_entry == nullptr)
		return 0;

	size_t size    = 0;
	size_t changed = 0;

	if (l_entry)
	{
		size            = l_entry->size();
		size_t numlines = size / sizeof(MapLine::HexenData);

		auto lines = new MapLine::HexenData[numlines];
		memcpy(lines, l_entry->rawData(), size);
		size_t lchanged = 0;

		// Perform replacement
		for (size_t l = 0; l < numlines; ++l)
		{
			if (lines[l].type == oldtype)
			{
				if ((!arg0 || lines[l].args[0] == oldarg0) && (!arg1 || lines[l].args[1] == oldarg1)
					&& (!arg2 || lines[l].args[2] == oldarg2) && (!arg3 || lines[l].args[3] == oldarg3)
					&& (!arg4 || lines[l].args[4] == oldarg4))
				{
					lines[l].type = newtype;
					if (arg0)
						lines[l].args[0] = newarg0;
					if (arg1)
						lines[l].args[1] = newarg1;
					if (arg2)
						lines[l].args[2] = newarg2;
					if (arg3)
						lines[l].args[3] = newarg3;
					if (arg4)
						lines[l].args[4] = newarg4;
					++lchanged;
				}
			}
		}
		// Import the changes if needed
		if (lchanged > 0)
		{
			l_entry->importMem(lines, size);
			changed += lchanged;
		}
		delete[] lines;
	}

	if (t_entry)
	{
		size             = t_entry->size();
		size_t numthings = size / sizeof(MapThing::HexenData);

		auto things = new MapThing::HexenData[numthings];
		memcpy(things, t_entry->rawData(), size);
		size_t tchanged = 0;

		// Perform replacement
		for (size_t t = 0; t < numthings; ++t)
		{
			if (things[t].type == oldtype)
			{
				if ((!arg0 || things[t].args[0] == oldarg0) && (!arg1 || things[t].args[1] == oldarg1)
					&& (!arg2 || things[t].args[2] == oldarg2) && (!arg3 || things[t].args[3] == oldarg3)
					&& (!arg4 || things[t].args[4] == oldarg4))
				{
					things[t].type = newtype;
					if (arg0)
						things[t].args[0] = newarg0;
					if (arg1)
						things[t].args[1] = newarg1;
					if (arg2)
						things[t].args[2] = newarg2;
					if (arg3)
						things[t].args[3] = newarg3;
					if (arg4)
						things[t].args[4] = newarg4;
					++tchanged;
				}
			}
		}
		// Import the changes if needed
		if (tchanged > 0)
		{
			t_entry->importMem(things, size);
			changed += tchanged;
		}
		delete[] things;
	}

	return changed;
}
size_t replaceSpecialsUDMF(
	ArchiveEntry* entry,
	int           oldtype,
	int           newtype,
	bool          arg0,
	bool          arg1,
	bool          arg2,
	bool          arg3,
	bool          arg4,
	int           oldarg0,
	int           oldarg1,
	int           oldarg2,
	int           oldarg3,
	int           oldarg4,
	int           newarg0,
	int           newarg1,
	int           newarg2,
	int           newarg3,
	int           newarg4)
{
	if (entry == nullptr)
		return 0;

	size_t changed = 0;
	// TODO: parse and replace code
	// Import the changes if needed
	if (changed > 0)
	{
		// entry->importMemChunk(mc);
	}
	return changed;
}
size_t ArchiveOperations::replaceSpecials(
	Archive* archive,
	int      oldtype,
	int      newtype,
	bool     lines,
	bool     things,
	bool     arg0,
	int      oldarg0,
	int      newarg0,
	bool     arg1,
	int      oldarg1,
	int      newarg1,
	bool     arg2,
	int      oldarg2,
	int      newarg2,
	bool     arg3,
	int      oldarg3,
	int      newarg3,
	bool     arg4,
	int      oldarg4,
	int      newarg4)
{
	size_t changed = 0;
	// Check archive was given
	if (!archive)
		return changed;

	// Get all maps
	auto   maps   = archive->detectMaps();
	string report = "";

	for (auto& map : maps)
	{
		size_t achanged = 0;
		// Is it an embedded wad?
		if (map.archive)
		{
			// Attempt to open entry as wad archive
			Archive* temp_archive = new WadArchive();
			if (temp_archive->open(map.head))
			{
				achanged = ArchiveOperations::replaceSpecials(
					temp_archive,
					oldtype,
					newtype,
					lines,
					things,
					arg0,
					oldarg0,
					newarg0,
					arg1,
					oldarg1,
					newarg1,
					arg2,
					oldarg2,
					newarg2,
					arg3,
					oldarg3,
					newarg3,
					arg4,
					oldarg4,
					newarg4);
				MemChunk mc;
				if (!(temp_archive->write(mc, true)))
				{
					achanged = 0;
				}
				else
				{
					temp_archive->close();
					if (!(map.head->importMemChunk(mc)))
					{
						achanged = 0;
					}
				}
			}

			// Cleanup
			delete temp_archive;
		}
		else
		{
			// Find the map entry to modify
			ArchiveEntry* mapentry = map.head;
			ArchiveEntry* t_entry  = nullptr;
			ArchiveEntry* l_entry  = nullptr;
			if (map.format == MapFormat::Doom || map.format == MapFormat::Doom64 || map.format == MapFormat::Hexen)
			{
				while (mapentry && mapentry != map.end)
				{
					if (things && mapentry->type() == EntryType::fromId("map_things"))
					{
						t_entry = mapentry;
						if (l_entry || !lines)
							break;
					}
					if (lines && mapentry->type() == EntryType::fromId("map_linedefs"))
					{
						l_entry = mapentry;
						if (t_entry || !things)
							break;
					}
					mapentry = mapentry->nextEntry();
				}
			}
			else if (map.format == MapFormat::UDMF)
			{
				while (mapentry && mapentry != map.end)
				{
					if (mapentry->type() == EntryType::fromId("udmf_textmap"))
					{
						l_entry = t_entry = mapentry;
						break;
					}
					mapentry = mapentry->nextEntry();
				}
			}

			// Did we get a map entry?
			if (l_entry || t_entry)
			{
				switch (map.format)
				{
				case MapFormat::Doom:
					if (arg1 || arg2 || arg3 || arg4) // Do nothing if Hexen specials are being modified
						break;
					achanged = replaceSpecialsDoom(l_entry, oldtype, newtype, arg0, oldarg0, newarg0);
					break;
				case MapFormat::Hexen:
					if (oldtype > 255 || newtype > 255) // Do nothing if Doom specials are being modified
						break;
					achanged = replaceSpecialsHexen(
						l_entry,
						t_entry,
						oldtype,
						newtype,
						arg0,
						arg1,
						arg2,
						arg3,
						arg4,
						oldarg0,
						oldarg1,
						oldarg2,
						oldarg3,
						oldarg4,
						newarg0,
						newarg1,
						newarg2,
						newarg3,
						newarg4);
					break;
				case MapFormat::Doom64:
					if (arg1 || arg2 || arg3 || arg4) // Do nothing if Hexen specials are being modified
						break;
					achanged = replaceSpecialsDoom64(l_entry, oldtype, newtype, arg0, oldarg0, newarg0);
					break;
				case MapFormat::UDMF:
					achanged = replaceSpecialsUDMF(
						l_entry,
						oldtype,
						newtype,
						arg0,
						arg1,
						arg2,
						arg3,
						arg4,
						oldarg0,
						oldarg1,
						oldarg2,
						oldarg3,
						oldarg4,
						newarg0,
						newarg1,
						newarg2,
						newarg3,
						newarg4);
					break;
				default: Log::warning("Unknown map format for " + map.head->name()); break;
				}
			}
		}
		report += S_FMT("%s:\t%i specials changed\n", map.head->name(), achanged);
		changed += achanged;
	}
	Log::info(1, report);
	return changed;
}

CONSOLE_COMMAND(replacespecials, 2, true)
{
	Archive* current = MainEditor::currentArchive();
	long     oldtype, newtype;
	bool     arg0 = false, arg1 = false, arg2 = false, arg3 = false, arg4 = false;
	long     oldarg0, oldarg1, oldarg2, oldarg3, oldarg4;
	long     newarg0, newarg1, newarg2, newarg3, newarg4;
	size_t   fullarg = args.size();
	size_t   oldtail = (fullarg / 2) - 1;
	size_t   newtail = fullarg - 1;
	bool     run     = false;

	if (fullarg > 2 && (fullarg % 2 == 0))
	{
		switch (fullarg)
		{
		case 12: arg4 = args[oldtail--].ToLong(&oldarg4) && args[newtail--].ToLong(&newarg4);
		case 10: arg3 = args[oldtail--].ToLong(&oldarg3) && args[newtail--].ToLong(&newarg3);
		case 8: arg2 = args[oldtail--].ToLong(&oldarg2) && args[newtail--].ToLong(&newarg2);
		case 6: arg1 = args[oldtail--].ToLong(&oldarg1) && args[newtail--].ToLong(&newarg1);
		case 4: arg0 = args[oldtail--].ToLong(&oldarg0) && args[newtail--].ToLong(&newarg0);
		case 2: run = args[oldtail--].ToLong(&oldtype) && args[newtail--].ToLong(&newtype); break;
		default: Log::warning(S_FMT("Invalid number of arguments: %d", fullarg));
		}
	}

	if (current && run)
	{
		ArchiveOperations::replaceSpecials(
			current,
			oldtype,
			newtype,
			true,
			true,
			arg0,
			oldarg0,
			newarg0,
			arg1,
			oldarg1,
			newarg1,
			arg2,
			oldarg2,
			newarg2,
			arg3,
			oldarg3,
			newarg3,
			arg4,
			oldarg4,
			newarg4);
	}
}

bool replaceTextureString(char* str, string oldtex, string newtex)
{
	bool go = true;
	for (unsigned c = 0; c < oldtex.Length(); ++c)
	{
		if (str[c] != oldtex[c] && oldtex[c] != '?' && oldtex[c] != '*')
			go = false;
		if (oldtex[c] == '*')
			break;
	}
	if (go)
	{
		for (unsigned i = 0; i < 8; ++i)
		{
			if (i < newtex.Len())
			{
				// Keep the rest of the name as-is?
				if (newtex[i] == '*')
					break;
				// Keep just this character as-is?
				if (newtex[i] == '?')
					continue;
				// Else, copy the character
				str[i] = newtex[i];
			}
			else
				str[i] = 0;
		}
	}
	return go;
}
size_t replaceFlatsDoomHexen(ArchiveEntry* entry, const string& oldtex, const string& newtex, bool floor, bool ceiling)
{
	if (entry == nullptr)
		return 0;

	size_t size       = entry->size();
	size_t numsectors = size / sizeof(MapSector::DoomData);
	bool   fchanged, cchanged;
	size_t changed = 0;

	auto sectors = new MapSector::DoomData[numsectors];
	memcpy(sectors, entry->rawData(), size);

	// Perform replacement
	for (size_t s = 0; s < numsectors; ++s)
	{
		fchanged = cchanged = false;
		if (floor)
			fchanged = replaceTextureString(sectors[s].f_tex, oldtex, newtex);
		if (ceiling)
			cchanged = replaceTextureString(sectors[s].c_tex, oldtex, newtex);
		if (fchanged || cchanged)
			++changed;
	}
	// Import the changes if needed
	if (changed > 0)
		entry->importMem(sectors, size);
	delete[] sectors;

	return changed;
}
size_t replaceWallsDoomHexen(
	ArchiveEntry* entry,
	const string& oldtex,
	const string& newtex,
	bool          lower,
	bool          middle,
	bool          upper)
{
	if (entry == nullptr)
		return 0;

	size_t size     = entry->size();
	size_t numsides = size / sizeof(MapSide::DoomData);
	bool   lchanged, mchanged, uchanged;
	size_t changed = 0;

	MapSide::DoomData* sides = new MapSide::DoomData[numsides];
	memcpy(sides, entry->rawData(), size);
	char compare[9];
	compare[8] = 0;

	// Perform replacement
	for (size_t s = 0; s < numsides; ++s)
	{
		lchanged = mchanged = uchanged = false;
		if (lower)
			lchanged = replaceTextureString(sides[s].tex_lower, oldtex, newtex);
		if (middle)
			mchanged = replaceTextureString(sides[s].tex_middle, oldtex, newtex);
		if (upper)
			uchanged = replaceTextureString(sides[s].tex_upper, oldtex, newtex);
		if (lchanged || mchanged || uchanged)
			++changed;
	}
	// Import the changes if needed
	if (changed > 0)
		entry->importMem(sides, size);
	delete[] sides;

	return changed;
}
size_t replaceFlatsDoom64(ArchiveEntry* entry, const string& oldtex, const string& newtex, bool floor, bool ceiling)
{
	if (entry == nullptr)
		return 0;

	size_t size       = entry->size();
	size_t numsectors = size / sizeof(MapSector::Doom64Data);
	bool   fchanged, cchanged;
	size_t changed = 0;

	uint16_t oldhash = App::resources().getTextureHash(oldtex);
	uint16_t newhash = App::resources().getTextureHash(newtex);

	auto sectors = new MapSector::Doom64Data[numsectors];
	memcpy(sectors, entry->rawData(), size);

	// Perform replacement
	for (size_t s = 0; s < numsectors; ++s)
	{
		fchanged = cchanged = false;
		if (floor && oldhash == sectors[s].f_tex)
		{
			sectors[s].f_tex = newhash;
			fchanged         = true;
		}
		if (ceiling && oldhash == sectors[s].c_tex)
		{
			sectors[s].c_tex = newhash;
			cchanged         = true;
		}
		if (fchanged || cchanged)
			++changed;
	}
	// Import the changes if needed
	if (changed > 0)
		entry->importMem(sectors, size);
	delete[] sectors;

	return changed;
}
size_t replaceWallsDoom64(
	ArchiveEntry* entry,
	const string& oldtex,
	const string& newtex,
	bool          lower,
	bool          middle,
	bool          upper)
{
	if (entry == nullptr)
		return 0;

	size_t size     = entry->size();
	size_t numsides = size / sizeof(MapSide::Doom64Data);
	bool   lchanged, mchanged, uchanged;
	size_t changed = 0;

	uint16_t oldhash = App::resources().getTextureHash(oldtex);
	uint16_t newhash = App::resources().getTextureHash(newtex);

	auto sides = new MapSide::Doom64Data[numsides];
	memcpy(sides, entry->rawData(), size);

	// Perform replacement
	for (size_t s = 0; s < numsides; ++s)
	{
		lchanged = mchanged = uchanged = false;
		if (lower && oldhash == sides[s].tex_lower)
		{
			sides[s].tex_lower = newhash;
			lchanged           = true;
		}
		if (middle && oldhash == sides[s].tex_middle)
		{
			sides[s].tex_middle = newhash;
			mchanged            = true;
		}
		if (upper && oldhash == sides[s].tex_upper)
		{
			sides[s].tex_upper = newhash;
			uchanged           = true;
		}
		if (lchanged || mchanged || uchanged)
			++changed;
	}
	// Import the changes if needed
	if (changed > 0)
		entry->importMem(sides, size);
	delete[] sides;

	return changed;
}
size_t replaceTexturesUDMF(
	ArchiveEntry* entry,
	const string& oldtex,
	const string& newtex,
	bool          floor,
	bool          ceiling,
	bool          lower,
	bool          middle,
	bool          upper)
{
	if (entry == nullptr)
		return 0;

	size_t changed = 0;
	// TODO: parse and replace code
	// Import the changes if needed
	if (changed > 0)
	{
		// entry->importMemChunk(mc);
	}
	return changed;
}
size_t ArchiveOperations::replaceTextures(
	Archive*      archive,
	const string& oldtex,
	const string& newtex,
	bool          floor,
	bool          ceiling,
	bool          lower,
	bool          middle,
	bool          upper)
{
	size_t changed = 0;
	// Check archive was given
	if (!archive)
		return changed;

	// Get all maps
	auto   maps   = archive->detectMaps();
	string report = "";

	for (auto& map : maps)
	{
		size_t achanged = 0;
		// Is it an embedded wad?
		if (map.archive)
		{
			// Attempt to open entry as wad archive
			Archive* temp_archive = new WadArchive();
			if (temp_archive->open(map.head))
			{
				achanged = ArchiveOperations::replaceTextures(
					temp_archive, oldtex, newtex, floor, ceiling, lower, middle, upper);
				MemChunk mc;
				if (!(temp_archive->write(mc, true)))
				{
					achanged = 0;
				}
				else
				{
					temp_archive->close();
					if (!(map.head->importMemChunk(mc)))
					{
						achanged = 0;
					}
				}
			}

			// Cleanup
			delete temp_archive;
		}
		else
		{
			// Find the map entry to modify
			ArchiveEntry* mapentry = map.head;
			ArchiveEntry* sectors  = nullptr;
			ArchiveEntry* sides    = nullptr;
			if (map.format == MapFormat::Doom || map.format == MapFormat::Doom64 || map.format == MapFormat::Hexen)
			{
				while (mapentry && mapentry != map.end)
				{
					if ((floor || ceiling) && (mapentry->type() == EntryType::fromId("map_sectors")))
					{
						sectors = mapentry;
						if (sides || !(lower || middle || upper))
							break;
					}
					if ((lower || middle || upper) && (mapentry->type() == EntryType::fromId("map_sidedefs")))
					{
						sides = mapentry;
						if (sectors || !(floor || ceiling))
							break;
					}
					mapentry = mapentry->nextEntry();
				}
			}
			else if (map.format == MapFormat::UDMF)
			{
				while (mapentry && mapentry != map.end)
				{
					if (mapentry->type() == EntryType::fromId("udmf_textmap"))
					{
						sectors = sides = mapentry;
						break;
					}
					mapentry = mapentry->nextEntry();
				}
			}

			// Did we get a map entry?
			if (sectors || sides)
			{
				switch (map.format)
				{
				case MapFormat::Doom:
				case MapFormat::Hexen:
					achanged = 0;
					if (sectors)
						achanged += replaceFlatsDoomHexen(sectors, oldtex, newtex, floor, ceiling);
					if (sides)
						achanged += replaceWallsDoomHexen(sides, oldtex, newtex, lower, middle, upper);
					break;
				case MapFormat::Doom64:
					achanged = 0;
					if (sectors)
						achanged += replaceFlatsDoom64(sectors, oldtex, newtex, floor, ceiling);
					if (sides)
						achanged += replaceWallsDoom64(sides, oldtex, newtex, lower, middle, upper);
					break;
				case MapFormat::UDMF:
					achanged = replaceTexturesUDMF(sectors, oldtex, newtex, floor, ceiling, lower, middle, upper);
					break;
				default: Log::warning("Unknown map format for " + map.head->name()); break;
				}
			}
		}
		report += S_FMT("%s:\t%i elements changed\n", map.head->name(), achanged);
		changed += achanged;
	}
	Log::info(1, report);
	return changed;
}

CONSOLE_COMMAND(replacetextures, 2, true)
{
	auto current = MainEditor::currentArchive();

	if (current)
	{
		ArchiveOperations::replaceTextures(current, args[0], args[1], true, true, true, true, true);
	}
}
