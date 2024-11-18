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
        // Configure the pipeline to stream depth and color frames
        rs2::config cfg;
        cfg.enable_stream(RS2_STREAM_DEPTH, 640, 480, RS2_FORMAT_Z16, 30);
        cfg.enable_stream(RS2_STREAM_COLOR, 640, 480, RS2_FORMAT_BGR8, 30);
        pipe.start(cfg);
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
                rs2::video_frame color = frames.get_color_frame();

                // Depth frame
                int depthWidth = depth.get_width();
                int depthHeight = depth.get_height();
                size_t depthDataSize = depthWidth * depthHeight * sizeof(uint16_t);

                // Color frame
                int colorWidth = color.get_width();
                int colorHeight = color.get_height();
                size_t colorDataSize = colorWidth * colorHeight * 3; // Assuming BGR8 format

                // Prepare data to send
                size_t totalSize = sizeof(int) * 4 + depthDataSize + colorDataSize;
                std::vector<char> buffer(totalSize);
                char* ptr = buffer.data();

                // Copy depth width and height
                memcpy(ptr, &depthWidth, sizeof(int));
                ptr += sizeof(int);
                memcpy(ptr, &depthHeight, sizeof(int));
                ptr += sizeof(int);

                // Copy depth data
                memcpy(ptr, depth.get_data(), depthDataSize);
                ptr += depthDataSize;

                // Copy color width and height
                memcpy(ptr, &colorWidth, sizeof(int));
                ptr += sizeof(int);
                memcpy(ptr, &colorHeight, sizeof(int));
                ptr += sizeof(int);

                // Copy color data
                memcpy(ptr, color.get_data(), colorDataSize);

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

        // Extract depth width and height
        int depthWidth, depthHeight;
        memcpy(&depthWidth, ptr, sizeof(int));
        ptr += sizeof(int);
        memcpy(&depthHeight, ptr, sizeof(int));
        ptr += sizeof(int);

        // Calculate depth data size
        size_t depthDataSize = depthWidth * depthHeight * sizeof(uint16_t);

        // Copy depth data into Buffer
        v8::Local<v8::Object> depthBuffer = Nan::CopyBuffer(ptr, depthDataSize).ToLocalChecked();
        ptr += depthDataSize;

        // Extract color width and height
        int colorWidth, colorHeight;
        memcpy(&colorWidth, ptr, sizeof(int));
        ptr += sizeof(int);
        memcpy(&colorHeight, ptr, sizeof(int));
        ptr += sizeof(int);

        // Calculate color data size
        size_t colorDataSize = colorWidth * colorHeight * 3; // Assuming BGR8 format

        // Copy color data into Buffer
        v8::Local<v8::Object> colorBuffer = Nan::CopyBuffer(ptr, colorDataSize).ToLocalChecked();
        ptr += colorDataSize;

        // Create a JavaScript object to hold the data
        v8::Local<v8::Object> depthFrame = Nan::New<v8::Object>();
        Nan::Set(depthFrame, Nan::New("width").ToLocalChecked(), Nan::New(depthWidth));
        Nan::Set(depthFrame, Nan::New("height").ToLocalChecked(), Nan::New(depthHeight));
        Nan::Set(depthFrame, Nan::New("data").ToLocalChecked(), depthBuffer);

        v8::Local<v8::Object> colorFrame = Nan::New<v8::Object>();
        Nan::Set(colorFrame, Nan::New("width").ToLocalChecked(), Nan::New(colorWidth));
        Nan::Set(colorFrame, Nan::New("height").ToLocalChecked(), Nan::New(colorHeight));
        Nan::Set(colorFrame, Nan::New("data").ToLocalChecked(), colorBuffer);

        // Create result object
        v8::Local<v8::Object> result = Nan::New<v8::Object>();
        Nan::Set(result, Nan::New("depthFrame").ToLocalChecked(), depthFrame);
        Nan::Set(result, Nan::New("colorFrame").ToLocalChecked(), colorFrame);

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

NODE_MODULE(realsense, Init)
