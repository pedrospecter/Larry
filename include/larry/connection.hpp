#pragma once

#include <string>
#include <string_view>

namespace larry {

/// A connection string as libpq wants it ("host=... port=... user=...
/// password=... dbname=... sslmode=..."), from any of the forms the user
/// may have been given: libpq's own, a URI ("postgresql://user:pass@host/db"),
/// or the ".NET" form Azure shows ("Host=...;Port=...;Username=...;Password=...;
/// Database={0}"). Keys are matched without regard to case; "{0}" or an empty
/// database means `default_database`; a value with spaces or quotes is quoted
/// the way libpq reads it; keys libpq does not know are dropped. For a host
/// on Azure without an SSL mode, sslmode=require is added, because Azure
/// requires it. For a host that is not this machine without a timeout,
/// connect_timeout=10 is added, so a cloud out of reach does not hold Larry
/// for minutes. Empty in gives empty out.
[[nodiscard]] std::string libpq_connection(std::string_view given,
                                           std::string_view default_database = "larry");

}  // namespace larry
