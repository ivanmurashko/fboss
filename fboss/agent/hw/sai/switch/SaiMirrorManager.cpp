/*
 *  Copyright (c) 2004-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under the BSD-style license found in the
 *  LICENSE file in the root directory of this source tree. An additional grant
 *  of patent rights can be found in the PATENTS file in the same directory.
 *
 */

#include "fboss/agent/hw/sai/switch/SaiMirrorManager.h"

#include "fboss/agent/FbossError.h"
#include "fboss/agent/hw/sai/store/SaiStore.h"
#include "fboss/agent/hw/sai/switch/SaiManagerTable.h"
#include "fboss/agent/hw/sai/switch/SaiPortManager.h"
#include "fboss/agent/hw/sai/switch/SaiSwitchManager.h"

#include "fboss/agent/platforms/sai/SaiPlatform.h"

namespace facebook::fboss {

SaiMirrorHandle::SaiMirror SaiMirrorManager::addMirrorSpan(
    PortSaiId monitorPort) {
  SaiLocalMirrorTraits::AdapterHostKey k{
      SAI_MIRROR_SESSION_TYPE_LOCAL, monitorPort};
  SaiLocalMirrorTraits::CreateAttributes attributes = k;
  auto& store = SaiStore::getInstance()->get<SaiLocalMirrorTraits>();
  return store.setObject(k, attributes);
}

SaiMirrorHandle::SaiMirror SaiMirrorManager::addMirrorErSpan(
    const std::shared_ptr<Mirror>& mirror,
    PortSaiId monitorPort) {
  auto mirrorTunnel = mirror->getMirrorTunnel().value();
  SaiEnhancedRemoteMirrorTraits::CreateAttributes attributes{
      SAI_MIRROR_SESSION_TYPE_ENHANCED_REMOTE,
      monitorPort,
      SAI_ERSPAN_ENCAPSULATION_TYPE_MIRROR_L3_GRE_TUNNEL,
      mirror->getDscp(),
      mirrorTunnel.srcIp,
      mirrorTunnel.dstIp,
      mirrorTunnel.srcMac,
      mirrorTunnel.dstMac,
      mirrorTunnel.greProtocol,
      mirrorTunnel.ttl,
      0 // TODO: truncate size
  };
  SaiEnhancedRemoteMirrorTraits::AdapterHostKey k{
      SAI_MIRROR_SESSION_TYPE_ENHANCED_REMOTE,
      monitorPort,
      mirrorTunnel.srcIp,
      mirrorTunnel.dstIp};
  auto& store = SaiStore::getInstance()->get<SaiEnhancedRemoteMirrorTraits>();
  return store.setObject(k, attributes);
}

#if SAI_API_VERSION >= SAI_VERSION(1, 7, 0)
SaiMirrorHandle::SaiMirror SaiMirrorManager::addMirrorSflow(
    const std::shared_ptr<Mirror>& mirror,
    PortSaiId monitorPort) {
  auto mirrorTunnel = mirror->getMirrorTunnel().value();
  SaiSflowMirrorTraits::CreateAttributes attributes{
      SAI_MIRROR_SESSION_TYPE_SFLOW,
      monitorPort,
      mirror->getDscp(),
      mirrorTunnel.srcIp,
      mirrorTunnel.dstIp,
      mirrorTunnel.srcMac,
      mirrorTunnel.dstMac,
      mirrorTunnel.udpPorts.value().udpSrcPort,
      mirrorTunnel.udpPorts.value().udpDstPort,
      mirrorTunnel.ttl,
  };
  SaiSflowMirrorTraits::AdapterHostKey k{
      SAI_MIRROR_SESSION_TYPE_SFLOW,
      monitorPort,
      mirrorTunnel.srcIp,
      mirrorTunnel.dstIp,
      mirrorTunnel.udpPorts.value().udpSrcPort,
      mirrorTunnel.udpPorts.value().udpDstPort};
  auto& store = SaiStore::getInstance()->get<SaiSflowMirrorTraits>();
  return store.setObject(k, attributes);
}
#endif

void SaiMirrorManager::addMirror(const std::shared_ptr<Mirror>& mirror) {
  auto mirrorHandleIter = mirrorHandles_.find(mirror->getID());
  if (mirrorHandleIter != mirrorHandles_.end()) {
    throw FbossError(
        "Attempted to add mirror which already exists: ", mirror->getID());
  }

  // TODO: Check for hw asic truncation support

  auto mirrorHandle = std::make_unique<SaiMirrorHandle>();
  auto monitorPortHandle = managerTable_->portManager().getPortHandle(
      mirror->getEgressPort().value());
  if (!monitorPortHandle) {
    throw FbossError(
        "Failed to find sai port for egress port for mirroring: ",
        mirror->getEgressPort().value());
  }
  if (mirror->getMirrorTunnel().has_value()) {
    auto mirrorTunnel = mirror->getMirrorTunnel().value();
    if (mirrorTunnel.udpPorts.has_value()) {
#if SAI_API_VERSION >= SAI_VERSION(1, 7, 0)
      mirrorHandle->mirror =
          addMirrorSflow(mirror, monitorPortHandle->port->adapterKey());
#else
      throw FbossError("sflow mirror not supported");
#endif
    } else {
      mirrorHandle->mirror =
          addMirrorErSpan(mirror, monitorPortHandle->port->adapterKey());
    }
  } else {
    mirrorHandle->mirror = addMirrorSpan(monitorPortHandle->port->adapterKey());
  }
  mirrorHandles_.emplace(mirror->getID(), std::move(mirrorHandle));
}

void SaiMirrorManager::removeMirror(const std::shared_ptr<Mirror>& mirror) {
  auto mirrorHandleIter = mirrorHandles_.find(mirror->getID());
  if (mirrorHandleIter == mirrorHandles_.end()) {
    throw FbossError(
        "Attempted to remove non-existent mirror: ", mirror->getID());
  }
  mirrorHandles_.erase(mirrorHandleIter);
}

void SaiMirrorManager::changeMirror(
    const std::shared_ptr<Mirror>& oldMirror,
    const std::shared_ptr<Mirror>& newMirror) {
  removeMirror(oldMirror);
  addMirror(newMirror);
}

SaiMirrorHandle* FOLLY_NULLABLE
SaiMirrorManager::getMirrorHandleImpl(const std::string& mirrorId) const {
  auto itr = mirrorHandles_.find(mirrorId);
  if (itr == mirrorHandles_.end()) {
    return nullptr;
  }
  if (!itr->second.get()) {
    XLOG(FATAL) << "Invalid null SaiMirrorHandle for " << mirrorId;
  }
  return itr->second.get();
}

const SaiMirrorHandle* FOLLY_NULLABLE
SaiMirrorManager::getMirrorHandle(const std::string& mirrorId) const {
  return getMirrorHandleImpl(mirrorId);
}

SaiMirrorHandle* FOLLY_NULLABLE
SaiMirrorManager::getMirrorHandle(const std::string& mirrorId) {
  return getMirrorHandleImpl(mirrorId);
}

SaiMirrorManager::SaiMirrorManager(
    SaiManagerTable* managerTable,
    const SaiPlatform* /*platform*/)
    : managerTable_(managerTable) {}

} // namespace facebook::fboss
