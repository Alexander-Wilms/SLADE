#pragma once

#include "Archive/ArchiveEntry.h"
#include "CTexture.h"
#include "PatchTable.h"

class TextureXList
{
public:
	// TEXTUREx texture patch
	struct Patch
	{
		int16_t  left;
		int16_t  top;
		uint16_t patch;
	};

	// Enum for different texturex formats
	enum class Format
	{
		Normal,
		Strife11,
		Nameless,
		Textures,
		Jaguar,
	};

	enum Flags
	{
		WorldPanning = 0x8000
	};

	TextureXList()  = default;
	~TextureXList() = default;

	uint32_t size() const { return textures_.size(); }

	CTexture* texture(size_t index);
	CTexture* texture(const string& name);
	Format    format() const { return txformat_; }
	string    textureXFormatString() const;
	int       textureIndex(const string& name);

	void setFormat(Format format) { txformat_ = format; }

	void           addTexture(CTexture::UPtr tex, int position = -1);
	CTexture::UPtr removeTexture(unsigned index);
	void           swapTextures(unsigned index1, unsigned index2);
	CTexture::UPtr replaceTexture(unsigned index, CTexture::UPtr replacement);

	void clear(bool clear_patches = false);
	void removePatch(const string& patch);

	bool readTEXTUREXData(ArchiveEntry* texturex, PatchTable& patch_table, bool add = false);
	bool writeTEXTUREXData(ArchiveEntry* texturex, PatchTable& patch_table);

	bool readTEXTURESData(ArchiveEntry* textures);
	bool writeTEXTURESData(ArchiveEntry* textures);

	bool convertToTEXTURES();
	bool findErrors();

private:
	vector<CTexture::UPtr> textures_;
	Format                 txformat_ = Format::Normal;
	CTexture               tex_invalid_{ "INVALID_TEXTURE" }; // Deliberately set the invalid name to >8 characters
};
