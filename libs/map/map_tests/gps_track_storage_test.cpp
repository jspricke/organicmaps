#include "testing/testing.hpp"

#include "map/gps_track_storage.hpp"

#include "platform/platform.hpp"

#include "coding/file_reader.hpp"
#include "coding/file_writer.hpp"

#include "base/file_name_utils.hpp"
#include "base/logging.hpp"
#include "base/scope_guard.hpp"

#include <array>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace gps_track_storage_test
{
namespace
{
location::GpsInfo Make(size_t i)
{
  location::GpsInfo info;
  info.m_timestamp = 1000. + i;
  info.m_latitude = 10. + i * 0.00001;
  info.m_longitude = 20. + i * 0.00001;
  info.m_altitude = 100. + i;
  info.m_speed = 5. + i;
  info.m_bearing = 90. + i;
  info.m_horizontalAccuracy = 10. + i;
  info.m_verticalAccuracy = 20. + i;
  info.m_source = location::EAndroidNative;
  return info;
}

std::string GetGpsTrackFilePath()
{
  return base::JoinPath(GetPlatform().WritableDir(), "gpstrack_storage_test.bin");
}

void CheckPoints(std::vector<location::GpsInfo> const & actual, std::vector<location::GpsInfo> const & expected)
{
  TEST_EQUAL(actual.size(), expected.size(), ());
  for (size_t i = 0; i < actual.size(); ++i)
  {
    TEST_EQUAL(actual[i].m_timestamp, expected[i].m_timestamp, (i));
    TEST_EQUAL(actual[i].m_latitude, expected[i].m_latitude, (i));
    TEST_EQUAL(actual[i].m_longitude, expected[i].m_longitude, (i));
    TEST_EQUAL(actual[i].m_altitude, expected[i].m_altitude, (i));
    TEST_EQUAL(actual[i].m_speed, expected[i].m_speed, (i));
    TEST_EQUAL(actual[i].m_bearing, expected[i].m_bearing, (i));
    TEST_EQUAL(actual[i].m_horizontalAccuracy, expected[i].m_horizontalAccuracy, (i));
    TEST_EQUAL(actual[i].m_verticalAccuracy, expected[i].m_verticalAccuracy, (i));
    TEST_EQUAL(actual[i].m_source, expected[i].m_source, (i));
  }
}
}  // namespace

UNIT_TEST(GpsTrackStorage_MissingBackup)
{
  auto const filePath = GetGpsTrackFilePath();
  FileWriter::DeleteFileX(filePath);
  SCOPE_GUARD(cleanup, [&] { FileWriter::DeleteFileX(filePath); });

  GpsTrackStorage storage(filePath);
  TEST(!Platform::IsFileExistsByFullPath(filePath), ());
  TEST(storage.Load().empty(), ());
  TEST(storage.Append({}), ());
  storage.Clear();
  TEST(!Platform::IsFileExistsByFullPath(filePath), ());
}

UNIT_TEST(GpsTrackStorage_WriteRead)
{
  auto const filePath = GetGpsTrackFilePath();
  FileWriter::DeleteFileX(filePath);
  SCOPE_GUARD(cleanup, [&] { FileWriter::DeleteFileX(filePath); });

  std::vector<location::GpsInfo> points;
  for (size_t i = 0; i < 1003; ++i)
    points.push_back(Make(i));

  {
    GpsTrackStorage storage(filePath);
    TEST(storage.Append(points), ());
    CheckPoints(storage.Load(), points);
  }

  {
    GpsTrackStorage storage(filePath);
    CheckPoints(storage.Load(), points);

    std::vector<location::GpsInfo> const extra = {Make(points.size()), Make(points.size() + 1)};
    TEST(storage.Append(extra), ());
    points.insert(points.end(), extra.begin(), extra.end());
    CheckPoints(storage.Load(), points);

    storage.Clear();
    TEST(!Platform::IsFileExistsByFullPath(filePath), ());
    TEST(storage.Load().empty(), ());

    TEST(storage.Append({points.back()}), ());
    CheckPoints(storage.Load(), {points.back()});
  }
}

UNIT_TEST(GpsTrackStorage_EmptyBackup)
{
  auto const filePath = GetGpsTrackFilePath();
  SCOPE_GUARD(cleanup, [&] { FileWriter::DeleteFileX(filePath); });

  {
    FileWriter writer(filePath);
    WriteToSink(writer, uint32_t{1});
  }

  GpsTrackStorage storage(filePath);
  TEST(storage.Load().empty(), ());
  TEST(Platform::IsFileExistsByFullPath(filePath), ());
  TEST(storage.Append({Make(0)}), ());
  CheckPoints(storage.Load(), {Make(0)});
}

UNIT_TEST(GpsTrackStorage_UnsupportedVersion)
{
  base::ScopedLogAbortLevelChanger const expectedError(LCRITICAL);
  auto const filePath = GetGpsTrackFilePath();
  SCOPE_GUARD(cleanup, [&] { FileWriter::DeleteFileX(filePath); });

  {
    FileWriter writer(filePath);
    WriteToSink(writer, uint32_t{0});
  }

  GpsTrackStorage storage(filePath);
  TEST(storage.Load().empty(), ());
  TEST(!Platform::IsFileExistsByFullPath(filePath), ());
  TEST(storage.Append({Make(0)}), ());
  CheckPoints(storage.Load(), {Make(0)});
}

UNIT_TEST(GpsTrackStorage_IncompleteHeader)
{
  base::ScopedLogAbortLevelChanger const expectedError(LCRITICAL);
  auto const filePath = GetGpsTrackFilePath();
  SCOPE_GUARD(cleanup, [&] { FileWriter::DeleteFileX(filePath); });

  for (size_t size = 0; size < sizeof(uint32_t); ++size)
  {
    {
      FileWriter writer(filePath);
      std::array<char, sizeof(uint32_t)> const bytes = {};
      writer.Write(bytes.data(), size);
    }
    GpsTrackStorage storage(filePath);
    TEST(storage.Load().empty(), (size));
    TEST(!Platform::IsFileExistsByFullPath(filePath), (size));
    TEST(storage.Append({Make(0)}), (size));
    CheckPoints(storage.Load(), {Make(0)});
  }
}

UNIT_TEST(GpsTrackStorage_IncompletePoint)
{
  base::ScopedLogAbortLevelChanger const expectedError(LCRITICAL);
  auto const filePath = GetGpsTrackFilePath();
  FileWriter::DeleteFileX(filePath);
  SCOPE_GUARD(cleanup, [&] { FileWriter::DeleteFileX(filePath); });

  {
    GpsTrackStorage storage(filePath);
    TEST(storage.Append({Make(0)}), ());
  }
  {
    FileWriter writer(filePath, FileWriter::OP_APPEND);
    std::array<char, 17> const partialPoint = {};
    writer.Write(partialPoint.data(), partialPoint.size());
  }

  GpsTrackStorage storage(filePath);
  CheckPoints(storage.Load(), {Make(0)});
  TEST(storage.Append({Make(1)}), ());
  CheckPoints(storage.Load(), {Make(0), Make(1)});
  TEST_EQUAL(FileReader(filePath).Size(), sizeof(uint32_t) + 2 * (8 * sizeof(double) + sizeof(uint8_t)), ());
}

UNIT_TEST(GpsTrackStorage_FailedAppendRecovers)
{
  base::ScopedLogAbortLevelChanger const expectedError(LCRITICAL);
  auto const directoryPath = GetGpsTrackFilePath();
  auto const filePath = base::JoinPath(directoryPath, "backup");
  std::error_code error;
  std::filesystem::remove_all(directoryPath, error);
  TEST(!error, (error.message()));
  SCOPE_GUARD(cleanup, [&]
  {
    std::error_code error;
    std::filesystem::remove_all(directoryPath, error);
  });

  {
    FileWriter writer(directoryPath);
  }

  GpsTrackStorage storage(filePath);
  TEST(storage.Load().empty(), ());
  TEST(!storage.Append({Make(0), Make(1)}), ());
  TEST(!storage.Append({Make(2)}), ());

  TEST(std::filesystem::remove(directoryPath, error), (error.message()));
  TEST(std::filesystem::create_directory(directoryPath, error), (error.message()));
  TEST(storage.Append({Make(3)}), ());
  CheckPoints(storage.Load(), {Make(3)});
  TEST_EQUAL(FileReader(filePath).Size(), sizeof(uint32_t) + 8 * sizeof(double) + sizeof(uint8_t), ());
}

UNIT_TEST(GpsTrackStorage_FailedLoadAndClearRecover)
{
  base::ScopedLogAbortLevelChanger const expectedError(LCRITICAL);
  auto const filePath = GetGpsTrackFilePath();
  auto const childPath = base::JoinPath(filePath, "child");
  std::error_code error;
  std::filesystem::remove_all(filePath, error);
  TEST(!error, (error.message()));
  SCOPE_GUARD(cleanup, [&]
  {
    std::error_code error;
    std::filesystem::remove_all(filePath, error);
  });

  TEST(std::filesystem::create_directory(filePath, error), (error.message()));
  {
    FileWriter writer(childPath);
  }

  GpsTrackStorage storage(filePath);
  TEST(storage.Load().empty(), ());
  TEST(std::filesystem::exists(childPath, error), (error.message()));
  TEST(!storage.Append({Make(0)}), ());
  storage.Clear();
  TEST(std::filesystem::exists(childPath, error), (error.message()));

  TEST(std::filesystem::remove(childPath, error), (error.message()));
  TEST(std::filesystem::remove(filePath, error), (error.message()));
  TEST(storage.Append({Make(1)}), ());
  CheckPoints(storage.Load(), {Make(1)});
}
}  // namespace gps_track_storage_test
