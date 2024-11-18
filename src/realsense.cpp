// addon.cpp

#include <nan.h>
#include <librealsense2/rs.hpp>
#include <vector>
#include <memory>

class RealSenseWrapper {
public:
    RealSenseWrapper() {
        // Start the pipeline with default configuration
        pipe.start();
    }

    ~RealSenseWrapper() {
        // Stop the pipeline on destruction
        pipe.stop();
    }

    std::vector<uint16_t> getDepthData(int& width, int& height) {
        // Wait for the next set of frames
        rs2::frameset frames = pipe.wait_for_frames();

        // Get the depth frame
        rs2::depth_frame depth = frames.get_depth_frame();

        // Get frame dimensions
        width = depth.get_width();
        height = depth.get_height();

        // Copy the depth data
        size_t size = width * height;
        std::vector<uint16_t> data(size);
        memcpy(data.data(), depth.get_data(), size * sizeof(uint16_t));

        return data;
    }

private:
    rs2::pipeline pipe;
};

// Persistent RealSenseWrapper instance
std::unique_ptr<RealSenseWrapper> rsWrapper;

// Initialize the RealSense camera
void InitCamera(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    if (!rsWrapper) {
        try {
            rsWrapper = std::make_unique<RealSenseWrapper>();
            info.GetReturnValue().Set(Nan::New(true));
        } catch (const rs2::error& e) {
            Nan::ThrowError(e.what());
        } catch (const std::exception& e) {
            Nan::ThrowError(e.what());
        }
    } else {
        info.GetReturnValue().Set(Nan::New(false)); // Already initialized
    }
}

// Close the RealSense camera
void CloseCamera(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    if (rsWrapper) {
        rsWrapper.reset();
        info.GetReturnValue().Set(Nan::New(true));
    } else {
        info.GetReturnValue().Set(Nan::New(false)); // Already closed
    }
}

// Asynchronous worker to get depth data
class GetDepthDataWorker : public Nan::AsyncWorker {
public:
    GetDepthDataWorker(Nan::Callback* callback)
        : Nan::AsyncWorker(callback) {}

    void Execute() override {
        if (!rsWrapper) {
            SetErrorMessage("Camera is not initialized.");
            return;
        }

        try {
            depthData = rsWrapper->getDepthData(width, height);
        } catch (const rs2::error& e) {
            SetErrorMessage(e.what());
        } catch (const std::exception& e) {
            SetErrorMessage(e.what());
        }
    }

    void HandleOKCallback() override {
        Nan::HandleScope scope;

        // Create a JavaScript object to hold the depth data and dimensions
        v8::Local<v8::Object> result = Nan::New<v8::Object>();

        // Create a Node.js Buffer from the depth data
        v8::Local<v8::Object> buffer = Nan::CopyBuffer(
            reinterpret_cast<char*>(depthData.data()), depthData.size() * sizeof(uint16_t)
        ).ToLocalChecked();

        // Set the properties
        Nan::Set(result, Nan::New("width").ToLocalChecked(), Nan::New(width));
        Nan::Set(result, Nan::New("height").ToLocalChecked(), Nan::New(height));
        Nan::Set(result, Nan::New("data").ToLocalChecked(), buffer);

        v8::Local<v8::Value> argv[] = { Nan::Null(), result };
        callback->Call(2, argv, async_resource);
    }

    void HandleErrorCallback() override {
        Nan::HandleScope scope;

        v8::Local<v8::Value> argv[] = { Nan::Error(ErrorMessage()) };
        callback->Call(1, argv, async_resource);
    }

private:
    int width = 0;
    int height = 0;
    std::vector<uint16_t> depthData;
};

// Get depth data asynchronously
void GetDepthData(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    if (info.Length() < 1 || !info[0]->IsFunction()) {
        Nan::ThrowTypeError("Callback function required");
        return;
    }

    Nan::Callback* callback = new Nan::Callback(info[0].As<v8::Function>());
    Nan::AsyncQueueWorker(new GetDepthDataWorker(callback));
}

// Initialize the addon
void Init(v8::Local<v8::Object> exports) {
    Nan::SetMethod(exports, "initCamera", InitCamera);
    Nan::SetMethod(exports, "closeCamera", CloseCamera);
    Nan::SetMethod(exports, "getDepthData", GetDepthData);
}

NODE_MODULE(addon, Init)
