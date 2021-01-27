/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */
#include "fboss/agent/hw/bcm/BcmPortIngressBufferManager.h"
#include "fboss/agent/hw/bcm/BcmCosQueueFBConvertors.h"
#include "fboss/agent/hw/bcm/BcmError.h"
#include "fboss/agent/hw/bcm/BcmPlatform.h"
#include "fboss/agent/hw/bcm/BcmSwitch.h"
#include "fboss/agent/state/Port.h"

#include <folly/logging/xlog.h>

extern "C" {
#include <bcm/cosq.h>
#include <bcm/types.h>
}

namespace {
// defaults in mmu_lossless=0x2 mode
// determined by dumping registers from HW
constexpr bcm_cosq_control_drop_limit_alpha_value_t kDefaultPgAlpha =
    bcmCosqControlDropLimitAlpha_8;
constexpr int kDefaultPortPgId = 0;
constexpr int kDefaultMinLimitBytes = 0;
constexpr int kDefaultHeadroomLimitBytes = 0;
constexpr int kdefaultResumeOffsetBytes = 0;
constexpr int kDefaultSharedBytesTh3 = 111490 * 254;
constexpr int kDefaultHeadroomBytesTh3 = 18528 * 254;
// arbit
const std::string kDefaultBufferPoolName = "default";
constexpr int kDefaultPgId = 0;
} // unnamed namespace

namespace facebook::fboss {

BcmPortIngressBufferManager::BcmPortIngressBufferManager(
    BcmSwitch* hw,
    const std::string& portName,
    bcm_gport_t portGport)
    : hw_(hw), portName_(portName), gport_(portGport), unit_(hw_->getUnit()) {}

void BcmPortIngressBufferManager::writeCosqTypeToHw(
    const int cosq,
    const bcm_cosq_control_t type,
    const int value,
    const std::string& typeStr) {
  auto rv = bcm_cosq_control_set(unit_, gport_, cosq, type, value);
  bcmCheckError(
      rv,
      "failed to set ",
      typeStr,
      " for port ",
      portName_,
      " pgId ",
      cosq,
      " value ",
      value);
}

void BcmPortIngressBufferManager::readCosqTypeFromHw(
    const int cosq,
    const bcm_cosq_control_t type,
    int* value,
    const std::string& typeStr) const {
  *value = 0;
  auto rv = bcm_cosq_control_get(unit_, gport_, cosq, type, value);
  bcmCheckError(
      rv, "failed to get ", typeStr, " for port ", portName_, " cosq ", cosq);
}

void BcmPortIngressBufferManager::programPg(
    const PortPgConfig* portPgCfg,
    const int cosq) {
  int sharedDynamicEnable = 1;
  const auto& scalingFactor = portPgCfg->getScalingFactor();
  XLOG(DBG2) << "Program port PG config for cosq: " << cosq
             << " on port: " << portName_;

  if (!scalingFactor) {
    sharedDynamicEnable = 0;
  }

  writeCosqTypeToHw(
      cosq,
      bcmCosqControlIngressPortPGSharedDynamicEnable,
      sharedDynamicEnable,
      "bcmCosqControlIngressPortPGSharedDynamicEnable");

  if (sharedDynamicEnable) {
    auto alpha = utility::cfgAlphaToBcmAlpha(*scalingFactor);
    writeCosqTypeToHw(
        cosq,
        bcmCosqControlDropLimitAlpha,
        alpha,
        "bcmCosqControlDropLimitAlpha");
  }

  int pgMinLimitBytes = portPgCfg->getMinLimitBytes();
  writeCosqTypeToHw(
      cosq,
      bcmCosqControlIngressPortPGMinLimitBytes,
      pgMinLimitBytes,
      "bcmCosqControlIngressPortPGMinLimitBytes");

  auto hdrmBytes = portPgCfg->getHeadroomLimitBytes();
  int headroomBytes = hdrmBytes ? *hdrmBytes : 0;
  writeCosqTypeToHw(
      cosq,
      bcmCosqControlIngressPortPGHeadroomLimitBytes,
      headroomBytes,
      "bcmCosqControlIngressPortPGHeadroomLimitBytes");

  auto resumeBytes = portPgCfg->getResumeOffsetBytes();
  if (resumeBytes) {
    writeCosqTypeToHw(
        cosq,
        bcmCosqControlIngressPortPGResetOffsetBytes,
        *resumeBytes,
        "bcmCosqControlIngressPortPGResetOffsetBytes");
  }
}

void BcmPortIngressBufferManager::resetPgToDefault(int pgId) {
  const auto& portPg = getDefaultPgSettings();
  programPg(&portPg, pgId);
}

void BcmPortIngressBufferManager::resetIngressPoolsToDefault() {
  XLOG(DBG2) << "Reset ingress service pools to default for port " << portName_;
  const auto& bufferPoolCfg = getDefaultIngressPoolSettings();

  // we use one common buffer pool across all ports/PGs in our implementation
  // SDK API forces us to use port, PG
  // To prevent multiple sdk calls for all PGs just reset for kDefaultPgId only,
  // as all PGs refer to the same buffer pool only
  writeCosqTypeToHw(
      kDefaultPgId,
      bcmCosqControlIngressPoolLimitBytes,
      bufferPoolCfg.getSharedBytes(),
      "bcmCosqControlIngressPoolLimitBytes");
  writeCosqTypeToHw(
      kDefaultPgId,
      bcmCosqControlIngressHeadroomPoolLimitBytes,
      bufferPoolCfg.getHeadroomBytes(),
      "bcmCosqControlIngressHeadroomPoolLimitBytes");
  writeCosqTypeToHw(
      kDefaultPgId,
      bcmCosqControlEgressPoolSharedLimitBytes,
      bufferPoolCfg.getSharedBytes(),
      "bcmCosqControlEgressPoolSharedLimitBytes");
}

void BcmPortIngressBufferManager::resetPgsToDefault() {
  XLOG(DBG2) << "Reset all programmed PGs to default for port " << portName_;
  auto pgIdList = getPgIdListInHw();
  for (const auto& pgId : pgIdList) {
    resetPgToDefault(pgId);
  }
  pgIdList.clear();
  setPgIdListInHw(pgIdList);
}

void BcmPortIngressBufferManager::reprogramPgs(
    const std::shared_ptr<Port> port) {
  PgIdSet newPgList = {};
  const auto portPgCfgs = port->getPortPgConfigs();
  const auto pgIdList = getPgIdListInHw();

  if (portPgCfgs) {
    for (const auto& portPgCfg : *portPgCfgs) {
      programPg(portPgCfg.get(), portPgCfg->getID());
      newPgList.insert(portPgCfg->getID());
    }

    // find pgs in original list but not in new list
    // so we  know which ones to reset
    PgIdSet resetPgList;
    std::set_difference(
        pgIdList.begin(),
        pgIdList.end(),
        newPgList.begin(),
        newPgList.end(),
        std::inserter(resetPgList, resetPgList.end()));

    for (const auto pg : resetPgList) {
      XLOG(DBG2) << "Reset PG " << pg << " to default for port " << portName_;
      resetPgToDefault(pg);
    }
  }
  // update to latest PG list
  setPgIdListInHw(newPgList);
  XLOG(DBG2) << "New PG list programmed for port " << portName_;
}

void BcmPortIngressBufferManager::reprogramIngressPools(
    const std::shared_ptr<Port> port) {
  const auto& portPgCfgs = port->getPortPgConfigs();
  for (const auto& portPgCfg : *portPgCfgs) {
    if (auto bufferPoolPtr = portPgCfg->getBufferPoolConfig()) {
      writeCosqTypeToHw(
          portPgCfg->getID() /* pgid */,
          bcmCosqControlIngressPoolLimitBytes,
          (*bufferPoolPtr)->getSharedBytes(),
          "bcmCosqControlIngressPoolLimitBytes");
      writeCosqTypeToHw(
          portPgCfg->getID() /* pgid */,
          bcmCosqControlIngressHeadroomPoolLimitBytes,
          (*bufferPoolPtr)->getHeadroomBytes(),
          "bcmCosqControlIngressHeadroomPoolLimitBytes");
      // program the egress one equivalently
      writeCosqTypeToHw(
          portPgCfg->getID() /* pgid */,
          bcmCosqControlEgressPoolSharedLimitBytes,
          (*bufferPoolPtr)->getSharedBytes(),
          "bcmCosqControlEgressPoolSharedLimitBytes");
    }
  }
}

//  there are 4 possible cases
//  case 1: No prev cfg, no new cfg
//  case 2: Prev cfg, no new cfg
//  case 3: No prev cfg, new cfg
//  case 4: Prev cfg, new cfg
void BcmPortIngressBufferManager::programIngressBuffers(
    const std::shared_ptr<Port>& port) {
  const auto pgIdList = getPgIdListInHw();
  const auto& portPgCfgs = port->getPortPgConfigs();
  if (!portPgCfgs && (pgIdList.size() == 0)) {
    // there is nothing to program or unprogram
    // case 1
    return;
  }

  if (!portPgCfgs) {
    // unprogram the existing pgs
    // case 2
    resetPgsToDefault();
    return;
  }

  // simply reprogram based on new config
  // case 3, 4
  reprogramPgs(port);
}

const PortPgConfig& getTH3DefaultPgSettings() {
  static const PortPgConfig portPgConfig{PortPgFields{
      .id = kDefaultPortPgId,
      .scalingFactor = utility::bcmAlphaToCfgAlpha(kDefaultPgAlpha),
      .name = std::nullopt,
      .minLimitBytes = kDefaultMinLimitBytes,
      .headroomLimitBytes = kDefaultHeadroomLimitBytes,
      .resumeOffsetBytes = kdefaultResumeOffsetBytes,
      .bufferPoolName = "",
  }};
  return portPgConfig;
}

const BufferPoolCfg& getTH3DefaultIngressPoolSettings() {
  static const BufferPoolCfg bufferPoolCfg{BufferPoolCfgFields{
      .id = kDefaultBufferPoolName,
      .sharedBytes = kDefaultSharedBytesTh3,
      .headroomBytes = kDefaultHeadroomBytesTh3,
  }};
  return bufferPoolCfg;
}

// static
const PortPgConfig& BcmPortIngressBufferManager::getDefaultChipPgSettings(
    utility::BcmChip chip) {
  switch (chip) {
    case utility::BcmChip::TOMAHAWK3:
      return getTH3DefaultPgSettings();
    default:
      // currently ony supported for TH3
      throw FbossError("Unsupported platform for PG settings: ", chip);
  }
}

// static
const BufferPoolCfg&
BcmPortIngressBufferManager::getDefaultChipIngressPoolSettings(
    utility::BcmChip chip) {
  switch (chip) {
    case utility::BcmChip::TOMAHAWK3:
      return getTH3DefaultIngressPoolSettings();
    default:
      // currently ony supported for TH3
      throw FbossError(
          "Unsupported platform for Ingress Pool settings: ", chip);
  }
}

const PortPgConfig& BcmPortIngressBufferManager::getDefaultPgSettings() const {
  return hw_->getPlatform()->getDefaultPortPgSettings();
}

const BufferPoolCfg&
BcmPortIngressBufferManager::getDefaultIngressPoolSettings() const {
  return hw_->getPlatform()->getDefaultPortIngressPoolSettings();
}

void BcmPortIngressBufferManager::getPgParamsHw(
    const int pgId,
    const std::shared_ptr<PortPgConfig>& pg) const {
  if (const auto alpha = getIngressAlpha(pgId)) {
    pg->setScalingFactor(alpha.value());
  }
  pg->setMinLimitBytes(getPgMinLimitBytes(pgId));
  pg->setResumeOffsetBytes(getPgResumeOffsetBytes(pgId));
  pg->setHeadroomLimitBytes(getPgHeadroomLimitBytes(pgId));
}

BufferPoolCfgPtr BcmPortIngressBufferManager::getCurrentIngressPoolSettings()
    const {
  const std::string bufferName = "currentIngressPool";
  auto cfg = std::make_shared<BufferPoolCfg>(bufferName);
  // pick the settings for pgid = 0, since its global pool
  // all others will have the same values
  cfg->setHeadroomBytes(getIngressPoolHeadroomBytes(kDefaultPgId));
  cfg->setSharedBytes(getIngressSharedBytes(kDefaultPgId));
  return cfg;
}

PortPgConfigs BcmPortIngressBufferManager::getCurrentPgSettingsHw() const {
  PortPgConfigs pgs = {};
  // walk all pgs in HW and derive the programmed values
  for (auto pgId = 0; pgId <= cfg::switch_config_constants::PORT_PG_VALUE_MAX();
       pgId++) {
    auto pg = std::make_shared<PortPgConfig>(static_cast<uint8_t>(pgId));
    getPgParamsHw(pgId, pg);
    pgs.emplace_back(pg);
  }
  return pgs;
}

PortPgConfigs BcmPortIngressBufferManager::getCurrentProgrammedPgSettingsHw()
    const {
  PortPgConfigs pgs = {};

  // walk all programmed list of the pgIds in the order {0 -> 7}
  // Retrive copy of pgIdsListInHw_
  // But if pgIdsListInHw_ is not programmed, we return back empty
  auto pgIdList = getPgIdListInHw();
  for (const auto pgId : pgIdList) {
    auto pg = std::make_shared<PortPgConfig>(static_cast<uint8_t>(pgId));
    getPgParamsHw(pgId, pg);
    pgs.emplace_back(pg);
  }
  return pgs;
}

int BcmPortIngressBufferManager::getIngressPoolHeadroomBytes(
    bcm_cos_queue_t cosQ) const {
  int headroomBytes = 0;
  readCosqTypeFromHw(
      cosQ,
      bcmCosqControlIngressHeadroomPoolLimitBytes,
      &headroomBytes,
      "bcmCosqControlIngressHeadroomPoolLimitBytes");
  return headroomBytes;
}

int BcmPortIngressBufferManager::getIngressSharedBytes(
    bcm_cos_queue_t cosQ) const {
  int sharedBytes = 0;
  readCosqTypeFromHw(
      cosQ,
      bcmCosqControlIngressPoolLimitBytes,
      &sharedBytes,
      "bcmCosqControlIngressPoolLimitBytes");
  return sharedBytes;
}

int BcmPortIngressBufferManager::getPgHeadroomLimitBytes(
    bcm_cos_queue_t cosQ) const {
  int headroomBytes = 0;
  readCosqTypeFromHw(
      cosQ,
      bcmCosqControlIngressPortPGHeadroomLimitBytes,
      &headroomBytes,
      "bcmCosqControlIngressPortPGHeadroomLimitBytes");
  return headroomBytes;
}

std::optional<cfg::MMUScalingFactor>
BcmPortIngressBufferManager::getIngressAlpha(bcm_cos_queue_t cosQ) const {
  int sharedDynamicEnable = 0;
  readCosqTypeFromHw(
      cosQ,
      bcmCosqControlIngressPortPGSharedDynamicEnable,
      &sharedDynamicEnable,
      "bcmCosqControlIngressPortPGSharedDynamicEnable");
  if (sharedDynamicEnable) {
    int bcmAlpha = 0;
    readCosqTypeFromHw(
        cosQ,
        bcmCosqControlDropLimitAlpha,
        &bcmAlpha,
        "bcmCosqControlDropLimitAlpha");
    auto scalingFactor = utility::bcmAlphaToCfgAlpha(
        static_cast<bcm_cosq_control_drop_limit_alpha_value_e>(bcmAlpha));
    return scalingFactor;
  }
  return std::nullopt;
}

int BcmPortIngressBufferManager::getPgMinLimitBytes(
    bcm_cos_queue_t cosQ) const {
  int minBytes = 0;
  readCosqTypeFromHw(
      cosQ,
      bcmCosqControlIngressPortPGMinLimitBytes,
      &minBytes,
      "bcmCosqControlIngressPortPGMinLimitBytes");
  return minBytes;
}

int BcmPortIngressBufferManager::getPgResumeOffsetBytes(
    bcm_cos_queue_t cosQ) const {
  int resumeBytes = 0;
  readCosqTypeFromHw(
      cosQ,
      bcmCosqControlIngressPortPGResetOffsetBytes,
      &resumeBytes,
      "bcmCosqControlIngressPortPGResetOffsetBytes");
  return resumeBytes;
}

PgIdSet BcmPortIngressBufferManager::getPgIdListInHw() const {
  std::lock_guard<std::mutex> g(pgIdListLock_);
  return std::set<int>(pgIdListInHw_.begin(), pgIdListInHw_.end());
}

void BcmPortIngressBufferManager::setPgIdListInHw(PgIdSet& newPgIdList) {
  std::lock_guard<std::mutex> g(pgIdListLock_);
  pgIdListInHw_ = std::move(newPgIdList);
}

} // namespace facebook::fboss
