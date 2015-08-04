/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/apu/audio_system.h"

#include "xenia/apu/apu_flags.h"
#include "xenia/apu/audio_driver.h"
#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/base/ring_buffer.h"
#include "xenia/base/string_buffer.h"
#include "xenia/base/threading.h"
#include "xenia/cpu/processor.h"
#include "xenia/cpu/thread_state.h"
#include "xenia/emulator.h"
#include "xenia/kernel/objects/xthread.h"
#include "xenia/profiling.h"

#include "xenia/apu/nop/nop_audio_system.h"
#if XE_PLATFORM_WIN32
#include "xenia/apu/xaudio2/xaudio2_audio_system.h"
#endif  // XE_PLATFORM_WIN32

// As with normal Microsoft, there are like twelve different ways to access
// the audio APIs. Early games use XMA*() methods almost exclusively to touch
// decoders. Later games use XAudio*() and direct memory writes to the XMA
// structures (as opposed to the XMA* calls), meaning that we have to support
// both.
//
// For ease of implementation, most audio related processing is handled in
// AudioSystem, and the functions here call off to it.
// The XMA*() functions just manipulate the audio system in the guest context
// and let the normal AudioSystem handling take it, to prevent duplicate
// implementations. They can be found in xboxkrnl_audio_xma.cc

namespace xe {
namespace apu {

std::unique_ptr<AudioSystem> AudioSystem::Create(Emulator* emulator) {
  if (FLAGS_apu.compare("nop") == 0) {
    return nop::NopAudioSystem::Create(emulator);
#if XE_PLATFORM_WIN32
  } else if (FLAGS_apu.compare("xaudio2") == 0) {
    return xaudio2::XAudio2AudioSystem::Create(emulator);
#endif  // WIN32
  } else {
    // Create best available.
    std::unique_ptr<AudioSystem> best;

#if XE_PLATFORM_WIN32
    best = xaudio2::XAudio2AudioSystem::Create(emulator);
    if (best) {
      return best;
    }
#endif  // XE_PLATFORM_WIN32

    // Fallback to nop.
    return nop::NopAudioSystem::Create(emulator);
  }
}

AudioSystem::AudioSystem(Emulator* emulator)
    : emulator_(emulator), memory_(emulator->memory()), worker_running_(false) {
  std::memset(clients_, 0, sizeof(clients_));
  for (size_t i = 0; i < kMaximumClientCount; ++i) {
    unused_clients_.push(i);
  }
  for (size_t i = 0; i < kMaximumClientCount; ++i) {
    client_semaphores_[i] =
        xe::threading::Semaphore::Create(0, kMaximumQueuedFrames);
    wait_handles_[i] = client_semaphores_[i].get();
  }
  shutdown_event_ = xe::threading::Event::CreateManualResetEvent(false);
  wait_handles_[kMaximumClientCount] = shutdown_event_.get();
}

AudioSystem::~AudioSystem() = default;

X_STATUS AudioSystem::Setup() {
  processor_ = emulator_->processor();

  worker_running_ = true;
  worker_thread_ =
      kernel::object_ref<kernel::XHostThread>(new kernel::XHostThread(
          emulator()->kernel_state(), 128 * 1024, 0, [this]() {
            WorkerThreadMain();
            return 0;
          }));
  worker_thread_->set_name("Audio Worker");
  worker_thread_->Create();

  return X_STATUS_SUCCESS;
}

void AudioSystem::WorkerThreadMain() {
  // Initialize driver and ringbuffer.
  Initialize();

  auto processor = emulator_->processor();

  // Main run loop.
  while (worker_running_) {
    auto result =
        xe::threading::WaitAny(wait_handles_, xe::countof(wait_handles_), true);
    if (result.first == xe::threading::WaitResult::kFailed ||
        (result.first == xe::threading::WaitResult::kSuccess &&
         result.second == kMaximumClientCount)) {
      continue;
    }

    size_t pumped = 0;
    if (result.first == xe::threading::WaitResult::kSuccess) {
      size_t index = result.second;
      do {
        lock_.lock();
        uint32_t client_callback = clients_[index].callback;
        uint32_t client_callback_arg = clients_[index].wrapped_callback_arg;
        lock_.unlock();

        if (client_callback) {
          SCOPE_profile_cpu_i("apu", "xe::apu::AudioSystem->client_callback");
          uint64_t args[] = {client_callback_arg};
          processor->Execute(worker_thread_->thread_state(), client_callback,
                             args, xe::countof(args));
        }
        pumped++;
        index++;
      } while (index < kMaximumClientCount &&
               xe::threading::Wait(client_semaphores_[index].get(), false,
                                   std::chrono::milliseconds(0)) ==
                   xe::threading::WaitResult::kSuccess);
    }

    if (!worker_running_) {
      break;
    }

    if (!pumped) {
      SCOPE_profile_cpu_i("apu", "Sleep");
      xe::threading::Sleep(std::chrono::milliseconds::duration(500));
    }
  }
  worker_running_ = false;

  // TODO(benvanik): call module API to kill?
}

void AudioSystem::Initialize() {}

void AudioSystem::Shutdown() {
  worker_running_ = false;
  shutdown_event_->Set();
  worker_thread_->Wait(0, 0, 0, nullptr);
  worker_thread_.reset();
}

X_STATUS AudioSystem::RegisterClient(uint32_t callback, uint32_t callback_arg,
                                     size_t* out_index) {
  assert_true(unused_clients_.size());
  std::lock_guard<xe::mutex> lock(lock_);

  auto index = unused_clients_.front();

  auto client_semaphore = client_semaphores_[index].get();
  auto ret = client_semaphore->Release(kMaximumQueuedFrames, nullptr);
  assert_true(ret);

  AudioDriver* driver;
  auto result = CreateDriver(index, client_semaphore, &driver);
  if (XFAILED(result)) {
    return result;
  }
  assert_not_null(driver);

  unused_clients_.pop();

  uint32_t ptr = memory()->SystemHeapAlloc(0x4);
  xe::store_and_swap<uint32_t>(memory()->TranslateVirtual(ptr), callback_arg);

  clients_[index] = {driver, callback, callback_arg, ptr};

  if (out_index) {
    *out_index = index;
  }

  return X_STATUS_SUCCESS;
}

void AudioSystem::SubmitFrame(size_t index, uint32_t samples_ptr) {
  SCOPE_profile_cpu_f("apu");

  std::lock_guard<xe::mutex> lock(lock_);
  assert_true(index < kMaximumClientCount);
  assert_true(clients_[index].driver != NULL);
  (clients_[index].driver)->SubmitFrame(samples_ptr);
}

void AudioSystem::UnregisterClient(size_t index) {
  SCOPE_profile_cpu_f("apu");

  std::lock_guard<xe::mutex> lock(lock_);
  assert_true(index < kMaximumClientCount);
  DestroyDriver(clients_[index].driver);
  clients_[index] = {0};
  unused_clients_.push(index);

  // Drain the semaphore of its count.
  auto client_semaphore = client_semaphores_[index].get();
  xe::threading::WaitResult wait_result;
  do {
    wait_result = xe::threading::Wait(client_semaphore, false,
                                      std::chrono::milliseconds(0));
  } while (wait_result == xe::threading::WaitResult::kSuccess);
  assert_true(wait_result == xe::threading::WaitResult::kTimeout);
}

}  // namespace apu
}  // namespace xe
