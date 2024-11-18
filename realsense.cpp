// realsense.cpp

#include <nan.h>
#include <librealsense2/rs.hpp>
#include <thread>
#include <atomic>
#include <vector>
#include <memory>

class RealSenseWorker : public Nan::AsyncProgressWorkerBase<char> {
public:
    RealSenseWorker(Nan::Callback* progressCallback, Nan::Callback* completeCallback)
        : Nan::AsyncProgressWorkerBase<char>(completeCallback), progressCallback(progressCallback), stopped(false) {
        // Start the pipeline with default configuration
        pipe.start();
    }

    ~RealSenseWorker() {
        // Stop the pipeline
        pipe.stop();
    }

    void Execute(const Nan::AsyncProgressWorkerBase<char>::ExecutionProgress& progress) override {
        while (!stopped) {
            try {
                rs2::frameset frames = pipe.wait_for_frames(5000);
                rs2::depth_frame depth = frames.get_depth_frame();

                int width = depth.get_width();
                int height = depth.get_height();
                size_t dataSize = width * height * sizeof(uint16_t);

                // Prepare data to send
                std::vector<char> buffer(sizeof(int) * 2 + dataSize);
                char* ptr = buffer.data();

                // Copy width and height
                memcpy(ptr, &width, sizeof(int));
                ptr += sizeof(int);
                memcpy(ptr, &height, sizeof(int));
                ptr += sizeof(int);

                // Copy depth data
                memcpy(ptr, depth.get_data(), dataSize);

                // Send the data to Node.js
                progress.Send(buffer.data(), buffer.size());
            } catch (const rs2::error& e) {
                SetErrorMessage(e.what());
                break;
            } catch (const std::exception& e) {
                SetErrorMessage(e.what());
                break;
            }
        }
    }

    void HandleProgressCallback(const char* data, size_t size) override {
        Nan::HandleScope scope;

        const char* ptr = data;

        // Extract width and height
        int width, height;
        memcpy(&width, ptr, sizeof(int));
        ptr += sizeof(int);
        memcpy(&height, ptr, sizeof(int));
        ptr += sizeof(int);

        size_t dataSize = size - 2 * sizeof(int);

        // Create a Node.js Buffer from the depth data
        v8::Local<v8::Object> buffer = Nan::CopyBuffer(ptr, dataSize).ToLocalChecked();

        // Create a JavaScript object to hold the data
        v8::Local<v8::Object> result = Nan::New<v8::Object>();
        Nan::Set(result, Nan::New("width").ToLocalChecked(), Nan::New(width));
        Nan::Set(result, Nan::New("height").ToLocalChecked(), Nan::New(height));
        Nan::Set(result, Nan::New("data").ToLocalChecked(), buffer);

        // Call the progress callback with the result
        v8::Local<v8::Value> argv[] = { result };
        progressCallback->Call(1, argv, async_resource);
    }

    void HandleOKCallback() override {
        Nan::HandleScope scope;
        // Call the completion callback without arguments
        callback->Call(0, nullptr, async_resource);
    }

    void Stop() {
        stopped = true;
    }

private:
    rs2::pipeline pipe;
    Nan::Callback* progressCallback;
    std::atomic<bool> stopped;
};

// Persistent RealSenseWorker instance
std::unique_ptr<RealSenseWorker> rsWorker;

// Start streaming
void StartStreaming(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    if (info.Length() < 2) {
        Nan::ThrowTypeError("Expected two arguments: progress callback and completion callback");
        return;
    }

    if (!info[0]->IsFunction() || !info[1]->IsFunction()) {
        Nan::ThrowTypeError("Both arguments must be functions");
        return;
    }

    Nan::Callback* progressCallback = new Nan::Callback(info[0].As<v8::Function>());
    Nan::Callback* completeCallback = new Nan::Callback(info[1].As<v8::Function>());

    rsWorker.reset(new RealSenseWorker(progressCallback, completeCallback));
    Nan::AsyncQueueWorker(rsWorker.get());
}

// Stop streaming
void StopStreaming(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    if (rsWorker) {
        rsWorker->Stop();
        rsWorker.reset();
    }
}

// Initialize the addon
void Init(v8::Local<v8::Object> exports) {
    Nan::SetMethod(exports, "startStreaming", StartStreaming);
    Nan::SetMethod(exports, "stopStreaming", StopStreaming);
}

NODE_MODULE(addon, Init)
