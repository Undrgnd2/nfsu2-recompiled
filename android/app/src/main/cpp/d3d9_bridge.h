#pragma once
#include <cstdint>
struct ANativeWindow;
void configureD3D9Bridge(void* guestBase,uint32_t (*allocator)(uint32_t),void (*deallocator)(uint32_t));
void shutdownD3D9Bridge();
void setD3D9HostWindow(ANativeWindow* window);
void setD3D9RenderSize(unsigned width,unsigned height);
// Launcher compatibility options; call before the game creates its device.
void setD3D9Compatibility(bool cpuTextures,unsigned backBuffers);
bool d3d9RequestsSurface();
void acknowledgeD3D9Surface(bool available);
uint32_t createGuestD3D9(uint32_t sdk);
uint32_t dispatchGuestD3D9(uint32_t token,const uint32_t* args,uint32_t* argumentCount);
// Photo mode's picture: the back buffer read at the next Present (the game's own image, before the touch controls);
// take: width, height, then RGBA pixels (alpha 255), or empty while none is ready.
#include <vector>
void requestD3D9Photo();
std::vector<uint8_t> takeD3D9Photo(unsigned* width,unsigned* height);
// Photo mode's look (undrgnd2.eu's panel): exposure, contrast, saturation, tint r g b, fade, vignette, then the crop
// kept (left, top, right, bottom, 0 .. 1). live: shown over the game while photo mode is on; the picture taken gets it
// too, cropped.
void setD3D9PhotoLook(bool live,const float values[12]);
// Exposure (-1 .. +1 stops) and White Level (0.4 .. 1) over the whole picture (DXVK's gamma ramp), from the next Present.
void setD3D9Tone(float exposure,float white);
