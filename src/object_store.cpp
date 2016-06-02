////////////////////////////////////////////////////////////////////////////
//
// Copyright 2015 Realm Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
////////////////////////////////////////////////////////////////////////////

#include "object_store.hpp"

#include "object_schema.hpp"
#include "schema.hpp"
#include "util/format.hpp"

#include <realm/group.hpp>
#include <realm/table.hpp>
#include <realm/table_view.hpp>
#include <realm/util/assert.hpp>

#include <string.h>

using namespace realm;

const uint64_t ObjectStore::NotVersioned = std::numeric_limits<uint64_t>::max();

namespace {
const char * const c_metadataTableName = "metadata";
const char * const c_versionColumnName = "version";
const size_t c_versionColumnIndex = 0;

const char * const c_primaryKeyTableName = "pk";
const char * const c_primaryKeyObjectClassColumnName = "pk_table";
const size_t c_primaryKeyObjectClassColumnIndex =  0;
const char * const c_primaryKeyPropertyNameColumnName = "pk_property";
const size_t c_primaryKeyPropertyNameColumnIndex =  1;

const size_t c_zeroRowIndex = 0;

const char c_object_table_prefix[] = "class_";

void create_metadata_tables(Group& group) {
    TableRef table = group.get_or_add_table(c_primaryKeyTableName);
    if (table->get_column_count() == 0) {
        table->add_column(type_String, c_primaryKeyObjectClassColumnName);
        table->add_column(type_String, c_primaryKeyPropertyNameColumnName);
    }

    table = group.get_or_add_table(c_metadataTableName);
    if (table->get_column_count() == 0) {
        table->add_column(type_Int, c_versionColumnName);

        // set initial version
        table->add_empty_row();
        table->set_int(c_versionColumnIndex, c_zeroRowIndex, ObjectStore::NotVersioned);
    }
}

void set_schema_version(Group& group, uint64_t version) {
    TableRef table = group.get_or_add_table(c_metadataTableName);
    table->set_int(c_versionColumnIndex, c_zeroRowIndex, version);
}

void set_primary_key_for_object(Group& group, StringData object_type, StringData primary_key) {
    TableRef table = group.get_table(c_primaryKeyTableName);

    // get row or create if new object and populate
    size_t row = table->find_first_string(c_primaryKeyObjectClassColumnIndex, object_type);
    if (row == not_found && primary_key.size()) {
        row = table->add_empty_row();
        table->set_string(c_primaryKeyObjectClassColumnIndex, row, object_type);
    }

    // set if changing, or remove if setting to nil
    if (primary_key.size() == 0) {
        if (row != not_found) {
            table->remove(row);
        }
    }
    else {
        table->set_string(c_primaryKeyPropertyNameColumnIndex, row, primary_key);
    }
}

template<typename Group>
auto table_for_object_schema(Group& group, ObjectSchema const& object_schema)
{
    return ObjectStore::table_for_object_type(group, object_schema.name);
}

void insert_column(Group& group, Table& table, Property const& property, size_t col_ndx)
{
    if (property.type == PropertyType::Object || property.type == PropertyType::Array) {
        auto target_name = ObjectStore::table_name_for_object_type(property.object_type);
        TableRef link_table = group.get_or_add_table(target_name);
        table.insert_column_link(col_ndx, DataType(property.type), property.name, *link_table);
    }
    else {
        table.insert_column(col_ndx, DataType(property.type), property.name, property.is_nullable);
        if (property.requires_index())
            table.add_search_index(col_ndx);
    }
}

void add_column(Group& group, Table& table, Property const& property)
{
    insert_column(group, table, property, table.get_column_count());
}

void replace_column(Group& group, Table& table, Property const& old_property, Property const& new_property)
{
    insert_column(group, table, new_property, old_property.table_column);
    table.remove_column(old_property.table_column + 1);
}

TableRef create_table(Group& group, ObjectSchema const& object_schema)
{
    auto name = ObjectStore::table_name_for_object_type(object_schema.name);
    auto table = group.get_or_add_table(name);
    // Table may already exist if it was created as a link target, but if so it
    // shouldn't have any columns
    REALM_ASSERT(table->get_column_count() == 0);

    for (auto const& prop : object_schema.persisted_properties) {
        add_column(group, *table, prop);
    }

    set_primary_key_for_object(group, object_schema.name, object_schema.primary_key);

    return table;
}

void copy_property_values(Property const& prop, Table& table)
{
    auto copy_property_values = [&](auto getter, auto setter) {
        for (size_t i = 0, count = table.size(); i < count; i++) {
            (table.*setter)(prop.table_column, i, (table.*getter)(prop.table_column + 1, i));
        }
    };

    switch (prop.type) {
        case PropertyType::Int:
            copy_property_values(&Table::get_int, &Table::set_int);
            break;
        case PropertyType::Bool:
            copy_property_values(&Table::get_bool, &Table::set_bool);
            break;
        case PropertyType::Float:
            copy_property_values(&Table::get_float, &Table::set_float);
            break;
        case PropertyType::Double:
            copy_property_values(&Table::get_double, &Table::set_double);
            break;
        case PropertyType::String:
            copy_property_values(&Table::get_string, &Table::set_string);
            break;
        case PropertyType::Data:
            copy_property_values(&Table::get_binary, &Table::set_binary);
            break;
        case PropertyType::Date:
            copy_property_values(&Table::get_timestamp, &Table::set_timestamp);
            break;
        default:
            break;
    }
}

void make_property_optional(Group& group, Table& table, Property const& property)
{
    insert_column(group, table, property, property.table_column);
    copy_property_values(property, table);
    table.remove_column(property.table_column + 1);
}
} // anonymous namespace

uint64_t ObjectStore::get_schema_version(Group const& group) {
    ConstTableRef table = group.get_table(c_metadataTableName);
    if (!table || table->get_column_count() == 0) {
        return ObjectStore::NotVersioned;
    }
    return table->get_int(c_versionColumnIndex, c_zeroRowIndex);
}

StringData ObjectStore::get_primary_key_for_object(Group const& group, StringData object_type) {
    ConstTableRef table = group.get_table(c_primaryKeyTableName);
    if (!table) {
        return "";
    }
    size_t row = table->find_first_string(c_primaryKeyObjectClassColumnIndex, object_type);
    if (row == not_found) {
        return "";
    }
    return table->get_string(c_primaryKeyPropertyNameColumnIndex, row);
}

StringData ObjectStore::object_type_for_table_name(StringData table_name) {
    if (table_name.begins_with(c_object_table_prefix)) {
        return table_name.substr(sizeof(c_object_table_prefix) - 1);
    }
    return StringData();
}

std::string ObjectStore::table_name_for_object_type(StringData object_type) {
    return std::string(c_object_table_prefix) + object_type.data();
}

TableRef ObjectStore::table_for_object_type(Group& group, StringData object_type) {
    auto name = table_name_for_object_type(object_type);
    return group.get_table(name);
}

ConstTableRef ObjectStore::table_for_object_type(Group const& group, StringData object_type) {
    auto name = table_name_for_object_type(object_type);
    return group.get_table(name);
}

namespace {
struct MigrationChecker {
    std::vector<ObjectSchemaValidationException> errors;

    void operator()(schema_change::AddTable op) { }

    void operator()(schema_change::AddProperty op)
    {
        errors.emplace_back("Property '%1.%2' has been added.", op.object->name, op.property->name);
    }

    void operator()(schema_change::RemoveProperty op)
    {
        errors.emplace_back("Property '%1.%2' has been removed.", op.object->name, op.property->name);
    }

    void operator()(schema_change::ChangePropertyType op)
    {
        errors.emplace_back("Property '%1.%2' has been changed from '%3' to '%4'.",
                            op.object->name, op.new_property->name,
                            string_for_property_type(op.old_property->type),
                            string_for_property_type(op.new_property->type));
    }

    void operator()(schema_change::MakePropertyNullable op)
    {
        errors.emplace_back("Property '%1.%2' has been made optional.", op.object->name, op.property->name);
    }

    void operator()(schema_change::MakePropertyRequired op)
    {
        errors.emplace_back("Property '%1.%2' has been made required.", op.object->name, op.property->name);
    }

    void operator()(schema_change::ChangePrimaryKey op)
    {
        if (op.property && !op.object->primary_key.empty()) {
            errors.emplace_back("Primary Key for class '%1 has changed from '%2' to '%3'.",
                                op.object->name, op.object->primary_key, op.property->name);
        }
        else if (op.property) {
            errors.emplace_back("Primary Key for class '%1 has been added.", op.object->name);
        }
        else {
            errors.emplace_back("Primary Key for class '%1 has been removed.", op.object->name);
        }
    }
};

struct IndexUpdater {
    Group& group;

    void operator()(schema_change::AddIndex op) const
    {
        try {
            table_for_object_schema(group, *op.object)->add_search_index(op.property->table_column);
        }
        catch (LogicError const&) {
            throw std::logic_error(util::format("Cannot index property '%1.%2': indexing properties of type '%3' is not yet implemented.",
                                                op.object->name, op.property->name, string_for_property_type(op.property->type)));
        }
    }

    void operator()(schema_change::RemoveIndex op) const
    {
        table_for_object_schema(group, *op.object)->remove_search_index(op.property->table_column);
    }
};

} // anonymous namespace

bool ObjectStore::needs_migration(std::vector<SchemaChange> const& changes)
{
    using namespace schema_change;
    struct Visitor {
        bool operator()(AddIndex) { return false; }
        bool operator()(AddProperty) { return true; }
        bool operator()(AddTable) { return false; }
        bool operator()(ChangePrimaryKey) { return true; }
        bool operator()(ChangePropertyType) { return true; }
        bool operator()(MakePropertyNullable) { return true; }
        bool operator()(MakePropertyRequired) { return true; }
        bool operator()(RemoveIndex) { return false; }
        bool operator()(RemoveProperty) { return true; }
    };

    return std::any_of(begin(changes), end(changes),
                       [](auto&& change) { return change.visit(Visitor()); });
}

void ObjectStore::verify_no_migration_required(std::vector<SchemaChange> const& changes)
{
    using namespace schema_change;
    struct Applier : MigrationChecker {
        const ObjectSchema* current_object_schema = nullptr;

        using MigrationChecker::operator();

        void operator()(AddTable op)
        {
            current_object_schema = op.object;
        }

        void operator()(AddProperty op)
        {
            if (op.object != current_object_schema) {
                MigrationChecker::operator()(op);
            }
        }

        void operator()(AddIndex) { }
        void operator()(RemoveIndex) { }
    } applier;

    for (auto& change : changes) {
        change.visit(applier);
    }

    if (!applier.errors.empty()) {
        throw SchemaMismatchException(applier.errors);
    }
}

static void apply_non_migration_changes(Group& group, std::vector<SchemaChange> const& changes)
{
    using namespace schema_change;
    struct Applier : MigrationChecker, IndexUpdater {
        Applier(Group& group) : IndexUpdater{group} { }
        using IndexUpdater::operator();
        using MigrationChecker::operator();

        TableRef table;
        const ObjectSchema* current_object_schema = nullptr;

        void operator()(AddTable op)
        {
            current_object_schema = op.object;
            table = create_table(group, *op.object);
        }

        void operator()(AddProperty op)
        {
            if (op.object == current_object_schema) {
                add_column(group, *table, *op.property);
            }
            else {
                MigrationChecker::operator()(op);
            }
        }
    } applier{group};

    for (auto& change : changes) {
        change.visit(applier);
    }

    if (!applier.errors.empty()) {
        throw SchemaMismatchException(applier.errors);
    }
}

static void create_initial_tables(Group& group, std::vector<SchemaChange> const& changes)
{
    using namespace schema_change;
    struct Applier : IndexUpdater {
        Applier(Group& group) : IndexUpdater{group} { }
        using IndexUpdater::operator();

        TableRef table;
        const ObjectSchema* current_object_schema = nullptr;

        void select_table(const ObjectSchema* object_schema)
        {
            if (object_schema != current_object_schema) {
                table = group.get_table(object_schema->name);
                current_object_schema = object_schema;
            }
        }

        void operator()(AddTable op)
        {
            current_object_schema = op.object;
            table = create_table(group, *op.object);
        }

        void operator()(AddProperty op)
        {
            select_table(op.object);
            add_column(group, *table, *op.property);
        }

        void operator()(RemoveProperty op)
        {
            select_table(op.object);
            // no delay here; only in migration version
            table->remove_column(op.property->table_column);
        }

        void operator()(ChangePropertyType op)
        {
            select_table(op.object);
            table->insert_column(op.old_property->table_column, type_Int, op.new_property->name);
            table->remove_column(op.old_property->table_column + 1);
        }

        void operator()(MakePropertyNullable op)
        {
            select_table(op.object);
            make_property_optional(group, *table, *op.property);
        }

        void operator()(MakePropertyRequired op)
        {
            select_table(op.object);
            table->insert_column(op.property->table_column, type_Int, op.property->name);
            table->remove_column(op.property->table_column + 1);
        }

        void operator()(ChangePrimaryKey op)
        {
            set_primary_key_for_object(group, op.object->name, op.property->name);
        }
    } applier{group};

    for (auto& change : changes) {
        change.visit(applier);
    }
}

static void apply_pre_migration_changes(Group& group, std::vector<SchemaChange> const& changes)
{
    using namespace schema_change;
    struct Applier : IndexUpdater {
        Applier(Group& group) : IndexUpdater{group} { }
        using IndexUpdater::operator();

        TableRef table;
        const ObjectSchema* current_object_schema = nullptr;

        Table& get_table(ObjectSchema const& object_schema)
        {
            if (&object_schema != current_object_schema) {
                table = table_for_object_schema(group, object_schema);
                current_object_schema = &object_schema;
            }
            return *table;
        }

        void operator()(AddTable op)
        {
            current_object_schema = op.object;
            table = create_table(group, *op.object);
        }

        void operator()(AddProperty op)
        {
            add_column(group, get_table(*op.object), *op.property);
        }

        void operator()(RemoveProperty)
        {
            // Is delayed until after the migration
        }

        void operator()(ChangePropertyType op)
        {
            auto& table = get_table(*op.object);
            replace_column(group, table, *op.old_property, *op.new_property);
        }

        void operator()(MakePropertyNullable op)
        {
            auto prop = *op.property;
            prop.is_nullable = true;
            make_property_optional(group, get_table(*op.object), prop);
        }

        void operator()(MakePropertyRequired op)
        {
            auto& table = get_table(*op.object);
            auto prop = *op.property;
            prop.is_nullable = false;
            insert_column(group, table, prop, prop.table_column);
            table.remove_column(prop.table_column + 1);
        }

        void operator()(ChangePrimaryKey op)
        {
            set_primary_key_for_object(group, op.object->name, op.property ? op.property->name : "");
        }
    } applier{group};

    for (auto& change : changes) {
        change.visit(applier);
    }
}

static void apply_post_migration_changes(Group& group, std::vector<SchemaChange> const& changes, Schema const& initial_schema)
{
    using namespace schema_change;
    struct Applier : IndexUpdater {
        Applier(Group& group, Schema const& initial_schema) : IndexUpdater{group}, initial_schema(initial_schema) { }
        using IndexUpdater::operator();

        Schema const& initial_schema;

        void operator()(RemoveProperty op)
        {
            if (!initial_schema.empty() && !initial_schema.find(op.object->name)->property_for_name(op.property->name))
                throw std::logic_error(util::format("Renamed property `%1.%2` does not exist.", op.object->name, op.property->name));
            auto table = table_for_object_schema(group, *op.object);
            table->remove_column(op.property->table_column);
        }

        void operator()(ChangePrimaryKey op)
        {
            if (!op.property)
                return;

            auto table = table_for_object_schema(group, *op.object);
            auto col = table->get_column_index(op.property->name);
            if (table->get_distinct_view(col).size() != table->size()) {
                throw DuplicatePrimaryKeyValueException(op.object->name, op.property->name);
            }
        }

        void operator()(ChangePropertyType) { }
        void operator()(MakePropertyNullable) { }
        void operator()(MakePropertyRequired) { }
        void operator()(AddTable) { }
        void operator()(AddProperty) { }
    } applier{group, initial_schema};

    for (auto& change : changes) {
        change.visit(applier);
    }
}

static void validate_primary_column_uniqueness(Group const& group)
{
    auto pk_table = group.get_table(c_primaryKeyTableName);
    for (size_t i = 0, count = pk_table->size(); i < count; ++i) {
        StringData object_type = pk_table->get_string(c_primaryKeyObjectClassColumnIndex, i);
        StringData primary_property = pk_table->get_string(c_primaryKeyPropertyNameColumnIndex, i);

        auto table = ObjectStore::table_for_object_type(group, object_type);
        if (table->get_distinct_view(table->get_column_index(primary_property)).size() != table->size()) {
            throw DuplicatePrimaryKeyValueException(object_type, primary_property);
        }
    }
}

void ObjectStore::apply_schema_changes(Group& group, Schema& schema, uint64_t& schema_version,
                                       Schema const& target_schema, uint64_t target_schema_version,
                                       std::vector<SchemaChange> const& changes, std::function<void()> migration_function)
{
    if (schema_version > target_schema_version && schema_version != ObjectStore::NotVersioned) {
        throw InvalidSchemaVersionException(schema_version, target_schema_version);
    }
    create_metadata_tables(group);

    if (schema_version == target_schema_version) {
        apply_non_migration_changes(group, changes);
        schema = target_schema;
        set_schema_columns(group, schema);
        return;
    }

    if (schema_version == ObjectStore::NotVersioned) {
        create_initial_tables(group, changes);
        set_schema_version(group, target_schema_version);
        schema_version = target_schema_version;
        schema = target_schema;
        set_schema_columns(group, schema);
        return;
    }

    apply_pre_migration_changes(group, changes);
    if (migration_function) {
        // Have to update the schema on the Realm before calling the migration
        // function as the migration will need it
        auto old_version = schema_version;
        auto old_schema = schema;
        schema_version = target_schema_version;
        schema = target_schema;
        set_schema_columns(group, schema);

        try {
            migration_function();

            // Migration function may have changed the schema, so we need to re-read it
            schema = schema_from_group(group);
            apply_post_migration_changes(group, schema.compare(target_schema), old_schema);
            validate_primary_column_uniqueness(group);
        }
        catch (...) {
            schema = move(old_schema);
            schema_version = old_version;
            throw;
        }
    }
    else {
        apply_post_migration_changes(group, changes, {});
    }

    set_schema_version(group, target_schema_version);
    schema_version = target_schema_version;
    schema = target_schema;
    set_schema_columns(group, schema);
}

Schema ObjectStore::schema_from_group(Group const& group) {
    std::vector<ObjectSchema> schema;
    for (size_t i = 0; i < group.size(); i++) {
        std::string object_type = object_type_for_table_name(group.get_table_name(i));
        if (object_type.length()) {
            schema.emplace_back(group, object_type);
        }
    }
    return schema;
}

void ObjectStore::set_schema_columns(Group const& group, Schema& schema)
{
    for (auto& object_schema : schema) {
        auto table = table_for_object_schema(group, object_schema);
        if (!table) {
            continue;
        }
        for (auto& property : object_schema.persisted_properties) {
            property.table_column = table->get_column_index(property.name);
        }
    }
}

void ObjectStore::delete_data_for_object(Group& group, StringData object_type) {
    TableRef table = table_for_object_type(group, object_type);
    if (table) {
        group.remove_table(table->get_index_in_group());
        set_primary_key_for_object(group, object_type, "");
    }
}

bool ObjectStore::is_empty(Group const& group) {
    for (size_t i = 0; i < group.size(); i++) {
        ConstTableRef table = group.get_table(i);
        std::string object_type = object_type_for_table_name(table->get_name());
        if (!object_type.length()) {
            continue;
        }
        if (!table->is_empty()) {
            return false;
        }
    }
    return true;
}

void ObjectStore::rename_property(Group& group, Schema& passed_schema, StringData object_type, StringData old_name, StringData new_name)
{
    TableRef table = table_for_object_type(group, object_type);
    if (!table) {
        throw std::logic_error(util::format("Cannot rename properties for type `%1` because it is not managed by the Realm.", object_type));
    }

    auto passed_object_schema = passed_schema.find(object_type);
    if (passed_object_schema == passed_schema.end()) {
        throw std::logic_error(util::format("Cannot rename properties for type `%1` because it has been removed from the Realm.", object_type));
    }

    ObjectSchema matching_schema(group, object_type);
    Property *old_property = matching_schema.property_for_name(old_name);
    if (!old_property) {
        throw std::logic_error(util::format("Cannot rename property `%1.%2` because it does not exist.", object_type, old_name));
    }

    Property *new_property = matching_schema.property_for_name(new_name);
    if (!new_property) {
        // new property not in new schema, which means we're probably renaming
        // to an intermediate property in a multi-version migration.
        // this is safe because the migration will fail schema validation unless
        // this property is renamed again.
        new_property = matching_schema.property_for_name(old_name);
        new_property->name = new_name;
        table->rename_column(old_property->table_column, new_name);
        return;
    }

    if (old_property->type != new_property->type || old_property->object_type != new_property->object_type) {
        throw std::logic_error(util::format("Cannot rename property `%1.%2` to `%3` because it would change from type `%4` to `%5`",
                                            object_type, old_name, new_name, old_property->type_string(), new_property->type_string()));
    }

    if (passed_object_schema->property_for_name(old_name)) {
        throw std::logic_error(util::format("Cannot rename property `%1.%2` because it is still present in the target schema.",
                                            object_type, old_name));
    }

    if (old_property->is_nullable && !new_property->is_nullable) {
        throw std::logic_error(util::format("Cannot rename property `%1.%2` to `%3` because it would change from nullable to required.",
                                            object_type, old_name, new_name));
    }

    size_t column_to_remove = new_property->table_column;
    table->rename_column(old_property->table_column, new_name);
    table->remove_column(column_to_remove);

    // update table_column for each property since it may have shifted
    for (auto& current_prop : passed_object_schema->persisted_properties) {
        auto target_prop = matching_schema.property_for_name(current_prop.name);
        current_prop.table_column = target_prop->table_column;
    }
    old_property->name = new_name;

    // update nullability for column
    if (new_property->is_nullable && !old_property->is_nullable) {
        auto prop = *new_property;
        prop.table_column = old_property->table_column;
        make_property_optional(group, *table, prop);
    }
}

InvalidSchemaVersionException::InvalidSchemaVersionException(uint64_t old_version, uint64_t new_version)
: logic_error(util::format("Provided schema version %1 is less than last set version %2.", new_version, old_version))
, m_old_version(old_version), m_new_version(new_version)
{
}

DuplicatePrimaryKeyValueException::DuplicatePrimaryKeyValueException(std::string object_type, std::string property)
: logic_error(util::format("Primary key property '%1.%2' has duplicate values after migration.", object_type, property))
, m_object_type(object_type), m_property(property)
{
}

template<typename... Args>
ObjectSchemaValidationException::ObjectSchemaValidationException(Args&&... args)
: std::logic_error(util::format(std::forward<Args>(args)...))
{
}

SchemaValidationException::SchemaValidationException(std::vector<ObjectSchemaValidationException> const& errors)
: std::logic_error([&] {
    std::string message = "Schema validation failed due to the following errors:";
    for (auto const& error : errors) {
        message += std::string("\n- ") + error.what();
    }
    return message;
}())
{
}

SchemaMismatchException::SchemaMismatchException(std::vector<ObjectSchemaValidationException> const& errors)
: std::logic_error([&] {
    std::string message = "Migration is required due to the following errors:";
    for (auto const& error : errors) {
        message += std::string("\n- ") + error.what();
    }
    return message;
}())
{
}
