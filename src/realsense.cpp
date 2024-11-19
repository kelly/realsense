#include <nan.h>
#include <librealsense2/rs.hpp>
#include <thread>
#include <atomic>
#include <vector>
#include <memory>
#include <chrono>

int GetIntOption(v8::Local<v8::Object> options, const char* key, int defaultValue) {
    v8::Local<v8::String> v8Key = Nan::New(key).ToLocalChecked();
    if (Nan::Has(options, v8Key).FromJust()) {
        v8::Local<v8::Value> val = Nan::Get(options, v8Key).ToLocalChecked();
        if (val->IsNumber()) {
            return Nan::To<int32_t>(val).FromJust();
        }
    }
    return defaultValue;
}

class RealSenseWorker : public Nan::AsyncProgressWorkerBase<char> {
public:
    RealSenseWorker(int depthWidth, int depthHeight,
                    int colorWidth, int colorHeight,
                    int fps, int maxFPS,
                    Nan::Callback* progressCallback, Nan::Callback* completeCallback)
        : Nan::AsyncProgressWorkerBase<char>(completeCallback), progressCallback(progressCallback), stopped(false),
          depthWidth(depthWidth), depthHeight(depthHeight),
          colorWidth(colorWidth), colorHeight(colorHeight),
          fps(fps), maxFPS(maxFPS) {

        // Configure the pipeline to stream depth and color frames with the same FPS
        rs2::config cfg;
        cfg.enable_stream(RS2_STREAM_DEPTH, depthWidth, depthHeight, RS2_FORMAT_Z16, fps);
        cfg.enable_stream(RS2_STREAM_COLOR, colorWidth, colorHeight, RS2_FORMAT_BGR8, fps);
        pipe.start(cfg);

        lastFrameTime = std::chrono::steady_clock::now();
    }

    ~RealSenseWorker() {
        // Stop the pipeline
        Stop();
    }

    void Execute(const Nan::AsyncProgressWorkerBase<char>::ExecutionProgress& progress) override {
        while (!stopped) {
            try {
                // Implement throttling
                if (maxFPS > 0) {
                    auto now = std::chrono::steady_clock::now();
                    auto timeSinceLastFrame = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastFrameTime).count();
                    int frameInterval = 1000 / maxFPS;
                    if (timeSinceLastFrame < frameInterval) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(frameInterval - timeSinceLastFrame));
                    }
                    lastFrameTime = std::chrono::steady_clock::now();
                }

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
            } catch (...) {
                SetErrorMessage("Unknown error occurred");
                break;
            }
        }
    }

    void HandleErrorCallback() override {
        Nan::HandleScope scope;

        v8::Local<v8::Value> argv[] = {
            Nan::Error(ErrorMessage())
        };

        callback->Call(1, argv, async_resource);
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

        size_t colorDataSize = colorWidth * colorHeight * 3; // Assuming BGR8 format

        // Copy color data into Buffer
        v8::Local<v8::Object> colorBuffer = Nan::CopyBuffer(ptr, colorDataSize).ToLocalChecked();
        ptr += colorDataSize;

        // Create JavaScript objects for depth and color frames
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
        if (stopped.exchange(true)) return; // Prevent multiple stops

        stopped = true;
        try {
            pipe.stop();
        } catch (...) {
            // Ignore exceptions during cleanup
        }
    }

private:
    rs2::pipeline pipe;
    
    Nan::Callback* progressCallback;
    std::atomic<bool> stopped;

    int depthWidth;
    int depthHeight;

    int colorWidth;
    int colorHeight;

    int fps;
    int maxFPS;

    // For throttling
    std::chrono::steady_clock::time_point lastFrameTime;
};

std::unique_ptr<RealSenseWorker> rsWorker;

// Start streaming
void StartStreaming(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    if (info.Length() < 3) {
        Nan::ThrowTypeError("Expected three arguments: options object, progress callback, and completion callback");
        return;
    }

    if (!info[0]->IsObject() || !info[1]->IsFunction() || !info[2]->IsFunction()) {
        Nan::ThrowTypeError("Expected an options object and two functions");
        return;
    }

    v8::Local<v8::Object> options = info[0].As<v8::Object>();
    Nan::Callback* progressCallback = new Nan::Callback(info[1].As<v8::Function>());
    Nan::Callback* completeCallback = new Nan::Callback(info[2].As<v8::Function>());

    // Extract options with default values
    int depthWidth = GetIntOption(options, "depthWidth", 640);
    int depthHeight = GetIntOption(options, "depthHeight", 480);

    int colorWidth = GetIntOption(options, "colorWidth", 640);
    int colorHeight = GetIntOption(options, "colorHeight", 480);

    int fps = GetIntOption(options, "fps", 30); // Single FPS parameter

    int maxFPS = GetIntOption(options, "maxFPS", 0); // 0 means no throttling

    // Pass the parameters to the RealSenseWorker constructor
    rsWorker.reset(new RealSenseWorker(depthWidth, depthHeight,
                                       colorWidth, colorHeight,
                                       fps, maxFPS,
                                       progressCallback, completeCallback));
    Nan::AsyncQueueWorker(rsWorker.get());
}


void StopStreaming(const Nan::FunctionCallbackInfo<v8::Value>& info) {

    if (rsWorker) {
        RealSenseWorker* worker = Nan::ObjectWrap::Unwrap<RealSenseWorker>(rsWorker.Get(info.GetIsolate()));
        worker->Stop();
        rsWorker.reset();
    }
}

// Initialize the addon
void Init(v8::Local<v8::Object> exports) {
    Nan::SetMethod(exports, "startStreaming", StartStreaming);
    Nan::SetMethod(exports, "stopStreaming", StopStreaming);
}

NODE_MODULE(realsense, Init)
