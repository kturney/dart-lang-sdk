// Copyright (c) 2015, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#include "vm/bootstrap_natives.h"

#include "include/dart_api.h"

#include "vm/debugger.h"
#include "vm/exceptions.h"
#include "vm/flags.h"
#include "vm/heap/heap.h"
#include "vm/isolate.h"
#include "vm/message.h"
#include "vm/native_entry.h"
#include "vm/object.h"
#include "vm/object_graph.h"
#include "vm/object_store.h"
#include "vm/service.h"
#include "vm/service_isolate.h"
#include "vm/zone_text_buffer.h"

namespace dart {

// Native implementations for the dart:developer library.
DEFINE_NATIVE_ENTRY(Developer_debugger, 0, 2) {
  GET_NON_NULL_NATIVE_ARGUMENT(Bool, when, arguments->NativeArgAt(0));
#if !defined(PRODUCT) && !defined(DART_PRECOMPILED_RUNTIME)
  GET_NATIVE_ARGUMENT(String, msg, arguments->NativeArgAt(1));
  Debugger* debugger = isolate->debugger();
  if (debugger == nullptr) {
    return when.ptr();
  }
  if (when.value()) {
    debugger->PauseDeveloper(msg);
  }
#endif
  return when.ptr();
}

DEFINE_NATIVE_ENTRY(Developer_inspect, 0, 1) {
  GET_NATIVE_ARGUMENT(Instance, inspectee, arguments->NativeArgAt(0));
#ifndef PRODUCT
  Service::SendInspectEvent(isolate, inspectee);
#endif  // !PRODUCT
  return inspectee.ptr();
}

DEFINE_NATIVE_ENTRY(Developer_log, 0, 8) {
#if defined(PRODUCT)
  return Object::null();
#else
  GET_NON_NULL_NATIVE_ARGUMENT(String, message, arguments->NativeArgAt(0));
  GET_NON_NULL_NATIVE_ARGUMENT(Integer, timestamp, arguments->NativeArgAt(1));
  GET_NON_NULL_NATIVE_ARGUMENT(Integer, sequence, arguments->NativeArgAt(2));
  GET_NON_NULL_NATIVE_ARGUMENT(Smi, level, arguments->NativeArgAt(3));
  GET_NON_NULL_NATIVE_ARGUMENT(String, name, arguments->NativeArgAt(4));
  GET_NATIVE_ARGUMENT(Instance, dart_zone, arguments->NativeArgAt(5));
  GET_NATIVE_ARGUMENT(Instance, error, arguments->NativeArgAt(6));
  GET_NATIVE_ARGUMENT(Instance, stack_trace, arguments->NativeArgAt(7));
  Service::SendLogEvent(isolate, sequence.AsInt64Value(),
                        timestamp.AsInt64Value(), level.Value(), name, message,
                        dart_zone, error, stack_trace);
  return Object::null();
#endif  // PRODUCT
}

DEFINE_NATIVE_ENTRY(Developer_postEvent, 0, 2) {
#if defined(PRODUCT)
  return Object::null();
#else
  GET_NON_NULL_NATIVE_ARGUMENT(String, event_kind, arguments->NativeArgAt(0));
  GET_NON_NULL_NATIVE_ARGUMENT(String, event_data, arguments->NativeArgAt(1));
  Service::SendExtensionEvent(isolate, event_kind, event_data);
  return Object::null();
#endif  // PRODUCT
}

DEFINE_NATIVE_ENTRY(Developer_lookupExtension, 0, 1) {
#if defined(PRODUCT)
  return Object::null();
#else
  GET_NON_NULL_NATIVE_ARGUMENT(String, name, arguments->NativeArgAt(0));
  return isolate->LookupServiceExtensionHandler(name);
#endif  // PRODUCT
}

DEFINE_NATIVE_ENTRY(Developer_registerExtension, 0, 2) {
#if defined(PRODUCT)
  return Object::null();
#else
  GET_NON_NULL_NATIVE_ARGUMENT(String, name, arguments->NativeArgAt(0));
  GET_NON_NULL_NATIVE_ARGUMENT(Instance, handler, arguments->NativeArgAt(1));
  // We don't allow service extensions to be registered for the
  // service isolate.  This can happen, for example, because the
  // service isolate uses dart:io.  If we decide that we want to start
  // supporting this in the future, it will take some work.
  if (!ServiceIsolate::IsServiceIsolateDescendant(isolate)) {
    isolate->RegisterServiceExtensionHandler(name, handler);
  }
  return Object::null();
#endif  // PRODUCT
}

DEFINE_NATIVE_ENTRY(Developer_getServiceMajorVersion, 0, 0) {
#if defined(PRODUCT)
  return Smi::New(0);
#else
  return Smi::New(SERVICE_PROTOCOL_MAJOR_VERSION);
#endif
}

DEFINE_NATIVE_ENTRY(Developer_getServiceMinorVersion, 0, 0) {
#if defined(PRODUCT)
  return Smi::New(0);
#else
  return Smi::New(SERVICE_PROTOCOL_MINOR_VERSION);
#endif
}

static void SendNull(const SendPort& port) {
  const Dart_Port destination_port_id = port.Id();
  PortMap::PostMessage(Message::New(destination_port_id, Object::null(),
                                    Message::kNormalPriority));
}

DEFINE_NATIVE_ENTRY(Developer_getServerInfo, 0, 1) {
  GET_NON_NULL_NATIVE_ARGUMENT(SendPort, port, arguments->NativeArgAt(0));
#if defined(PRODUCT)
  SendNull(port);
  return Object::null();
#else
  ServiceIsolate::WaitForServiceIsolateStartup();
  if (!ServiceIsolate::IsRunning()) {
    SendNull(port);
  } else {
    ServiceIsolate::RequestServerInfo(port);
  }
  return Object::null();
#endif
}

DEFINE_NATIVE_ENTRY(Developer_webServerControl, 0, 3) {
  GET_NON_NULL_NATIVE_ARGUMENT(SendPort, port, arguments->NativeArgAt(0));
#if defined(PRODUCT)
  SendNull(port);
  return Object::null();
#else
  GET_NON_NULL_NATIVE_ARGUMENT(Bool, enabled, arguments->NativeArgAt(1));
  GET_NATIVE_ARGUMENT(Bool, silence_output, arguments->NativeArgAt(2));
  ServiceIsolate::WaitForServiceIsolateStartup();
  if (!ServiceIsolate::IsRunning()) {
    SendNull(port);
  } else {
    ServiceIsolate::ControlWebServer(port, enabled.value(), silence_output);
  }
  return Object::null();
#endif
}

DEFINE_NATIVE_ENTRY(Developer_getIsolateIDFromSendPort, 0, 1) {
#if defined(PRODUCT)
  return Object::null();
#else
  GET_NON_NULL_NATIVE_ARGUMENT(SendPort, port, arguments->NativeArgAt(0));
  int64_t port_id = port.Id();
  return String::NewFormatted(ISOLATE_SERVICE_ID_FORMAT_STRING, port_id);
#endif
}

DEFINE_NATIVE_ENTRY(Developer_reachability_barrier, 0, 0) {
  IsolateGroup* isolate_group = thread->isolate_group();
  ASSERT(isolate_group != nullptr);
  Heap* heap = isolate_group->heap();
  ASSERT(heap != nullptr);
  return Integer::New(heap->ReachabilityBarrier());
}

DEFINE_NATIVE_ENTRY(Developer_NativeRuntime_buildId, 0, 0) {
#if defined(DART_PRECOMPILED_RUNTIME)
  IsolateGroup* isolate_group = thread->isolate_group();
  ASSERT(isolate_group != nullptr);
  if (const uint8_t* instructions =
          isolate_group->source()->snapshot_instructions) {
    const auto& build_id = OS::GetAppBuildId(instructions);
    if (build_id.data != nullptr) {
      ZoneTextBuffer buffer(zone);
      for (intptr_t i = 0; i < build_id.len; i++) {
        buffer.Printf("%2.2x", build_id.data[i]);
      }
      return String::New(buffer.buffer());
    }
  }
#endif
  return String::null();
}

DEFINE_NATIVE_ENTRY(Developer_NativeRuntime_writeHeapSnapshotToFile, 0, 1) {
#if defined(DART_ENABLE_HEAP_SNAPSHOT_WRITER)
  const String& filename =
      String::CheckedHandle(zone, arguments->NativeArgAt(0));
  bool successful = false;
  {
    FileHeapSnapshotWriter file_writer(thread, filename.ToCString(),
                                       &successful);
    HeapSnapshotWriter writer(thread, &file_writer);
    writer.Write();
  }
  if (!successful) {
    Exceptions::ThrowUnsupportedError(
        "Could not create & write heapsnapshot to disc. Possibly due to "
        "missing embedder functionality.");
  }
#else
  Exceptions::ThrowUnsupportedError(
      "Heap snapshots are only supported in non-product mode.");
#endif  // !defined(PRODUCT)
  return Object::null();
}

}  // namespace dart
