/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/hw/bcm/BcmEcmpUtils.h"
#include "fboss/agent/hw/bcm/tests/BcmTest.h"
#include "fboss/agent/test/EcmpSetupHelper.h"

#include "fboss/agent/FibHelpers.h"
#include "fboss/agent/hw/test/HwSwitchEnsembleRouteUpdateWrapper.h"
#include "fboss/agent/state/Route.h"
#include "fboss/agent/state/SwitchState.h"

#include "fboss/agent/hw/test/ConfigFactory.h"
#include "fboss/agent/test/ResourceLibUtil.h"

#include <folly/IPAddress.h>
extern "C" {
#include <bcm/l3.h>
}

using folly::CIDRNetwork;
using folly::IPAddress;
using std::string;

namespace facebook::fboss {

class BcmAddDelEcmpTest : public BcmTest {
 public:
  BcmAddDelEcmpTest() {
    cidrNetworks_ = {
        IPAddress::createNetwork("0.0.0.0/0"),
        IPAddress::createNetwork("::/0"),
        IPAddress::createNetwork("2001::/64"),
        IPAddress::createNetwork("2400::/56"),
        IPAddress::createNetwork("10.10.10.0/24"),
        IPAddress::createNetwork("11.11.11.0/24"),
    };
  }

 protected:
  cfg::SwitchConfig initialConfig() const override {
    auto cfg = utility::onePortPerVlanConfig(
        getHwSwitch(), masterLogicalPortIds(), cfg::PortLoopbackMode::MAC);
    return cfg;
  }
  template <typename AddrT>
  void setupECMPForwarding(
      const utility::EcmpSetupAnyNPorts<AddrT>& ecmpHelper,
      int ecmpWidth,
      const RoutePrefix<AddrT>& prefix) {
    auto newState = ecmpHelper.setupECMPForwarding(
        getProgrammedState(), ecmpWidth, {prefix});
    newState = ecmpHelper.resolveNextHops(newState, ecmpWidth);
    applyNewState(newState);
  }
  void delRoute(const CIDRNetwork& network) {
    HwSwitchEnsembleRouteUpdateWrapper updater(getHwSwitchEnsemble());
    updater.delRoute(kRid, network.first, network.second, ClientID(1001));
    updater.program();
  }
  void runTest() {
    auto cfg = initialConfig();
    applyNewConfig(cfg);
    // create as many entries as possible to verify no ecmp out of resource
    auto generator = utility::PrefixGenerator<folly::IPAddressV6>(64);
    std::vector<RoutePrefixV6> prefixes = generator.getNextN(
        masterLogicalPortIds().size() - 1 - cidrNetworks_.size());
    std::transform(
        prefixes.begin(),
        prefixes.end(),
        std::back_inserter(cidrNetworks_),
        [](RoutePrefixV6 prefix) {
          return IPAddress::createNetwork(prefix.str());
        });
    auto setup = [=]() {
      EXPECT_LT(cidrNetworks_.size(), masterLogicalPortIds().size());
      auto ecmpWidth = cidrNetworks_.size() + 1;
      for (auto& cidrNetwork : cidrNetworks_) {
        if (cidrNetwork.first.isV6()) {
          setupECMPForwarding(
              utility::EcmpSetupAnyNPorts6(getProgrammedState(), kRid),
              ecmpWidth--,
              RoutePrefixV6{cidrNetwork.first.asV6(), cidrNetwork.second});
        } else {
          setupECMPForwarding(
              utility::EcmpSetupAnyNPorts4(getProgrammedState(), kRid),
              ecmpWidth--,
              RoutePrefixV4{cidrNetwork.first.asV4(), cidrNetwork.second});
        }
      }
    };
    auto setupPostWB = [this]() {
      auto routesRemaining = cidrNetworks_.size();
      for (auto ritr = cidrNetworks_.rbegin(); ritr != cidrNetworks_.rend();
           ++ritr) {
        auto& network = *ritr;
        auto routeTable =
            getProgrammedState()->getRouteTables()->getRouteTable(kRid);
        bool deleted = false;
        if (network.first.isV6()) {
          if (findRoute<folly::IPAddressV6>(
                  getHwSwitchEnsemble()->isStandaloneRibEnabled(),
                  kRid,
                  network,
                  getProgrammedState())) {
            delRoute(network);
            deleted = true;
          }
        } else {
          if (findRoute<folly::IPAddressV4>(
                  getHwSwitchEnsemble()->isStandaloneRibEnabled(),
                  kRid,
                  network,
                  getProgrammedState())) {
            delRoute(network);
            deleted = true;
          }
        }
        --routesRemaining;
        if (deleted) {
          break;
        }
      }
      cidrNetworks_.resize(routesRemaining);
    };
    auto verify = [=]() {
      auto ecmpCount = utility::getEcmpsInHw(getHwSwitch()).size();
      EXPECT_EQ(cidrNetworks_.size(), ecmpCount);
    };
    auto verifyPostWB = [=]() {
      auto ecmpCount = utility::getEcmpsInHw(getHwSwitch()).size();
      EXPECT_EQ(cidrNetworks_.size(), ecmpCount);
    };
    verifyAcrossWarmBoots(setup, verify, setupPostWB, verifyPostWB);
  }

 private:
  const RouterID kRid{0};
  std::vector<CIDRNetwork> cidrNetworks_;
};

TEST_F(BcmAddDelEcmpTest, addDelRoutes) {
  runTest();
}

} // namespace facebook::fboss
