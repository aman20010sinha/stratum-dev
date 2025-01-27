// Copyright 2020-present Open Networking Foundation
// Copyright 2022 Intel Corporation
// SPDX-License-Identifier: Apache-2.0

#ifndef STRATUM_HAL_LIB_TDI_DPDK_DPDK_SWITCH_H_
#define STRATUM_HAL_LIB_TDI_DPDK_DPDK_SWITCH_H_

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "p4/v1/p4runtime.pb.h"
#include "stratum/hal/lib/common/common.pb.h"
#include "stratum/hal/lib/common/switch_interface.h"

namespace stratum {
namespace hal {
namespace tdi {

class DpdkChassisManager;
class TdiSdeInterface;
class TdiNode;

class PortParamInterface {
public:
    virtual ~PortParamInterface() {}

    virtual bool IsPortParamSet(
        uint64 node_id, uint32 port_id,
        SetRequest::Request::Port::ValueCase value_case) = 0;

    virtual ::util::Status SetPortParam(
        uint64 node_id, uint32 port_id,
        const SingletonPort& singleton_port,
        SetRequest::Request::Port::ValueCase value_case) = 0;

    virtual ::util::Status SetHotplugParam(
        uint64 node_id, uint32 port_id, const SingletonPort& singleton_port,
        DpdkHotplugParam param_type) = 0;
};

class DpdkSwitch : virtual public SwitchInterface,
                   virtual public PortParamInterface {
 public:
  ~DpdkSwitch() override;

  // SwitchInterface public methods.
  ::util::Status PushChassisConfig(const ChassisConfig& config) override
      LOCKS_EXCLUDED(chassis_lock);
  ::util::Status VerifyChassisConfig(const ChassisConfig& config) override
      LOCKS_EXCLUDED(chassis_lock);
  ::util::Status PushForwardingPipelineConfig(
      uint64 node_id, const ::p4::v1::ForwardingPipelineConfig& config) override
      LOCKS_EXCLUDED(chassis_lock);
  ::util::Status SaveForwardingPipelineConfig(
      uint64 node_id, const ::p4::v1::ForwardingPipelineConfig& config) override
      LOCKS_EXCLUDED(chassis_lock);
  ::util::Status CommitForwardingPipelineConfig(uint64 node_id) override
      LOCKS_EXCLUDED(chassis_lock);
  ::util::Status VerifyForwardingPipelineConfig(
      uint64 node_id, const ::p4::v1::ForwardingPipelineConfig& config) override
      LOCKS_EXCLUDED(chassis_lock);
  ::util::Status Shutdown() override LOCKS_EXCLUDED(chassis_lock);
  ::util::Status Freeze() override;
  ::util::Status Unfreeze() override;
  ::util::Status WriteForwardingEntries(const ::p4::v1::WriteRequest& req,
                                        std::vector<::util::Status>* results)
      override LOCKS_EXCLUDED(chassis_lock);
  ::util::Status ReadForwardingEntries(
      const ::p4::v1::ReadRequest& req,
      WriterInterface<::p4::v1::ReadResponse>* writer,
      std::vector<::util::Status>* details) override
      LOCKS_EXCLUDED(chassis_lock);
  ::util::Status RegisterStreamMessageResponseWriter(
      uint64 node_id,
      std::shared_ptr<WriterInterface<::p4::v1::StreamMessageResponse>> writer)
      override LOCKS_EXCLUDED(chassis_lock);
  ::util::Status UnregisterStreamMessageResponseWriter(uint64 node_id) override
      LOCKS_EXCLUDED(chassis_lock);
  ::util::Status HandleStreamMessageRequest(
      uint64 node_id, const ::p4::v1::StreamMessageRequest& request) override
      LOCKS_EXCLUDED(chassis_lock);
  ::util::Status RegisterEventNotifyWriter(
      std::shared_ptr<WriterInterface<GnmiEventPtr>> writer) override
      LOCKS_EXCLUDED(chassis_lock);
  ::util::Status UnregisterEventNotifyWriter() override
      LOCKS_EXCLUDED(chassis_lock) LOCKS_EXCLUDED(chassis_lock);
  ::util::Status RetrieveValue(uint64 node_id, const DataRequest& requests,
                               WriterInterface<DataResponse>* writer,
                               std::vector<::util::Status>* details) override
      LOCKS_EXCLUDED(chassis_lock);
  ::util::Status SetValue(uint64 node_id, const SetRequest& request,
                          std::vector<::util::Status>* details) override
      LOCKS_EXCLUDED(chassis_lock);
  ::util::StatusOr<std::vector<std::string>> VerifyState() override;

  // Determines whether the specified port configuration parameter
  // has already been set.
  bool IsPortParamSet(
      uint64 node_id, uint32 port_id,
      SetRequest::Request::Port::ValueCase value_case) override;

  // Sets the value of a port configuration parameter.
  ::util::Status SetPortParam(
      uint64 node_id, uint32 port_id, const SingletonPort& singleton_port,
      SetRequest::Request::Port::ValueCase value_case) override;

  // Sets the value of a hotplug configuration parameter.
  ::util::Status SetHotplugParam(
      uint64 node_id, uint32 port_id, const SingletonPort& singleton_port,
      DpdkHotplugParam param_type) override;

  // Factory function for creating the instance of the class.
  static std::unique_ptr<DpdkSwitch> CreateInstance(
      DpdkChassisManager* chassis_manager,
      TdiSdeInterface* sde_interface,
      const std::map<int, TdiNode*>& device_id_to_tdi_node);

  // DpdkSwitch is neither copyable nor movable.
  DpdkSwitch(const DpdkSwitch&) = delete;
  DpdkSwitch& operator=(const DpdkSwitch&) = delete;
  DpdkSwitch(DpdkSwitch&&) = delete;
  DpdkSwitch& operator=(DpdkSwitch&&) = delete;

 private:
  // Private constructor. Use CreateInstance() to create an instance of this
  // class.
  DpdkSwitch(DpdkChassisManager* chassis_manager,
             TdiSdeInterface* sde_interface,
             const std::map<int, TdiNode*>& device_id_to_tdi_node);

  // Helper to get TdiNode pointer from device_id number or return error
  // indicating invalid device_id.
  ::util::StatusOr<TdiNode*> GetTdiNodeFromDeviceId(int device_id) const;

  // Helper to get TdiNode pointer from node id or return error indicating
  // invalid/unknown/uninitialized node.
  ::util::StatusOr<TdiNode*> GetTdiNodeFromNodeId(uint64 node_id) const;

  // Pointer to a TdiSdeInterface implementation that wraps TDI API calls.
  TdiSdeInterface* sde_interface_;  // not owned by this class.

  // Per chassis Managers. Note that there is only one instance of this class
  // per chassis.
  DpdkChassisManager* chassis_manager_;  // not owned by the class.

  // Map from zero-based device_id number corresponding to a node/ASIC to a
  // pointer to TdiNode which contains all the per-node managers for that
  // node/ASIC. This map is initialized in the constructor and will not change
  // during the lifetime of the class.
  // TODO(max): Does this need to be protected by chassis_lock?
  const std::map<int, TdiNode*> device_id_to_tdi_node_;  // pointers not owned

  // Map from the node ids to to a pointer to TdiNode which contain all the
  // per-node managers for that node/ASIC. Created whenever a config is pushed.
  // At any point in time, this map will contain as keys the ids of the nodes
  // that had a successful config push.
  // TODO(max): Does this need to be protected by chassis_lock?
  std::map<uint64, TdiNode*> node_id_to_tdi_node_;  //  pointers not owned
};

}  // namespace tdi
}  // namespace hal
}  // namespace stratum

#endif  // STRATUM_HAL_LIB_TDI_DPDK_DPDK_SWITCH_H_
