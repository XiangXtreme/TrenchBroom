/*
  Copyright (C) 2026 XiangXtreme

  This file is part of TrenchBroom.
*/

#pragma once

// Shared SQLite plumbing for the project file and the asset library index.
//
// Both containers need the same error unwinding, statement binding and content
// addressing, and both are treated as untrusted input when opened. Keeping one
// implementation means the hardened connection parameters and the "verify bytes, do
// not trust the hash" content deduplication cannot drift apart.

#include <fcntl.h>
#include <fmt/format.h>
#include <sqlite3.h>

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <io.h>
#include <sys/stat.h>
#else
#include <unistd.h>
#endif

#include "base/Result.h"
#include "base/Uuid.h"

namespace tb::project::detail
{

// Internal exceptions unwind statements and transactions before returning a public
// Result.
class StoreError : public std::runtime_error
{
public:
  using std::runtime_error::runtime_error;
};

// FNV is an index hint only. Full byte comparison below prevents hash collisions from
// aliasing distinct payloads; it is not used as an integrity or security checksum.
inline std::string contentIndex(const std::vector<std::uint8_t>& data)
{
  auto hash = std::uint64_t{14695981039346656037ULL};
  for (const auto byte : data)
    hash = (hash ^ byte) * 1099511628211ULL;
  return fmt::format("{:016x}", hash);
}

// Reserve with OS-level exclusive creation, including empty existing files. The
// reservation outlives the SQLite connection so failed operations can clean up.
class NewFile
{
  std::filesystem::path m_path;
  bool m_keep = false;

public:
  explicit NewFile(const std::filesystem::path& path)
    : m_path{path}
  {
#ifdef _WIN32
    const auto fd = _wopen(
      path.c_str(), _O_CREAT | _O_EXCL | _O_WRONLY | _O_BINARY, _S_IREAD | _S_IWRITE);
#else
    const auto fd = ::open(path.c_str(), O_CREAT | O_EXCL | O_WRONLY, 0600);
#endif
    if (fd < 0)
      throw StoreError{fmt::format("Cannot create container: {}", std::strerror(errno))};
#ifdef _WIN32
    _close(fd);
#else
    ::close(fd);
#endif
  }
  ~NewFile()
  {
    if (!m_keep)
    {
      auto ec = std::error_code{};
      std::filesystem::remove(m_path, ec);
    }
  }
  NewFile(const NewFile&) = delete;
  NewFile& operator=(const NewFile&) = delete;
  void keep() { m_keep = true; }
};

template <typename F>
auto attempt(F&& f) -> Result<std::invoke_result_t<F>>
{
  try
  {
    if constexpr (std::is_void_v<std::invoke_result_t<F>>)
    {
      f();
      return {};
    }
    else
    {
      return f();
    }
  }
  catch (const StoreError& e)
  {
    return Error{e.what()};
  }
  catch (const std::filesystem::filesystem_error& e)
  {
    return Error{e.what()};
  }
}

inline void check(sqlite3* db, const int code)
{
  if (code != SQLITE_OK)
  {
    throw StoreError{fmt::format("Project storage: {}", sqlite3_errmsg(db))};
  }
}

inline void execute(sqlite3* db, const char* sql)
{
  check(db, sqlite3_exec(db, sql, nullptr, nullptr, nullptr));
}

inline std::string utf8Path(const std::filesystem::path& path)
{
  const auto s = path.u8string();
  return {reinterpret_cast<const char*>(s.data()), s.size()};
}

class Database
{
private:
  sqlite3* m_db = nullptr;

public:
  Database(const std::filesystem::path& path, const bool create)
  {
    const auto code = sqlite3_open_v2(
      utf8Path(path).c_str(),
      &m_db,
      SQLITE_OPEN_READWRITE | (create ? SQLITE_OPEN_CREATE : 0),
      nullptr);
    if (code != SQLITE_OK)
    {
      const auto message = std::string{sqlite3_errmsg(m_db)};
      sqlite3_close(m_db);
      throw StoreError{message};
    }
    sqlite3_extended_result_codes(m_db, 1);
    sqlite3_busy_timeout(m_db, 1000);
    sqlite3_db_config(m_db, SQLITE_DBCONFIG_DEFENSIVE, 1, nullptr);
    sqlite3_db_config(m_db, SQLITE_DBCONFIG_TRUSTED_SCHEMA, 0, nullptr);
  }

  ~Database() { sqlite3_close(m_db); }
  Database(const Database&) = delete;
  Database& operator=(const Database&) = delete;
  sqlite3* get() const { return m_db; }
};

class Statement
{
private:
  sqlite3* m_db;
  sqlite3_stmt* m_stmt = nullptr;

public:
  Statement(sqlite3* db, const char* sql)
    : m_db{db}
  {
    check(db, sqlite3_prepare_v2(db, sql, -1, &m_stmt, nullptr));
  }
  ~Statement() { sqlite3_finalize(m_stmt); }
  Statement(const Statement&) = delete;
  Statement& operator=(const Statement&) = delete;

  void bind(const int index, const std::string_view value)
  {
    check(
      m_db,
      sqlite3_bind_text64(
        m_stmt, index, value.data(), value.size(), SQLITE_TRANSIENT, SQLITE_UTF8));
  }
  void bind(const int index, const std::uint64_t value)
  {
    if (value > static_cast<std::uint64_t>(std::numeric_limits<sqlite3_int64>::max()))
    {
      throw StoreError{"Stored revision is out of range."};
    }
    check(m_db, sqlite3_bind_int64(m_stmt, index, static_cast<sqlite3_int64>(value)));
  }
  void bind(const int index, const std::vector<std::uint8_t>& value)
  {
    if (value.empty())
    {
      check(m_db, sqlite3_bind_zeroblob(m_stmt, index, 0));
    }
    else
    {
      check(
        m_db,
        sqlite3_bind_blob64(m_stmt, index, value.data(), value.size(), SQLITE_TRANSIENT));
    }
  }
  bool next()
  {
    const auto code = sqlite3_step(m_stmt);
    if (code == SQLITE_ROW)
      return true;
    if (code == SQLITE_DONE)
      return false;
    check(m_db, code);
    return false;
  }
  // A statement stepped to completion must be reset before it can be bound again.
  void reset()
  {
    check(m_db, sqlite3_reset(m_stmt));
    sqlite3_clear_bindings(m_stmt);
  }
  void run()
  {
    if (next())
      throw StoreError{"Unexpected storage query result."};
  }
  std::string text(const int column) const
  {
    const auto* data = sqlite3_column_text(m_stmt, column);
    const auto size = sqlite3_column_bytes(m_stmt, column);
    return data
             ? std::string{reinterpret_cast<const char*>(data), static_cast<size_t>(size)}
             : std::string{};
  }
  std::uint64_t integer(const int column) const
  {
    const auto value = sqlite3_column_int64(m_stmt, column);
    if (value < 0)
      throw StoreError{"Invalid negative stored revision."};
    return static_cast<std::uint64_t>(value);
  }
  int flag(const int column) const
  {
    return sqlite3_column_int(m_stmt, column) != 0 ? 1 : 0;
  }
  std::vector<std::uint8_t> bytes(const int column) const
  {
    const auto* data =
      static_cast<const std::uint8_t*>(sqlite3_column_blob(m_stmt, column));
    const auto size = sqlite3_column_bytes(m_stmt, column);
    return size ? std::vector<std::uint8_t>{data, data + size}
                : std::vector<std::uint8_t>{};
  }
};

class Transaction
{
private:
  sqlite3* m_db;
  bool m_committed = false;

public:
  explicit Transaction(sqlite3* db, const bool write = true)
    : m_db{db}
  {
    execute(db, write ? "BEGIN IMMEDIATE" : "BEGIN");
  }
  ~Transaction()
  {
    if (!m_committed)
      sqlite3_exec(m_db, "ROLLBACK", nullptr, nullptr, nullptr);
  }
  Transaction(const Transaction&) = delete;
  Transaction& operator=(const Transaction&) = delete;
  void commit()
  {
    execute(m_db, "COMMIT");
    m_committed = true;
  }
};

// Content addressed storage shared by both containers. Returns the content row ID.
inline std::string storeContent(sqlite3* db, const std::vector<std::uint8_t>& data)
{
  const auto hash = contentIndex(data);
  auto find = Statement{db, "SELECT id FROM content WHERE hash=? AND data=?"};
  find.bind(1, hash);
  find.bind(2, data);
  if (find.next())
    return find.text(0);
  const auto id = generateUuid();
  auto insert = Statement{db, "INSERT INTO content(id,hash,data) VALUES(?,?,?)"};
  insert.bind(1, id);
  insert.bind(2, hash);
  insert.bind(3, data);
  insert.run();
  return id;
}

} // namespace tb::project::detail
