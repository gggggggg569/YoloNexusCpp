#include "amd_neural_runner.h"

#include <dml_provider_factory.h>

#include <stdexcept>

namespace yolo_nexus {

void AppendAmdDirectMlProvider(Ort::SessionOptions& options, int device_id) {
    const OrtDmlApi* dml_api = nullptr;
    const OrtStatus* status = Ort::GetApi().GetExecutionProviderApi(
        "DML",
        ORT_API_VERSION,
        reinterpret_cast<const void**>(&dml_api));

    if (status != nullptr || dml_api == nullptr) {
        throw std::runtime_error("DirectML execution provider is not available in this ONNX Runtime build.");
    }

    Ort::ThrowOnError(dml_api->SessionOptionsAppendExecutionProvider_DML(options, device_id));
}

}
