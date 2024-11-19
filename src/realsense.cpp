#include <nan.h>
#include <librealsense2/rs.hpp>
#include <thread>
#include <atomic>
#include <vector>
#include <memory>
#include <chrono>
#include <mutex>

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
        : Nan::AsyncProgressWorkerBase<char>(completeCallback), 
          progressCallback(progressCallback), 
          stopped(false),
          depthWidth(depthWidth), depthHeight(depthHeight),
          colorWidth(colorWidth), colorHeight(colorHeight),
          fps(fps), maxFPS(maxFPS) {
        
        try {
            // Configure the pipeline to stream depth and color frames with the same FPS
            rs2::config cfg;
            cfg.enable_stream(RS2_STREAM_DEPTH, depthWidth, depthHeight, RS2_FORMAT_Z16, fps);
            cfg.enable_stream(RS2_STREAM_COLOR, colorWidth, colorHeight, RS2_FORMAT_BGR8, fps);
            pipe.start(cfg);

            lastFrameTime = std::chrono::steady_clock::now();
        } catch (const rs2::error& e) {
            SetErrorMessage(("Pipeline configuration error: " + std::string(e.what())).c_str());
            stopped = true;
        }
    }

    ~RealSenseWorker() {
        Stop();
        delete progressCallback;
    }

    void Execute(const Nan::AsyncProgressWorkerBase<char>::ExecutionProgress& progress) override {
        while (!stopped) {
            try {
                // Throttling mechanism
                if (maxFPS > 0) {
                    auto now = std::chrono::steady_clock::now();
                    auto timeSinceLastFrame = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastFrameTime).count();
                    int frameInterval = 1000 / maxFPS;
                    if (timeSinceLastFrame < frameInterval) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(frameInterval - timeSinceLastFrame));
                    }
                    lastFrameTime = std::chrono::steady_clock::now();
                }

                // Use a timeout to prevent indefinite blocking
                rs2::frameset frames = pipe.wait_for_frames(5000);

                rs2::depth_frame depth = frames.get_depth_frame();
                rs2::video_frame color = frames.get_color_frame();

                // Prepare buffers and send data
                int depthWidth = depth.get_width();
                int depthHeight = depth.get_height();
                size_t depthDataSize = depthWidth * depthHeight * sizeof(uint16_t);

                int colorWidth = color.get_width();
                int colorHeight = color.get_height();
                size_t colorDataSize = colorWidth * colorHeight * 3;

                size_t totalSize = sizeof(int) * 4 + depthDataSize + colorDataSize;
                std::vector<char> buffer(totalSize);
                char* ptr = buffer.data();

                // Copy depth dimensions and data
                memcpy(ptr, &depthWidth, sizeof(int));
                ptr += sizeof(int);
                memcpy(ptr, &depthHeight, sizeof(int));
                ptr += sizeof(int);
                memcpy(ptr, depth.get_data(), depthDataSize);
                ptr += depthDataSize;

                // Copy color dimensions and data
                memcpy(ptr, &colorWidth, sizeof(int));
                ptr += sizeof(int);
                memcpy(ptr, &colorHeight, sizeof(int));
                ptr += sizeof(int);
                memcpy(ptr, color.get_data(), colorDataSize);

                // Send data to Node.js
                progress.Send(buffer.data(), buffer.size());

            } catch (const rs2::error& e) {
                // Log error and potentially break or continue based on error type
                SetErrorMessage(("RealSense error: " + std::string(e.what())).c_str());
                break;
            } catch (const std::exception& e) {
                SetErrorMessage(("Standard exception: " + std::string(e.what())).c_str());
                break;
            } catch (...) {
                SetErrorMessage("Unknown error occurred");
                break;
            }
        }
    }

    void HandleProgressCallback(const char* data, size_t size) override {
        Nan::HandleScope scope;

        const char* ptr = data;

        // Extract depth frame details
        int depthWidth, depthHeight;
        memcpy(&depthWidth, ptr, sizeof(int));
        ptr += sizeof(int);
        memcpy(&depthHeight, ptr, sizeof(int));
        ptr += sizeof(int);

        size_t depthDataSize = depthWidth * depthHeight * sizeof(uint16_t);

        // Create depth buffer
        v8::Local<v8::Object> depthBuffer = Nan::CopyBuffer(ptr, depthDataSize).ToLocalChecked();
        ptr += depthDataSize;

        // Extract color frame details
        int colorWidth, colorHeight;
        memcpy(&colorWidth, ptr, sizeof(int));
        ptr += sizeof(int);
        memcpy(&colorHeight, ptr, sizeof(int));
        ptr += sizeof(int);

        size_t colorDataSize = colorWidth * colorHeight * 3;

        // Create color buffer
        v8::Local<v8::Object> colorBuffer = Nan::CopyBuffer(ptr, colorDataSize).ToLocalChecked();

        // Create frame objects
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

        // Call progress callback
        v8::Local<v8::Value> argv[] = { result };
        progressCallback->Call(1, argv, async_resource);
    }

    void HandleOKCallback() override {
        Nan::HandleScope scope;
        callback->Call(0, nullptr, async_resource);
    }

    void HandleErrorCallback() override {
        Nan::HandleScope scope;
        v8::Local<v8::Value> argv[] = {
            Nan::Error(ErrorMessage())
        };
        callback->Call(1, argv, async_resource);
    }

    void Stop() {
        std::lock_guard<std::mutex> lock(stopMutex);
        if (stopped) return;
        stopped = true;
        try {
            pipe.stop();
        } catch (...) {
            // Silently handle any exceptions during stop
        }
    }

private:
    rs2::pipeline pipe;
    Nan::Callback* progressCallback;
    std::atomic<bool> stopped;
    std::mutex stopMutex;

    int depthWidth, depthHeight;
    int colorWidth, colorHeight;
    int fps, maxFPS;

    std::chrono::steady_clock::time_point lastFrameTime;
};

// Global pointer with careful management
std::unique_ptr<RealSenseWorker> rsWorker;
std::mutex rsWorkerMutex;

void StartStreaming(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    // Validate input arguments
    if (info.Length() < 3 || 
        !info[0]->IsObject() || 
        !info[1]->IsFunction() || 
        !info[2]->IsFunction()) {
        return Nan::ThrowTypeError("Expected: options object, progress callback, completion callback");
    }

    std::lock_guard<std::mutex> lock(rsWorkerMutex);

    // Stop any existing worker before creating a new one
    if (rsWorker) {
        rsWorker->Stop();
        rsWorker.reset();
    }

    v8::Local<v8::Object> options = info[0].As<v8::Object>();
    Nan::Callback* progressCallback = new Nan::Callback(info[1].As<v8::Function>());
    Nan::Callback* completeCallback = new Nan::Callback(info[2].As<v8::Function>());

    // Extract options with defaults
    int depthWidth = GetIntOption(options, "depthWidth", 640);
    int depthHeight = GetIntOption(options, "depthHeight", 480);
    int colorWidth = GetIntOption(options, "colorWidth", 640);
    int colorHeight = GetIntOption(options, "colorHeight", 480);
    int fps = GetIntOption(options, "fps", 30);
    int maxFPS = GetIntOption(options, "maxFPS", 0);

    // Create and queue worker
    rsWorker.reset(new RealSenseWorker(depthWidth, depthHeight,
                                       colorWidth, colorHeight,
                                       fps, maxFPS,
                                       progressCallback, completeCallback));
    Nan::AsyncQueueWorker(rsWorker.get());
}

void StopStreaming(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    std::lock_guard<std::mutex> lock(rsWorkerMutex);
    
    if (rsWorker) {
        rsWorker->Stop();
        rsWorker.reset();
    }
}

void Init(v8::Local<v8::Object> exports) {
    Nan::SetMethod(exports, "startStreaming", StartStreaming);
    Nan::SetMethod(exports, "stopStreaming", StopStreaming);
}

NODE_MODULE(realsense, Init)