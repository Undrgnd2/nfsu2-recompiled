#include <android_native_app_glue.h>
#include <android/log.h>
#include <vulkan/vulkan.h>
#include <algorithm>
#include <vector>
#include <cmath>
#include <atomic>
#include <mutex>
#include <stdexcept>
#include <string>
#include <cstring>
#include <thread>
#include <chrono>
#include <filesystem>
#include <cstdlib>
#include <unistd.h>
#include "vertex_formats.h"
#include "guest_memory.h"
#include "game_data.h"
#include "guest_runtime.h"
#include "d3d9_bridge.h"
#include "guest_boot_worker.h"
#include "gamepad_input.h"
#include "vulkan_driver.h"
#include "nfs_widescreen.h"
#include "nfs_drift.h"

static void showStatus(android_app* app, const char* message) {
    JNIEnv* env = nullptr;
    if (app->activity->vm->AttachCurrentThread(&env, nullptr) != JNI_OK) return;
    jclass cls = env->GetObjectClass(app->activity->clazz);
    jmethodID method = env->GetMethodID(cls, "setStatus", "(Ljava/lang/String;)V");
    if (method) {
        jstring text = env->NewStringUTF(message);
        env->CallVoidMethod(app->activity->clazz, method, text);
        env->DeleteLocalRef(text);
    }
    if (env->ExceptionCheck()) { env->ExceptionDescribe(); env->ExceptionClear(); }
    env->DeleteLocalRef(cls);
    app->activity->vm->DetachCurrentThread();
}

// Independent bring-up target. No guest game code is executed yet.
static void check(VkResult r, const char* operation) {
    if (r != VK_SUCCESS) throw std::runtime_error(std::string(operation) + ": " + std::to_string(r));
}
#define VK_CHECK(call) check((call), #call)
struct Renderer {
    VkInstance instance{};
    VkSurfaceKHR surface{};
    VkPhysicalDevice gpu{};
    VkDevice device{};
    VkQueue queue{};
    uint32_t family{};
    VkSwapchainKHR swapchain{};
    VkExtent2D extent{};
    VkRenderPass pass{};
    VkCommandPool pool{};
    VkCommandBuffer command{};
    VkSemaphore acquired{};
    std::vector<VkImageView> views;
    std::vector<VkFramebuffer> frames;
    std::vector<VkSemaphore> ready;
    uint64_t presented = 0;

    void stop() {
        if (device) {
            vkDeviceWaitIdle(device);
            for (auto f : frames) vkDestroyFramebuffer(device, f, nullptr);
            for (auto v : views) vkDestroyImageView(device, v, nullptr);
            for (auto s : ready) vkDestroySemaphore(device, s, nullptr);
            if (acquired) vkDestroySemaphore(device, acquired, nullptr);
            if (pool) vkDestroyCommandPool(device, pool, nullptr);
            if (pass) vkDestroyRenderPass(device, pass, nullptr);
            if (swapchain) vkDestroySwapchainKHR(device, swapchain, nullptr);
            vkDestroyDevice(device, nullptr);
        }
        if (surface) vkDestroySurfaceKHR(instance, surface, nullptr);
        if (instance) vkDestroyInstance(instance, nullptr);
        *this = Renderer{};
    }
    void start(ANativeWindow* window) {
        const char* extensions[] = {VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_ANDROID_SURFACE_EXTENSION_NAME};
        VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
        app.pApplicationName = "NFSU2 Vulkan 1.1 bootstrap";
        app.apiVersion = VK_API_VERSION_1_1;
        VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        ci.pApplicationInfo = &app; ci.enabledExtensionCount = 2; ci.ppEnabledExtensionNames = extensions;
        VK_CHECK(vkCreateInstance(&ci, nullptr, &instance));
        VkAndroidSurfaceCreateInfoKHR sci{VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR};
        sci.window = window;
        VK_CHECK(vkCreateAndroidSurfaceKHR(instance, &sci, nullptr, &surface));
        uint32_t count = 0;
        VK_CHECK(vkEnumeratePhysicalDevices(instance, &count, nullptr));
        std::vector<VkPhysicalDevice> devices(count);
        VK_CHECK(vkEnumeratePhysicalDevices(instance, &count, devices.data()));
        for (auto candidate : devices) {
            VkPhysicalDeviceProperties props{}; vkGetPhysicalDeviceProperties(candidate, &props);
            if (props.apiVersion < VK_API_VERSION_1_1) continue;
            uint32_t ec = 0;
            VK_CHECK(vkEnumerateDeviceExtensionProperties(candidate, nullptr, &ec, nullptr));
            std::vector<VkExtensionProperties> exts(ec);
            VK_CHECK(vkEnumerateDeviceExtensionProperties(candidate, nullptr, &ec, exts.data()));
            bool hasSwapchain = false;
            for (auto& e : exts) hasSwapchain |= std::strcmp(e.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0;
            if (!hasSwapchain) continue;
            uint32_t qc = 0; vkGetPhysicalDeviceQueueFamilyProperties(candidate, &qc, nullptr);
            std::vector<VkQueueFamilyProperties> queues(qc);
            vkGetPhysicalDeviceQueueFamilyProperties(candidate, &qc, queues.data());
            for (uint32_t i = 0; i < qc; ++i) {
                VkBool32 present = false;
                VK_CHECK(vkGetPhysicalDeviceSurfaceSupportKHR(candidate, i, surface, &present));
                if ((queues[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && present) { gpu = candidate; family = i; break; }
            }
            if (gpu) {
                __android_log_print(ANDROID_LOG_INFO, "NFSU2", "GPU %s; API %u.%u; required Vulkan 1.1", props.deviceName,
                    VK_VERSION_MAJOR(props.apiVersion), VK_VERSION_MINOR(props.apiVersion));
                auto vertexMissing = unsupportedD3DVertexFormats(candidate, vkGetPhysicalDeviceFormatProperties);
                __android_log_print(vertexMissing.empty() ? ANDROID_LOG_INFO : ANDROID_LOG_WARN, "NFSU2",
                    "D3D9 vertex types without Vulkan vertex fetch: %s", vertexMissing.empty() ? "none" : vertexMissing.c_str());
                break;
            }
        }
        if (!gpu) throw std::runtime_error("No Vulkan 1.1 graphics/present device");
        float priority = 1.f;
        VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        qci.queueFamilyIndex = family; qci.queueCount = 1; qci.pQueuePriorities = &priority;
        const char* dext = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
        VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
        dci.queueCreateInfoCount = 1; dci.pQueueCreateInfos = &qci;
        dci.enabledExtensionCount = 1; dci.ppEnabledExtensionNames = &dext;
        // No optional physical-device features or newer Vulkan extensions.
        VK_CHECK(vkCreateDevice(gpu, &dci, nullptr, &device));
        vkGetDeviceQueue(device, family, 0, &queue);
        VkSurfaceCapabilitiesKHR caps{};
        VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(gpu, surface, &caps));
        if (!(caps.supportedUsageFlags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)) throw std::runtime_error("Surface lacks color attachment usage");
        uint32_t fc = 0; VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(gpu, surface, &fc, nullptr));
        if (!fc) throw std::runtime_error("Surface has no formats");
        std::vector<VkSurfaceFormatKHR> formats(fc);
        VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(gpu, surface, &fc, formats.data()));
        auto format = formats.front();
        if (format.format == VK_FORMAT_UNDEFINED) format.format = VK_FORMAT_R8G8B8A8_UNORM;
        for (auto f : formats) if (f.format == VK_FORMAT_R8G8B8A8_UNORM || f.format == VK_FORMAT_B8G8R8A8_UNORM) { format = f; break; }
        extent = caps.currentExtent;
        if (extent.width == UINT32_MAX) {
            extent.width = std::clamp(uint32_t(std::max(1, ANativeWindow_getWidth(window))), caps.minImageExtent.width, caps.maxImageExtent.width);
            extent.height = std::clamp(uint32_t(std::max(1, ANativeWindow_getHeight(window))), caps.minImageExtent.height, caps.maxImageExtent.height);
        }
        if (!extent.width || !extent.height) throw std::runtime_error("Surface is minimized");
        VkSwapchainCreateInfoKHR sw{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
        sw.surface = surface; sw.minImageCount = caps.minImageCount + 1;
        if (caps.maxImageCount) sw.minImageCount = std::min(sw.minImageCount, caps.maxImageCount);
        sw.imageFormat = format.format; sw.imageColorSpace = format.colorSpace; sw.imageExtent = extent;
        sw.imageArrayLayers = 1; sw.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        sw.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE; sw.preTransform = caps.currentTransform;
        for (auto bit : {VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR, VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
                         VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR, VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR})
            if (caps.supportedCompositeAlpha & bit) { sw.compositeAlpha = bit; break; }
        sw.presentMode = VK_PRESENT_MODE_FIFO_KHR; sw.clipped = VK_TRUE;
        VK_CHECK(vkCreateSwapchainKHR(device, &sw, nullptr, &swapchain));
        uint32_t ic = 0; VK_CHECK(vkGetSwapchainImagesKHR(device, swapchain, &ic, nullptr));
        std::vector<VkImage> images(ic); VK_CHECK(vkGetSwapchainImagesKHR(device, swapchain, &ic, images.data()));
        VkAttachmentDescription attachment{};
        attachment.format = format.format; attachment.samples = VK_SAMPLE_COUNT_1_BIT;
        attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED; attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        VkAttachmentReference ref{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkSubpassDescription sub{}; sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        sub.colorAttachmentCount = 1; sub.pColorAttachments = &ref;
        VkSubpassDependency dep{}; dep.srcSubpass = VK_SUBPASS_EXTERNAL; dep.dstSubpass = 0;
        dep.srcStageMask = dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        VkRenderPassCreateInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
        rp.attachmentCount = 1; rp.pAttachments = &attachment; rp.subpassCount = 1; rp.pSubpasses = &sub;
        rp.dependencyCount = 1; rp.pDependencies = &dep;
        VK_CHECK(vkCreateRenderPass(device, &rp, nullptr, &pass));
        VkSemaphoreCreateInfo sem{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        VK_CHECK(vkCreateSemaphore(device, &sem, nullptr, &acquired));
        for (auto image : images) {
            VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
            vi.image = image; vi.viewType = VK_IMAGE_VIEW_TYPE_2D; vi.format = format.format;
            vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            VkImageView view{}; VK_CHECK(vkCreateImageView(device, &vi, nullptr, &view)); views.push_back(view);
            VkFramebufferCreateInfo fb{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
            fb.renderPass = pass; fb.attachmentCount = 1; fb.pAttachments = &views.back();
            fb.width = extent.width; fb.height = extent.height; fb.layers = 1;
            VkFramebuffer frame{}; VK_CHECK(vkCreateFramebuffer(device, &fb, nullptr, &frame)); frames.push_back(frame);
            VkSemaphore s{}; VK_CHECK(vkCreateSemaphore(device, &sem, nullptr, &s)); ready.push_back(s);
        }
        VkCommandPoolCreateInfo cp{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        cp.queueFamilyIndex = family; cp.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        VK_CHECK(vkCreateCommandPool(device, &cp, nullptr, &pool));
        VkCommandBufferAllocateInfo ca{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        ca.commandPool = pool; ca.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; ca.commandBufferCount = 1;
        VK_CHECK(vkAllocateCommandBuffers(device, &ca, &command));
        __android_log_print(ANDROID_LOG_INFO, "NFSU2", "Vulkan ready %ux%u; game draw bridge pending", extent.width, extent.height);
    }
    bool draw() {
        uint32_t index;
        auto result = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, acquired, VK_NULL_HANDLE, &index);
        if (result == VK_ERROR_OUT_OF_DATE_KHR) return false;
        if (result != VK_SUBOPTIMAL_KHR) check(result, "acquire");
        VK_CHECK(vkResetCommandBuffer(command, 0));
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        VK_CHECK(vkBeginCommandBuffer(command, &begin));
        VkClearValue color{}; color.color = {{0.035f, 0.16f, 0.22f, 1.f}};
        VkRenderPassBeginInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        rp.renderPass = pass; rp.framebuffer = frames[index]; rp.renderArea.extent = extent;
        rp.clearValueCount = 1; rp.pClearValues = &color;
        vkCmdBeginRenderPass(command, &rp, VK_SUBPASS_CONTENTS_INLINE); vkCmdEndRenderPass(command);
        VK_CHECK(vkEndCommandBuffer(command));
        VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submit.waitSemaphoreCount = 1; submit.pWaitSemaphores = &acquired; submit.pWaitDstStageMask = &waitStage;
        submit.commandBufferCount = 1; submit.pCommandBuffers = &command;
        submit.signalSemaphoreCount = 1; submit.pSignalSemaphores = &ready[index];
        VK_CHECK(vkQueueSubmit(queue, 1, &submit, VK_NULL_HANDLE));
        VkPresentInfoKHR present{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
        present.waitSemaphoreCount = 1; present.pWaitSemaphores = &ready[index];
        present.swapchainCount = 1; present.pSwapchains = &swapchain; present.pImageIndices = &index;
        result = vkQueuePresentKHR(queue, &present);
        // Deliberately serialized for bring-up; replace with frame fences for the game renderer.
        VK_CHECK(vkQueueWaitIdle(queue));
        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) return false;
        check(result, "present");
        if (++presented == 1 || presented % 300 == 0)
            __android_log_print(ANDROID_LOG_INFO, "NFSU2", "Presented frames: %llu", static_cast<unsigned long long>(presented));
        return true;
    }
};
struct Host {
    Renderer renderer;
    bool resumed = false;
    bool failed = false;
    bool guestAttempted = false;
    std::string guestStatus;
    GuestBootWorker guestWorker;
};
static GamepadInput gamepad;
// Device ids the Java side accepted as real gamepads (updateGamepads). Events
// from anything else never reach the pad: several MediaTek phones expose
// sensors and side keys as joysticks whose X axis rests off centre.
static std::mutex gamepadDevicesMutex;
static std::vector<int32_t> gamepadDevices;
static std::atomic<bool> gamepadsRemoved{false};
static bool acceptedGamepad(const AInputEvent* event){
    std::lock_guard<std::mutex> lock(gamepadDevicesMutex);
    return std::find(gamepadDevices.begin(),gamepadDevices.end(),AInputEvent_getDeviceId(event))!=gamepadDevices.end();
}
static void setPadKey(unsigned scan,bool down){setGuestKey(scan,down);}
static void publishPad(){
    auto state=gamepad.padState();
    // Z: LT pushes it one way and RT the other, as DirectInput shows an Xbox pad.
    const float axes[6]={state.leftX,state.leftY,state.leftTrigger-state.rightTrigger,state.rightX,state.rightY,0.f};
    setGuestPad(axes,state.buttons,state.hatX,state.hatY);
}
static void onCommand(android_app* app, int32_t command) {
    auto& h = *static_cast<Host*>(app->userData);
    if (command == APP_CMD_RESUME) { h.resumed = true; h.failed = false; }
    if (command == APP_CMD_PAUSE) { setGuestPaused(true);gamepad.releaseAll(setPadKey);clearGuestInput();h.resumed = false; h.renderer.stop();acknowledgeD3D9Surface(false);setD3D9HostWindow(nullptr); }
    if (command == APP_CMD_TERM_WINDOW || command == APP_CMD_WINDOW_RESIZED || command == APP_CMD_INIT_WINDOW) {
        setGuestPaused(true);h.renderer.stop(); h.failed = false;
        acknowledgeD3D9Surface(false);setD3D9HostWindow(nullptr);
    }
}
// Set when the player actually uses the pad (a button, a stick past half way,
// a trigger), so the touch controls can hide again after a touch revealed
// them; GameActivity polls it with the driving-mode check.
static std::atomic<bool> gamepadUsed{false};
static void notePadUse(const AInputEvent* event){
    if(AInputEvent_getType(event)==AINPUT_EVENT_TYPE_KEY){if(AKeyEvent_getAction(event)==AKEY_EVENT_ACTION_DOWN)gamepadUsed=true;return;}
    for(int32_t axis:{AMOTION_EVENT_AXIS_X,AMOTION_EVENT_AXIS_Y,AMOTION_EVENT_AXIS_Z,AMOTION_EVENT_AXIS_RZ,AMOTION_EVENT_AXIS_HAT_X,AMOTION_EVENT_AXIS_HAT_Y})
        if(std::fabs(AMotionEvent_getAxisValue(event,axis,0))>.5f){gamepadUsed=true;return;}
    for(int32_t axis:{AMOTION_EVENT_AXIS_LTRIGGER,AMOTION_EVENT_AXIS_RTRIGGER,AMOTION_EVENT_AXIS_BRAKE,AMOTION_EVENT_AXIS_GAS})
        if(AMotionEvent_getAxisValue(event,axis,0)>.3f){gamepadUsed=true;return;}
}
extern "C" JNIEXPORT jboolean JNICALL Java_com_nfsu2_androidevolved_GameActivity_nativeGamepadUsed(JNIEnv*,jclass){return gamepadUsed.exchange(false)?JNI_TRUE:JNI_FALSE;}
static int32_t onInput(android_app*,AInputEvent* event){
    int32_t type=AInputEvent_getType(event);
    gamepad.setKeyEmulation(!guestGamepadInUse());
    if(type==AINPUT_EVENT_TYPE_MOTION){
        if(!acceptedGamepad(event))return 0;
        bool handled=gamepad.motion(event,guestDrivingControls(),setPadKey);if(handled){notePadUse(event);publishPad();}return handled?1:0;
    }
    if(type!=AINPUT_EVENT_TYPE_KEY)return 0;
    // Any key of an accepted pad, whatever source it reports (pads in keyboard
    // mode send KEYBOARD); keys it does not map fall through to the table below.
    if(acceptedGamepad(event)&&gamepad.key(event,guestDrivingControls(),setPadKey)){notePadUse(event);publishPad();return 1;}
    int32_t key=AKeyEvent_getKeyCode(event);unsigned scan=0;
    switch(key){
    case AKEYCODE_DPAD_UP:scan=0xc8;break;case AKEYCODE_DPAD_DOWN:scan=0xd0;break;
    case AKEYCODE_DPAD_LEFT:scan=0xcb;break;case AKEYCODE_DPAD_RIGHT:scan=0xcd;break;
    case AKEYCODE_ENTER:case AKEYCODE_BUTTON_A:scan=0x1c;break;
    case AKEYCODE_ESCAPE:case AKEYCODE_BACK:case AKEYCODE_BUTTON_B:scan=1;break;
    case AKEYCODE_SPACE:case AKEYCODE_BUTTON_X:scan=0x39;break;
    case AKEYCODE_DEL:scan=0xe;break;
    case AKEYCODE_SHIFT_LEFT:case AKEYCODE_BUTTON_Y:scan=0x2a;break;
    case AKEYCODE_CTRL_LEFT:scan=0x1d;break;case AKEYCODE_TAB:scan=0xf;break;
    default:{static const unsigned letters[]={0x1e,0x30,0x2e,0x20,0x12,0x21,0x22,0x23,0x17,0x24,0x25,0x26,0x32,0x31,0x18,0x19,0x10,0x13,0x1f,0x14,0x16,0x2f,0x11,0x2d,0x15,0x2c};
        if(key>=AKEYCODE_A&&key<=AKEYCODE_Z)scan=letters[key-AKEYCODE_A];
        else if(key>=AKEYCODE_0&&key<=AKEYCODE_9)scan=key==AKEYCODE_0?0xb:unsigned(key-AKEYCODE_0)+1;
        break;}
    }
    if(!scan)return 0;int action=AKeyEvent_getAction(event);
    if(action!=AKEY_EVENT_ACTION_DOWN&&action!=AKEY_EVENT_ACTION_UP)return 0;
    setGuestKey(scan,action==AKEY_EVENT_ACTION_DOWN);return 1;
}
extern "C" JNIEXPORT void JNICALL Java_com_nfsu2_androidevolved_GameActivity_nativeKey(JNIEnv*,jclass,jint scan,jboolean down){
    // 0xF1: the DRIFT touch button (no key of the game's): drift mode in free roam, src/runtime/drift.c.
    if(scan==0xf1){if(down==JNI_TRUE)nfs_drift_press();return;}
    // 0xF2: the GHOST touch button: ghost mode in free roam, src/runtime/ghost.c.
    if(scan==0xf2){if(down==JNI_TRUE)nfs_ghost_press();return;}
    // 0xF3: the Career status button: the pad's D-pad down, as on undrgnd2.eu (the keyboard has no key for it).
    if(scan==0xf3){setGuestTouchStatus(down==JNI_TRUE);return;}
    // 0xF4: the LIGHTS button: tapped off or on, held high beam (src/runtime/lights.c).
    if(scan==0xf4){nfs_lights_key(down==JNI_TRUE);return;}
    // 0xF5: the SHOWOFF button: the parked car's hood, trunk, doors opened step by step (src/runtime/showoff.c).
    if(scan==0xf5){if(down==JNI_TRUE)nfs_showoff_press();return;}
    // 0xF6..0xF8: the cruise control's button, − and + (src/runtime/cruise.c).
    if(scan>=0xf6&&scan<=0xf8){if(down==JNI_TRUE)nfs_cruise_key(scan);return;}
    setGuestKey(unsigned(scan),down==JNI_TRUE);
    __android_log_print(ANDROID_LOG_INFO,"NFSU2","Android touch DIK=%02x down=%d",unsigned(scan),int(down));
}
extern "C" JNIEXPORT jboolean JNICALL Java_com_nfsu2_androidevolved_GameActivity_nativeDrivingControls(JNIEnv*,jclass){return guestDrivingControls()?JNI_TRUE:JNI_FALSE;}
extern "C" JNIEXPORT jintArray JNICALL Java_com_nfsu2_androidevolved_GameActivity_nativeDriftStatus(JNIEnv* env,jclass){
    int status[7];nfs_drift_status(status);
    jintArray out=env->NewIntArray(7);if(out)env->SetIntArrayRegion(out,0,7,reinterpret_cast<const jint*>(status));return out;
}
extern "C" JNIEXPORT jintArray JNICALL Java_com_nfsu2_androidevolved_GameActivity_nativeGhostStatus(JNIEnv* env,jclass){
    int status[8];nfs_ghost_status(status);status[4]=nfs_lights_state();status[5]=nfs_showoff_status();status[6]=nfs_engage_status();status[7]=nfs_teleport_status();
    jintArray out=env->NewIntArray(8);if(out)env->SetIntArrayRegion(out,0,8,reinterpret_cast<const jint*>(status));return out;
}
extern "C" JNIEXPORT void JNICALL Java_com_nfsu2_androidevolved_GameActivity_nativeHiddenRaces(JNIEnv*,jclass,jboolean on){nfs_hiddenraces_enable(on==JNI_TRUE);}
extern "C" JNIEXPORT void JNICALL Java_com_nfsu2_androidevolved_GameActivity_nativeOpenCity(JNIEnv*,jclass,jboolean on){nfs_walls_city_open(on==JNI_TRUE);}
extern "C" JNIEXPORT jint JNICALL Java_com_nfsu2_androidevolved_GameActivity_nativePower(JNIEnv*,jclass,jint level){return nfs_power(level);}
extern "C" JNIEXPORT jintArray JNICALL Java_com_nfsu2_androidevolved_GameActivity_nativeHapticsStatus(JNIEnv* env,jclass){
    int status[4];nfs_haptics_status(status);
    jintArray out=env->NewIntArray(4);if(out)env->SetIntArrayRegion(out,0,4,reinterpret_cast<const jint*>(status));return out;
}
extern "C" JNIEXPORT jintArray JNICALL Java_com_nfsu2_androidevolved_GameActivity_nativeCruiseStatus(JNIEnv* env,jclass){
    int status[5];nfs_cruise_status(status);
    jintArray out=env->NewIntArray(5);if(out)env->SetIntArrayRegion(out,0,5,reinterpret_cast<const jint*>(status));return out;
}
extern "C" JNIEXPORT void JNICALL Java_com_nfsu2_androidevolved_GameActivity_nativePhotoShot(JNIEnv*,jclass){requestD3D9Photo();}
extern "C" JNIEXPORT jintArray JNICALL Java_com_nfsu2_androidevolved_GameActivity_nativeRadarStatus(JNIEnv* env,jclass,jboolean on){
    nfs_radars_enable(on==JNI_TRUE);int status[13];nfs_radars_status(status);
    jintArray out=env->NewIntArray(13);if(out)env->SetIntArrayRegion(out,0,13,reinterpret_cast<const jint*>(status));return out;
}
extern "C" JNIEXPORT jint JNICALL Java_com_nfsu2_androidevolved_GameActivity_nativeTeleport(JNIEnv*,jclass,jint what){return nfs_teleport(what);}
// The radars for the world map's list: each one's record (0 none yet), its distance from the car is not known here.
extern "C" JNIEXPORT jintArray JNICALL Java_com_nfsu2_androidevolved_GameActivity_nativeRadarList(JNIEnv* env,jclass){
    int n=nfs_radar_count();if(n<0)n=0;std::vector<jint> v(static_cast<size_t>(n));
    for(int i=0;i<n;++i){float s[3],d[2];int rec=0;nfs_radar_info(i,s,d,&rec);v[size_t(i)]=rec;}
    jintArray out=env->NewIntArray(n);if(out&&n)env->SetIntArrayRegion(out,0,n,v.data());return out;
}
extern "C" JNIEXPORT jstring JNICALL Java_com_nfsu2_androidevolved_GameActivity_nativeCarList(JNIEnv* env,jclass){
    char buf[1200];nfs_maxperf_list(buf,int(sizeof buf));return env->NewStringUTF(buf);
}
extern "C" JNIEXPORT jint JNICALL Java_com_nfsu2_androidevolved_GameActivity_nativeMaxPerf(JNIEnv*,jclass,jint slot){return nfs_maxperf(slot);}
extern "C" JNIEXPORT jint JNICALL Java_com_nfsu2_androidevolved_GameActivity_nativeCheat(JNIEnv*,jclass,jint cmd,jint arg){return nfs_cheat(cmd,arg);}
extern "C" JNIEXPORT jint JNICALL Java_com_nfsu2_androidevolved_GameActivity_nativeFrameCount(JNIEnv*,jclass){return jint(guestPresentCount());}
extern "C" JNIEXPORT void JNICALL Java_com_nfsu2_androidevolved_GameActivity_nativeTone(JNIEnv*,jclass,jfloat exposure,jfloat white){setD3D9Tone(exposure,white);}
extern "C" JNIEXPORT void JNICALL Java_com_nfsu2_androidevolved_GameActivity_nativePhotoLook(JNIEnv* env,jclass,jboolean live,jfloatArray values){
    float v[12]={1,1,1,1,1,1,0,0,0,0,1,1};
    if(values&&env->GetArrayLength(values)>=12)env->GetFloatArrayRegion(values,0,12,v);
    setD3D9PhotoLook(live==JNI_TRUE,v);
}
// Photo mode's picture once it is read: width, height, then the pixels as ARGB ints (Bitmap's), or null.
extern "C" JNIEXPORT jintArray JNICALL Java_com_nfsu2_androidevolved_GameActivity_nativePhotoTake(JNIEnv* env,jclass){
    unsigned w=0,h=0;auto rgba=takeD3D9Photo(&w,&h);
    if(rgba.empty()||!w||!h)return nullptr;
    jintArray out=env->NewIntArray(jsize(2+size_t(w)*h));if(!out)return nullptr;
    std::vector<jint> argb(2+size_t(w)*h);argb[0]=jint(w);argb[1]=jint(h);
    for(size_t i=0;i<size_t(w)*h;++i){const uint8_t* p=&rgba[i*4];argb[2+i]=jint(0xff000000u|uint32_t(p[0])<<16|uint32_t(p[1])<<8|p[2]);}
    env->SetIntArrayRegion(out,0,jsize(argb.size()),argb.data());return out;
}
extern "C" JNIEXPORT void JNICALL Java_com_nfsu2_androidevolved_GameActivity_nativePhoto(JNIEnv*,jclass,jint cmd,jint a,jint b){nfs_photo_input(cmd,a,b);}
extern "C" JNIEXPORT jintArray JNICALL Java_com_nfsu2_androidevolved_GameActivity_nativePhotoStatus(JNIEnv* env,jclass){
    int status[6];nfs_photo_status(status);
    jintArray out=env->NewIntArray(6);if(out)env->SetIntArrayRegion(out,0,6,reinterpret_cast<const jint*>(status));return out;
}
extern "C" JNIEXPORT jstring JNICALL Java_com_nfsu2_androidevolved_GameActivity_nativeTakeNotice(JNIEnv* env,jclass){
    std::string notice=takeGuestNotice();return notice.empty()?nullptr:env->NewStringUTF(notice.c_str());
}
extern "C" JNIEXPORT void JNICALL Java_com_nfsu2_androidevolved_GameActivity_nativePointer(JNIEnv*,jclass,jfloat x,jfloat y,jint phase){
    setGuestPointer(x,y,phase);
}
extern "C" JNIEXPORT void JNICALL Java_com_nfsu2_androidevolved_GameActivity_nativeTypeText(JNIEnv* env,jclass,jstring value,jboolean replace){
    if(!value)return;
    const jchar* chars=env->GetStringChars(value,nullptr);if(!chars)return;
    jsize length=env->GetStringLength(value);
    std::vector<uint32_t> characters;if(replace==JNI_TRUE)characters.assign(40,8);
    for(jsize i=0;i<length&&i<40;++i)if(chars[i]>=0x20&&chars[i]<0x7f)characters.push_back(chars[i]);
    env->ReleaseStringChars(value,chars);
    typeGuestText(characters);
    __android_log_print(ANDROID_LOG_INFO,"NFSU2","Android text delivered to guest: %d characters",int(length));
}
extern "C" JNIEXPORT void JNICALL Java_com_nfsu2_androidevolved_GameActivity_nativeLanguage(JNIEnv* env,jclass,jstring language){
    const char* value=env->GetStringUTFChars(language,nullptr);if(!value)return;
    setGuestLanguage(value);env->ReleaseStringUTFChars(language,value);
}
extern "C" JNIEXPORT void JNICALL Java_com_nfsu2_androidevolved_GameActivity_nativeResolution(JNIEnv*,jclass,jint width,jint height){setGuestResolution(unsigned(width),unsigned(height));}
extern "C" JNIEXPORT void JNICALL Java_com_nfsu2_androidevolved_GameActivity_nativeFrameLimit(JNIEnv*,jclass,jint cap){setGuestFrameLimit(unsigned(cap));}
extern "C" JNIEXPORT void JNICALL Java_com_nfsu2_androidevolved_GameActivity_nativeGamepadConnected(JNIEnv*,jclass,jboolean connected){setGuestPadConnected(connected==JNI_TRUE);}
extern "C" JNIEXPORT void JNICALL Java_com_nfsu2_androidevolved_GameActivity_nativeGamepadDevices(JNIEnv* env,jclass,jintArray ids){
    std::vector<int32_t> devices;
    if(ids){jsize count=env->GetArrayLength(ids);devices.resize(size_t(count));if(count)env->GetIntArrayRegion(ids,0,count,devices.data());}
    std::lock_guard<std::mutex> lock(gamepadDevicesMutex);
    if(devices!=gamepadDevices)__android_log_print(ANDROID_LOG_INFO,"NFS","Accepted gamepads: %zu device(s)",devices.size());
    if(devices.size()<gamepadDevices.size())gamepadsRemoved=true;
    gamepadDevices=std::move(devices);
}
extern "C" JNIEXPORT void JNICALL Java_com_nfsu2_androidevolved_GameActivity_nativeWidescreen(JNIEnv*,jclass,jboolean enabled){setGuestWidescreen(enabled==JNI_TRUE);}
// Minimap at the top-left: the bottom-left corner sits under the steering thumb.
extern "C" JNIEXPORT void JNICALL Java_com_nfsu2_androidevolved_GameActivity_nativeOnlineServer(JNIEnv* env,jclass,jstring host){
    const char* text=env->GetStringUTFChars(host,nullptr);setGuestOnlineServer(text?text:"");env->ReleaseStringUTFChars(host,text);
}
extern "C" JNIEXPORT void JNICALL Java_com_nfsu2_androidevolved_GameActivity_nativeMinimapTop(JNIEnv*,jclass,jboolean top){nfs_widescreen_set_minimap_offset(top==JNI_TRUE?-230.0f:0.0f);}
static bool compatCpuTextures=false;static unsigned compatBackBuffers=2;
extern "C" JNIEXPORT void JNICALL Java_com_nfsu2_androidevolved_GameActivity_nativeBackBuffers(JNIEnv*,jclass,jint count){
    compatBackBuffers=unsigned(count);setD3D9Compatibility(compatCpuTextures,compatBackBuffers);
}
extern "C" JNIEXPORT void JNICALL Java_com_nfsu2_androidevolved_GameActivity_nativeCpuTextures(JNIEnv*,jclass,jboolean enabled){
    compatCpuTextures=enabled==JNI_TRUE;setD3D9Compatibility(compatCpuTextures,compatBackBuffers);
}
// A driver imported in the launcher replaces the system one for DXVK. The bootstrap
// renderer, which links the system loader, is then skipped so one process never
// drives the GPU through two Vulkan drivers.
static bool customVulkanDriver=false;
static std::string customVulkanError;
extern "C" void dxvkAndroidSetInstanceProcAddr(PFN_vkGetInstanceProcAddr entry);
extern "C" JNIEXPORT void JNICALL Java_com_nfsu2_androidevolved_GameActivity_nativeGpuDriver(JNIEnv* env,jclass,jstring hooks,jstring temp,jstring directory,jstring library){
    auto text=[&](jstring value){const char* c=value?env->GetStringUTFChars(value,nullptr):nullptr;std::string r=c?c:"";if(c)env->ReleaseStringUTFChars(value,c);return r;};
    std::string h=text(hooks),t=text(temp),d=text(directory),l=text(library);
    if(l.empty())return;
    customVulkanDriver=true;
    void* handle=nfsOpenVulkan(h.c_str(),t.c_str(),d.c_str(),l.c_str());
    auto entry=nfsVulkanEntry(handle);
    if(!entry){customVulkanError="Impossible de charger le pilote Vulkan "+l+". Choisis le pilote du système dans le launcher.";return;}
    dxvkAndroidSetInstanceProcAddr(entry);
}
static void configurePipelineCache(android_app* app){
    const char* files=app->activity->internalDataPath;
    setenv("DXVK_STATE_CACHE","0",1); // Keep startup usable if cache storage is unavailable.
    if(!files||!*files)return;
    // Renderer cache epoch 2: avoid stale caches from the old DXVK preparation.
    // Keep the directory name in sync with GraphicsCache.java.
    auto directory=std::filesystem::path(files).parent_path()/"cache"/"dxvk-v2";
    std::error_code error;std::filesystem::create_directories(directory,error);
    if(error||access(directory.c_str(),W_OK)!=0){
        __android_log_print(ANDROID_LOG_WARN,"NFSU2","DXVK pipeline state cache unavailable; continuing without it");return;
    }
    setenv("DXVK_STATE_CACHE_PATH",directory.c_str(),1);
    setenv("DXVK_STATE_CACHE","1",1);
    __android_log_print(ANDROID_LOG_INFO,"NFSU2","DXVK persistent pipeline state cache: %s",directory.c_str());
}
void android_main(android_app* app) {
    configurePipelineCache(app);
    Host host; app->userData = &host; app->onAppCmd = onCommand;app->onInputEvent=onInput;
    try {
        testGuestMemory();
        __android_log_print(ANDROID_LOG_INFO, "NFSU2", "Guest memory self-check passed (32-bit offsets on ARM64)");
    } catch (const std::exception& e) {
        __android_log_print(ANDROID_LOG_ERROR, "NFSU2", "%s", e.what());
        ANativeActivity_finish(app->activity);
        host.failed = true;
    }
    while (!app->destroyRequested) {
        int events; android_poll_source* source = nullptr;
        const bool active = host.resumed && app->window && !host.failed;
        int id = ALooper_pollOnce(active ? (d3d9RequestsSurface()?16:0) : -1, nullptr, &events, reinterpret_cast<void**>(&source));
        if (id >= 0 && source) source->process(app, source);
        if(gamepadsRemoved.exchange(false)){gamepad.releaseAll(setPadKey);publishPad();}
        gamepad.setKeyEmulation(!guestGamepadInUse());gamepad.refresh(guestDrivingControls(),setPadKey);
        if (app->destroyRequested) break;
        if (!host.resumed || !app->window || host.failed) continue;
        try {
            setD3D9HostWindow(app->window);
            if(d3d9RequestsSurface()){host.renderer.stop();acknowledgeD3D9Surface(true);}
            setGuestPaused(false);
            if(!customVulkanError.empty())throw std::runtime_error(customVulkanError);
            if (!host.renderer.device&&!d3d9RequestsSurface()&&!(customVulkanDriver&&host.guestAttempted)) {
                checkGameData();
                if(customVulkanDriver){
                    setGuestDisplaySize(unsigned(ANativeWindow_getWidth(app->window)),unsigned(ANativeWindow_getHeight(app->window)));
                }else{
                    host.renderer.start(app->window);
                    setGuestDisplaySize(host.renderer.extent.width,host.renderer.extent.height);
                }
                if (!host.guestAttempted) {
                    host.guestAttempted = true;
                    host.guestWorker.start(std::string(kGameRoot) + "/SPEED2.EXE");
                }
            }
            auto status=host.guestWorker.status();
            if(status!=host.guestStatus){
                host.guestStatus=status;
                showStatus(app,("Données accessibles : mémoire interne/nfsu2\nVulkan 1.1 : affichage actif\n"+status).c_str());
            }
            if (host.renderer.device&&!host.renderer.draw()) host.renderer.stop();
        } catch (const std::exception& e) {
            __android_log_print(ANDROID_LOG_ERROR, "NFSU2", "%s", e.what());
            showStatus(app, e.what());
            host.renderer.stop(); host.failed = true;
        }
    }
    host.renderer.stop();
    acknowledgeD3D9Surface(false);setD3D9HostWindow(nullptr);
}
