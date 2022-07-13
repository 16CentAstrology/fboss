// Copyright 2004-present Facebook. All Rights Reserved.

#pragma once

#include <boost/container/flat_set.hpp>

#include <fboss/agent/state/LabelForwardingEntry.h>
#include "fboss/agent/if/gen-cpp2/ctrl_types.h"
#include "fboss/agent/state/LabelForwardingEntry.h"
#include "fboss/agent/state/NodeMap.h"
#include "fboss/agent/state/Route.h"

namespace facebook::fboss {

using LabelForwardingRoute = NodeMapTraits<Label, LabelForwardingEntry>;

struct LabelForwardingInformationBaseThriftTraits
    : public ThriftyNodeMapTraits<int32_t, state::LabelForwardingEntryFields> {
  static inline const std::string& getThriftKeyName() {
    static const std::string _key = "prefix";
    return _key;
  }

  static KeyType parseKey(const folly::dynamic& key) {
    return key.asInt();
  }

  static int32_t convertKey(const Label& label) {
    return label.value();
  }
};

class LabelForwardingInformationBase
    : public ThriftyNodeMapT<
          LabelForwardingInformationBase,
          LabelForwardingRoute,
          LabelForwardingInformationBaseThriftTraits> {
  using BaseT = ThriftyNodeMapT<
      LabelForwardingInformationBase,
      LabelForwardingRoute,
      LabelForwardingInformationBaseThriftTraits>;

 public:
  LabelForwardingInformationBase();

  ~LabelForwardingInformationBase();

  const std::shared_ptr<LabelForwardingEntry>& getLabelForwardingEntry(
      Label topLabel) const;

  std::shared_ptr<LabelForwardingEntry> getLabelForwardingEntryIf(
      Label topLabel) const;

  static std::shared_ptr<LabelForwardingInformationBase> fromFollyDynamicLegacy(
      const folly::dynamic& json);

  std::shared_ptr<LabelForwardingEntry> cloneLabelEntry(
      std::shared_ptr<LabelForwardingEntry> entry);

  LabelForwardingInformationBase* modify(std::shared_ptr<SwitchState>* state);

  LabelForwardingEntry* modifyLabelEntry(
      std::shared_ptr<SwitchState>* state,
      std::shared_ptr<LabelForwardingEntry> entry);

  LabelForwardingInformationBase* programLabel(
      std::shared_ptr<SwitchState>* state,
      Label label,
      ClientID client,
      AdminDistance distance,
      LabelNextHopSet nexthops);

  LabelForwardingInformationBase* unprogramLabel(
      std::shared_ptr<SwitchState>* state,
      Label label,
      ClientID client);

  LabelForwardingInformationBase* purgeEntriesForClient(
      std::shared_ptr<SwitchState>* state,
      ClientID client);

  static bool isValidNextHopSet(const LabelNextHopSet& nexthops);

  // Used for resolving route when mpls rib is not enabled
  static void resolve(std::shared_ptr<LabelForwardingEntry> entry) {
    entry->setResolved(*entry->getBestEntry().second);
  }

  // For backward compatibility with old format
  static folly::dynamic toFollyDynamicOldFormat(
      std::shared_ptr<LabelForwardingEntry> entry);
  static std::shared_ptr<LabelForwardingEntry> labelEntryFromFollyDynamic(
      folly::dynamic entry);

  static void noRibToRibEntryConvertor(
      std::shared_ptr<LabelForwardingEntry>& entry);

 private:
  // Inherit the constructors required for clone()
  using ThriftyNodeMapT::ThriftyNodeMapT;
  friend class CloneAllocator;
  static std::shared_ptr<LabelForwardingEntry> fromFollyDynamicOldFormat(
      folly::dynamic entry);
};

} // namespace facebook::fboss
