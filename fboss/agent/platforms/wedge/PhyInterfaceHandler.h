// Copyright 2004-present Facebook. All Rights Reserved.

#pragma once

#include "fboss/lib/phy/ExternalPhy.h"

namespace facebook {
namespace fboss {

class PhyInterfaceHandler {
 public:
  virtual ~PhyInterfaceHandler() {}

  /*
   * initExternalPhyMap
   * A virtual function for initialzing the ExternalPhy obejcts. This function
   * needs to be implemented by inheriting class. If it has to be implemented
   * locally then that should called PhyManager function. If it has to be in
   * separate process then that inheriting class implemntation should  be
   * a thrift call to port service process
   */
  virtual bool initExternalPhyMap() = 0;

  /*
   * programOnePort
   * A virtual function for the ExternalPhy obejcts. The inheriting class
   * need to implement this function. If the Phy code is in same process
   * then that should called PhyManager function otherwise it should  be
   * a thrift call to port service process
   *
   * Note: portProfileId needs to be removed once all Phy code is moved to
   * qsfp_service as based on portId, profileID the qsfp_service can construct
   * the phyPortConfig
   */
  virtual void programOnePort(
      int /* slotId */,
      int /* mdioId */,
      int /* phyId */,
      int32_t /* portId */,
      cfg::PortProfileID /* portProfileId */,
      phy::PhyPortConfig /* config */) = 0;

  /*
   * setPortPrbs
   * A virtual function for the ExternalPhy obejcts. The inheriting class
   * need to implement this function. If the Phy code is in same process
   * then that should called PhyManager function otherwise it should  be
   * a thrift call to port service process
   *
   * Note: portProfileId needs to be removed once all Phy code is moved to
   * qsfp_service as based on portId, profileID the qsfp_service can construct
   * the phyPortConfig
   */
  virtual void setPortPrbs(
      int /* slotId */,
      int /* mdioId */,
      int /* phyId */,
      int32_t /* portId */,
      cfg::PortProfileID /* portProfileId */,
      phy::PhyPortConfig /* config */,
      phy::Side /* side */,
      bool /* enable */,
      int32_t /* polynominal */) = 0;

  /*
   * getPortStats
   * A virtual function for the ExternalPhy obejcts. The inheriting class
   * need to implement this function. If the Phy code is in same process
   * then that should called PhyManager function otherwise it should  be
   * a thrift call to port service process
   *
   * Note: portProfileId needs to be removed once all Phy code is moved to
   * qsfp_service as based on portId, profileID the qsfp_service can construct
   * the phyPortConfig
   */
  virtual phy::ExternalPhyPortStats getPortStats(
      int /* slotId */,
      int /* mdioId */,
      int /* phyId */,
      int32_t /* portId */,
      cfg::PortProfileID /* portProfileId */,
      phy::PhyPortConfig /* config */) = 0;

  /*
   * getPortPrbsStats
   * A virtual function for the ExternalPhy obejcts. The inheriting class
   * need to implement this function. If the Phy code is in same process
   * then that should called PhyManager function otherwise it should  be
   * a thrift call to port service process
   *
   * Note: portProfileId needs to be removed once all Phy code is moved to
   * qsfp_service as based on portId, profileID the qsfp_service can construct
   * the phyPortConfig
   */
  virtual phy::ExternalPhyPortStats getPortPrbsStats(
      int /* slotId */,
      int /* mdioId */,
      int /* phyId */,
      int32_t /* portId */,
      cfg::PortProfileID /* portProfileId */,
      phy::PhyPortConfig /* config */) = 0;

  /*
   * getLaneSpeed
   * A virtual function for the ExternalPhy obejcts. The inheriting class
   * need to implement this function. If the Phy code is in same process
   * then that should called PhyManager function otherwise it should  be
   * a thrift call to port service process
   *
   * Note: portProfileId needs to be removed once all Phy code is moved to
   * qsfp_service as based on portId, profileID the qsfp_service can construct
   * the phyPortConfig
   */
  virtual float_t getLaneSpeed(
      int /* slotId */,
      int /* mdioId */,
      int /* phyId */,
      int32_t /* portId */,
      cfg::PortProfileID /* portProfileId */,
      phy::PhyPortConfig /* config */,
      phy::Side /* side */) = 0;

  /*
   * initializeSlotPhys
   * A virtual function for the ExternalPhy obejcts. The sub-class needs to
   * implement this function. The implementation will be different for
   * Minipack abd Yamp. If the Phy code is in same process then that should
   * called PhyManager function otherwise it should  be a thrift call to port
   * service process
   */
  virtual void initializeSlotPhys(int /* slotId */, bool /* warmboot */) = 0;

 protected:
};

} // namespace fboss
} // namespace facebook
