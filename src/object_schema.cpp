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

#include "object_schema.hpp"

#include "object_store.hpp"
#include "property.hpp"
#include "schema.hpp"

#include <realm/data_type.hpp>
#include <realm/table.hpp>

using namespace realm;

#define ASSERT_PROPERTY_TYPE_VALUE(property, type) \
    static_assert(static_cast<int>(PropertyType::property) == type_##type, \
                  "PropertyType and DataType must have the same values")

ASSERT_PROPERTY_TYPE_VALUE(Int, Int);
ASSERT_PROPERTY_TYPE_VALUE(Bool, Bool);
ASSERT_PROPERTY_TYPE_VALUE(Float, Float);
ASSERT_PROPERTY_TYPE_VALUE(Double, Double);
ASSERT_PROPERTY_TYPE_VALUE(Data, Binary);
ASSERT_PROPERTY_TYPE_VALUE(Date, Timestamp);
ASSERT_PROPERTY_TYPE_VALUE(Any, Mixed);
ASSERT_PROPERTY_TYPE_VALUE(Object, Link);
ASSERT_PROPERTY_TYPE_VALUE(Array, LinkList);

ObjectSchema::ObjectSchema() = default;
ObjectSchema::~ObjectSchema() = default;

ObjectSchema::ObjectSchema(std::string name, std::initializer_list<Property> persisted_properties)
: name(std::move(name))
, persisted_properties(persisted_properties)
{
    for (auto const& prop : persisted_properties) {
        if (prop.is_primary) {
            primary_key = prop.name;
        }
    }
}

ObjectSchema::ObjectSchema(Group const& group, const std::string &name) : name(name) {
    ConstTableRef table = ObjectStore::table_for_object_type(group, name);

    size_t count = table->get_column_count();
    persisted_properties.reserve(count);
    for (size_t col = 0; col < count; col++) {
        Property property;
        property.name = table->get_column_name(col).data();
        property.type = (PropertyType)table->get_column_type(col);
        property.is_indexed = table->has_search_index(col);
        property.is_nullable = table->is_nullable(col) || property.type == PropertyType::Object;
        property.table_column = col;
        if (property.type == PropertyType::Object || property.type == PropertyType::Array) {
            // set link type for objects and arrays
            ConstTableRef linkTable = table->get_link_target(col);
            property.object_type = ObjectStore::object_type_for_table_name(linkTable->get_name().data());
        }
        persisted_properties.push_back(std::move(property));
    }

    primary_key = realm::ObjectStore::get_primary_key_for_object(group, name);
    set_primary_key_property();
}

Property *ObjectSchema::property_for_name(StringData name) {
    for (auto& prop : persisted_properties) {
        if (StringData(prop.name) == name) {
            return &prop;
        }
    }
    for (auto& prop : computed_properties) {
        if (StringData(prop.name) == name) {
            return &prop;
        }
    }
    return nullptr;
}

const Property *ObjectSchema::property_for_name(StringData name) const {
    return const_cast<ObjectSchema *>(this)->property_for_name(name);
}

void ObjectSchema::set_primary_key_property()
{
    if (primary_key.length()) {
        if (auto primary_key_prop = primary_key_property()) {
            primary_key_prop->is_primary = true;
        }
    }
}

static void validate_property(Schema const& schema,
                              std::string const& object_name,
                              Property const& prop,
                              Property const** primary,
                              std::vector<ObjectSchemaValidationException>& exceptions)
{
    // check nullablity
    if (prop.is_nullable && !prop.type_is_nullable()) {
        exceptions.emplace_back("Property `" + object_name + "." + prop.name + "` of type `"
                                + string_for_property_type(prop.type) + "` cannot be nullable.");
    }
    else if (prop.type == PropertyType::Object && !prop.is_nullable) {
        exceptions.emplace_back("Property `" + object_name + "." + prop.name + "` of type `Object` must be nullable.");
    }

    // check primary keys
    if (prop.is_primary) {
        if (prop.type != PropertyType::Int && prop.type != PropertyType::String) {
        exceptions.emplace_back("Property `" + object_name + "." + prop.name + "` of type `"
                                + string_for_property_type(prop.type) + "` cannot be made the primary key.");
        }
        if (*primary) {
            exceptions.emplace_back("Properties`" + prop.name + "` and `" + (*primary)->name
                                    + "` are both marked as the primary key of `" + object_name + "`.");
        }
        *primary = &prop;
    }

    // check indexable
    if (prop.is_indexed && !prop.is_indexable()) {
        exceptions.emplace_back("Property `" + object_name + "." + prop.name + "` of type `"
                                + string_for_property_type(prop.type) + "` cannot be indexed.");
    }

    // check that only link properties have object types
    if (prop.type != PropertyType::LinkingObjects && !prop.link_origin_property_name.empty()) {
        exceptions.emplace_back("Property `" + object_name + "." + prop.name + "` of type `"
                                + string_for_property_type(prop.type) + "` cannot have an origin property name.");
    }
    else if (prop.type == PropertyType::LinkingObjects && prop.link_origin_property_name.empty()) {
        exceptions.emplace_back("Property `" + object_name + "." + prop.name + "` of type `"
                                + string_for_property_type(prop.type) + "` must have an origin property name.");
    }

    if (prop.type != PropertyType::Object && prop.type != PropertyType::Array && prop.type != PropertyType::LinkingObjects) {
        if (!prop.object_type.empty()) {
            exceptions.emplace_back("Property `" + object_name + "." + prop.name + "` of type `"
                                    + string_for_property_type(prop.type) + "` cannot have an object type.");
        }
        return;
    }


    // check that the object_type is valid for link properties
    auto it = schema.find(prop.object_type);
    if (it == schema.end()) {
        exceptions.emplace_back("Property `" + object_name + "." + prop.name + "` of type `"
                                + string_for_property_type(prop.type) + "` has unknown object type `" + prop.object_type + "`");
        return;
    }
    if (prop.type != PropertyType::LinkingObjects) {
        return;
    }

    const Property *origin_property = it->property_for_name(prop.link_origin_property_name);
    if (!origin_property) {
        exceptions.emplace_back("Property `" + prop.object_type + "."
                                + prop.link_origin_property_name + "` declared as origin of linking objects property `"
                                + object_name + "." + prop.name + "` does not exist.");
    }
    else if (origin_property->type != PropertyType::Object && origin_property->type != PropertyType::Array) {
        exceptions.emplace_back("Property `" + prop.object_type + "."
                                + prop.link_origin_property_name + "` declared as origin of linking objects property `"
                                + object_name + "." + prop.name + "` is not a link.");
    }
    else if (origin_property->object_type != object_name) {
        exceptions.emplace_back("Property `" + prop.object_type + "."
                                + prop.link_origin_property_name + "` declared as origin of linking objects property `"
                                + object_name + "." + prop.name + "` links to type `" + origin_property->object_type + "`");
    }
}

void ObjectSchema::validate(Schema const& schema, std::vector<ObjectSchemaValidationException>& exceptions) const
{
    const Property *primary = nullptr;
    for (auto const& prop : persisted_properties) {
        validate_property(schema, name, prop, &primary, exceptions);
    }
    for (auto const& prop : computed_properties) {
        validate_property(schema, name, prop, &primary, exceptions);
    }

    if (!primary_key.empty() && !primary && !primary_key_property()) {
        exceptions.emplace_back("Specified primary key `" + name + "." + primary_key + "` does not exist.");
    }
}

namespace realm {
bool operator==(ObjectSchema const& a, ObjectSchema const& b)
{
    return std::tie(a.name, a.primary_key, a.persisted_properties, a.computed_properties)
        == std::tie(b.name, b.primary_key, b.persisted_properties, b.computed_properties);

}
}
