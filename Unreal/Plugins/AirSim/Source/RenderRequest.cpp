#include "RenderRequest.h"
#include "TextureResource.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Async/TaskGraphInterfaces.h"
#include "ImageUtils.h"

#include "AirBlueprintLib.h"
#include "Async/Async.h"
#include "RenderingThread.h"
#include <algorithm>

std::mutex RenderRequest::pending_mutex_;
bool RenderRequest::shutting_down_ = false;
std::vector<std::weak_ptr<RenderRequest>> RenderRequest::pending_requests_;

RenderRequest::RenderRequest(UGameViewportClient* game_viewport, std::function<void()>&& query_camera_pose_cb)
    : req_size_(0), wait_signal_(new msr::airlib::WorkerThreadSignal), game_viewport_(game_viewport), query_camera_pose_cb_(std::move(query_camera_pose_cb))
{
}

RenderRequest::~RenderRequest()
{
}

void RenderRequest::BeginPlay()
{
    check(IsInGameThread());
    std::lock_guard<std::mutex> lock(pending_mutex_);
    shutting_down_ = false;
}

void RenderRequest::Shutdown()
{
    check(IsInGameThread());
    std::vector<std::shared_ptr<RenderRequest>> pending;
    {
        std::lock_guard<std::mutex> lock(pending_mutex_);
        if (shutting_down_)
            return;
        shutting_down_ = true;
        for (auto& weak : pending_requests_) {
            if (auto request = weak.lock()) {
                request->cancelled_ = true;
                pending.push_back(std::move(request));
            }
        }
        pending_requests_.clear();
    }

    // These delegates and queued game-thread tasks may reference the caller's
    // camera/response data. Prevent them from running before waking the caller.
    for (auto it = pending.rbegin(); it != pending.rend(); ++it) {
        auto& request = *it;
        if (request->end_draw_handle_.IsValid()) {
            request->game_viewport_->OnEndDraw().Remove(request->end_draw_handle_);
            request->end_draw_handle_.Reset();
            request->game_viewport_->bDisableWorldRendering = request->saved_DisableWorldRendering_;
        }
    }
    // A render command may already be reading into the result buffers.
    // Keep the caller and its resources alive until those commands finish.
    if (!pending.empty())
        FlushRenderingCommands();
    for (auto& request : pending)
        request->wait_signal_->signal();
}

// read pixels from render target using render thread, then compress the result into PNG
// argument on the thread that calls this method.
bool RenderRequest::getScreenshot(std::shared_ptr<RenderParams> params[], std::vector<std::shared_ptr<RenderResult>>& results, unsigned int req_size, bool use_safe_method)
{
    if (req_size == 0)
        return true;
    //TODO: is below really needed?
    for (unsigned int i = 0; i < req_size; ++i) {
        results.push_back(std::make_shared<RenderResult>());

        if (!params[i]->pixels_as_float)
            results[i]->bmp.Reset();
        else
            results[i]->bmp_float.Reset();
        results[i]->time_stamp = 0;
    }

    //make sure we are not on the rendering thread
    CheckNotBlockedOnRenderThread();

    if (use_safe_method) {
        for (unsigned int i = 0; i < req_size; ++i) {
            //TODO: below doesn't work right now because it must be running in game thread
            FIntPoint img_size;
            if (!params[i]->pixels_as_float) {
                //below is documented method but more expensive because it forces flush
                FTextureRenderTargetResource* rt_resource = params[i]->render_target->GameThread_GetRenderTargetResource();
                auto flags = setupRenderResource(rt_resource, params[i].get(), results[i].get(), img_size);
                rt_resource->ReadPixels(results[i]->bmp, flags);
            }
            else {
                FTextureRenderTargetResource* rt_resource = params[i]->render_target->GetRenderTargetResource();
                setupRenderResource(rt_resource, params[i].get(), results[i].get(), img_size);
                rt_resource->ReadFloat16Pixels(results[i]->bmp_float);
            }
        }
    }
    else {
        //wait for render thread to pick up our task
        params_.assign(params, params + req_size);
        results_ = results;
        req_size_ = req_size;

        auto self = shared_from_this();
        {
            std::lock_guard<std::mutex> lock(pending_mutex_);
            if (shutting_down_)
                return false;
            pending_requests_.erase(std::remove_if(pending_requests_.begin(), pending_requests_.end(),
                [](const std::weak_ptr<RenderRequest>& request) { return request.expired(); }), pending_requests_.end());
            pending_requests_.push_back(self);
        }

        // Queue up the task of querying camera pose in the game thread and synchronizing render thread with camera pose
        AsyncTask(ENamedThreads::GameThread, [self, this]() {
            check(IsInGameThread());
            if (cancelled_)
                return;

            saved_DisableWorldRendering_ = game_viewport_->bDisableWorldRendering;
            game_viewport_->bDisableWorldRendering = 0;
            end_draw_handle_ = game_viewport_->OnEndDraw().AddLambda([self, this] {
                check(IsInGameThread());
                if (cancelled_)
                    return;

                // capture CameraPose for this frame
                query_camera_pose_cb_();

                // The completion is called immeidately after GameThread sends the
                // rendering commands to RenderThread. Hence, our ExecuteTask will
                // execute *immediately* after RenderThread renders the scene!
                ENQUEUE_RENDER_COMMAND(SceneDrawCompletion)
                (
                    [self](FRHICommandListImmediate& RHICmdList) {
                        self->ExecuteTask();
                    });

                game_viewport_->bDisableWorldRendering = saved_DisableWorldRendering_;

                assert(end_draw_handle_.IsValid());
                game_viewport_->OnEndDraw().Remove(end_draw_handle_);
                end_draw_handle_.Reset();
            });

            // while we're still on GameThread, enqueue request for capture the scene!
            for (unsigned int i = 0; i < req_size_; ++i) {
                params_[i]->render_component->CaptureSceneDeferred();
            }
        });

        // Both successful readback and Shutdown release this wait. Shutdown
        // drains render commands first, so the caller can safely destroy its data.
        wait_signal_->wait();
        if (cancelled_)
            return false;
    }

    for (unsigned int i = 0; i < req_size; ++i) {
        if (!params[i]->pixels_as_float) {
            if (results[i]->width != 0 && results[i]->height != 0) {
                results[i]->image_data_uint8.SetNumUninitialized(results[i]->width * results[i]->height * 3, EAllowShrinking::No);
                if (params[i]->compress)
                    UAirBlueprintLib::CompressImageArray(results[i]->width, results[i]->height, results[i]->bmp, results[i]->image_data_uint8);
                else {
                    uint8* ptr = results[i]->image_data_uint8.GetData();
                    for (const auto& item : results[i]->bmp) {
                        *ptr++ = item.B;
                        *ptr++ = item.G;
                        *ptr++ = item.R;
                    }
                }
            }
        }
        else {
            results[i]->image_data_float.SetNumUninitialized(results[i]->width * results[i]->height);
            float* ptr = results[i]->image_data_float.GetData();
            for (const auto& item : results[i]->bmp_float) {
                *ptr++ = item.R.GetFloat();
            }
        }
    }
    return true;
}

FReadSurfaceDataFlags RenderRequest::setupRenderResource(const FTextureRenderTargetResource* rt_resource, const RenderParams* params, RenderResult* result, FIntPoint& size)
{
    size = rt_resource->GetSizeXY();
    result->width = size.X;
    result->height = size.Y;
    FReadSurfaceDataFlags flags(RCM_UNorm, CubeFace_MAX);
    flags.SetLinearToGamma(false);

    return flags;
}

void RenderRequest::ExecuteTask()
{
    if (cancelled_)
        return; // Shutdown signals the waiter after draining render commands.
    if (!params_.empty() && req_size_ > 0) {
        for (unsigned int i = 0; i < req_size_; ++i) {
            FRHICommandListImmediate& RHICmdList = GetImmediateCommandList_ForRenderCommand();
            auto rt_resource = params_[i]->render_target->GetRenderTargetResource();
            if (rt_resource != nullptr) {
                const FTextureRHIRef& rhi_texture = rt_resource->GetRenderTargetTexture();
                FIntPoint size;
                auto flags = setupRenderResource(rt_resource, params_[i].get(), results_[i].get(), size);

                //should we be using ENQUEUE_UNIQUE_RENDER_COMMAND_ONEPARAMETER which was in original commit by @saihv
                //https://github.com/Microsoft/AirSim/pull/162/commits/63e80c43812300a8570b04ed42714a3f6949e63f#diff-56b790f9394f7ca1949ddbb320d8456fR64
                if (!params_[i]->pixels_as_float) {
                    //below is undocumented method that avoids flushing, but it seems to segfault every 2000 or so calls
                    RHICmdList.ReadSurfaceData(
                        rhi_texture,
                        FIntRect(0, 0, size.X, size.Y),
                        results_[i]->bmp,
                        flags);
                }
                else {
                    RHICmdList.ReadSurfaceFloatData(
                        rhi_texture,
                        FIntRect(0, 0, size.X, size.Y),
                        results_[i]->bmp_float,
                        CubeFace_PosX,
                        0,
                        0);
                }
            }

            results_[i]->time_stamp = msr::airlib::ClockFactory::get()->nowNanos();
        }

        req_size_ = 0;

        wait_signal_->signal();
    }
}
