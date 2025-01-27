// Copyright 2019-present Barefoot Networks, Inc.
// Copyright 2022 Intel Corporation
// SPDX-License-Identifier: Apache-2.0

// Target-agnostic SDE wrapper Table Data methods.

#include "stratum/hal/lib/tdi/tdi_sde_wrapper.h"

#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "gflags/gflags_declare.h"
#include "stratum/glue/integral_types.h"
#include "stratum/glue/gtl/stl_util.h"
#include "stratum/glue/status/status.h"
#include "stratum/glue/status/statusor.h"
#include "stratum/hal/lib/p4/utils.h"
#include "stratum/hal/lib/tdi/macros.h"
#include "stratum/hal/lib/tdi/tdi_constants.h"
#include "stratum/hal/lib/tdi/tdi_sde_common.h"
#include "stratum/hal/lib/tdi/tdi_sde_helpers.h"
#include "stratum/hal/lib/tdi/utils.h"
#include "stratum/lib/macros.h"

DECLARE_bool(incompatible_enable_tdi_legacy_bytestring_responses);

namespace stratum {
namespace hal {
namespace tdi {

using namespace stratum::hal::tdi::helpers;

::util::Status TableData::SetParam(int id, const std::string& value) {
  tdi_id_t action_id = 0;
  size_t field_size_bits = 0;
  const ::tdi::DataFieldInfo * dataFieldInfo;
  const ::tdi::Table* table;
  RETURN_IF_TDI_ERROR(table_data_->getParent(&table));

  action_id = table_data_->actionIdGet();
  dataFieldInfo = table->tableInfoGet()->dataFieldGet(id, action_id);
  RETURN_IF_NULL(dataFieldInfo);
  field_size_bits = dataFieldInfo->sizeGet();

  std::string p = P4RuntimeByteStringToPaddedByteString(
      value, NumBitsToNumBytes(field_size_bits));
  RETURN_IF_TDI_ERROR(table_data_->setValue(
      id, reinterpret_cast<const uint8*>(p.data()), p.size()));

  return ::util::OkStatus();
}

::util::Status TableData::GetParam(int id, std::string* value) const {
  size_t field_size_bits;
  const ::tdi::DataFieldInfo * dataFieldInfo;
  const ::tdi::Table* table;
  RETURN_IF_TDI_ERROR(table_data_->getParent(&table));

  tdi_id_t action_id = table_data_->actionIdGet();
  dataFieldInfo = table->tableInfoGet()->dataFieldGet(id, action_id);
  RETURN_IF_NULL(dataFieldInfo);
  field_size_bits = dataFieldInfo->sizeGet();

  value->clear();
  value->resize(NumBitsToNumBytes(field_size_bits));
  RETURN_IF_TDI_ERROR(table_data_->getValue(
      id, value->size(),
      reinterpret_cast<uint8*>(gtl::string_as_array(value))));
  if (!FLAGS_incompatible_enable_tdi_legacy_bytestring_responses) {
    *value = ByteStringToP4RuntimeByteString(*value);
  }
  return ::util::OkStatus();
}

::util::Status TableData::SetActionMemberId(uint64 action_member_id) {
  return SetField(table_data_.get(), kActionMemberId, action_member_id);
}

::util::Status TableData::GetActionMemberId(uint64* action_member_id) const {
  return GetField(*(table_data_.get()), kActionMemberId, action_member_id);
}

::util::Status TableData::SetSelectorGroupId(uint64 selector_group_id) {
  return SetField(table_data_.get(), kSelectorGroupId, selector_group_id);
}

::util::Status TableData::GetSelectorGroupId(uint64* selector_group_id) const {
  return GetField(*(table_data_.get()), kSelectorGroupId, selector_group_id);
}

::util::Status TableData::SetMeterConfig(bool in_pps, uint64 cir, uint64 cburst,
                                         uint64 pir, uint64 pburst) {
  if (in_pps) {
    RETURN_IF_ERROR(SetField(table_data_.get(), kMeterCirPps, cir));
    RETURN_IF_ERROR(
        SetField(table_data_.get(), kMeterCommitedBurstPackets, cburst));
    RETURN_IF_ERROR(SetField(table_data_.get(), kMeterPirPps, pir));
    RETURN_IF_ERROR(SetField(table_data_.get(), kMeterPeakBurstPackets, pburst));
  } else {
    RETURN_IF_ERROR(
        SetField(table_data_.get(), kMeterCirKbps, BytesPerSecondToKbits(cir)));
    RETURN_IF_ERROR(SetField(table_data_.get(), kMeterCommitedBurstKbits,
                             BytesPerSecondToKbits(cburst)));
    RETURN_IF_ERROR(
        SetField(table_data_.get(), kMeterPirKbps, BytesPerSecondToKbits(pir)));
    RETURN_IF_ERROR(SetField(table_data_.get(), kMeterPeakBurstKbits,
                             BytesPerSecondToKbits(pburst)));
  }
  return ::util::OkStatus();
}

::util::Status TableData::GetMeterConfig(bool in_pps, uint64* cir,
                                         uint64* cburst, uint64* pir,
                                         uint64* pburst) const {
  if (in_pps) {
    RETURN_IF_ERROR(GetField(*(table_data_.get()), kMeterCirPps, cir));
    RETURN_IF_ERROR(
        GetField(*(table_data_.get()), kMeterCommitedBurstPackets, cburst));
    RETURN_IF_ERROR(GetField(*(table_data_.get()), kMeterPirPps, pir));
    RETURN_IF_ERROR(
        GetField(*(table_data_.get()), kMeterPeakBurstPackets, pburst));
  } else {
    RETURN_IF_ERROR(GetField(*(table_data_.get()), kMeterCirKbps, cir));
    RETURN_IF_ERROR(
        GetField(*(table_data_.get()), kMeterCommitedBurstKbits, cburst));
    RETURN_IF_ERROR(GetField(*(table_data_.get()), kMeterPirKbps, pir));
    RETURN_IF_ERROR(
        GetField(*(table_data_.get()), kMeterPeakBurstKbits, pburst));
    *cir = KbitsToBytesPerSecond(*cir);
    *cburst = KbitsToBytesPerSecond(*cburst);
    *pir = KbitsToBytesPerSecond(*pir);
    *pburst = KbitsToBytesPerSecond(*pburst);
  }
  return ::util::OkStatus();
}

// The P4Runtime `CounterData` message has no mechanism to differentiate between
// byte-only, packet-only or both counter types. This make it impossible to
// recognize a counter reset (set, e.g., bytes to zero) request from a set
// request for a packet-only counter. Therefore we have to be careful when
// making set calls for those fields against the SDE.

::util::Status TableData::SetCounterData(uint64 bytes, uint64 packets) {
  std::vector<tdi_id_t> data_field_ids;
  tdi_id_t field_id_bytes = 0;
  tdi_id_t field_id_packets = 0;
  const ::tdi::DataFieldInfo *dataFieldInfoPackets;
  const ::tdi::DataFieldInfo *dataFieldInfoBytes;
  const ::tdi::Table* table;
  RETURN_IF_TDI_ERROR(table_data_->getParent(&table));

  tdi_id_t action_id = table_data_->actionIdGet();
  dataFieldInfoPackets = table->tableInfoGet()->dataFieldGet(kCounterPackets, action_id);
  dataFieldInfoBytes = table->tableInfoGet()->dataFieldGet(kCounterBytes, action_id);
  RETURN_IF_NULL(dataFieldInfoPackets);
  RETURN_IF_NULL(dataFieldInfoBytes);
  field_id_packets = dataFieldInfoPackets->idGet();
  field_id_bytes = dataFieldInfoBytes->idGet();

  RETURN_IF_TDI_ERROR(table_data_->setValue(field_id_bytes, bytes));
  RETURN_IF_TDI_ERROR(table_data_->setValue(field_id_packets, packets));

  return ::util::OkStatus();
}

::util::Status TableData::GetCounterData(uint64* bytes, uint64* packets) const {
  CHECK_RETURN_IF_FALSE(bytes);
  CHECK_RETURN_IF_FALSE(packets);
  tdi_id_t field_id_bytes = 0;
  tdi_id_t field_id_packets = 0;
  const ::tdi::DataFieldInfo *dataFieldInfoPackets;
  const ::tdi::DataFieldInfo *dataFieldInfoBytes;
  const ::tdi::Table* table;
  RETURN_IF_TDI_ERROR(table_data_->getParent(&table));

  tdi_id_t action_id = table_data_->actionIdGet();
  dataFieldInfoPackets = table->tableInfoGet()->dataFieldGet(kCounterPackets, action_id);
  dataFieldInfoBytes = table->tableInfoGet()->dataFieldGet(kCounterBytes, action_id);
  RETURN_IF_NULL(dataFieldInfoPackets);
  RETURN_IF_NULL(dataFieldInfoBytes);
  field_id_packets = dataFieldInfoPackets->idGet();
  field_id_bytes = dataFieldInfoBytes->idGet();

  // Clear values in case we set only one of them later.
  *bytes = 0;
  *packets = 0;

  RETURN_IF_TDI_ERROR(table_data_->getValue(field_id_bytes, bytes));
  RETURN_IF_TDI_ERROR(table_data_->getValue(field_id_packets, packets));

  return ::util::OkStatus();
}

::util::Status TableData::GetActionId(int* action_id) const {
  CHECK_RETURN_IF_FALSE(action_id);
  const ::tdi::Table* table;
  RETURN_IF_TDI_ERROR(table_data_->getParent(&table));
  *action_id = table_data_->actionIdGet();
  return ::util::OkStatus();
}

::util::Status TableData::Reset(int action_id) {
  const ::tdi::Table* table;
  RETURN_IF_TDI_ERROR(table_data_->getParent(&table));
  if (action_id) {
    RETURN_IF_TDI_ERROR(table->dataReset(action_id, table_data_.get()));
  } else {
    RETURN_IF_TDI_ERROR(table->dataReset(table_data_.get()));
  }

  return ::util::OkStatus();
}

::util::StatusOr<std::unique_ptr<TdiSdeInterface::TableDataInterface>>
TableData::CreateTableData(const ::tdi::TdiInfo* tdi_info, int table_id,
                           int action_id) {
  const ::tdi::Table* table;
  RETURN_IF_TDI_ERROR(tdi_info->tableFromIdGet(table_id, &table));
  std::unique_ptr<::tdi::TableData> table_data;
  if (action_id) {
    RETURN_IF_TDI_ERROR(table->dataAllocate(action_id, &table_data));
  } else {
    RETURN_IF_TDI_ERROR(table->dataAllocate(&table_data));
  }
  auto data = std::unique_ptr<TdiSdeInterface::TableDataInterface>(
      new TableData(std::move(table_data)));
  return data;
}

}  // namespace tdi
}  // namespace hal
}  // namespace stratum
