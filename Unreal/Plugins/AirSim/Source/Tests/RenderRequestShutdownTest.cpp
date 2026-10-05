#include "RenderRequest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Async/TaskGraphInterfaces.h"
#include <chrono>
#include <future>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRenderRequestShutdownTest, "AirSim.RenderRequest.Shutdown",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FRenderRequestShutdownTest::RunTest(const FString& Parameters)
{
    // Keep the game thread here while the worker queues its capture. No real
    // viewport is needed: cancelled callbacks must never touch these nulls.
    for (int session = 0; session < 2; ++session) {
        RenderRequest::BeginPlay();
        bool pose_callback_called = false;
        auto request = std::make_shared<RenderRequest>(nullptr, [&]() { pose_callback_called = true; });
        std::weak_ptr<RenderRequest> weak_request = request;
        auto result = std::async(std::launch::async, [request]() {
            std::shared_ptr<RenderRequest::RenderParams> params[] = {
                std::make_shared<RenderRequest::RenderParams>(nullptr, nullptr, false, false)
            };
            std::vector<std::shared_ptr<RenderRequest::RenderResult>> images;
            return request->getScreenshot(params, images, 1, false);
        });

        bool registered = false;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!registered && std::chrono::steady_clock::now() < deadline) {
            {
                std::lock_guard<std::mutex> lock(RenderRequest::pending_mutex_);
                registered = !RenderRequest::pending_requests_.empty();
            }
            if (!registered)
                FPlatformProcess::Sleep(0.001f);
        }
        TestTrue(TEXT("Capture registered while the game thread is unavailable"), registered);
        RenderRequest::Shutdown();
        TestFalse(TEXT("Shutdown releases the worker with a cancelled result"), result.get());
        RenderRequest::Shutdown(); // idempotent, as HUD and SimMode both stop.

        request.reset();
        FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);
        TestFalse(TEXT("Late game-thread callback does not access caller data"), pose_callback_called);
        TestTrue(TEXT("Cancelled request is released after queued work drains"), weak_request.expired());

        auto late_request = std::make_shared<RenderRequest>(nullptr, []() {});
        std::shared_ptr<RenderRequest::RenderParams> params[] = {
            std::make_shared<RenderRequest::RenderParams>(nullptr, nullptr, false, false)
        };
        std::vector<std::shared_ptr<RenderRequest::RenderResult>> images;
        TestFalse(TEXT("New capture is rejected after shutdown"), late_request->getScreenshot(params, images, 1, false));
    }
    RenderRequest::BeginPlay();
    return true;
}
#endif
