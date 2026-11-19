// SPDX-License-Identifier: GPL-2.0-or-later
#include "check.hpp"
#include "media/catalog_match.hpp"

#include <cstdint>
#include <vector>

using namespace tonearm;

namespace {
MotionQuery current(const char *artist, const char *album, const char *albumArtist = "")
{
	MotionQuery q;
	q.artist = artist;
	q.album = album;
	q.albumArtist = albumArtist;
	return q;
}
} // namespace

TEST("catalog: exact artist and album find a unique release")
{
	const auto id = exactCatalogAlbumId(current("Alicia Keys", "The Diary of Alicia Keys"),
		{{200, "Another Artist", "The Diary of Alicia Keys"}, {101, "Alicia Keys", "The Diary of Alicia Keys"}});
	CHECK(id && *id == 101);
}

TEST("catalog: case, accents, and edition punctuation normalize")
{
	const auto id = exactCatalogAlbumId(current("Beyoncé", "Renaissance (Deluxe Edition)"),
		{{101, "BEYONCE", "RENAISSANCE [Deluxe]"}});
	CHECK(id && *id == 101);
}

TEST("catalog: editions must not be confused")
{
	CHECK(!exactCatalogAlbumId(current("Taylor Swift", "Midnights (3am Edition)"),
		{{111, "Taylor Swift", "Midnights"}, {112, "Taylor Swift", "Midnights (Deluxe Edition)"}}));
}

TEST("catalog: ambiguity between distinct IDs returns no match")
{
	CHECK(!exactCatalogAlbumId(current("Adele", "19"),
		{{100, "Adele", "19"}, {200, "Adele", "19"}}));
}

TEST("catalog: a duplicated entry with the same ID is not ambiguous")
{
	const auto id = exactCatalogAlbumId(current("Adele", "19"),
		{{100, "Adele", "19"}, {100, "ADELE", "19"}});
	CHECK(id && *id == 100);
}

TEST("catalog: album artist may identify a compilation")
{
	const auto id = exactCatalogAlbumId(current("Dua Lipa", "Barbie The Album", "Various Artists"),
		{{333, "Various Artists", "Barbie The Album"}});
	CHECK(id && *id == 333);
}

TEST("catalog: soundtrack credit contains the exact playing performer")
{
	const auto q = current("Cynthia Erivo", "Wicked: For Good - The Soundtrack");
	CHECK(catalogArtistMatches(q, "Wicked Movie Cast, Cynthia Erivo & Ariana Grande"));
	CHECK(catalogArtistMatches(q, "Wicked Movie Cast, Cynthia Erivo and Ariana Grande"));
	CHECK(!catalogArtistMatches(q, "Wicked Movie Cast, Ariana Grande"));
	CHECK(!catalogArtistMatches(current("Erivo", q.album.c_str()), "Wicked Movie Cast, Cynthia Erivo & Ariana Grande"));
	const auto id = exactCatalogAlbumId(q,
		{{321, "Wicked Movie Cast, Cynthia Erivo & Ariana Grande", "Wicked: For Good – The Soundtrack"}});
	CHECK(id && *id == 321);
}

TEST("catalog: soundtrack match still requires exact album identity")
{
	const auto id = exactCatalogAlbumId(current("Cynthia Erivo", "Wicked: For Good - The Soundtrack"),
		{{321, "Wicked Movie Cast, Cynthia Erivo & Ariana Grande", "Wicked - The Soundtrack"}});
	CHECK(!id);
}

TEST("motion: only compatible edition aliases share an animated cover")
{
	CHECK(compatibleMotionAlbumTitle("Doo-Wops & Hooligans (Deluxe)", "Doo-Wops & Hooligans"));
	CHECK(compatibleMotionAlbumTitle("The Diary of Alicia Keys", "The Diary of Alicia Keys (20th Anniversary)"));
	CHECK(compatibleMotionAlbumTitle("eternal sunshine deluxe: brighter days ahead", "eternal sunshine"));
	CHECK(compatibleMotionAlbumTitle("B'Day (20th Anniversary Deluxe Edition)", "B'Day"));
	CHECK(!compatibleMotionAlbumTitle("Midnights (3am Edition)", "Midnights (Deluxe Edition)"));
	CHECK(!compatibleMotionAlbumTitle("Wicked: For Good - The Soundtrack", "Wicked: The Soundtrack"));
	CHECK(!compatibleMotionAlbumTitle("The Diary of Alicia Keys", "The Diary of Alicia Keys II"));
	CHECK(!compatibleMotionAlbumTitle("", "The Diary of Alicia Keys"));
}

TEST("motion: edition fallback query preserves the album identity")
{
	CHECK(motionBaseAlbumQuery("Doo-Wops & Hooligans (Deluxe)") == "Doo-Wops & Hooligans");
	CHECK(motionBaseAlbumQuery("eternal sunshine deluxe: brighter days ahead") == "eternal sunshine");
	CHECK(motionBaseAlbumQuery("The Diary of Alicia Keys").empty());
	CHECK(motionBaseAlbumQuery("Wicked: For Good - The Soundtrack").empty());
}

TEST("motion: verified soundtrack track can be credited separately from soundtrack composers")
{
	MotionQuery q = current("Halle", "The Little Mermaid (Original Motion Picture Soundtrack)");
	q.title = "Part of Your World (Reprise)";
	const CatalogAlbum album{1684317223ULL, "Alan Menken & Howard Ashman",
	                         "The Little Mermaid (Original Motion Picture Soundtrack)"};
	CHECK(verifiedSoundtrackTrack(q, album, album.artist, album.title, "Halle", q.title));
	CHECK(!verifiedSoundtrackTrack(q, album, album.artist, album.title, "Melissa McCarthy", q.title));
	CHECK(!verifiedSoundtrackTrack(q, album, album.artist, album.title, "Halle", "Under the Sea"));
	CHECK(!verifiedSoundtrackTrack(q, album, "Tim Mountain", album.title, "Halle", q.title));
	CHECK(!verifiedSoundtrackTrack(q, album, album.artist, "The Little Mermaid (Original Broadway Cast Recording)", "Halle", q.title));
	CHECK(!verifiedSoundtrackTrack(q, {1684317223ULL, album.artist, "The Little Mermaid (Original Motion Picture Soundtrack) [Deluxe Edition]"},
	                              album.artist, album.title, "Halle", q.title));
}

TEST("catalog: missing identifiers and incomplete metadata are not accepted")
{
	CHECK(!exactCatalogAlbumId(current("", "19"), {{101, "Adele", "19"}}));
	CHECK(!exactCatalogAlbumId(current("Adele", ""), {{101, "Adele", "19"}}));
	CHECK(!exactCatalogAlbumId(current("Adele", "19"), {{0, "Adele", "19"}}));
}
