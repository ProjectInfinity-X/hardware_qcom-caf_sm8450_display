/*
 * Copyright (c) 2014-2021, The Linux Foundation. All rights reserved.
 * Not a Contribution.
 *
 * Copyright 2015 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * Changes from Qualcomm Innovation Center are provided under the following license:
 *
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef __HWC_DISPLAY_H__
#define __HWC_DISPLAY_H__

#include <QService.h>
#include <android/hardware/graphics/common/1.2/types.h>
#include <core/core_interface.h>
#include <hardware/hwcomposer.h>
#include <private/color_params.h>
#include <sys/stat.h>
#include <algorithm>
#include <bitset>
#include <map>
#include <queue>
#include <set>
#include <string>
#include <utility>
#include <vector>
#include "histogram_collector.h"
#include "hwc_buffer_allocator.h"
#include "hwc_callbacks.h"
#include "hwc_display_event_handler.h"
#include "hwc_layers.h"
#include "hwc_buffer_sync_handler.h"
#include <vendor/qti/hardware/display/composer/3.1/IQtiComposerClient.h>

using android::hardware::graphics::common::V1_2::ColorMode;
using android::hardware::graphics::common::V1_2::Dataspace;
using android::hardware::graphics::common::V1_1::RenderIntent;
using android::hardware::graphics::common::V1_2::Hdr;
namespace composer_V2_4 = ::android::hardware::graphics::composer::V2_4;
using HwcAttribute = composer_V2_4::IComposerClient::Attribute;
using VsyncPeriodChangeConstraints = composer_V2_4::IComposerClient::VsyncPeriodChangeConstraints;
using VsyncPeriodChangeTimeline = composer_V2_4::VsyncPeriodChangeTimeline;
using VsyncPeriodNanos = composer_V2_4::VsyncPeriodNanos;
using ClientTargetProperty = composer_V2_4::IComposerClient::ClientTargetProperty;

namespace sdm {

class HWCToneMapper;

// Subclasses set this to their type. This has to be different from DisplayType.
// This is to avoid RTTI and dynamic_cast
enum DisplayClass {
  DISPLAY_CLASS_BUILTIN,
  DISPLAY_CLASS_PLUGGABLE,
  DISPLAY_CLASS_VIRTUAL,
  DISPLAY_CLASS_NULL
};

enum {
  INPUT_LAYER_DUMP,
  OUTPUT_LAYER_DUMP,
};

enum SecureSessionType {
  kSecureDisplay,
  kSecureCamera,
  kSecureTUI,
  kSecureMax,
};

// CWB client currently using the block
enum CWBClient {
  kCWBClientNone,       // No client connected
  kCWBClientFrameDump,  // Dump to file
  kCWBClientColor,      // Internal client i.e. Color Manager
  kCWBClientExternal,   // External client calling through private APIs
  kCWBClientComposer,   // Client to HWC i.e. SurfaceFlinger
};

enum CWBStatus {
  kCWBAvailable,     // Available to accept new CWB request
  kCWBConfigure,     // CWB is Configured in the current frame
  kCWBTeardown,      // CWB tear down in the current frame. Frame's Retire fence would be cached.
                     // New CWB requests coming in are rejected until this retire fence signals.
  kCWBPostTeardown,  // CWB teardown done in previous frame.
};

struct CwbState {
  hwc2_display_t cwb_disp_id = -1;                          // display id on which cwb is either
                                                            // requested, active or tearing down.
  CWBClient cwb_client = kCWBClientNone;                    // the client actively performing cwb.
  CWBStatus cwb_status = CWBStatus::kCWBAvailable;          // current cwb statuss
  shared_ptr<Fence> teardown_frame_retire_fence = nullptr;  // cache cwb disable frame retire fence
                                                            // to reject requests until it signals.
};

struct TransientRefreshRateInfo {
  uint32_t transient_vsync_period;
  int64_t vsync_applied_time;
};

class HWCColorMode {
 public:
  HWCColorMode(){};
  explicit HWCColorMode(DisplayInterface *display_intf);
  virtual ~HWCColorMode() {}
  virtual HWC2::Error Init();
  virtual HWC2::Error DeInit();
  virtual void Dump(std::ostringstream *os);
  virtual uint32_t GetColorModeCount();
  virtual uint32_t GetRenderIntentCount(ColorMode mode);
  virtual HWC2::Error GetColorModes(uint32_t *out_num_modes, ColorMode *out_modes);
  virtual HWC2::Error GetRenderIntents(ColorMode mode, uint32_t *out_num_intents,
                                       RenderIntent *out_modes);
  HWC2::Error SetColorModeWithRenderIntent(ColorMode mode, RenderIntent intent);
  HWC2::Error SetColorModeById(int32_t color_mode_id);
  HWC2::Error SetColorModeFromClientApi(std::string mode_string);
  virtual HWC2::Error SetColorTransform(const float *matrix, android_color_transform_t hint);
  virtual HWC2::Error RestoreColorTransform();
  virtual ColorMode GetCurrentColorMode() { return current_color_mode_; }
  virtual RenderIntent GetCurrentRenderIntent() { return current_render_intent_; }
  virtual HWC2::Error ApplyCurrentColorModeWithRenderIntent(bool hdr_present);
  virtual HWC2::Error CacheColorModeWithRenderIntent(ColorMode mode, RenderIntent intent);
  void ReapplyMode() { apply_mode_ = true; };
  virtual HWC2::Error NotifyDisplayCalibrationMode(bool in_calibration) {
    return HWC2::Error::Unsupported;
  }

 protected:
  template <class T>
  void CopyColorTransformMatrix(const T *input_matrix, double *output_matrix) {
    for (uint32_t i = 0; i < kColorTransformMatrixCount; i++) {
      output_matrix[i] = static_cast<double>(input_matrix[i]);
    }
  }

  static const uint32_t kColorTransformMatrixCount = 16;
  DisplayInterface *display_intf_ = NULL;
  bool apply_mode_ = false;
  ColorMode current_color_mode_ = ColorMode::NATIVE;
  RenderIntent current_render_intent_ = RenderIntent::COLORIMETRIC;
  DynamicRangeType curr_dynamic_range_ = kSdrType;

  double color_matrix_[kColorTransformMatrixCount] = { 1.0, 0.0, 0.0, 0.0, \
                                                       0.0, 1.0, 0.0, 0.0, \
                                                       0.0, 0.0, 1.0, 0.0, \
                                                       0.0, 0.0, 0.0, 1.0 };

 private:
  void PopulateColorModes();
  HWC2::Error ValidateColorModeWithRenderIntent(ColorMode mode, RenderIntent intent);
  HWC2::Error SetPreferredColorModeInternal(const std::string &mode_string, bool from_client,
                                            ColorMode *color_mode, DynamicRangeType *dynamic_range);

  typedef std::map<DynamicRangeType, std::string> DynamicRangeMap;
  typedef std::map<RenderIntent, DynamicRangeMap> RenderIntentMap;
  // Initialize supported mode/render intent/dynamic range combination
  std::map<ColorMode, RenderIntentMap> color_mode_map_ = {};
  std::map<ColorMode, DynamicRangeMap> preferred_mode_ = {};
};

class HWCDisplay : public DisplayEventHandler {
 public:
  enum DisplayStatus {
    kDisplayStatusInvalid = -1,
    kDisplayStatusOffline,
    kDisplayStatusOnline,
    kDisplayStatusPause,       // Pause + PowerOff
    kDisplayStatusResume,      // Resume + PowerOn
  };

  struct HWCLayerStack {
    HWCLayer *client_target = nullptr;                   // Also known as framebuffer target
    std::map<hwc2_layer_t, HWCLayer *> layer_map;        // Look up by Id - TODO
    std::multiset<HWCLayer *, SortLayersByZ> layer_set;  // Maintain a set sorted by Z
  };

  virtual ~HWCDisplay() {}
  virtual int Init();
  virtual int Deinit();

  // Framebuffer configurations
  virtual void SetIdleTimeoutMs(uint32_t timeout_ms, uint32_t inactive_ms);
  virtual HWC2::Error SetFrameDumpConfig(uint32_t count, uint32_t bit_mask_layer_type,
                                         int32_t format);
  virtual HWC2::Error SetFrameDumpConfig(uint32_t count, uint32_t bit_mask_layer_type,
                                         int32_t format, CwbConfig &cwb_config);
  virtual DisplayError SetMaxMixerStages(uint32_t max_mixer_stages);
  virtual DisplayError ControlPartialUpdate(bool enable, uint32_t *pending) {
    return kErrorNotSupported;
  }
  virtual HWC2::PowerMode GetCurrentPowerMode();
  virtual int SetFrameBufferResolution(uint32_t x_pixels, uint32_t y_pixels);
  virtual void GetFrameBufferResolution(uint32_t *x_pixels, uint32_t *y_pixels);
  virtual int SetDisplayStatus(DisplayStatus display_status);
  virtual int OnMinHdcpEncryptionLevelChange(uint32_t min_enc_level);
  virtual int Perform(uint32_t operation, ...);
  virtual int HandleSecureSession(const std::bitset<kSecureMax> &secure_sessions,
                                  bool *power_on_pending, bool is_active_secure_display);
  virtual DisplayError HandleSecureEvent(SecureEvent secure_event, bool *needs_refresh,
                                         bool update_event_only);
  virtual DisplayError PostHandleSecureEvent(SecureEvent secure_event);
  virtual int GetActiveSecureSession(std::bitset<kSecureMax> *secure_sessions) { return 0; };
  virtual DisplayError SetMixerResolution(uint32_t width, uint32_t height);
  virtual DisplayError GetMixerResolution(uint32_t *width, uint32_t *height);
  virtual void GetPanelResolution(uint32_t *width, uint32_t *height);
  virtual void GetRealPanelResolution(uint32_t *width, uint32_t *height);
  virtual void Dump(std::ostringstream *os);

  // CWB related methods
  virtual int GetCwbBufferResolution(CwbConfig *cwb_config, uint32_t *x_pixels,
                                     uint32_t *y_pixels);
  virtual HWC2::Error SetReadbackBuffer(const native_handle_t *buffer,
                                        shared_ptr<Fence> acquire_fence, CwbConfig cwb_config,
                                        CWBClient client);
  virtual HWC2::Error GetReadbackBufferFence(shared_ptr<Fence> *release_fence);
  virtual DisplayError TeardownConcurrentWriteback(bool *needs_refresh);
  // Captures frame output in the buffer specified by output_buffer_info. The API is
  // non-blocking and the client is expected to check operation status later on.
  // Returns -1 if the input is invalid.
  virtual int FrameCaptureAsync(const BufferInfo &output_buffer_info, const CwbConfig &cwb_config) {
    return -1;
  }
  // Returns the status of frame capture operation requested with FrameCaptureAsync().
  // -EAGAIN : No status obtain yet, call API again after another frame.
  // < 0 : Operation happened but failed.
  // 0 : Success.
  virtual int GetFrameCaptureStatus() { return -EAGAIN; }

  virtual DisplayError SetHWDetailedEnhancerConfig(void *params) {
    return kErrorNotSupported;
  }

  virtual HWC2::Error SetDisplayDppsAdROI(uint32_t h_start, uint32_t h_end,
                                          uint32_t v_start, uint32_t v_end,
                                          uint32_t factor_in, uint32_t factor_out) {
    return HWC2::Error::Unsupported;
  }
  virtual HWC2::Error SetFrameTriggerMode(uint32_t mode) {
    return HWC2::Error::Unsupported;
  }

  virtual bool IsSmartPanelConfig(uint32_t config_id) {
    return false;
  }

  virtual bool HasOverridenDozeMode(void) {
    return false;
  }

  virtual bool HasSmartPanelConfig(void) {
    return false;
  }

  virtual bool VsyncEnablePending() {
    return false;
  }

  // Display Configurations
  static uint32_t GetThrottlingRefreshRate() { return HWCDisplay::throttling_refresh_rate_; }
  static void SetThrottlingRefreshRate(uint32_t newRefreshRate)
              { HWCDisplay::throttling_refresh_rate_ = newRefreshRate; }
  virtual int SetNoisePlugInOverride(bool override_en, int32_t attn, int32_t noise_zpos);
  virtual int SetActiveDisplayConfig(uint32_t config);
  virtual int GetActiveDisplayConfig(uint32_t *config);
  virtual int GetDisplayConfigCount(uint32_t *count);
  virtual int GetDisplayAttributesForConfig(int config,
                                            DisplayConfigVariableInfo *display_attributes);
  virtual int GetSupportedDisplayRefreshRates(std::vector<uint32_t> *supported_refresh_rates);
  bool IsModeSwitchAllowed(uint32_t config);

  virtual DisplayError Flush() {
    return kErrorNotSupported;
  }

  uint32_t GetMaxRefreshRate() { return max_refresh_rate_; }
  int ToggleScreenUpdates(bool enable);
  int ColorSVCRequestRoute(const PPDisplayAPIPayload &in_payload, PPDisplayAPIPayload *out_payload,
                           PPPendingParams *pending_action);
  void SolidFillPrepare();
  DisplayClass GetDisplayClass();
  int GetVisibleDisplayRect(hwc_rect_t *rect);
  void BuildLayerStack(void);
  void BuildSolidFillStack(void);
  HWCLayer *GetHWCLayer(hwc2_layer_t layer_id);
  uint32_t GetGeometryChanges() { return geometry_changes_; }
  ColorMode GetCurrentColorMode() {
    return (color_mode_ ? color_mode_->GetCurrentColorMode() : ColorMode::SRGB);
  }
  RenderIntent GetCurrentRenderIntent() {
    return (color_mode_ ? color_mode_->GetCurrentRenderIntent() : RenderIntent::COLORIMETRIC);
  }
  bool HWCClientNeedsValidate() {
    return (has_client_composition_ || layer_stack_.flags.single_buffered_layer_present);
  }
  bool CheckResourceState(bool *res_exhausted);
  virtual HWC2::Error SetColorModeFromClientApi(int32_t color_mode_id) {
    return HWC2::Error::Unsupported;
  }
  bool IsFirstCommitDone() { return !first_cycle_; }
  virtual void ProcessActiveConfigChange();

  // HWC2 APIs
  virtual HWC2::Error AcceptDisplayChanges(void);
  virtual HWC2::Error GetActiveConfig(hwc2_config_t *out_config);
  virtual HWC2::Error SetActiveConfig(hwc2_config_t config);
  virtual HWC2::Error SetPanelLuminanceAttributes(float min_lum, float max_lum) {
    return HWC2::Error::Unsupported;
  }
  virtual HWC2::Error SetClientTarget(buffer_handle_t target, shared_ptr<Fence> acquire_fence,
                                      int32_t dataspace, hwc_region_t damage);
  virtual HWC2::Error SetClientTarget_3_1(buffer_handle_t target, shared_ptr<Fence> acquire_fence,
                                          int32_t dataspace, hwc_region_t damage);
  virtual HWC2::Error GetClientTarget(buffer_handle_t target, shared_ptr<Fence> acquire_fence,
                                      int32_t dataspace, hwc_region_t damage);
  virtual HWC2::Error SetColorMode(ColorMode mode) { return HWC2::Error::Unsupported; }
  virtual HWC2::Error SetColorModeWithRenderIntent(ColorMode mode, RenderIntent intent) {
    return HWC2::Error::Unsupported;
  }
  virtual HWC2::Error SetColorModeById(int32_t color_mode_id) {
    return HWC2::Error::Unsupported;
  }
  virtual HWC2::Error RestoreColorTransform() {
    return HWC2::Error::Unsupported;
  }
  virtual HWC2::Error SetColorTransform(const float *matrix, android_color_transform_t hint) {
    return HWC2::Error::Unsupported;
  }
  virtual HWC2::Error HandleColorModeTransform(android_color_mode_t mode,
                                               android_color_transform_t hint,
                                               const double *matrix) {
    return HWC2::Error::Unsupported;
  }
  virtual DisplayError SetDynamicDSIClock(uint64_t bitclk) {
    return kErrorNotSupported;
  }
  virtual DisplayError GetDynamicDSIClock(uint64_t *bitclk) {
    return kErrorNotSupported;
  }
  virtual DisplayError GetSupportedDSIClock(std::vector<uint64_t> *bitclk) {
    return kErrorNotSupported;
  }
  virtual HWC2::Error UpdateDisplayId(hwc2_display_t id) {
    return HWC2::Error::Unsupported;
  }
  virtual HWC2::Error SetPendingRefresh() {
    return HWC2::Error::Unsupported;
  }
  virtual HWC2::Error SetPanelBrightness(float brightness) {
    return HWC2::Error::Unsupported;
  }
  virtual HWC2::Error GetPanelBrightness(float *brightness) {
    return HWC2::Error::Unsupported;
  }
  virtual HWC2::Error GetPanelMaxBrightness(uint32_t *max_brightness_level) {
    return HWC2::Error::Unsupported;
  }
  virtual HWC2::Error GetDisplayConfigs(uint32_t *out_num_configs, hwc2_config_t *out_configs);
  virtual HWC2::Error GetDisplayAttribute(hwc2_config_t config, HwcAttribute attribute,
                                          int32_t *out_value);
  virtual HWC2::Error GetClientTargetSupport(uint32_t width, uint32_t height, int32_t format,
                                             int32_t dataspace);
  virtual HWC2::Error GetColorModes(uint32_t *outNumModes, ColorMode *outModes);
  virtual HWC2::Error GetRenderIntents(ColorMode mode, uint32_t *out_num_intents,
                                       RenderIntent *out_intents);
  virtual HWC2::Error GetChangedCompositionTypes(uint32_t *out_num_elements,
                                                 hwc2_layer_t *out_layers, int32_t *out_types);
  virtual HWC2::Error GetDisplayRequests(int32_t *out_display_requests, uint32_t *out_num_elements,
                                         hwc2_layer_t *out_layers, int32_t *out_layer_requests);
  virtual HWC2::Error GetDisplayName(uint32_t *out_size, char *out_name);
  virtual HWC2::Error GetDisplayType(int32_t *out_type);
  virtual HWC2::Error SetCursorPosition(hwc2_layer_t layer, int x, int y);
  virtual HWC2::Error SetVsyncEnabled(HWC2::Vsync enabled);
  virtual HWC2::Error SetPowerMode(HWC2::PowerMode mode, bool teardown);
  virtual HWC2::Error CreateLayer(hwc2_layer_t *out_layer_id);
  virtual HWC2::Error DestroyLayer(hwc2_layer_t layer_id);
  virtual HWC2::Error SetLayerZOrder(hwc2_layer_t layer_id, uint32_t z);
  virtual HWC2::Error SetLayerType(hwc2_layer_t layer_id, IQtiComposerClient::LayerType type);
  virtual HWC2::Error GetReleaseFences(uint32_t *out_num_elements, hwc2_layer_t *out_layers,
                                       std::vector<shared_ptr<Fence>> *out_fences);
  virtual HWC2::Error Present(shared_ptr<Fence> *out_retire_fence) = 0;
  virtual HWC2::Error GetHdrCapabilities(uint32_t *out_num_types, int32_t* out_types,
                                         float* out_max_luminance,
                                         float* out_max_average_luminance,
                                         float* out_min_luminance);
  virtual HWC2::Error GetPerFrameMetadataKeys(uint32_t *out_num_keys,
                                              PerFrameMetadataKey *out_keys);
  virtual HWC2::Error SetDisplayAnimating(bool animating) {
    animating_ = animating;
    return HWC2::Error::None;
  }
  virtual bool IsDisplayCommandMode();
  virtual HWC2::Error SetQSyncMode(QSyncMode qsync_mode) {
    return HWC2::Error::Unsupported;
  }
  virtual DisplayError ControlIdlePowerCollapse(bool enable, bool synchronous) {
    return kErrorNone;
  }
  virtual HWC2::Error GetDisplayIdentificationData(uint8_t *out_port, uint32_t *out_data_size,
                                                   uint8_t *out_data);
  virtual HWC2::Error SetBLScale(uint32_t level) {
    return HWC2::Error::Unsupported;
  }
  virtual void GetLayerStack(HWCLayerStack *stack);
  virtual void SetLayerStack(HWCLayerStack *stack);
  virtual void PostPowerMode();
  virtual HWC2::PowerMode GetPendingPowerMode() {
    return pending_power_mode_;
  }
  virtual void SetPendingPowerMode(HWC2::PowerMode mode) {
    pending_power_mode_ = mode;
  }
  virtual void ClearPendingPowerMode() {
    pending_power_mode_ = current_power_mode_;
  }
  virtual void NotifyClientStatus(bool connected) { client_connected_ = connected; }
  virtual int PostInit() { return 0; }

  virtual HWC2::Error SetDisplayedContentSamplingEnabledVndService(bool enabled);
  virtual HWC2::Error SetDisplayedContentSamplingEnabled(int32_t enabled, uint8_t component_mask,
                                                         uint64_t max_frames);
  virtual HWC2::Error GetDisplayedContentSamplingAttributes(int32_t *format, int32_t *dataspace,
                                                            uint8_t *supported_components);
  virtual HWC2::Error GetDisplayedContentSample(
      uint64_t max_frames, uint64_t timestamp, uint64_t *numFrames,
      int32_t samples_size[NUM_HISTOGRAM_COLOR_COMPONENTS],
      uint64_t *samples[NUM_HISTOGRAM_COLOR_COMPONENTS]);

  virtual HWC2::Error GetDisplayVsyncPeriod(VsyncPeriodNanos *vsync_period);
  virtual HWC2::Error SetActiveConfigWithConstraints(
      hwc2_config_t config, const VsyncPeriodChangeConstraints *vsync_period_change_constraints,
      VsyncPeriodChangeTimeline *out_timeline);

  HWC2::Error SetDisplayElapseTime(uint64_t time);
  virtual bool IsDisplayIdle() { return false; };
  virtual bool HasReadBackBufferSupport() { return false; }
  virtual HWC2::Error NotifyDisplayCalibrationMode(bool in_calibration) {
    return HWC2::Error::Unsupported;
  };
  virtual HWC2::Error CommitOrPrepare(bool validate_only, shared_ptr<Fence> *out_retire_fence,
                                      uint32_t *out_num_types, uint32_t *out_num_requests,
                                      bool *needs_commit);
  virtual HWC2::Error PreValidateDisplay(bool *exit_validate) { return HWC2::Error::None; }
  HWC2::Error TryDrawMethod(IQtiComposerClient::DrawMethod client_drawMethod);
  virtual HWC2::Error SetAlternateDisplayConfig(bool set) {
    return HWC2::Error::Unsupported;
  }
  virtual void IsMultiDisplay(bool is_multi_display) {
    is_multi_display_ = is_multi_display;
  }
  virtual HWC2::Error SetDimmingEnable(int int_enabled) {
    return HWC2::Error::Unsupported;
  }
  virtual HWC2::Error SetDimmingMinBl(int min_bl) {
    return HWC2::Error::Unsupported;
  }
  virtual HWC2::Error GetClientTargetProperty(ClientTargetProperty *out_client_target_property);
  virtual void GetConfigInfo(std::map<uint32_t, DisplayConfigVariableInfo> *variable_config_map,
                             int *active_config_index, uint32_t *num_configs);
  virtual void SetConfigInfo(std::map<uint32_t, DisplayConfigVariableInfo>& variable_config_map,
                             int active_config_index, uint32_t num_configs) {};
  virtual void MarkClientActive(bool is_client_up);
  virtual void Abort();

 protected:
  static uint32_t throttling_refresh_rate_;
  // Maximum number of layers supported by display manager.
  static const uint32_t kMaxLayerCount = 32;
  static bool mmrm_restricted_;
  HWCDisplay(CoreInterface *core_intf, BufferAllocator *buffer_allocator, HWCCallbacks *callbacks,
             HWCDisplayEventHandler *event_handler, qService::QService *qservice, DisplayType type,
             hwc2_display_t id, int32_t sdm_id, DisplayClass display_class);

  // DisplayEventHandler methods
  virtual DisplayError VSync(const DisplayEventVSync &vsync);
  virtual DisplayError Refresh();
  virtual DisplayError CECMessage(char *message);
  virtual DisplayError HistogramEvent(int source_fd, uint32_t blob_id);
  virtual DisplayError HandleEvent(DisplayEvent event);
  virtual DisplayError HandleQsyncState(const QsyncEventData &qsync_data);
  virtual DisplayError NotifyFpsMitigation(const float fps, DisplayConcurrencyType concurrency,
                                           bool concurrency_begin);
  virtual void DumpOutputBuffer(const BufferInfo &buffer_info, void *base,
                                shared_ptr<Fence> &retire_fence);
  virtual HWC2::Error PrepareLayerStack(uint32_t *out_num_types, uint32_t *out_num_requests);
  virtual HWC2::Error CommitLayerStack(void);
  virtual HWC2::Error PostCommitLayerStack(shared_ptr<Fence> *out_retire_fence);
  virtual DisplayError DisablePartialUpdateOneFrame() {
    return kErrorNotSupported;
  }
  virtual void ReqPerfHintRelease() { return; }
  const char *GetDisplayString();
  void MarkLayersForGPUBypass(void);
  void MarkLayersForClientComposition(void);
  void UpdateConfigs();
  virtual void ApplyScanAdjustment(hwc_rect_t *display_frame);
  bool IsLayerUpdating(HWCLayer *layer);
  uint32_t SanitizeRefreshRate(uint32_t req_refresh_rate);
  virtual void GetUnderScanConfig() { }
  int32_t SetClientTargetDataSpace(int32_t dataspace);
  int SetFrameBufferConfig(uint32_t x_pixels, uint32_t y_pixels);
  int32_t GetDisplayConfigGroup(DisplayConfigGroupInfo variable_config);
  HWC2::Error GetVsyncPeriodByActiveConfig(VsyncPeriodNanos *vsync_period);
  bool GetTransientVsyncPeriod(VsyncPeriodNanos *vsync_period);
  std::tuple<int64_t, int64_t> RequestActiveConfigChange(hwc2_config_t config,
                                                         VsyncPeriodNanos current_vsync_period,
                                                         int64_t desired_time);
  std::tuple<int64_t, int64_t> EstimateVsyncPeriodChangeTimeline(
      VsyncPeriodNanos current_vsync_period, int64_t desired_time);
  void SubmitActiveConfigChange(VsyncPeriodNanos current_vsync_period);
  bool IsActiveConfigReadyToSubmit(int64_t time);
  bool IsActiveConfigApplied(int64_t time, int64_t vsync_applied_time);
  bool IsSameGroup(hwc2_config_t config_id1, hwc2_config_t config_id2);
  bool AllowSeamless(hwc2_config_t request_config);
  void SetVsyncsApplyRateChange(uint32_t vsyncs) { vsyncs_to_apply_rate_change_ = vsyncs; }
  HWC2::Error SubmitDisplayConfig(hwc2_config_t config);
  HWC2::Error GetCachedActiveConfig(hwc2_config_t *config);
  void SetActiveConfigIndex(int active_config_index);
  HWC2::Error PostPrepareLayerStack(uint32_t *out_num_types, uint32_t *out_num_requests);
  HWC2::Error HandlePrepareError(DisplayError error);
  int GetActiveConfigIndex();
  DisplayError ValidateTUITransition (SecureEvent secure_event);
  void MMRMEvent(bool restricted);
  void UpdateRefreshRate();
  void UpdateActiveConfig();
  void DumpInputBuffers(void);
  void RetrieveFences(shared_ptr<Fence> *out_retire_fence);
  void SetDrawMethod();

  // CWB related methods
  void SetCwbState();
  void ResetCwbState();
  void HandleFrameOutput();
  void HandleFrameDump();
  virtual void HandleFrameCapture(){};

  bool layer_stack_invalid_ = true;
  CoreInterface *core_intf_ = nullptr;
  HWCBufferAllocator *buffer_allocator_ = NULL;
  HWCCallbacks *callbacks_  = nullptr;
  HWCDisplayEventHandler *event_handler_ = nullptr;
  DisplayType type_ = kDisplayTypeMax;
  hwc2_display_t id_ = UINT64_MAX;
  int32_t sdm_id_ = -1;
  DisplayInterface *display_intf_ = NULL;
  LayerStack layer_stack_;
  HWCLayer *client_target_ = nullptr;                   // Also known as framebuffer target
  std::map<hwc2_layer_t, HWCLayer *> layer_map_;        // Look up by Id - TODO
  std::multiset<HWCLayer *, SortLayersByZ> layer_set_;  // Maintain a set sorted by Z
  std::map<hwc2_layer_t, HWC2::Composition> layer_changes_;
  std::map<hwc2_layer_t, HWC2::LayerRequest> layer_requests_;
  bool flush_on_error_ = false;
  bool flush_ = false;
  HWC2::PowerMode current_power_mode_ = HWC2::PowerMode::Off;
  HWC2::PowerMode pending_power_mode_ = HWC2::PowerMode::Off;
  bool swap_interval_zero_ = false;
  bool display_paused_ = false;
  uint32_t min_refresh_rate_ = 0;
  uint32_t max_refresh_rate_ = 0;
  uint32_t qsync_fps_ = 0;
  uint32_t current_refresh_rate_ = 0;
  bool use_metadata_refresh_rate_ = false;
  bool boot_animation_completed_ = false;
  bool shutdown_pending_ = false;
  std::bitset<kSecureMax> active_secure_sessions_ = 0;
  bool solid_fill_enable_ = false;
  Layer *solid_fill_layer_ = NULL;
  LayerRect solid_fill_rect_ = {};
  LayerSolidFill solid_fill_color_ = {};
  LayerRect display_rect_;
  bool color_tranform_failed_ = false;
  HWCColorMode *color_mode_ = NULL;
  HWCToneMapper *tone_mapper_ = nullptr;
  uint32_t num_configs_ = 0;
  int disable_hdr_handling_ = 0;  // disables HDR handling.
  int disable_sdr_histogram_ = 0;  // disables handling of SDR histogram data.
  bool pending_commit_ = false;
  bool is_cmd_mode_ = false;
  bool partial_update_enabled_ = false;
  bool skip_commit_ = false;
  std::map<uint32_t, DisplayConfigVariableInfo> variable_config_map_;
  std::vector<uint32_t> hwc_config_map_;
  bool client_connected_ = true;
  bool pending_config_ = false;
  bool has_client_composition_ = false;
  LayerRect window_rect_ = {};
  bool windowed_display_ = false;
  uint32_t vsyncs_to_apply_rate_change_ = 1;
  hwc2_config_t pending_refresh_rate_config_ = UINT_MAX;
  int64_t pending_refresh_rate_refresh_time_ = INT64_MAX;
  int64_t pending_refresh_rate_applied_time_ = INT64_MAX;
  std::deque<TransientRefreshRateInfo> transient_refresh_rate_info_;
  std::mutex transient_refresh_rate_lock_;
  std::mutex active_config_lock_;
  int active_config_index_ = -1;
  uint32_t active_refresh_rate_ = 0;
  SecureEvent secure_event_ = kSecureEventMax;
  bool display_pause_pending_ = false;
  bool display_idle_ = false;
  bool animating_ = false;
  DisplayDrawMethod draw_method_ = kDrawDefault;
  uint32_t fb_width_ = 0;
  uint32_t fb_height_ = 0;
  bool bypass_drawcycle_ = false;

  // CWB state & configuration
  CwbConfig cwb_config_ = {};
  static CwbState cwb_state_;
  static std::mutex cwb_state_lock_;  // cwb state lock. Set before accesing or updating cwb_state_

  // Readback buffer configuration
  LayerBuffer output_buffer_ = {};
  bool readback_buffer_queued_ = false;
  bool readback_configured_ = false;

  // Members for N frame dump to file
  bool dump_output_to_file_ = false;
  uint32_t dump_frame_count_ = 0;
  uint32_t dump_frame_index_ = 0;
  bool dump_input_layers_ = false;
  BufferInfo output_buffer_info_ = {};
  void *output_buffer_base_ = nullptr;  // points to base address of output_buffer_info_
  bool dump_pending_ = false;

  // Members for 1 frame capture in a client provided buffer
  bool frame_capture_buffer_queued_ = false;
  int frame_capture_status_ = -EAGAIN;
  uint32_t geometry_changes_ = GeometryChanges::kNone;
  bool is_multi_display_ = false;
  buffer_handle_t client_target_handle_ = 0;
  shared_ptr<Fence> client_acquire_fence_ = nullptr;
  int32_t client_dataspace_ = 0;
  hwc_region_t client_damage_region_ = {};
  bool validate_done_ = false;

 private:
  bool CanSkipSdmPrepare(uint32_t *num_types, uint32_t *num_requests);
  void WaitOnPreviousFence();
  void DumpStacktrace();
  qService::QService *qservice_ = NULL;
  DisplayClass display_class_;
  uint32_t geometry_changes_on_doze_suspend_ = GeometryChanges::kNone;
  bool first_cycle_ = true;  // false if a display commit has succeeded on the device.
  shared_ptr<Fence> release_fence_ = nullptr;
  hwc2_config_t pending_config_index_ = 0;
  bool pending_first_commit_config_ = false;
  hwc2_config_t pending_first_commit_config_index_ = 0;
  bool game_supported_ = false;
  uint64_t elapse_timestamp_ = 0;
  bool draw_method_set_ = false;
  bool client_target_3_1_set_ = false;
  bool is_client_up_ = false;
};

inline int HWCDisplay::Perform(uint32_t operation, ...) {
  return 0;
}

}  // namespace sdm

#endif  // __HWC_DISPLAY_H__
