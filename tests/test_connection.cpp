// Tests for libpq_connection: any form of connection string into libpq's.

#include "larry/connection.hpp"

#include "check.hpp"

using larry::libpq_connection;

TEST(libpq_form_stays) {
    CHECK(libpq_connection("dbname=larry") == "dbname=larry");
    CHECK(libpq_connection("host=192.168.10.133 port=5432 dbname=larry user=postgres") ==
          "host=192.168.10.133 port=5432 dbname=larry user=postgres connect_timeout=10");
    CHECK(libpq_connection("  dbname=larry  ") == "dbname=larry");
    CHECK(libpq_connection("") == "");
    CHECK(libpq_connection("   ") == "");
}

TEST(the_dot_net_form_azure_shows) {
    CHECK(libpq_connection("Host=datacrio.postgres.database.azure.com;Port=5432;Username=cw1inc;"
                           "Password=secret;Database={0}") ==
          "host=datacrio.postgres.database.azure.com port=5432 user=cw1inc password=secret "
          "dbname=larry sslmode=require connect_timeout=10");
    CHECK(libpq_connection("Host=x.postgres.database.azure.com;Database=;User Id=u;Pwd=p;SSL Mode=Require;"
                           "Trust Server Certificate=true") ==
          "host=x.postgres.database.azure.com dbname=larry user=u password=p sslmode=require connect_timeout=10");
    CHECK(libpq_connection("Server=h;Database=d;Ssl Mode=VerifyFull;Timeout=5") ==
          "host=h dbname=d sslmode=verify-full connect_timeout=5");
    CHECK(libpq_connection("Host=h;Database={0}", "other") == "host=h dbname=other connect_timeout=10");
}

TEST(keys_in_any_case_and_values_with_spaces) {
    CHECK(libpq_connection("Host=h Port=1 DBNAME=d") == "host=h port=1 dbname=d connect_timeout=10");
    CHECK(libpq_connection("host=h password='a b' dbname=d") == "host=h password='a b' dbname=d connect_timeout=10");
    CHECK(libpq_connection("Host=h;Password=it's;Database=d") == "host=h password='it\\'s' dbname=d connect_timeout=10");
    CHECK(libpq_connection("host=h password=#abc dbname=d") == "host=h password=#abc dbname=d connect_timeout=10");
    CHECK(libpq_connection("host=h options='-c search_path=x'") == "host=h options='-c search_path=x' connect_timeout=10");
    CHECK(libpq_connection("Pooling=true;Host=h;Database=d") == "host=h dbname=d connect_timeout=10");
}

TEST(azure_needs_ssl) {
    CHECK(libpq_connection("host=a.postgres.database.azure.com dbname=larry") ==
          "host=a.postgres.database.azure.com dbname=larry sslmode=require connect_timeout=10");
    CHECK(libpq_connection("host=a.postgres.database.azure.com dbname=larry sslmode=disable") ==
          "host=a.postgres.database.azure.com dbname=larry sslmode=disable connect_timeout=10");
    CHECK(libpq_connection("host=pi.local dbname=larry") == "host=pi.local dbname=larry connect_timeout=10");
}

TEST(uris_stay_uris) {
    CHECK(libpq_connection("postgresql://u:p@h:5432/d") == "postgresql://u:p@h:5432/d?connect_timeout=10");
    CHECK(libpq_connection("postgresql://u:p@a.postgres.database.azure.com/d") ==
          "postgresql://u:p@a.postgres.database.azure.com/d?sslmode=require&connect_timeout=10");
    CHECK(libpq_connection("postgresql://u:p@a.postgres.database.azure.com/d?application_name=larry") ==
          "postgresql://u:p@a.postgres.database.azure.com/d?application_name=larry&sslmode=require&connect_timeout=10");
    CHECK(libpq_connection("postgres://u:p@a.postgres.database.azure.com/d?sslmode=require") ==
          "postgres://u:p@a.postgres.database.azure.com/d?sslmode=require&connect_timeout=10");
}

TEST(a_host_out_of_reach_gets_a_timeout_and_this_machine_does_not) {
    CHECK(libpq_connection("host=localhost dbname=larry") == "host=localhost dbname=larry");
    CHECK(libpq_connection("host=127.0.0.1 dbname=larry") == "host=127.0.0.1 dbname=larry");
    CHECK(libpq_connection("host=/var/run/postgresql dbname=larry") == "host=/var/run/postgresql dbname=larry");
    CHECK(libpq_connection("host=h dbname=d connect_timeout=3") == "host=h dbname=d connect_timeout=3");
    CHECK(libpq_connection("Server=h;Database=d;Timeout=3") == "host=h dbname=d connect_timeout=3");
    CHECK(libpq_connection("postgresql://u:p@localhost/d") == "postgresql://u:p@localhost/d");
    CHECK(libpq_connection("postgresql://u:p@h/d?connect_timeout=3") == "postgresql://u:p@h/d?connect_timeout=3");
}

int main() {
    return larry::test::run();
}
