/*
 * Copyright 2021 The Android Open Source Project
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

#undef LOG_TAG
#define LOG_TAG "LayerTracing"
#define ATRACE_TAG ATRACE_TAG_GRAPHICS

#include <filesystem>

#include <SurfaceFlinger.h>
#include <android-base/stringprintf.h>
#include <log/log.h>
#include <utils/SystemClock.h>
#include <utils/Trace.h>

#include "LayerTracing.h"
#include "RingBuffer.h"

namespace android {

LayerTracing::LayerTracing()
      : mBuffer(std::make_unique<RingBuffer<LayersTraceFileProto, LayersTraceProto>>()) {}

LayerTracing::~LayerTracing() = default;

bool LayerTracing::enable() {
    std::scoped_lock lock(mTraceLock);
    if (mEnabled) {
        return false;
    }
    mBuffer->setSize(mBufferSizeInBytes);
    mEnabled = true;
    return true;
}

bool LayerTracing::disable(std::string filename, bool writeToFile) {
    std::scoped_lock lock(mTraceLock);
    if (!mEnabled) {
        return false;
    }
    mEnabled = false;
    if (writeToFile) {
        LayersTraceFileProto fileProto = createTraceFileProto();
        mBuffer->writeToFile(fileProto, filename);
    }
    mBuffer->reset();
    return true;
}

void LayerTracing::appendToStream(std::ofstream& out) {
    std::scoped_lock lock(mTraceLock);
    LayersTraceFileProto fileProto = createTraceFileProto();
    mBuffer->appendToStream(fileProto, out);
    mBuffer->reset();
}

bool LayerTracing::isEnabled() const {
    std::scoped_lock lock(mTraceLock);
    return mEnabled;
}

status_t LayerTracing::writeToFile(std::string filename) {
    std::scoped_lock lock(mTraceLock);
    if (!mEnabled) {
        return STATUS_OK;
    }
    LayersTraceFileProto fileProto = createTraceFileProto();
    return mBuffer->writeToFile(fileProto, filename);
}

void LayerTracing::setTraceFlags(uint32_t flags) {
    std::scoped_lock lock(mTraceLock);
    mFlags = flags;
}

void LayerTracing::setBufferSize(size_t bufferSizeInBytes) {
    std::scoped_lock lock(mTraceLock);
    mBufferSizeInBytes = bufferSizeInBytes;
}

bool LayerTracing::flagIsSet(uint32_t flags) const {
    return (mFlags & flags) == flags;
}
uint32_t LayerTracing::getFlags() const {
    return mFlags;
}

LayersTraceFileProto LayerTracing::createTraceFileProto() {
    LayersTraceFileProto fileProto;
    fileProto.set_magic_number(uint64_t(LayersTraceFileProto_MagicNumber_MAGIC_NUMBER_H) << 32 |
                               LayersTraceFileProto_MagicNumber_MAGIC_NUMBER_L);
    auto timeOffsetNs = static_cast<std::uint64_t>(systemTime(SYSTEM_TIME_REALTIME) -
                                                   systemTime(SYSTEM_TIME_MONOTONIC));
    fileProto.set_real_to_elapsed_time_offset_nanos(timeOffsetNs);
    return fileProto;
}

void LayerTracing::dump(std::string& result) const {
    std::scoped_lock lock(mTraceLock);
    base::StringAppendF(&result, "Tracing state: %s\n", mEnabled ? "enabled" : "disabled");
    mBuffer->dump(result);
}

void LayerTracing::notify(bool visibleRegionDirty, int64_t time, int64_t vsyncId,
                          LayersProto* layers, std::string hwcDump,
                          google::protobuf::RepeatedPtrField<DisplayProto>* displays) {
    std::scoped_lock lock(mTraceLock);
    if (!mEnabled) {
        return;
    }

    if (!visibleRegionDirty && !flagIsSet(LayerTracing::TRACE_BUFFERS)) {
        return;
    }

    ATRACE_CALL();
    LayersTraceProto entry;
    entry.set_elapsed_realtime_nanos(time);
    const char* where = visibleRegionDirty ? "visibleRegionsDirty" : "bufferLatched";
    entry.set_where(where);
    entry.mutable_layers()->Swap(layers);

    if (flagIsSet(LayerTracing::TRACE_HWC)) {
        entry.set_hwc_blob(hwcDump);
    }
    if (!flagIsSet(LayerTracing::TRACE_COMPOSITION)) {
        entry.set_excludes_composition_state(true);
    }
    entry.mutable_displays()->Swap(displays);
    entry.set_vsync_id(vsyncId);
    mBuffer->emplace(std::move(entry));
}

} // namespace android
