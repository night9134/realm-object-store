#include "catch.hpp"

#include "util/test_file.hpp"

#include "object_schema.hpp"
#include "object_store.hpp"
#include "property.hpp"
#include "schema.hpp"

#include <realm/group.hpp>
#include <realm/table.hpp>

using namespace realm;

#define VERIFY_SCHEMA(r) do { \
    for (auto&& object_schema : (r).schema()) { \
        auto table = ObjectStore::table_for_object_type((r).read_group(), object_schema.name); \
        REQUIRE(table); \
        CAPTURE(object_schema.name) \
        for (auto&& prop : object_schema.persisted_properties) { \
            size_t col = table->get_column_index(prop.name); \
            CAPTURE(prop.name) \
            REQUIRE(col != npos); \
            REQUIRE(col == prop.table_column); \
            REQUIRE(table->get_column_type(col) == static_cast<int>(prop.type)); \
            REQUIRE(table->has_search_index(col) == prop.requires_index()); \
        } \
    } \
} while (0)

#define REQUIRE_UPDATE_SUCCEEDS(r, s, version) do { \
    REQUIRE_NOTHROW((r).update_schema(s, version)); \
    VERIFY_SCHEMA(r); \
    REQUIRE((r).schema() == s); \
} while (0)

#define REQUIRE_NO_MIGRATION_NEEDED(r, schema1, schema2) do { \
    REQUIRE_UPDATE_SUCCEEDS(r, schema1, 0); \
    REQUIRE_UPDATE_SUCCEEDS(r, schema2, 0); \
} while (0)

#define REQUIRE_MIGRATION_NEEDED(r, schema1, schema2) do { \
    REQUIRE_UPDATE_SUCCEEDS(r, schema1, 0); \
    REQUIRE_THROWS((r).update_schema(schema2)); \
    REQUIRE((r).schema() == schema1); \
    REQUIRE_UPDATE_SUCCEEDS(r, schema2, 1); \
} while (0)

TEST_CASE("[migration] Automatic") {
    InMemoryTestFile config;
    config.automatic_change_notifications = false;

    SECTION("no migration required") {
        SECTION("add object schema") {
            auto realm = Realm::get_shared_realm(config);

            Schema schema1 = {};
            Schema schema2 = {
                {"object", {
                    {"value", PropertyType::Int, "", "", false, false, false}
                }},
            };
            Schema schema3 = {
                {"object", {
                    {"value", PropertyType::Int, "", "", false, false, false}
                }},
                {"object2", {
                    {"value", PropertyType::Int, "", "", false, false, false}
                }},
            };
            REQUIRE_UPDATE_SUCCEEDS(*realm, schema1, 0);
            REQUIRE_UPDATE_SUCCEEDS(*realm, schema2, 0);
            REQUIRE_UPDATE_SUCCEEDS(*realm, schema3, 0);
        }

        SECTION("remove object schema") {
            auto realm = Realm::get_shared_realm(config);

            Schema schema1 = {};
            Schema schema2 = {
                {"object", {
                    {"value", PropertyType::Int, "", "", false, false, false}
                }},
            };
            Schema schema3 = {
                {"object", {
                    {"value", PropertyType::Int, "", "", false, false, false}
                }},
                {"object2", {
                    {"value", PropertyType::Int, "", "", false, false, false}
                }},
            };
            REQUIRE_UPDATE_SUCCEEDS(*realm, schema3, 0);
            REQUIRE_UPDATE_SUCCEEDS(*realm, schema2, 0);
            REQUIRE_UPDATE_SUCCEEDS(*realm, schema1, 0);
        }

        SECTION("add index") {
            auto realm = Realm::get_shared_realm(config);

            Schema schema1 = {
                {"object", {
                    {"value", PropertyType::Int, "", "", false, false, false}
                }},
            };
            Schema schema2 = {
                {"object", {
                    {"value", PropertyType::Int, "", "", false, true, false}
                }},
            };
            REQUIRE_NO_MIGRATION_NEEDED(*realm, schema1, schema2);
        }

        SECTION("remove index") {
            auto realm = Realm::get_shared_realm(config);

            Schema schema1 = {
                {"object", {
                    {"value", PropertyType::Int, "", "", false, true, false}
                }},
            };
            Schema schema2 = {
                {"object", {
                    {"value", PropertyType::Int, "", "", false, false, false}
                }},
            };
            REQUIRE_NO_MIGRATION_NEEDED(*realm, schema1, schema2);
        }

        SECTION("reordering properties") {
            auto realm = Realm::get_shared_realm(config);

            Schema schema1 = {
                {"object", {
                    {"col1", PropertyType::Int, "", "", false, false, false},
                    {"col2", PropertyType::Int, "", "", false, false, false},
                }},
            };
            Schema schema2 = {
                {"object", {
                    {"col2", PropertyType::Int, "", "", false, false, false},
                    {"col1", PropertyType::Int, "", "", false, false, false},
                }},
            };
            REQUIRE_NO_MIGRATION_NEEDED(*realm, schema1, schema2);
        }
    }

    SECTION("migration required") {
        SECTION("add property to existing object schema") {
            auto realm = Realm::get_shared_realm(config);

            Schema schema1 = {
                {"object", {
                    {"col1", PropertyType::Int, "", "", false, false, false},
                }},
            };
            Schema schema2 = {
                {"object", {
                    {"col1", PropertyType::Int, "", "", false, false, false},
                    {"col2", PropertyType::Int, "", "", false, false, false},
                }},
            };
            REQUIRE_MIGRATION_NEEDED(*realm, schema1, schema2);
        }

        SECTION("remove property from existing object schema") {
            auto realm = Realm::get_shared_realm(config);

            Schema schema1 = {
                {"object", {
                    {"col1", PropertyType::Int, "", "", false, false, false},
                    {"col2", PropertyType::Int, "", "", false, false, false},
                }},
            };
            Schema schema2 = {
                {"object", {
                    {"col1", PropertyType::Int, "", "", false, false, false},
                }},
            };
            REQUIRE_MIGRATION_NEEDED(*realm, schema1, schema2);
        }

        SECTION("change property type") {
            auto realm = Realm::get_shared_realm(config);

            Schema schema1 = {
                {"object", {
                    {"value", PropertyType::Int, "", "", false, false, false},
                }},
            };
            Schema schema2 = {
                {"object", {
                    {"value", PropertyType::Float, "", "", false, false, false},
                }},
            };
            REQUIRE_MIGRATION_NEEDED(*realm, schema1, schema2);
        }

        SECTION("make property nullable") {
            auto realm = Realm::get_shared_realm(config);

            Schema schema1 = {
                {"object", {
                    {"value", PropertyType::Int, "", "", false, false, false},
                }},
            };
            Schema schema2 = {
                {"object", {
                    {"value", PropertyType::Int, "", "", false, false, true},
                }},
            };
            REQUIRE_MIGRATION_NEEDED(*realm, schema1, schema2);
        }

        SECTION("make property required") {
            auto realm = Realm::get_shared_realm(config);

            Schema schema1 = {
                {"object", {
                    {"value", PropertyType::Int, "", "", false, false, true},
                }},
            };
            Schema schema2 = {
                {"object", {
                    {"value", PropertyType::Int, "", "", false, false, false},
                }},
            };
            REQUIRE_MIGRATION_NEEDED(*realm, schema1, schema2);
        }

        SECTION("change link target") {
            auto realm = Realm::get_shared_realm(config);

            Schema schema1 = {
                {"target 1", {
                    {"value", PropertyType::Int, "", "", false, false, false},
                }},
                {"target 2", {
                    {"value", PropertyType::Int, "", "", false, false, false},
                }},
                {"origin", {
                    {"value", PropertyType::Object, "target 1", "", false, false, true},
                }},
            };
            Schema schema2 = {
                {"target 1", {
                    {"value", PropertyType::Int, "", "", false, false, false},
                }},
                {"target 2", {
                    {"value", PropertyType::Int, "", "", false, false, false},
                }},
                {"origin", {
                    {"value", PropertyType::Object, "target 2", "", false, false, true},
                }},
            };
            REQUIRE_MIGRATION_NEEDED(*realm, schema1, schema2);
        }

        SECTION("add pk") {
            auto realm = Realm::get_shared_realm(config);

            Schema schema1 = {
                {"object", {
                    {"value", PropertyType::Int, "", "", false, false, false},
                }},
            };
            Schema schema2 = {
                {"object", {
                    {"value", PropertyType::Int, "", "", true, false, false},
                }},
            };
            REQUIRE_MIGRATION_NEEDED(*realm, schema1, schema2);
        }

        SECTION("remove pk") {
            auto realm = Realm::get_shared_realm(config);

            Schema schema1 = {
                {"object", {
                    {"value", PropertyType::Int, "", "", true, false, false},
                }},
            };
            Schema schema2 = {
                {"object", {
                    {"value", PropertyType::Int, "", "", false, false, false},
                }},
            };
            REQUIRE_MIGRATION_NEEDED(*realm, schema1, schema2);
        }
    }

    SECTION("allowed mismatches for read-only") {
        TestFile config;
        config.automatic_change_notifications = false;

        SECTION("index") {
            Schema schema1 = {
                {"object", {
                    {"indexed", PropertyType::Int, "", "", false, true, false},
                    {"unindexed", PropertyType::Int, "", "", false, false, false},
                }},
            };
            Schema schema2 = {
                {"object", {
                    {"indexed", PropertyType::Int, "", "", false, false, false},
                    {"unindexed", PropertyType::Int, "", "", false, true, false},
                }},
            };
            {
                auto realm = Realm::get_shared_realm(config);
                realm->update_schema(schema1);
            }
            config.schema_mode = SchemaMode::ReadOnly;
            auto realm = Realm::get_shared_realm(config);
            REQUIRE_NOTHROW(realm->update_schema(schema2));
            REQUIRE(realm->schema() == schema2);
        }

        SECTION("missing tables") {
            Schema schema1 = {
                {"object", {
                    {"value", PropertyType::Int, "", "", false, false, false},
                }},
            };
            Schema schema2 = {
                {"object", {
                    {"value", PropertyType::Int, "", "", false, false, false},
                }},
                {"second object", {
                    {"value", PropertyType::Int, "", "", false, false, false},
                }},
            };
            {
                auto realm = Realm::get_shared_realm(config);
                realm->update_schema(schema1);
            }
            config.schema_mode = SchemaMode::ReadOnly;
            auto realm = Realm::get_shared_realm(config);
            REQUIRE_NOTHROW(realm->update_schema(schema2));
            REQUIRE(realm->schema() == schema2);
        }
    }

    SECTION("disallowed mismatches for read-only") {
        TestFile config;
        config.automatic_change_notifications = false;

        SECTION("add column") {
            Schema schema1 = {
                {"object", {
                    {"value", PropertyType::Int, "", "", false, false, false},
                }},
            };
            Schema schema2 = {
                {"object", {
                    {"value", PropertyType::Int, "", "", false, false, false},
                    {"value 2", PropertyType::Int, "", "", false, false, false},
                }},
            };
            {
                auto realm = Realm::get_shared_realm(config);
                realm->update_schema(schema1);
            }
            config.schema_mode = SchemaMode::ReadOnly;
            auto realm = Realm::get_shared_realm(config);
            REQUIRE_THROWS(realm->update_schema(schema2));
        }

        SECTION("bump schema version") {
            Schema schema = {
                {"object", {
                    {"value", PropertyType::Int, "", "", false, false, false},
                }},
            };
            {
                auto realm = Realm::get_shared_realm(config);
                realm->update_schema(schema);
            }
            config.schema_mode = SchemaMode::ReadOnly;
            auto realm = Realm::get_shared_realm(config);
            REQUIRE_THROWS(realm->update_schema(schema, 1));
        }
    }

    SECTION("migration block invocations") {
        SECTION("not called for initial creation of schema") {
            Schema schema = {
                {"object", {
                    {"value", PropertyType::Int, "", "", false, false, false},
                }},
            };
            auto realm = Realm::get_shared_realm(config);
            realm->update_schema(schema, 5, [](SharedRealm, SharedRealm) { REQUIRE(false); });
        }

        SECTION("not called when schema version is unchanged even if there are schema changes") {
            Schema schema1 = {
                {"object", {
                    {"value", PropertyType::Int, "", "", false, false, false},
                }},
            };
            Schema schema2 = {
                {"object", {
                    {"value", PropertyType::Int, "", "", false, false, false},
                }},
                {"second object", {
                    {"value", PropertyType::Int, "", "", false, false, false},
                }},
            };
            auto realm = Realm::get_shared_realm(config);
            realm->update_schema(schema1, 1);
            realm->update_schema(schema2, 1, [](SharedRealm, SharedRealm) { REQUIRE(false); });
        }

        SECTION("called when schema version is bumped even if there are no schema changes") {
            Schema schema = {
                {"object", {
                    {"value", PropertyType::Int, "", "", false, false, false},
                }},
            };
            auto realm = Realm::get_shared_realm(config);
            realm->update_schema(schema);
            bool called = false;
            realm->update_schema(schema, 5, [&](SharedRealm, SharedRealm) { called = true; });
            REQUIRE(called);
        }
    }

    SECTION("migration errors") {
        SECTION("schema version cannot go down") {
            auto realm = Realm::get_shared_realm(config);
            realm->update_schema({}, 1);
            realm->update_schema({}, 2);
            REQUIRE_THROWS(realm->update_schema({}, 0));
        }

        SECTION("insert duplicate keys for existing PK during migration") {
            Schema schema = {
                {"object", {
                    {"value", PropertyType::Int, "", "", true, false, false},
                }},
            };
            auto realm = Realm::get_shared_realm(config);
            realm->update_schema(schema, 1);
            REQUIRE_THROWS(realm->update_schema(schema, 2, [](SharedRealm, SharedRealm realm) {
                auto table = ObjectStore::table_for_object_type(realm->read_group(), "object");
                table->add_empty_row(2);
            }));
        }

        SECTION("add pk to existing table with duplicate keys") {
            Schema schema = {
                {"object", {
                    {"value", PropertyType::Int, "", "", false, false, false},
                }},
            };
            auto realm = Realm::get_shared_realm(config);
            realm->update_schema(schema, 1);

            auto table = ObjectStore::table_for_object_type(realm->read_group(), "object");
            table->add_empty_row(2);

            schema = {
                {"object", {
                    {"value", PropertyType::Int, "", "", true, false, false},
                }},
            };
            REQUIRE_THROWS(realm->update_schema(schema, 2, nullptr));
        }

        SECTION("throwing an exception from migration function rolls back all changes") {
            Schema schema1 = {
                {"object", {
                    {"value", PropertyType::Int, "", "", false, false, false},
                }},
            };
            Schema schema2 = {
                {"object", {
                    {"value", PropertyType::Int, "", "", false, false, false},
                    {"value2", PropertyType::Int, "", "", false, false, false},
                }},
            };
            auto realm = Realm::get_shared_realm(config);
            realm->update_schema(schema1, 1);

            REQUIRE_THROWS(realm->update_schema(schema2, 2, [](SharedRealm, SharedRealm realm) {
                auto table = ObjectStore::table_for_object_type(realm->read_group(), "object");
                table->add_empty_row();
                throw 5;
            }));

            auto table = ObjectStore::table_for_object_type(realm->read_group(), "object");
            REQUIRE(table->size() == 0);
            REQUIRE(realm->schema_version() == 1);
            REQUIRE(realm->schema() == schema1);
        }
    }

    SECTION("valid migrations") {
        SECTION("changing all columns does not lose row count") {
            Schema schema = {
                {"object", {
                    {"value", PropertyType::Int, "", "", false, false, false},
                }},
            };
            auto realm = Realm::get_shared_realm(config);
            realm->update_schema(schema, 1);

            realm->begin_transaction();
            auto table = ObjectStore::table_for_object_type(realm->read_group(), "object");
            table->add_empty_row(10);
            realm->commit_transaction();

            schema = {
                {"object", {
                    {"value", PropertyType::Float, "", "", false, false, false},
                }},
            };
            realm->update_schema(schema, 2);
            REQUIRE(table->size() == 10);
        }

        SECTION("values for required properties are copied when converitng to nullable") {
            Schema schema = {
                {"object", {
                    {"value", PropertyType::Int, "", "", false, false, false},
                }},
            };
            auto realm = Realm::get_shared_realm(config);
            realm->update_schema(schema, 1);

            realm->begin_transaction();
            auto table = ObjectStore::table_for_object_type(realm->read_group(), "object");
            table->add_empty_row(10);
            for (size_t i = 0; i < 10; ++i)
                table->set_int(0, i, i);
            realm->commit_transaction();

            schema.find("object")->persisted_properties[0].is_nullable = true;
            realm->update_schema(schema, 2);
            for (size_t i = 0; i < 10; ++i)
                REQUIRE(table->get_int(0, i) == i);
        }

        SECTION("values for nullable properties are discarded when converitng to required") {
            Schema schema = {
                {"object", {
                    {"value", PropertyType::Int, "", "", false, false, true},
                }},
            };
            auto realm = Realm::get_shared_realm(config);
            realm->update_schema(schema, 1);

            realm->begin_transaction();
            auto table = ObjectStore::table_for_object_type(realm->read_group(), "object");
            table->add_empty_row(10);
            for (size_t i = 0; i < 10; ++i)
                table->set_int(0, i, i);
            realm->commit_transaction();

            schema.find("object")->persisted_properties[0].is_nullable = false;
            realm->update_schema(schema, 2);
            for (size_t i = 0; i < 10; ++i)
                REQUIRE(table->get_int(0, i) == 0);
        }
    }

    SECTION("property renaming") {

    }
}

TEST_CASE("[migration] ResetFile") {
    TestFile config;
    config.schema_mode = SchemaMode::ResetFile;

    Schema initial_schema = {
        {"object", {
            {"value", PropertyType::Int, "", "", false, false, false},
        }},
    };

    {
        auto realm = Realm::get_shared_realm(config);
        realm->update_schema(initial_schema);
        realm->begin_transaction();
        ObjectStore::table_for_object_type(realm->read_group(), "object")->add_empty_row();
        realm->commit_transaction();
    }
    auto realm = Realm::get_shared_realm(config);

    SECTION("file is reset when schema version increases") {
        realm->update_schema(initial_schema, 1);
        REQUIRE(ObjectStore::table_for_object_type(realm->read_group(), "object")->size() == 0);
    }

    SECTION("file is reset when an existing table is modified") {
        Schema schema = {
            {"object", {
                {"value", PropertyType::Int, "", "", false, false, false},
                {"value 2", PropertyType::Int, "", "", false, false, false},
            }},
        };
        realm->update_schema(schema);
        REQUIRE(ObjectStore::table_for_object_type(realm->read_group(), "object")->size() == 0);
    }

    SECTION("file is not reset when adding a new table") {
        Schema schema = {
            {"object", {
                {"value", PropertyType::Int, "", "", false, false, false},
            }},
            {"object 2", {
                {"value", PropertyType::Int, "", "", false, false, false},
            }},
        };
        realm->update_schema(schema);
        REQUIRE(ObjectStore::table_for_object_type(realm->read_group(), "object")->size() == 1);
    }

    SECTION("file is not reset when adding an index") {
        initial_schema.find("object")->property_for_name("value")->is_indexed = true;
        realm->update_schema(initial_schema);
        REQUIRE(ObjectStore::table_for_object_type(realm->read_group(), "object")->size() == 1);
    }

    SECTION("file is not reset when removing an index") {
        initial_schema.find("object")->property_for_name("value")->is_indexed = true;
        realm->update_schema(initial_schema);
        initial_schema.find("object")->property_for_name("value")->is_indexed = false;
        realm->update_schema(initial_schema);
        REQUIRE(ObjectStore::table_for_object_type(realm->read_group(), "object")->size() == 1);
    }
}

TEST_CASE("[migration] Additive") {
    TestFile config;
    config.schema_mode = SchemaMode::Additive;
    auto realm = Realm::get_shared_realm(config);

    Schema initial_schema = {
        {"object", {
            {"value", PropertyType::Int, "", "", false, true, false},
            {"value 2", PropertyType::Int, "", "", false, false, true},
        }},
    };
    realm->update_schema(initial_schema);

    SECTION("can add new properties to existing tables") {
        REQUIRE_NOTHROW(realm->update_schema({
            {"object", {
                {"value", PropertyType::Int, "", "", false, true, false},
                {"value 2", PropertyType::Int, "", "", false, false, true},
                {"value 3", PropertyType::Int, "", "", false, false, false},
            }},
        }));
        REQUIRE(ObjectStore::table_for_object_type(realm->read_group(), "object")->get_column_count() == 3);
    }

    SECTION("can add new tables") {
        REQUIRE_NOTHROW(realm->update_schema({
            {"object", {
                {"value", PropertyType::Int, "", "", false, true, false},
                {"value 2", PropertyType::Int, "", "", false, false, true},
            }},
            {"object 2", {
                {"value", PropertyType::Int, "", "", false, false, false},
            }},
        }));
        REQUIRE(ObjectStore::table_for_object_type(realm->read_group(), "object"));
        REQUIRE(ObjectStore::table_for_object_type(realm->read_group(), "object 2"));
    }

    SECTION("indexes are updated when schema version is bumped") {
        auto table = ObjectStore::table_for_object_type(realm->read_group(), "object");
        REQUIRE(table->has_search_index(0));
        REQUIRE(!table->has_search_index(1));

        REQUIRE_NOTHROW(realm->update_schema({
            {"object", {
                {"value", PropertyType::Int, "", "", false, false, false},
                {"value 2", PropertyType::Int, "", "", false, true, true},
            }},
        }, 1));

        REQUIRE(!table->has_search_index(0));
        REQUIRE(table->has_search_index(1));
    }

    SECTION("indexes are not updated when schema version is not bumped") {
        auto table = ObjectStore::table_for_object_type(realm->read_group(), "object");
        REQUIRE(table->has_search_index(0));
        REQUIRE(!table->has_search_index(1));

        REQUIRE_NOTHROW(realm->update_schema({
            {"object", {
                {"value", PropertyType::Int, "", "", false, false, false},
                {"value 2", PropertyType::Int, "", "", false, true, true},
            }},
        }));

        REQUIRE(table->has_search_index(0));
        REQUIRE(!table->has_search_index(1));

    }

    SECTION("cannot remove properties from existing tables") {
        REQUIRE_THROWS(realm->update_schema({
            {"object", {
                {"value", PropertyType::Int, "", "", false, true, false},
            }},
        }));
    }

    SECTION("cannot change existing property types") {
        REQUIRE_THROWS(realm->update_schema({
            {"object", {
                {"value", PropertyType::Int, "", "", false, true, false},
                {"value 2", PropertyType::Float, "", "", false, false, true},
            }},
        }));
    }

    SECTION("cannot change existing property nullability") {
        REQUIRE_THROWS(realm->update_schema({
            {"object", {
                {"value", PropertyType::Int, "", "", false, true, false},
                {"value 2", PropertyType::Int, "", "", false, false, false},
            }},
        }));
        REQUIRE_THROWS(realm->update_schema({
            {"object", {
                {"value", PropertyType::Int, "", "", false, true, true},
                {"value 2", PropertyType::Int, "", "", false, false, true},
            }},
        }));
    }

    SECTION("cannot change existing link targets") {
        REQUIRE_NOTHROW(realm->update_schema({
            {"object", {
                {"value", PropertyType::Int, "", "", false, true, false},
                {"value 2", PropertyType::Int, "", "", false, false, true},
            }},
            {"object 2", {
                {"link", PropertyType::Object, "object", "", false, false, true},
            }},
        }));
        REQUIRE_THROWS(realm->update_schema({
            {"object", {
                {"value", PropertyType::Int, "", "", false, true, false},
                {"value 2", PropertyType::Int, "", "", false, false, true},
            }},
            {"object 2", {
                {"link", PropertyType::Object, "object 2", "", false, false, true},
            }},
        }));
    }

    SECTION("cannot change primary keys") {
        REQUIRE_THROWS(realm->update_schema({
            {"object", {
                {"value", PropertyType::Int, "", "", true, true, false},
                {"value 2", PropertyType::Int, "", "", false, false, true},
            }},
        }));

        REQUIRE_NOTHROW(realm->update_schema({
            {"object", {
                {"value", PropertyType::Int, "", "", false, true, false},
                {"value 2", PropertyType::Int, "", "", false, false, true},
            }},
            {"object 2", {
                {"pk", PropertyType::Int, "", "", true, false, false},
            }},
        }));

        REQUIRE_THROWS(realm->update_schema({
            {"object", {
                {"value", PropertyType::Int, "", "", false, true, false},
                {"value 2", PropertyType::Int, "", "", false, false, true},
            }},
            {"object 2", {
                {"pk", PropertyType::Int, "", "", false, false, false},
            }},
        }));
    }

    SECTION("schema version is allowed to go down") {
        REQUIRE_NOTHROW(realm->update_schema(initial_schema, 1));
        REQUIRE(realm->schema_version() == 1);
        REQUIRE_NOTHROW(realm->update_schema(initial_schema, 0));
        REQUIRE(realm->schema_version() == 1);
    }

    SECTION("migration function is not used") {
        REQUIRE_NOTHROW(realm->update_schema(initial_schema, 1,
                                             [&](SharedRealm, SharedRealm) { REQUIRE(false); }));
    }
}
