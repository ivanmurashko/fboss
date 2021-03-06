// Copyright 2004-present Facebook. All Rights Reserved.

#include "fboss/agent/hw/sai/fake/FakeSaiLag.h"

#include "fboss/agent/hw/sai/fake/FakeSai.h"

using facebook::fboss::FakeLag;
using facebook::fboss::FakeLagMember;
using facebook::fboss::FakeSai;

sai_status_t remove_lag_fn(sai_object_id_t lag_id) {
  auto fs = FakeSai::getInstance();
  fs->lagManager.remove(lag_id);
  return SAI_STATUS_SUCCESS;
}

sai_status_t set_lag_attribute_fn(
    sai_object_id_t lag_id,
    const sai_attribute_t* attr) {
  auto fs = FakeSai::getInstance();
  switch (attr->id) {
    // TODO: upgrade OSS to 1.7.1
#ifdef IS_OSS
    case SAI_LAG_ATTR_END:
#else
    case SAI_LAG_ATTR_LABEL:
#endif
    {
      auto& lag = fs->lagManager.get(lag_id);
      std::copy(
          attr->value.chardata,
          attr->value.chardata + lag.label.size(),
          std::begin(lag.label));
    } break;
    default:
      return SAI_STATUS_NOT_SUPPORTED;
  }
  return SAI_STATUS_SUCCESS;
}

sai_status_t get_lag_attribute_fn(
    sai_object_id_t lag_id,
    uint32_t attr_count,
    sai_attribute_t* attr_list) {
  auto fs = FakeSai::getInstance();
  for (int i = 0; i < attr_count; ++i) {
    switch (attr_list[i].id) {
      case SAI_LAG_ATTR_PORT_LIST: {
        const auto& lagMemberMap = fs->lagManager.get(lag_id).fm().map();
        if (lagMemberMap.size() > attr_list[i].value.objlist.count) {
          attr_list[i].value.objlist.count = lagMemberMap.size();
          return SAI_STATUS_BUFFER_OVERFLOW;
        }
        attr_list[i].value.objlist.count = lagMemberMap.size();
        int j = 0;
        for (const auto& m : lagMemberMap) {
          attr_list[i].value.objlist.list[j++] = m.first;
        }
      } break;

#ifdef IS_OSS
      case SAI_LAG_ATTR_END:
#else
      case SAI_LAG_ATTR_LABEL:
#endif
      {
        const auto& label = fs->lagManager.get(lag_id).label;
        std::copy(
            std::begin(label), std::end(label), attr_list[i].value.chardata);
      } break;
      default:
        return SAI_STATUS_NOT_SUPPORTED + i;
    }
  }
  return SAI_STATUS_SUCCESS;
}

sai_status_t create_lag_fn(
    sai_object_id_t* lag_id,
    sai_object_id_t /*switch_id*/,
    uint32_t count,
    const sai_attribute_t* attr_list) {
  auto fs = FakeSai::getInstance();
  *lag_id = fs->lagManager.create();
  for (auto i = 0; i < count; i++) {
    auto rv = set_lag_attribute_fn(*lag_id, &attr_list[i]);
    if (rv != SAI_STATUS_SUCCESS) {
      return rv;
    }
  }
  return SAI_STATUS_SUCCESS;
}

sai_status_t create_lag_member_fn(
    sai_object_id_t* lag_member_id,
    sai_object_id_t /*switch_id*/,
    uint32_t attr_count,
    const sai_attribute_t* attr_list) {
  auto fs = FakeSai::getInstance();

  sai_object_id_t lag_id = SAI_NULL_OBJECT_ID;
  sai_object_id_t port_id = SAI_NULL_OBJECT_ID;

  for (int i = 0; i < attr_count; ++i) {
    switch (attr_list[i].id) {
      case SAI_LAG_MEMBER_ATTR_LAG_ID:
        lag_id = attr_list[i].value.oid;
        break;
      case SAI_LAG_MEMBER_ATTR_PORT_ID:
        port_id = attr_list[i].value.oid;
        break;
      default:
        return SAI_STATUS_NOT_SUPPORTED + i;
    }
  }
  *lag_member_id = fs->lagManager.createMember(lag_id, lag_id, port_id);
  return SAI_STATUS_SUCCESS;
}

sai_status_t remove_lag_member_fn(sai_object_id_t lag_member_id) {
  auto fs = FakeSai::getInstance();
  fs->lagManager.removeMember(lag_member_id);
  return SAI_STATUS_SUCCESS;
}

sai_status_t set_lag_member_attribute_fn(
    sai_object_id_t /*lag_member_id*/,
    const sai_attribute_t* /*attr*/) {
  return SAI_STATUS_NOT_IMPLEMENTED;
}

sai_status_t get_lag_member_attribute_fn(
    sai_object_id_t lag_member_id,
    uint32_t attr_count,
    sai_attribute_t* attr_list) {
  auto fs = FakeSai::getInstance();
  auto& lagMember = fs->lagManager.getMember(lag_member_id);
  for (int i = 0; i < attr_count; ++i) {
    switch (attr_list[i].id) {
      case SAI_LAG_MEMBER_ATTR_LAG_ID:
        attr_list[i].value.oid = lagMember.lagId_;
        break;
      case SAI_LAG_MEMBER_ATTR_PORT_ID:
        attr_list[i].value.oid = lagMember.portId_;
        break;
      default:
        return SAI_STATUS_NOT_SUPPORTED + i;
    }
  }
  return SAI_STATUS_SUCCESS;
}

namespace facebook::fboss {
static sai_lag_api_t _lag_api;

void populate_lag_api(sai_lag_api_t** lag_api) {
  _lag_api.create_lag = &create_lag_fn;
  _lag_api.remove_lag = &remove_lag_fn;
  _lag_api.set_lag_attribute = &set_lag_attribute_fn;
  _lag_api.get_lag_attribute = &get_lag_attribute_fn;
  _lag_api.create_lag_member = &create_lag_member_fn;
  _lag_api.remove_lag_member = &remove_lag_member_fn;
  _lag_api.set_lag_member_attribute = &set_lag_member_attribute_fn;
  _lag_api.get_lag_member_attribute = &get_lag_member_attribute_fn;
  *lag_api = &_lag_api;
}
} // namespace facebook::fboss
