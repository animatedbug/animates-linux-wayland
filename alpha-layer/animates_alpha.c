// Minimal implicit Vulkan layer: make swapchains use premultiplied composite
// alpha so the compositor (niri) blends Animates' per-pixel alpha instead of
// showing it on an opaque black background. Only active with
// ANIMATES_ALPHA_LAYER=1 (see animates_alpha.json).
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <vulkan/vk_layer.h>
#include <vulkan/vulkan.h>

#define EXPORT __attribute__((visibility("default")))
#define MAX_OBJS 64

typedef void *Key;
static Key key_of(const void *handle) { return *(Key *)handle; }

typedef struct {
    Key key;
    PFN_vkGetInstanceProcAddr gipa;
    PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR get_caps;
} Inst;

typedef struct {
    Key key;
    PFN_vkGetDeviceProcAddr gdpa;
    PFN_vkCreateSwapchainKHR create_swapchain;
    VkPhysicalDevice phys;
    PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR get_caps;
} Dev;

static Inst insts[MAX_OBJS];
static Dev devs[MAX_OBJS];
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

// Find the slot for key, or a free one (or recycle slot 0) when inserting.
#define LOOKUP(arr, k, insert)                                             \
    ({                                                                     \
        __typeof__(&arr[0]) hit = NULL, free_slot = NULL;                  \
        for (int i = 0; i < MAX_OBJS; i++) {                               \
            if (arr[i].key == (k)) { hit = &arr[i]; break; }               \
            if (!arr[i].key && !free_slot) free_slot = &arr[i];            \
        }                                                                  \
        if (!hit && (insert)) hit = free_slot ? free_slot : &arr[0];       \
        hit;                                                               \
    })

static VKAPI_ATTR VkResult VKAPI_CALL
layer_CreateInstance(const VkInstanceCreateInfo *info, const VkAllocationCallbacks *alloc, VkInstance *out)
{
    VkLayerInstanceCreateInfo *ci = (VkLayerInstanceCreateInfo *)info->pNext;
    while (ci && !(ci->sType == VK_STRUCTURE_TYPE_LOADER_INSTANCE_CREATE_INFO && ci->function == VK_LAYER_LINK_INFO))
        ci = (VkLayerInstanceCreateInfo *)ci->pNext;
    if (!ci)
        return VK_ERROR_INITIALIZATION_FAILED;

    PFN_vkGetInstanceProcAddr gipa = ci->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    ci->u.pLayerInfo = ci->u.pLayerInfo->pNext;
    PFN_vkCreateInstance next = (PFN_vkCreateInstance)gipa(VK_NULL_HANDLE, "vkCreateInstance");
    VkResult r = next(info, alloc, out);
    if (r != VK_SUCCESS)
        return r;

    pthread_mutex_lock(&lock);
    Inst *in = LOOKUP(insts, key_of(*out), 1);
    in->key = key_of(*out);
    in->gipa = gipa;
    in->get_caps = (PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR)gipa(*out, "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
    pthread_mutex_unlock(&lock);
    return r;
}

static VKAPI_ATTR VkResult VKAPI_CALL
layer_CreateDevice(VkPhysicalDevice phys, const VkDeviceCreateInfo *info, const VkAllocationCallbacks *alloc, VkDevice *out)
{
    VkLayerDeviceCreateInfo *ci = (VkLayerDeviceCreateInfo *)info->pNext;
    while (ci && !(ci->sType == VK_STRUCTURE_TYPE_LOADER_DEVICE_CREATE_INFO && ci->function == VK_LAYER_LINK_INFO))
        ci = (VkLayerDeviceCreateInfo *)ci->pNext;
    if (!ci)
        return VK_ERROR_INITIALIZATION_FAILED;

    PFN_vkGetInstanceProcAddr gipa = ci->u.pLayerInfo->pfnNextGetInstanceProcAddr;
    PFN_vkGetDeviceProcAddr gdpa = ci->u.pLayerInfo->pfnNextGetDeviceProcAddr;
    ci->u.pLayerInfo = ci->u.pLayerInfo->pNext;

    // Physical devices share their instance's dispatch key.
    pthread_mutex_lock(&lock);
    Inst *in = LOOKUP(insts, key_of(phys), 0);
    PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR get_caps = in ? in->get_caps : NULL;
    pthread_mutex_unlock(&lock);

    PFN_vkCreateDevice next = (PFN_vkCreateDevice)gipa(VK_NULL_HANDLE, "vkCreateDevice");
    VkResult r = next(phys, info, alloc, out);
    if (r != VK_SUCCESS)
        return r;

    pthread_mutex_lock(&lock);
    Dev *d = LOOKUP(devs, key_of(*out), 1);
    d->key = key_of(*out);
    d->gdpa = gdpa;
    d->create_swapchain = (PFN_vkCreateSwapchainKHR)gdpa(*out, "vkCreateSwapchainKHR");
    d->phys = phys;
    d->get_caps = get_caps;
    pthread_mutex_unlock(&lock);
    return r;
}

static VKAPI_ATTR VkResult VKAPI_CALL
layer_CreateSwapchainKHR(VkDevice dev, const VkSwapchainCreateInfoKHR *info, const VkAllocationCallbacks *alloc, VkSwapchainKHR *out)
{
    pthread_mutex_lock(&lock);
    Dev *found = LOOKUP(devs, key_of(dev), 0);
    Dev d = found ? *found : (Dev){0};
    pthread_mutex_unlock(&lock);
    if (!d.create_swapchain)
        return VK_ERROR_INITIALIZATION_FAILED;

    VkSwapchainCreateInfoKHR mod = *info;
    VkSurfaceCapabilitiesKHR caps = {0};
    if (d.get_caps && d.get_caps(d.phys, info->surface, &caps) == VK_SUCCESS) {
        if (caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR)
            mod.compositeAlpha = VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR;
        else if (caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR)
            mod.compositeAlpha = VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR;
    }
    fprintf(stderr, "animates_alpha: swapchain %ux%u format %d compositeAlpha %#x -> %#x (supported %#x)\n",
            info->imageExtent.width, info->imageExtent.height, info->imageFormat,
            info->compositeAlpha, mod.compositeAlpha, caps.supportedCompositeAlpha);
    return d.create_swapchain(dev, &mod, alloc, out);
}

EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL animates_GetDeviceProcAddr(VkDevice dev, const char *name);

EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL animates_GetInstanceProcAddr(VkInstance inst, const char *name)
{
    if (!strcmp(name, "vkGetInstanceProcAddr")) return (PFN_vkVoidFunction)animates_GetInstanceProcAddr;
    if (!strcmp(name, "vkCreateInstance")) return (PFN_vkVoidFunction)layer_CreateInstance;
    if (!strcmp(name, "vkCreateDevice")) return (PFN_vkVoidFunction)layer_CreateDevice;
    if (!strcmp(name, "vkGetDeviceProcAddr")) return (PFN_vkVoidFunction)animates_GetDeviceProcAddr;
    if (!strcmp(name, "vkCreateSwapchainKHR")) return (PFN_vkVoidFunction)layer_CreateSwapchainKHR;
    if (!inst)
        return NULL;

    pthread_mutex_lock(&lock);
    Inst *in = LOOKUP(insts, key_of(inst), 0);
    PFN_vkGetInstanceProcAddr gipa = in ? in->gipa : NULL;
    pthread_mutex_unlock(&lock);
    return gipa ? gipa(inst, name) : NULL;
}

EXPORT VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL animates_GetDeviceProcAddr(VkDevice dev, const char *name)
{
    if (!strcmp(name, "vkGetDeviceProcAddr")) return (PFN_vkVoidFunction)animates_GetDeviceProcAddr;
    if (!strcmp(name, "vkCreateSwapchainKHR")) return (PFN_vkVoidFunction)layer_CreateSwapchainKHR;

    pthread_mutex_lock(&lock);
    Dev *d = LOOKUP(devs, key_of(dev), 0);
    PFN_vkGetDeviceProcAddr gdpa = d ? d->gdpa : NULL;
    pthread_mutex_unlock(&lock);
    return gdpa ? gdpa(dev, name) : NULL;
}
