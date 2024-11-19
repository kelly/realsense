#include <nan.h>
#include <librealsense2/rs.hpp>
#include <thread>
#include <atomic>
#include <vector>
#include <memory>
#include <chrono>
#include <queue>
#include <mutex>
#include <uv.h>

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

struct FrameData {
    int depthWidth;
    int depthHeight;
    std::vector<uint16_t> depthData;

    int colorWidth;
    int colorHeight;
    std::vector<uint8_t> colorData; // Assuming BGR8 format
};

class RealSenseWorker : public Nan::AsyncWorker {
public:
    RealSenseWorker(int depthWidth, int depthHeight,
                    int colorWidth, int colorHeight,
                    int fps, int maxFPS,
                    Nan::Callback* progressCallback, Nan::Callback* completeCallback)
        : Nan::AsyncWorker(completeCallback), progressCallback(progressCallback), stopped(false),
          depthWidth(depthWidth), depthHeight(depthHeight),
          colorWidth(colorWidth), colorHeight(colorHeight),
          fps(fps), maxFPS(maxFPS), finished(false) {

        // Configure the pipeline to stream depth and color frames with the same FPS
        rs2::config cfg;
        cfg.enable_stream(RS2_STREAM_DEPTH, depthWidth, depthHeight, RS2_FORMAT_Z16, fps);
        cfg.enable_stream(RS2_STREAM_COLOR, colorWidth, colorHeight, RS2_FORMAT_BGR8, fps);
        pipe.start(cfg);

        lastFrameTime = std::chrono::steady_clock::now();

        // Initialize uv_async_t
        uv_async_init(uv_default_loop(), &asyncHandle, AsyncCallback);
        asyncHandle.data = this;
    }

    ~RealSenseWorker() {
        Stop();
    }

    void Execute() override {
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

                FrameData frameData;

                // Depth frame
                frameData.depthWidth = depth.get_width();
                frameData.depthHeight = depth.get_height();
                size_t depthDataSize = frameData.depthWidth * frameData.depthHeight;
                frameData.depthData.resize(depthDataSize);
                memcpy(frameData.depthData.data(), depth.get_data(), depthDataSize * sizeof(uint16_t));

                // Color frame
                frameData.colorWidth = color.get_width();
                frameData.colorHeight = color.get_height();
                size_t colorDataSize = frameData.colorWidth * frameData.colorHeight * 3; // Assuming BGR8 format
                frameData.colorData.resize(colorDataSize);
                memcpy(frameData.colorData.data(), color.get_data(), colorDataSize);

                // Push frame data to queue
                {
                    std::lock_guard<std::mutex> lock(queueMutex);
                    frameQueue.push(std::move(frameData));
                }

                // Notify main thread
                uv_async_send(&asyncHandle);

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
        finished = true;
    }

    void HandleErrorCallback() override {
        Nan::HandleScope scope;

        v8::Local<v8::Value> argv[] = {
            Nan::Error(ErrorMessage())
        };

        callback->Call(1, argv, async_resource);
    }

    static void AsyncCallback(uv_async_t* handle) {
        RealSenseWorker* self = static_cast<RealSenseWorker*>(handle->data);
        self->ProcessData();
    }

    void ProcessData() {
        Nan::HandleScope scope;

        std::queue<FrameData> localQueue;

        // Move frames from the shared queue to a local queue
        {
            std::lock_guard<std::mutex> lock(queueMutex);
            std::swap(frameQueue, localQueue);
        }

        while (!localQueue.empty()) {
            FrameData& frameData = localQueue.front();

            // Create JavaScript objects for depth and color frames
            v8::Local<v8::Object> depthBuffer = Nan::CopyBuffer(
                reinterpret_cast<char*>(frameData.depthData.data()),
                frameData.depthData.size() * sizeof(uint16_t)).ToLocalChecked();

            v8::Local<v8::Object> colorBuffer = Nan::CopyBuffer(
                reinterpret_cast<char*>(frameData.colorData.data()),
                frameData.colorData.size()).ToLocalChecked();

            v8::Local<v8::Object> depthFrame = Nan::New<v8::Object>();
            Nan::Set(depthFrame, Nan::New("width").ToLocalChecked(), Nan::New(frameData.depthWidth));
            Nan::Set(depthFrame, Nan::New("height").ToLocalChecked(), Nan::New(frameData.depthHeight));
            Nan::Set(depthFrame, Nan::New("data").ToLocalChecked(), depthBuffer);

            v8::Local<v8::Object> colorFrame = Nan::New<v8::Object>();
            Nan::Set(colorFrame, Nan::New("width").ToLocalChecked(), Nan::New(frameData.colorWidth));
            Nan::Set(colorFrame, Nan::New("height").ToLocalChecked(), Nan::New(frameData.colorHeight));
            Nan::Set(colorFrame, Nan::New("data").ToLocalChecked(), colorBuffer);

            // Create result object
            v8::Local<v8::Object> result = Nan::New<v8::Object>();
            Nan::Set(result, Nan::New("depthFrame").ToLocalChecked(), depthFrame);
            Nan::Set(result, Nan::New("colorFrame").ToLocalChecked(), colorFrame);

            // Call the progress callback with the result
            v8::Local<v8::Value> argv[] = { result };
            progressCallback->Call(1, argv, async_resource);

            localQueue.pop();
        }
    }

    void HandleOKCallback() override {
        // Clean up the uv_async_t handle
        uv_close(reinterpret_cast<uv_handle_t*>(&asyncHandle), nullptr);

        Nan::HandleScope scope;
        // Call the completion callback without arguments
        callback->Call(0, nullptr, async_resource);
    }

    void Stop() {
        if (stopped) return;
        stopped = true;
        try {
            pipe.stop();
        } catch (...) {
            // Ignore exceptions during cleanup
        }
    }

    bool IsFinished() const { return finished; }

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

    // To check if the worker has finished
    std::atomic<bool> finished;

    // Frame queue and mutex
    std::queue<FrameData> frameQueue;
    std::mutex queueMutex;

    // uv_async_t handle
    uv_async_t asyncHandle;
};

std::unique_ptr<RealSenseWorker> rsWorker;

// Start streaming
void StartStreaming(const Nan::FunctionCallbackInfo<v8::Value>& info) {
    if (rsWorker) {
        Nan::ThrowError("Streaming is already started");
        return;
    }

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
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();

    if (rsWorker) {
        rsWorker->Stop();

        v8::MaybeLocal<v8::Promise::Resolver> maybeResolver = v8::Promise::Resolver::New(context);
        v8::Local<v8::Promise::Resolver> resolver;
        if (!maybeResolver.ToLocal(&resolver)) {
            Nan::ThrowError("Failed to create Promise::Resolver");
            return;
        }
        info.GetReturnValue().Set(resolver->GetPromise());

        // Store the resolver
        auto persistentResolver = new Nan::Persistent<v8::Promise::Resolver>(resolver);

        // Create a timer to check if the worker has finished
        uv_timer_t* timer = new uv_timer_t;
        timer->data = persistentResolver;

        uv_timer_init(uv_default_loop(), timer);

        uv_timer_start(timer, [](uv_timer_t* handle) {
            auto persistentResolver = static_cast<Nan::Persistent<v8::Promise::Resolver>*>(handle->data);
            v8::Isolate* isolate = v8::Isolate::GetCurrent();
            v8::HandleScope scope(isolate);
            v8::Local<v8::Context> context = isolate->GetCurrentContext();

            if (rsWorker == nullptr || rsWorker->IsFinished()) {
                // Stop the timer
                uv_timer_stop(handle);
                uv_close((uv_handle_t*)handle, [](uv_handle_t* h) { delete h; });

                // Resolve the promise
                v8::Local<v8::Promise::Resolver> resolver = persistentResolver->Get(isolate);
                v8::Maybe<bool> didResolve = resolver->Resolve(context, Nan::Undefined());
                if (didResolve.IsNothing() || !didResolve.FromJust()) {
                    Nan::ThrowError("Failed to resolve the promise");
                }

                persistentResolver->Reset();
                delete persistentResolver;
                rsWorker.reset();
            }
        }, 0, 100); // Check every 100 ms
    } else {
        // If there's no worker, return a resolved promise
        v8::MaybeLocal<v8::Promise::Resolver> maybeResolver = v8::Promise::Resolver::New(context);
        v8::Local<v8::Promise::Resolver> resolver;
        if (!maybeResolver.ToLocal(&resolver)) {
            Nan::ThrowError("Failed to create Promise::Resolver");
            return;
        }
        v8::Maybe<bool> didResolve = resolver->Resolve(context, Nan::Undefined());
        if (didResolve.IsNothing() || !didResolve.FromJust()) {
            Nan::ThrowError("Failed to resolve the promise");
            return;
        }
        info.GetReturnValue().Set(resolver->GetPromise());
    }
}

// Initialize the addon
void Init(v8::Local<v8::Object> exports) {
    Nan::SetMethod(exports, "startStreaming", StartStreaming);
    Nan::SetMethod(exports, "stopStreaming", StopStreaming);
}

NODE_MODULE(realsense, Init)
