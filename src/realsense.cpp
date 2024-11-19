#include <nan.h>
#include <librealsense2/rs.hpp>
#include <thread>
#include <atomic>
#include <vector>
#include <memory>
#include <chrono>

class RealSenseWorker : public Nan::AsyncProgressWorkerBase<char> {
public:
    RealSenseWorker(
        v8::Local<v8::Object> options,
        v8::Local<v8::Function> progressCallback, 
        v8::Local<v8::Function> completionCallback,
        Nan::Callback* callback
    ) : Nan::AsyncProgressWorkerBase<char>(callback),
        stopped(false) {
        
        // Safely capture callbacks
        this->progressCallback.Reset(progressCallback);
        this->completionCallback.Reset(completionCallback);

        // Extract options safely
        v8::Local<v8::Context> context = Nan::GetCurrentContext();
        
        // Use safe getter with defaults
        depthWidth = Nan::To<int32_t>(
            Nan::Get(options, Nan::New("depthWidth").ToLocalChecked())
            .ToLocalChecked()
        ).FromMaybe(640);
        
        depthHeight = Nan::To<int32_t>(
            Nan::Get(options, Nan::New("depthHeight").ToLocalChecked())
            .ToLocalChecked()
        ).FromMaybe(480);
        
        colorWidth = Nan::To<int32_t>(
            Nan::Get(options, Nan::New("colorWidth").ToLocalChecked())
            .ToLocalChecked()
        ).FromMaybe(640);
        
        colorHeight = Nan::To<int32_t>(
            Nan::Get(options, Nan::New("colorHeight").ToLocalChecked())
            .ToLocalChecked()
        ).FromMaybe(480);
        
        fps = Nan::To<int32_t>(
            Nan::Get(options, Nan::New("fps").ToLocalChecked())
            .ToLocalChecked()
        ).FromMaybe(30);
        
        maxFPS = Nan::To<int32_t>(
            Nan::Get(options, Nan::New("maxFPS").ToLocalChecked())
            .ToLocalChecked()
        ).FromMaybe(0);
    }

    ~RealSenseWorker() {
        Stop();
    }

    void Execute(const Nan::AsyncProgressWorkerBase<char>::ExecutionProgress& progress) override {
        try {
            // Configure the pipeline safely
            rs2::config cfg;
            cfg.enable_stream(RS2_STREAM_DEPTH, depthWidth, depthHeight, RS2_FORMAT_Z16, fps);
            cfg.enable_stream(RS2_STREAM_COLOR, colorWidth, colorHeight, RS2_FORMAT_BGR8, fps);
            
            rs2::pipeline_profile profile = pipe.start(cfg);

            lastFrameTime = std::chrono::steady_clock::now();

            // Main processing loop
            while (!stopped) {
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

                // Wait for frames with a timeout
                rs2::frameset frames = pipe.wait_for_frames(5000);
                if (!frames) {
                    continue;
                }

                rs2::depth_frame depth = frames.get_depth_frame();
                rs2::video_frame color = frames.get_color_frame();

                // Prepare buffers
                int currentDepthWidth = depth.get_width();
                int currentDepthHeight = depth.get_height();
                size_t depthDataSize = currentDepthWidth * currentDepthHeight * sizeof(uint16_t);

                int currentColorWidth = color.get_width();
                int currentColorHeight = color.get_height();
                size_t colorDataSize = currentColorWidth * currentColorHeight * 3;

                // Total buffer size
                size_t totalSize = sizeof(int) * 4 + depthDataSize + colorDataSize;
                std::vector<char> buffer(totalSize);
                char* ptr = buffer.data();

                // Copy depth frame details
                memcpy(ptr, &currentDepthWidth, sizeof(int));
                ptr += sizeof(int);
                memcpy(ptr, &currentDepthHeight, sizeof(int));
                ptr += sizeof(int);
                memcpy(ptr, depth.get_data(), depthDataSize);
                ptr += depthDataSize;

                // Copy color frame details
                memcpy(ptr, &currentColorWidth, sizeof(int));
                ptr += sizeof(int);
                memcpy(ptr, &currentColorHeight, sizeof(int));
                ptr += sizeof(int);
                memcpy(ptr, color.get_data(), colorDataSize);

                // Send data to Node.js
                progress.Send(buffer.data(), buffer.size());
            }
        } catch (const rs2::error& e) {
            SetErrorMessage(("RealSense error: " + std::string(e.what())).c_str());
        } catch (const std::exception& e) {
            SetErrorMessage(("Standard exception: " + std::string(e.what())).c_str());
        } catch (...) {
            SetErrorMessage("Unknown error occurred");
        }

        // Ensure pipeline is stopped
        try {
            pipe.stop();
        } catch (...) {}
    }

    void HandleProgressCallback(const char* data, size_t size) override {
        Nan::HandleScope scope;

        v8::Local<v8::Function> progressFunc = Nan::New(progressCallback);
        if (progressFunc.IsEmpty()) return;

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
        Nan::Call(progressFunc, Nan::GetCurrentContext()->Global(), 1, argv);
    }

    void HandleOKCallback() override {
        Nan::HandleScope scope;
        
        // Safely call completion callback
        v8::Local<v8::Function> completeFunc = Nan::New(completionCallback);
        if (!completeFunc.IsEmpty()) {
            Nan::Call(completeFunc, Nan::GetCurrentContext()->Global(), 0, nullptr);
        }
    }

    void HandleErrorCallback() override {
        Nan::HandleScope scope;

        // Safely handle error callback
        v8::Local<v8::Function> completeFunc = Nan::New(completionCallback);
        if (!completeFunc.IsEmpty()) {
            v8::Local<v8::Value> argv[] = {
                Nan::Error(ErrorMessage())
            };
            Nan::Call(completeFunc, Nan::GetCurrentContext()->Global(), 1, argv);
        }
    }

    void Stop() {
        stopped = true;
        try {
            pipe.stop();
        } catch (...) {}
    }

private:
    rs2::pipeline pipe;
    
    // Use Nan persistent handles for callbacks
    Nan::Persistent<v8::Function> progressCallback;
    Nan::Persistent<v8::Function> completionCallback;

    std::atomic<bool> stopped;

    // Frame parameters
    int depthWidth, depthHeight;
    int colorWidth, colorHeight;
    int fps, maxFPS;

    std::chrono::steady_clock::time_point lastFrameTime;
};

// Singleton worker management
class RealSenseManager {
public:
    static void StartStreaming(const Nan::FunctionCallbackInfo<v8::Value>& info) {
        // Validate arguments
        if (info.Length() < 3 || 
            !info[0]->IsObject() || 
            !info[1]->IsFunction() || 
            !info[2]->IsFunction()) {
            return Nan::ThrowTypeError("Expected: options object, progress callback, completion callback");
        }

        // Stop any existing worker
        if (worker) {
            worker->Stop();
            delete worker;
            worker = nullptr;
        }

        // Create completion callback for the worker
        Nan::Callback* callback = new Nan::Callback(info[2].As<v8::Function>());

        // Create new worker
        worker = new RealSenseWorker(
            info[0].As<v8::Object>(), 
            info[1].As<v8::Function>(),
            info[2].As<v8::Function>(),
            callback
        );

        // Queue worker
        Nan::AsyncQueueWorker(worker);
    }

    static void StopStreaming(const Nan::FunctionCallbackInfo<v8::Value>& info) {
        if (worker) {
            worker->Stop();
            delete worker;
            worker = nullptr;
        }
    }

    static void Init(v8::Local<v8::Object> exports) {
        Nan::SetMethod(exports, "startStreaming", StartStreaming);
        Nan::SetMethod(exports, "stopStreaming", StopStreaming);
    }

private:
    static RealSenseWorker* worker;
};

// Initialize static member
RealSenseWorker* RealSenseManager::worker = nullptr;

NODE_MODULE(realsense, RealSenseManager::Init)