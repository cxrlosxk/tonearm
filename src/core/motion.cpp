// SPDX-License-Identifier: GPL-2.0-or-later
#include "motion.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <sstream>

namespace tonearm {

bool wantsMotion(const Style &s)
{
	return s.artworkMode == ArtworkMode::AnimatedWhenAvailable && !s.reducedMotion && s.showArtwork;
}

namespace {

// Decodes one UTF-8 code point; invalid bytes are returned as-is (as Latin-1).
uint32_t nextCodePoint(const std::string &s, size_t &i)
{
	const auto b = static_cast<unsigned char>(s[i]);
	int len = b < 0x80 ? 1 : (b >> 5) == 0x6 ? 2 : (b >> 4) == 0xE ? 3 : (b >> 3) == 0x1E ? 4 : 0;
	if (len == 0 || i + len > s.size()) {
		++i;
		return b;
	}
	uint32_t cp = len == 1 ? b : len == 2 ? (b & 0x1F) : len == 3 ? (b & 0x0F) : (b & 0x07);
	for (int k = 1; k < len; ++k) {
		const auto c = static_cast<unsigned char>(s[i + k]);
		if ((c >> 6) != 0x2) {
			++i;
			return b;
		}
		cp = (cp << 6) | (c & 0x3F);
	}
	i += len;
	return cp;
}

void appendUtf8(std::string &out, uint32_t cp)
{
	if (cp < 0x80) {
		out += static_cast<char>(cp);
	} else if (cp < 0x800) {
		out += static_cast<char>(0xC0 | (cp >> 6));
		out += static_cast<char>(0x80 | (cp & 0x3F));
	} else if (cp < 0x10000) {
		out += static_cast<char>(0xE0 | (cp >> 12));
		out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
		out += static_cast<char>(0x80 | (cp & 0x3F));
	} else {
		out += static_cast<char>(0xF0 | (cp >> 18));
		out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
		out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
		out += static_cast<char>(0x80 | (cp & 0x3F));
	}
}

// Folds Latin-1 Supplement and Latin Extended-A letters to ASCII.
const char *foldLatin(uint32_t cp)
{
	static const char *const latin1[64] = {
		"a", "a", "a", "a", "a", "a", "ae", "c", "e", "e", "e", "e", "i", "i", "i", "i", // C0-CF
		"d", "n", "o", "o", "o", "o", "o", nullptr, "o", "u", "u", "u", "u", "y", "th", "ss", // D0-DF
		"a", "a", "a", "a", "a", "a", "ae", "c", "e", "e", "e", "e", "i", "i", "i", "i", // E0-EF
		"d", "n", "o", "o", "o", "o", "o", nullptr, "o", "u", "u", "u", "u", "y", "th", "y", // F0-FF
	};
	// Latin Extended-A, U+0100..U+017F, in pairs (upper, lower) mostly.
	static const char extA[] = "aaaaaaccccccccddddeeeeeeeeeegggggggghhhhiiiiiiiiiiiijjkkkllllllllllnnnnnnnnnoooooooorrrrrrssssssssttttttuuuuuuuuuuuuwwyyyzzzzzzs";
	if (cp >= 0xC0 && cp <= 0xFF)
		return latin1[cp - 0xC0];
	if (cp >= 0x100 && cp <= 0x17F) {
		static const auto table = [] {
			std::vector<std::string> t;
			for (size_t k = 0; k < sizeof(extA) - 1; ++k)
				t.emplace_back(1, extA[k]);
			return t;
		}();
		return table[cp - 0x100].c_str();
	}
	return nullptr;
}

bool isApostrophe(uint32_t cp)
{
	return cp == '\'' || cp == 0x2019 || cp == 0x2018 || cp == 0x02BC || cp == '`' || cp == 0xB4;
}

bool isDash(uint32_t cp)
{
	return cp == '-' || cp == 0x2010 || cp == 0x2011 || cp == 0x2012 || cp == 0x2013 || cp == 0x2014 || cp == 0x2015 || cp == 0x2212;
}

std::vector<std::string> words(const std::string &normalized)
{
	std::vector<std::string> out;
	std::istringstream in(normalized);
	std::string w;
	while (in >> w)
		out.push_back(w);
	return out;
}

std::string join(const std::vector<std::string> &w, size_t from = 0, size_t to = std::string::npos)
{
	std::string out;
	to = std::min(to, w.size());
	for (size_t i = from; i < to; ++i) {
		if (!out.empty())
			out += ' ';
		out += w[i];
	}
	return out;
}

// Words that mark a bracketed group or dash suffix as an edition qualifier.
bool isEditionWord(const std::string &w)
{
	static const char *const kWords[] = {
		"deluxe", "remaster", "remastered", "remasters", "anniversary", "expanded", "edition", "version", "live",
		"single", "ep", "bonus", "special", "collector", "collectors", "super", "platinum", "mono", "stereo",
		"acoustic", "instrumental", "demo", "demos", "reissue", "redux", "extended", "remix", "remixes", "sped",
		"slowed", "explicit", "clean", "complete", "legacy", "edit", "3am", "tour", "limited", "director",
		"directors", "standard", "mix", "mixes", "international", "japan", "japanese", "commentary",
	};
	for (const char *k : kWords)
		if (w == k)
			return true;
	return false;
}

// Qualifier words that don't distinguish releases ("Deluxe" == "Deluxe Edition").
bool isFillerWord(const std::string &w)
{
	return w == "edition" || w == "version" || w == "the" || w == "a" || w == "an" || w == "and" || w == "of" ||
	       w == "explicit" || w == "clean";
}

std::string canonicalEditionWord(const std::string &w)
{
	if (w == "remastered" || w == "remasters")
		return "remaster";
	if (w == "collector")
		return "collectors";
	if (w == "demos")
		return "demo";
	if (w == "mixes")
		return "mix";
	if (w == "remixes")
		return "remix";
	if (w == "directors")
		return "director";
	return w;
}

void addEdition(std::set<std::string> &edition, const std::string &normalizedGroup)
{
	for (const std::string &w : words(normalizedGroup))
		if (!isFillerWord(w))
			edition.insert(canonicalEditionWord(w));
}

bool hasEditionWord(const std::string &normalizedGroup)
{
	for (const std::string &w : words(normalizedGroup))
		if (isEditionWord(w))
			return true;
	return false;
}

} // namespace

std::string normalizeName(const std::string &s)
{
	std::string out;
	out.reserve(s.size());
	size_t i = 0;
	while (i < s.size()) {
		const uint32_t cp = nextCodePoint(s, i);
		if (isApostrophe(cp))
			continue; // "Don't" -> "dont"
		if (cp == '&' || cp == '+') {
			out += " and ";
			continue;
		}
		if (cp < 0x80) {
			const char c = static_cast<char>(cp);
			if (c >= 'A' && c <= 'Z')
				out += static_cast<char>(c - 'A' + 'a');
			else if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))
				out += c;
			else
				out += ' ';
			continue;
		}
		if (const char *f = foldLatin(cp)) {
			out += f;
			continue;
		}
		// General punctuation and symbols become separators; other scripts are kept.
		if ((cp >= 0x2000 && cp <= 0x206F) || (cp >= 0x80 && cp <= 0xBF) || cp == 0xD7 || cp == 0xF7 || (cp >= 0x3000 && cp <= 0x303F) ||
		    (cp >= 0xFF00 && cp <= 0xFF0F)) {
			out += ' ';
			continue;
		}
		appendUtf8(out, cp);
	}
	return join(words(out));
}

AlbumKey albumKey(const std::string &album)
{
	AlbumKey key;
	// Normalize dash variants so " – Single" and " - Single" behave alike.
	std::string s;
	{
		size_t i = 0;
		while (i < album.size()) {
			const size_t start = i;
			const uint32_t cp = nextCodePoint(album, i);
			if (isDash(cp))
				s += '-';
			else
				s.append(album, start, i - start);
		}
	}

	// 1. Bracketed groups: qualifiers are removed, anything else stays in the title.
	std::string kept;
	for (size_t i = 0; i < s.size();) {
		const char open = s[i];
		if (open == '(' || open == '[') {
			const char close = open == '(' ? ')' : ']';
			const size_t end = s.find(close, i + 1);
			if (end != std::string::npos) {
				const std::string inner = normalizeName(s.substr(i + 1, end - i - 1));
				if (hasEditionWord(inner)) {
					addEdition(key.edition, inner);
				} else {
					kept += ' ';
					kept += s.substr(i + 1, end - i - 1);
					kept += ' ';
				}
				i = end + 1;
				continue;
			}
		}
		kept += s[i++];
	}

	// 2. Dash suffixes such as Apple Music's " - Single" or "Album - 2011 Remaster".
	for (;;) {
		const size_t dash = kept.rfind(" - ");
		if (dash == std::string::npos)
			break;
		const std::string suffix = normalizeName(kept.substr(dash + 3));
		if (!hasEditionWord(suffix))
			break;
		addEdition(key.edition, suffix);
		kept.erase(dash);
	}
	key.base = normalizeName(kept);
	return key;
}

std::vector<std::string> artistKeys(const std::string &artist)
{
	std::vector<std::string> out;
	const std::string full = normalizeName(artist);
	if (full.empty())
		return out;
	out.push_back(full);
	// First listed artist: cut at the first separator.
	std::string first = artist;
	for (const char *sep : {",", ";", " & ", " / ", " x ", " X ", " feat.", " feat ", " ft.", " ft ", " featuring ", " Feat.", " Ft.", " with ", " and "}) {
		const size_t at = first.find(sep);
		if (at != std::string::npos && at > 0)
			first.erase(at);
	}
	const std::string f = normalizeName(first);
	if (!f.empty() && f != full)
		out.push_back(f);
	return out;
}

CropRect coverCrop(int frameW, int frameH, float dstW, float dstH)
{
	CropRect c{0, 0, static_cast<float>(frameW), static_cast<float>(frameH)};
	if (frameW <= 0 || frameH <= 0 || dstW <= 0 || dstH <= 0)
		return c;
	const float frameAspect = static_cast<float>(frameW) / static_cast<float>(frameH);
	const float dstAspect = dstW / dstH;
	if (frameAspect > dstAspect) {
		c.w = frameH * dstAspect;
		c.x = (frameW - c.w) * 0.5f;
	} else {
		c.h = frameW / dstAspect;
		c.y = (frameH - c.h) * 0.5f;
	}
	return c;
}

MotionShape preferredShape(MotionOrientation o, float artWidth, float artHeight)
{
	switch (o) {
	case MotionOrientation::Square:
		return MotionShape::Square;
	case MotionOrientation::Portrait:
		return MotionShape::Portrait;
	case MotionOrientation::Automatic:
		break;
	}
	return artWidth > 0 && artHeight / artWidth > 1.12f ? MotionShape::Portrait : MotionShape::Square;
}

std::optional<MotionLibrary::Entry> MotionLibrary::parse(const std::string &fileName, const std::string &folder)
{
	const size_t dot = fileName.rfind('.');
	if (dot == std::string::npos || dot == 0)
		return std::nullopt;
	std::string ext = fileName.substr(dot + 1);
	std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	static const char *const kExt[] = {"mp4", "m4v", "mov", "webm", "mkv", "m3u8"};
	if (std::none_of(std::begin(kExt), std::end(kExt), [&](const char *e) { return ext == e; }))
		return std::nullopt;

	std::string stem = fileName.substr(0, dot);
	Entry e;
	// Trailing shape tag: [tall], [portrait], [3x4], [square], [1x1].
	if (!stem.empty() && stem.back() == ']') {
		const size_t open = stem.rfind('[');
		if (open != std::string::npos) {
			const std::string tag = normalizeName(stem.substr(open + 1, stem.size() - open - 2));
			if (tag == "tall" || tag == "portrait" || tag == "3x4") {
				e.shape = MotionShape::Portrait;
				stem.erase(open);
			} else if (tag == "square" || tag == "1x1") {
				stem.erase(open);
			}
		}
	}
	while (!stem.empty() && stem.back() == ' ')
		stem.pop_back();

	// "Artist - Album": try every " - " split; the album side may contain dashes too.
	for (size_t at = stem.find(" - "); at != std::string::npos; at = stem.find(" - ", at + 1)) {
		const std::string artist = normalizeName(stem.substr(0, at));
		AlbumKey album = albumKey(stem.substr(at + 3));
		if (artist.empty() || album.base.empty())
			continue;
		e.artistCandidates.push_back(artist);
		e.albumCandidates.push_back(std::move(album));
	}
	if (e.artistCandidates.empty())
		return std::nullopt;
	e.path = folder.empty() ? fileName : folder + "/" + fileName;
	return e;
}

void MotionLibrary::build(const std::vector<std::string> &fileNames, const std::string &folder)
{
	entries_.clear();
	for (const std::string &f : fileNames)
		if (auto e = parse(f, folder))
			entries_.push_back(std::move(*e));
}

std::optional<MotionAsset> MotionLibrary::find(const MotionQuery &q) const
{
	if (q.album.empty() || q.artist.empty())
		return std::nullopt; // no album, no confident match
	const AlbumKey album = albumKey(q.album);
	std::vector<std::string> artists = artistKeys(q.artist);
	for (const std::string &a : artistKeys(q.albumArtist))
		if (std::find(artists.begin(), artists.end(), a) == artists.end())
			artists.push_back(a);
	if (album.base.empty() || artists.empty())
		return std::nullopt;

	const Entry *square = nullptr, *portrait = nullptr;
	int matchedEntries = 0;
	for (const Entry &e : entries_) {
		bool match = false;
		for (size_t i = 0; i < e.artistCandidates.size() && !match; ++i) {
			if (!(e.albumCandidates[i] == album))
				continue;
			match = std::find(artists.begin(), artists.end(), e.artistCandidates[i]) != artists.end();
		}
		if (!match)
			continue;
		++matchedEntries;
		const Entry *&slot = e.shape == MotionShape::Portrait ? portrait : square;
		if (slot && slot->path != e.path)
			return std::nullopt; // two different files claim the same release and shape: ambiguous
		slot = &e;
	}
	(void)matchedEntries;
	const Entry *pick = q.preferred == MotionShape::Portrait ? (portrait ? portrait : square) : (square ? square : portrait);
	if (!pick)
		return std::nullopt;
	return MotionAsset{pick->path, pick->shape};
}

} // namespace tonearm
