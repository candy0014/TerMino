#include "tetr/frontend/ui_surface.h"
namespace Frontend {
namespace {thread_local UiSink sink;thread_local std::optional<UiSize> size;}
UiSurfaceScope::UiSurfaceScope(UiSink target,UiSize extent):previous_(std::move(sink)),previousSize_(size) {
    sink=std::move(target);size=extent;
}
UiSurfaceScope::~UiSurfaceScope() {sink=std::move(previous_);size=previousSize_;}
bool ui_surface_active() noexcept {return static_cast<bool>(sink);}
std::optional<UiSize> ui_surface_size() noexcept {return size;}
void present_ui(UiFrame frame) {if(size)frame.extent=*size;if(sink)sink(std::move(frame));}
void clear_ui() {present_ui({});}
}
