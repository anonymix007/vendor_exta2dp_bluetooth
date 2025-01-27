/*
 * Copyright (C) 2022 The Android Open Source Project
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

#define LOG_TAG "BtAudioAIDLService"

#include <mutex>

#include <android-base/properties.h>
#include <android/binder_manager.h>
#include <android/binder_process.h>
#include <utils/Log.h>

#include "BluetoothAudioProviderFactory.h"

using ::aidl::android::hardware::bluetooth::audio::
    BluetoothAudioProviderFactory;

static std::mutex aidl_register_mutex;
static bool aidl_hal_registered = false;
static bool aidl_hal_disabled = false;

extern "C" __attribute__((visibility("default")))
binder_status_t createIBluetoothAudioProviderFactory() {
  std::lock_guard<std::mutex> guard(aidl_register_mutex);
  binder_status_t aidl_status = STATUS_OK;

  if (!aidl_hal_disabled) {
    aidl_hal_disabled = !android::base::GetBoolProperty("persist.vendor.qcom.bluetooth.aidl_hal", true);
  }

  if (aidl_hal_disabled) {
    ALOGD("%s: aidl hal is disabled", __func__);
    return aidl_status;
  }

  if (!aidl_hal_registered) {
    ALOGD("%s: Registering the AIDL service", __func__);
    auto factory = ::ndk::SharedRefBase::make<BluetoothAudioProviderFactory>();
    const std::string instance_name =
        std::string() + BluetoothAudioProviderFactory::descriptor + "/default";
    aidl_status = AServiceManager_addService(
        factory->asBinder().get(), instance_name.c_str());
    aidl_hal_registered = aidl_status == STATUS_OK;
    ALOGW_IF(aidl_status != STATUS_OK, "Could not register %s, status=%d",
             instance_name.c_str(), aidl_status);
  } else {
    ALOGD("%s: Already registered the AIDL service", __func__);
  }

  return aidl_status;
}

extern "C" __attribute__((visibility("default")))
bool isIBluetoothAudioProviderFactoryAvailable() {
  std::lock_guard<std::mutex> guard(aidl_register_mutex);
  return aidl_hal_registered;
}
