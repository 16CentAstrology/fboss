#pragma once

#include <folly/dynamic.h>
#include <folly/json.h>
#include <folly/logging/xlog.h>
#include <thrift/lib/cpp/util/EnumUtils.h>
#include <thrift/lib/cpp2/protocol/Serializer.h>
#include <type_traits>

#include "fboss/agent/Constants.h"
#include "fboss/agent/state/NodeBase.h"
#include "fboss/agent/state/NodeMap.h"

namespace facebook::fboss {

// All thrift state maps need to have an items field
inline constexpr folly::StringPiece kItems{"items"};

template <typename KeyT, typename NodeT>
struct ThriftyNodeMapTraits : public NodeMapTraits<
                                  KeyT,
                                  NodeT,
                                  NodeMapNoExtraFields,
                                  std::map<KeyT, NodeT>> {
  static inline const std::string& getThriftKeyName() {
    static const std::string _key = "id";
    return _key;
  }

  // convert key from cpp version to one thrift will like
  template <typename NodeKey>
  static const KeyT convertKey(const NodeKey& key) {
    return static_cast<KeyT>(key);
  }

  // parse dynamic key into thrift acceptable type
  static const KeyT parseKey(const folly::dynamic& key) {
    return key.asInt();
  }
};

class ThriftyUtils {
 public:
  static void renameField(
      folly::dynamic& dyn,
      const std::string& oldName,
      const std::string& newName) {
    if (auto it = dyn.find(oldName); it != dyn.items().end()) {
      // keep old value for backwards compat
      dyn.insert(newName, it->second);
    }
  }

  template <typename EnumT>
  static void changeEnumToInt(folly::dynamic& dyn, const std::string& key) {
    if (auto it = dyn.find(key); it != dyn.items().end()) {
      EnumT val;
      if (apache::thrift::util::tryParseEnum(it->second.asString(), &val)) {
        it->second = static_cast<int>(val);
      }
    }
  }

  template <typename EnumT>
  static void changeEnumToString(folly::dynamic& dyn, const std::string& key) {
    if (auto it = dyn.find(key); it != dyn.items().end()) {
      try {
        it->second = apache::thrift::util::enumName(
            static_cast<EnumT>(it->second.asInt()));
      } catch (const folly::TypeError&) {
        // no problem, it was already a string
      }
    }
  }

  template <typename T>
  static bool listEq(
      std::vector<std::shared_ptr<T>> a,
      std::vector<std::shared_ptr<T>> b) {
    return a.size() == b.size() &&
        std::equal(
               std::begin(a),
               std::end(a),
               std::begin(b),
               std::end(b),
               [](const auto& lhs, const auto& rhs) { return *lhs == *rhs; });
  }

  template <typename T>
  static bool listEq(
      std::optional<std::vector<std::shared_ptr<T>>> a,
      std::optional<std::vector<std::shared_ptr<T>>> b) {
    return (!a.has_value() && !b.has_value()) ||
        (a.has_value() && b.has_value() && listEq(*a, *b));
  }

  static bool nodeNeedsMigration(const folly::dynamic& dyn) {
    return !dyn.getDefault(kThriftySchemaUpToDate, false).asBool();
  }

  static auto constexpr kThriftySchemaUpToDate = "__thrifty_schema_uptodate";
};

class ThriftyFields {
 public:
  //  migrateTo does not modify dyn so we don't have to change the call sites of
  //  fromFollyDynamic, migrateFrom does not have this limitation
  static folly::dynamic migrateToThrifty(const folly::dynamic& dyn) {
    return dyn;
  }
  static void migrateFromThrifty(folly::dynamic& dyn) {
    dyn[ThriftyUtils::kThriftySchemaUpToDate] = true;
  }
};

// Base class to convert NodeMaps to thrift
template <typename NodeMap, typename TraitsT, typename ThriftyTraitsT>
class ThriftyNodeMapT : public NodeMapT<NodeMap, TraitsT> {
 public:
  using NodeMapT<NodeMap, TraitsT>::NodeMapT;

  static std::shared_ptr<NodeMap> fromThrift(
      const typename ThriftyTraitsT::NodeContainer& map) {
    auto mapObj = std::make_shared<NodeMap>();

    for (auto& node : map) {
      auto fieldsObj = TraitsT::Node::Fields::fromThrift(node.second);
      mapObj->addNode(std::make_shared<typename TraitsT::Node>(fieldsObj));
    }

    return mapObj;
  }

  static std::shared_ptr<NodeMap> fromFollyDynamic(folly::dynamic const& dyn) {
    if (ThriftyUtils::nodeNeedsMigration(dyn)) {
      return fromFollyDynamicImpl(NodeMap::migrateToThrifty(dyn));
    } else {
      // Schema is up to date meaning there is not migration required
      return fromFollyDynamicImpl(dyn);
    }
  }

  static std::shared_ptr<NodeMap> fromFollyDynamicImpl(
      folly::dynamic const& dyn) {
    typename ThriftyTraitsT::NodeContainer mapTh;
    for (auto& [key, val] : dyn.items()) {
      if (key == kEntries || key == kExtraFields ||
          key == ThriftyUtils::kThriftySchemaUpToDate) {
        continue;
      }
      auto jsonStr = folly::toJson(val);
      auto inBuf =
          folly::IOBuf::wrapBufferAsValue(jsonStr.data(), jsonStr.size());
      auto node = apache::thrift::SimpleJSONSerializer::deserialize<
          typename ThriftyTraitsT::Node>(folly::io::Cursor{&inBuf});
      mapTh[ThriftyTraitsT::parseKey(key)] = node;
    }
    return fromThrift(mapTh);
  }

  typename ThriftyTraitsT::NodeContainer toThrift() const {
    typename ThriftyTraitsT::NodeContainer items;
    for (auto& node : *this) {
      items[getNodeThriftKey(node)] = node->getFields()->toThrift();
    }

    return items;
  }

  folly::dynamic toFollyDynamic() const override {
    auto obj = this->toThrift();
    folly::dynamic dyn = folly::dynamic::object();
    for (auto& [key, val] : obj) {
      std::string jsonStr;
      apache::thrift::SimpleJSONSerializer::serialize(val, &jsonStr);
      dyn[folly::to<std::string>(key)] = folly::parseJson(jsonStr);
    }

    NodeMap::migrateFromThrifty(dyn);
    return dyn;
  }

  /*
   * Old style NodeMapT serlization has the nodes in a list but with thrift we
   * can probably just encode a map directly. So in thrift we'll use the name
   * "items" instead of "entries and to migrate we'll duplicate the data as a
   * list under "entries" and a map under "items"
   */
  static folly::dynamic migrateToThrifty(const folly::dynamic& dyn) {
    folly::dynamic newItems = folly::dynamic::object;
    for (auto& item : dyn[kEntries]) {
      if (ThriftyUtils::nodeNeedsMigration(item)) {
        newItems[item[ThriftyTraitsT::getThriftKeyName()].asString()] =
            TraitsT::Node::Fields::migrateToThrifty(item);
      } else {
        newItems[item[ThriftyTraitsT::getThriftKeyName()].asString()] = item;
      }
    }
    return newItems;
  }

  static void migrateFromThrifty(folly::dynamic& dyn) {
    auto schemaUpToDate = true;
    folly::dynamic entries = folly::dynamic::array;

    for (auto& item : dyn.items()) {
      TraitsT::Node::Fields::migrateFromThrifty(item.second);
      if (!item.second.getDefault(ThriftyUtils::kThriftySchemaUpToDate, false)
               .asBool()) {
        schemaUpToDate = false;
      }
      entries.push_back(item.second);
    }

    dyn[kEntries] = entries;
    // TODO: fill out extra fields as needed
    dyn[kExtraFields] = folly::dynamic::object;

    dyn[ThriftyUtils::kThriftySchemaUpToDate] = schemaUpToDate;
  }

  // for testing purposes
  folly::dynamic toFollyDynamicLegacy() const {
    return NodeMapT<NodeMap, TraitsT>::toFollyDynamic();
  }

  static std::shared_ptr<NodeMap> fromFollyDynamicLegacy(
      folly::dynamic const& dyn) {
    return NodeMapT<NodeMap, TraitsT>::fromFollyDynamic(dyn);
  }

  // return node id in the node map as it would be represented in thrift
  static typename ThriftyTraitsT::KeyType getNodeThriftKey(
      const std::shared_ptr<typename TraitsT::Node>& node) {
    return ThriftyTraitsT::convertKey(TraitsT::getKey(node));
  }

  bool operator==(
      const ThriftyNodeMapT<NodeMap, TraitsT, ThriftyTraitsT>& rhs) const {
    if (this->size() != rhs.size()) {
      return false;
    }
    for (auto& node : *this) {
      if (auto other = rhs.getNodeIf(TraitsT::getKey(node));
          !other || *node != *other) {
        return false;
      }
    }
    return true;
  }

  bool operator!=(
      const ThriftyNodeMapT<NodeMap, TraitsT, ThriftyTraitsT>& rhs) const {
    return !(*this == rhs);
  }
};

//
// Special version of NodeBaseT that features methods to convert
// object to/from JSON using Thrift serializers. For this to work
// one must supply Thrift type (ThrifT) that stores FieldsT state.
//
// TODO: in future, FieldsT and ThrifT should be one type
//
template <typename ThriftT, typename NodeT, typename FieldsT>
class ThriftyBaseT : public NodeBaseT<NodeT, FieldsT> {
  static_assert(std::is_base_of_v<ThriftyFields, FieldsT>);

 public:
  using NodeBaseT<NodeT, FieldsT>::NodeBaseT;
  using Fields = FieldsT;

  static std::shared_ptr<NodeT> fromThrift(const ThriftT& obj) {
    auto fields = FieldsT::fromThrift(obj);
    return std::make_shared<NodeT>(fields);
  }

  static std::shared_ptr<NodeT> fromFollyDynamic(folly::dynamic const& dyn) {
    if (ThriftyUtils::nodeNeedsMigration(dyn)) {
      return fromJson(folly::toJson(FieldsT::migrateToThrifty(dyn)));
    } else {
      // Schema is up to date meaning there is no migration required
      return fromJson(folly::toJson(dyn));
    }
  }

  static std::shared_ptr<NodeT> fromJson(const folly::fbstring& jsonStr) {
    auto inBuf =
        folly::IOBuf::wrapBufferAsValue(jsonStr.data(), jsonStr.size());
    auto obj = apache::thrift::SimpleJSONSerializer::deserialize<ThriftT>(
        folly::io::Cursor{&inBuf});
    return ThriftyBaseT::fromThrift(obj);
  }

  ThriftT toThrift() const {
    return this->getFields()->toThrift();
  }

  std::string str() const {
    auto obj = this->toThrift();
    std::string jsonStr;
    apache::thrift::SimpleJSONSerializer::serialize(obj, &jsonStr);
    return jsonStr;
  }

  folly::dynamic toFollyDynamic() const override {
    auto dyn = folly::parseJson(this->str());
    FieldsT::migrateFromThrifty(dyn);
    return dyn;
  }

  // for testing purposes
  folly::dynamic toFollyDynamicLegacy() const {
    return this->getFields()->toFollyDynamicLegacy();
  }

  static std::shared_ptr<NodeT> fromFollyDynamicLegacy(
      folly::dynamic const& dyn) {
    return std::make_shared<NodeT>(FieldsT::fromFollyDynamicLegacy(dyn));
  }

  bool operator==(const ThriftyBaseT<ThriftT, NodeT, FieldsT>& rhs) const {
    return *this->getFields() == *rhs.getFields();
  }

  bool operator!=(const ThriftyBaseT<ThriftT, NodeT, FieldsT>& rhs) const {
    return !(*this == rhs);
  }
};
} // namespace facebook::fboss
