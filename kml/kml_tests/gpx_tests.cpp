#include "testing/testing.hpp"

#include "kml/serdes_common.hpp"
#include "kml/serdes_gpx.hpp"

#include "geometry/mercator.hpp"

#include "coding/file_reader.hpp"

#include "platform/platform.hpp"

namespace gpx_tests
{
static kml::FileData loadGpxFromString(std::string_view content) {
  try
  {
    kml::FileData dataFromText;
    kml::DeserializerGpx(dataFromText).Deserialize(MemReader(content));
    return dataFromText;
  }
  catch (kml::DeserializerGpx::DeserializeException const & exc)
  {
    TEST(false, ("Exception raised", exc.what()));
  }
}

static kml::FileData loadGpxFromFile(std::string const & file) {
  auto const fileName = GetPlatform().TestsDataPathForFile(file);
  std::string text;
  FileReader(fileName).ReadAsString(text);
  return loadGpxFromString(text);
}

UNIT_TEST(Gpx_Test_Point)
{
  std::string_view constexpr input = R"(<?xml version="1.0" encoding="UTF-8"?>
<gpx version="1.0">
 <wpt lat="42.81025" lon="-1.65727">
  <time>2022-09-05T08:39:39.3700Z</time>
  <name>Waypoint 1</name>
 </wpt>
)";

  kml::FileData data;
  kml::BookmarkData bookmarkData;
  bookmarkData.m_name[kml::kDefaultLang] = "Waypoint 1";
  bookmarkData.m_point = mercator::FromLatLon(42.81025, -1.65727);
  bookmarkData.m_customName[kml::kDefaultLang] = "Waypoint 1";
  bookmarkData.m_color = {kml::PredefinedColor::Red, 0};
  data.m_bookmarksData.emplace_back(std::move(bookmarkData));

  kml::FileData const dataFromText = loadGpxFromString(input);

  TEST_EQUAL(dataFromText, data, ());
}

UNIT_TEST(Gpx_Test_Route)
{
  std::string_view constexpr input = R"(<?xml version="1.0" encoding="UTF-8"?>
<gpx version="1.0">
<trk>
    <name>new</name>
    <type>Cycling</type>
    <trkseg>
      <trkpt lat="54.23955053156179" lon="24.114990234375004">
          <ele>130.5</ele>
      </trkpt>
      <trkpt lat="54.32933804825253" lon="25.136718750000004">
          <ele>0.0</ele>
      </trkpt>
      <trkpt lat="54.05293900056246" lon="25.72998046875">
          <ele>0.0</ele>
      </trkpt>
    </trkseg>
    <trkseg>
      <trkpt lat="55.23955053156179" lon="25.114990234375004">
          <ele>131.5</ele>
      </trkpt>
      <trkpt lat="54.32933804825253" lon="25.136718750000004">
          <ele>0.0</ele>
      </trkpt>
    </trkseg>
</trk>
</gpx>
)";

  kml::FileData const dataFromText = loadGpxFromString(input);
  auto const & lines = dataFromText.m_tracksData[0].m_geometry.m_lines;
  TEST_EQUAL(lines.size(), 2, ());
  {
    auto const & line = lines[0];
    TEST_EQUAL(line.size(), 3, ());
    TEST_EQUAL(line.back(), geometry::PointWithAltitude(mercator::FromLatLon(54.05293900056246, 25.72998046875), 0), ());
  }
  {
    auto const & line = lines[1];
    TEST_EQUAL(line.size(), 2, ());
    TEST_EQUAL(line.back(), geometry::PointWithAltitude(mercator::FromLatLon(54.32933804825253, 25.136718750000004), 0), ());
  }
  // Also test default colors for tracks.
  {
    TEST_EQUAL(dataFromText.m_tracksData.size(), 1, ());
    TEST_EQUAL(dataFromText.m_tracksData[0].m_layers.size(), 1, ());
    auto const & layer = dataFromText.m_tracksData[0].m_layers[0];
    TEST_EQUAL(layer.m_color.m_rgba, kml::kDefaultTrackColor, ());
    TEST_EQUAL(layer.m_color.m_predefinedColor, kml::PredefinedColor::None, ());
    TEST_EQUAL(layer.m_lineWidth, kml::kDefaultTrackWidth, ());
  }

}

UNIT_TEST(Gpx_Altitude_Issues)
{
  std::string_view constexpr input = R"(<?xml version="1.0" encoding="UTF-8"?>
<gpx version="1.0">
<trk>
    <name>new</name>
    <type>Cycling</type>
    <trkseg>
      <trkpt lat="1" lon="1"></trkpt>
      <trkpt lat="2" lon="2"><ele>1.0</ele></trkpt>
      <trkpt lat="3" lon="3"></trkpt>
      <trkpt lat="4" lon="4"><ele>2.0</ele></trkpt>
      <trkpt lat="5" lon="5"><ele>Wrong</ele></trkpt>
      <trkpt lat="6" lon="6"><ele>3</ele></trkpt>
    </trkseg>
</trk>
</gpx>
)";

  kml::FileData const dataFromText = loadGpxFromString(input);
  auto line = dataFromText.m_tracksData[0].m_geometry.m_lines[0];
  TEST_EQUAL(line.size(), 6, ());
  TEST_EQUAL(line[0], geometry::PointWithAltitude(mercator::FromLatLon(1, 1), geometry::kInvalidAltitude), ());
  TEST_EQUAL(line[1], geometry::PointWithAltitude(mercator::FromLatLon(2, 2), 1), ());
  TEST_EQUAL(line[2], geometry::PointWithAltitude(mercator::FromLatLon(3, 3), geometry::kInvalidAltitude), ());
  TEST_EQUAL(line[3], geometry::PointWithAltitude(mercator::FromLatLon(4, 4), 2), ());
  TEST_EQUAL(line[4], geometry::PointWithAltitude(mercator::FromLatLon(5, 5), geometry::kInvalidAltitude), ());
  TEST_EQUAL(line[5], geometry::PointWithAltitude(mercator::FromLatLon(6, 6), 3), ());
}

UNIT_TEST(GoMap)
{
  kml::FileData const dataFromFile = loadGpxFromFile("gpx_test_data/go_map.gpx");
  auto const & line = dataFromFile.m_tracksData[0].m_geometry.m_lines[0];
  TEST_EQUAL(line.size(), 101, ());
}

UNIT_TEST(GpxStudio)
{
  kml::FileData const dataFromFile = loadGpxFromFile("gpx_test_data/gpx_studio.gpx");
  auto const & line = dataFromFile.m_tracksData[0].m_geometry.m_lines[0];
  TEST_EQUAL(line.size(), 328, ());
}

UNIT_TEST(OsmTrack)
{
  kml::FileData const dataFromFile = loadGpxFromFile("gpx_test_data/osm_track.gpx");
  auto line = dataFromFile.m_tracksData[0].m_geometry.m_lines[0];
  TEST_EQUAL(line.size(), 182, ());
}

UNIT_TEST(TowerCollector)
{
  kml::FileData const dataFromFile = loadGpxFromFile("gpx_test_data/tower_collector.gpx");
  auto line = dataFromFile.m_tracksData[0].m_geometry.m_lines[0];
  TEST_EQUAL(line.size(), 35, ());
}

UNIT_TEST(PointsOnly)
{
  kml::FileData const dataFromFile = loadGpxFromFile("gpx_test_data/points.gpx");
  auto bookmarks = dataFromFile.m_bookmarksData;
  TEST_EQUAL(bookmarks.size(), 3, ());
  TEST_EQUAL("Point 1", bookmarks[0].m_name[kml::kDefaultLang], ());
  TEST_EQUAL(bookmarks[0].m_point, mercator::FromLatLon(48.20984622935899, 16.376023292541507), ());
}

UNIT_TEST(Route)
{
  kml::FileData dataFromFile = loadGpxFromFile("gpx_test_data/route.gpx");
  auto line = dataFromFile.m_tracksData[0].m_geometry.m_lines[0];
  TEST_EQUAL(line.size(), 2, ());
  TEST_EQUAL(dataFromFile.m_categoryData.m_name[kml::kDefaultLang], "Some random route", ());
  TEST_EQUAL(line[0], geometry::PointWithAltitude(mercator::FromLatLon(48.20984622935899, 16.376023292541507), 184), ());
  TEST_EQUAL(line[1], geometry::PointWithAltitude(mercator::FromLatLon(48.209503040543545, 16.381065845489506), 187), ());
}

UNIT_TEST(Color)
{
  kml::FileData const dataFromFile = loadGpxFromFile("gpx_test_data/color.gpx");
  uint32_t const red = 0xFF0000FF;
  uint32_t const blue = 0x0000FFFF;
  uint32_t const black = 0x000000FF;
  TEST_EQUAL(red, dataFromFile.m_tracksData[0].m_layers[0].m_color.m_rgba, ());
  TEST_EQUAL(blue, dataFromFile.m_tracksData[1].m_layers[0].m_color.m_rgba, ());
  TEST_EQUAL(black, dataFromFile.m_tracksData[2].m_layers[0].m_color.m_rgba, ());
  TEST_EQUAL(red, dataFromFile.m_tracksData[3].m_layers[0].m_color.m_rgba, ());
}

UNIT_TEST(MultiTrackNames)
{
  kml::FileData dataFromFile = loadGpxFromFile("gpx_test_data/color.gpx");
  TEST_EQUAL("new", dataFromFile.m_categoryData.m_name[kml::kDefaultLang], ());
  TEST_EQUAL("Short description", dataFromFile.m_categoryData.m_description[kml::kDefaultLang], ());
  TEST_EQUAL("new red", dataFromFile.m_tracksData[0].m_name[kml::kDefaultLang], ());
  TEST_EQUAL("description 1", dataFromFile.m_tracksData[0].m_description[kml::kDefaultLang], ());
  TEST_EQUAL("new blue", dataFromFile.m_tracksData[1].m_name[kml::kDefaultLang], ());
  TEST_EQUAL("description 2", dataFromFile.m_tracksData[1].m_description[kml::kDefaultLang], ());
}

UNIT_TEST(Empty)
{
  kml::FileData dataFromFile = loadGpxFromFile("gpx_test_data/empty.gpx");
  TEST_EQUAL("new", dataFromFile.m_categoryData.m_name[kml::kDefaultLang], ());
  TEST_EQUAL(0, dataFromFile.m_tracksData.size(), ());
}

UNIT_TEST(OsmandColor1)
{
  kml::FileData const dataFromFile = loadGpxFromFile("gpx_test_data/osmand1.gpx");
  uint32_t constexpr expected = 0xFF7800FF;
  TEST_EQUAL(dataFromFile.m_tracksData.size(), 4, ());
  TEST_EQUAL(expected, dataFromFile.m_tracksData[0].m_layers[0].m_color.m_rgba, ());
  TEST_EQUAL(expected, dataFromFile.m_tracksData[1].m_layers[0].m_color.m_rgba, ());
  TEST_EQUAL(expected, dataFromFile.m_tracksData[2].m_layers[0].m_color.m_rgba, ());
  TEST_EQUAL(expected, dataFromFile.m_tracksData[3].m_layers[0].m_color.m_rgba, ());
}

UNIT_TEST(OsmandColor2)
{
  kml::FileData const dataFromFile = loadGpxFromFile("gpx_test_data/osmand2.gpx");
  uint32_t const expected1 = 0x00FF00FF;
  uint32_t const expected2 = 0x1010A0FF;
  TEST_EQUAL(expected1, dataFromFile.m_bookmarksData[0].m_color.m_rgba, ());
  TEST_EQUAL(expected2, dataFromFile.m_bookmarksData[1].m_color.m_rgba, ());
}

UNIT_TEST(Gpx_Test_Cmt)
{
  std::string const input = R"(<?xml version="1.0" encoding="UTF-8"?>
<gpx version="1.0">
 <wpt lat="1" lon="2"><name>1</name><desc>d1</desc></wpt>
 <wpt lat="1" lon="2"><name>2</name><desc>d2</desc><cmt>c2</cmt></wpt>
 <wpt lat="1" lon="2"><name>3</name><cmt>c3</cmt></wpt>
 <wpt lat="1" lon="2"><name>4</name>
  <desc>
d4
d5


  </desc>
  <cmt>c4</cmt>
 </wpt>
 <wpt lat="1" lon="2"><name>5</name><cmt>qqq</cmt><desc>qqq</desc></wpt>
)";
  kml::FileData const dataFromText = loadGpxFromString(input);
  TEST_EQUAL("d1", dataFromText.m_bookmarksData[0].m_description.at(kml::kDefaultLang), ());
  TEST_EQUAL("d2\n\nc2", dataFromText.m_bookmarksData[1].m_description.at(kml::kDefaultLang), ());
  TEST_EQUAL("c3", dataFromText.m_bookmarksData[2].m_description.at(kml::kDefaultLang), ());
  TEST_EQUAL("d4\nd5\n\nc4", dataFromText.m_bookmarksData[3].m_description.at(kml::kDefaultLang), ());
  TEST_EQUAL("qqq", dataFromText.m_bookmarksData[4].m_description.at(kml::kDefaultLang), ());
}

UNIT_TEST(OpentracksColor)
{
  kml::FileData dataFromFile = loadGpxFromFile("gpx_test_data/opentracks_color.gpx");
  uint32_t const expected = 0xC0C0C0FF;
  TEST_EQUAL(expected, dataFromFile.m_tracksData[0].m_layers[0].m_color.m_rgba, ());
}

}  // namespace gpx_tests
